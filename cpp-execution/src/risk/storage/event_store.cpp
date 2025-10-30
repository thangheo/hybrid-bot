#include "risk/storage/event_store.hpp"
#include <sqlite3.h>
#include <iostream>
#include "common/logger.hpp"
#include <memory>
#include <vector>
#include <cstring>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <atomic>

namespace trading::risk::storage {

struct EventStore::Impl {
    sqlite3* db = nullptr;
    std::string path;
    // Async queue members
    std::thread worker;
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::queue<PositionReconciliationEvent> queue;
    std::atomic<bool> stopRequested{false};
    std::mutex dbMutex; // protect sqlite3* for multi-threaded access
    std::atomic<size_t> queueSize{0};
    // bounded queue settings
    size_t queueCapacity = 0; // 0 == unbounded
    EventStore::BackpressureMode backpressureMode = EventStore::BackpressureMode::BLOCK;
    // DB tuning
    int busyTimeoutMs = 1000;
    size_t batchSize = 50;
};

EventStore::EventStore(const std::string& dbPath) {
    impl_ = new Impl();
    impl_->path = dbPath;
    int rc = sqlite3_open(dbPath.c_str(), &impl_->db);
    if (rc != SQLITE_OK) {
        LOG_ERROR("EventStore: failed to open sqlite db: %s", sqlite3_errmsg(impl_->db));
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }

    // Apply basic PRAGMA tuning defaults (may be overridden by constructor)
    if (impl_->db) {
        // Set busy timeout
        sqlite3_busy_timeout(impl_->db, impl_->busyTimeoutMs);
        // Enable WAL mode for better concurrency
        char* err = nullptr;
        int rc2 = sqlite3_exec(impl_->db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, &err);
        if (rc2 != SQLITE_OK) {
            LOG_WARN("EventStore: PRAGMA journal_mode=WAL failed: %s", (err ? err : "unknown"));
            if (err) sqlite3_free(err);
        }
        // Set synchronous to NORMAL for durability/throughput tradeoff
        rc2 = sqlite3_exec(impl_->db, "PRAGMA synchronous = NORMAL;", nullptr, nullptr, &err);
        if (rc2 != SQLITE_OK) {
            LOG_WARN("EventStore: PRAGMA synchronous failed: %s", (err ? err : "unknown"));
            if (err) sqlite3_free(err);
        }
        // Use memory for temp store to reduce IO
        rc2 = sqlite3_exec(impl_->db, "PRAGMA temp_store = MEMORY;", nullptr, nullptr, &err);
        if (rc2 != SQLITE_OK) {
            LOG_WARN("EventStore: PRAGMA temp_store failed: %s", (err ? err : "unknown"));
            if (err) sqlite3_free(err);
        }
    }

    // Start background worker
    impl_->worker = std::thread([this](){
        std::vector<PositionReconciliationEvent> batch;
        batch.reserve(impl_->batchSize ? impl_->batchSize : 50);
        while (!impl_->stopRequested.load()) {
            std::unique_lock<std::mutex> lk(impl_->queueMutex);
            impl_->queueCv.wait(lk, [this]{ return impl_->stopRequested.load() || !impl_->queue.empty(); });
            // collect up to batchSize events
            while (!impl_->queue.empty() && batch.size() < impl_->batchSize) {
                batch.push_back(std::move(impl_->queue.front()));
                impl_->queue.pop();
                impl_->queueSize.fetch_sub(1, std::memory_order_relaxed);
            }
            lk.unlock();

            if (!batch.empty()) {
                // persist batch inside a single transaction
                std::lock_guard<std::mutex> dblock(impl_->dbMutex);
                char* err = nullptr;
                int rc = sqlite3_exec(impl_->db, "BEGIN IMMEDIATE;", nullptr, nullptr, &err);
                if (rc != SQLITE_OK) {
                    LOG_WARN("EventStore(worker): BEGIN failed: %s", (err ? err : "unknown"));
                    if (err) sqlite3_free(err);
                }

                const char* sql = "INSERT OR IGNORE INTO reconciliation_events (timestamp, exchange, trading_pair, expected, actual, discrepancy, discrepancy_percent, auto_correct, message, external_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
                sqlite3_stmt* stmt = nullptr;
                rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
                if (rc != SQLITE_OK) {
                    LOG_WARN("EventStore(worker): prepare failed: %s", sqlite3_errmsg(impl_->db));
                } else {
                    for (auto &ev : batch) {
                        // compute externalId
                        std::string externalId = ev.exchangeId.toString() + ":" + ev.tradingPair.toString() + ":" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(ev.timestamp.time_since_epoch()).count());
                        sqlite3_reset(stmt);
                        sqlite3_clear_bindings(stmt);
                        sqlite3_bind_int64(stmt, 1, std::chrono::duration_cast<std::chrono::milliseconds>(ev.timestamp.time_since_epoch()).count());
                        sqlite3_bind_text(stmt, 2, ev.exchangeId.toString().c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 3, ev.tradingPair.toString().c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_double(stmt, 4, ev.expectedQuantity);
                        sqlite3_bind_double(stmt, 5, ev.actualQuantity);
                        sqlite3_bind_double(stmt, 6, ev.actualQuantity - ev.expectedQuantity);
                        sqlite3_bind_double(stmt, 7, ev.discrepancyPercentage);
                        sqlite3_bind_int(stmt, 8, ev.autoCorrect ? 1 : 0);
                        sqlite3_bind_text(stmt, 9, ev.message.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 10, externalId.c_str(), -1, SQLITE_TRANSIENT);
                        int stepRc = sqlite3_step(stmt);
                        if (stepRc != SQLITE_DONE && stepRc != SQLITE_CONSTRAINT) {
                            LOG_WARN("EventStore(worker): insert failed: %s", sqlite3_errmsg(impl_->db));
                        }
                    }
                    sqlite3_finalize(stmt);
                }

                // Commit transaction
                rc = sqlite3_exec(impl_->db, "COMMIT;", nullptr, nullptr, &err);
                if (rc != SQLITE_OK) {
                    LOG_WARN("EventStore(worker): COMMIT failed: %s", (err ? err : "unknown"));
                    if (err) sqlite3_free(err);
                }

                batch.clear();
            }
        }

        // drain remaining items on shutdown with same batching logic
        std::unique_lock<std::mutex> lk(impl_->queueMutex);
        while (!impl_->queue.empty()) {
            while (!impl_->queue.empty() && batch.size() < impl_->batchSize) {
                batch.push_back(std::move(impl_->queue.front()));
                impl_->queue.pop();
                impl_->queueSize.fetch_sub(1, std::memory_order_relaxed);
            }
            lk.unlock();
            if (!batch.empty()) {
                std::lock_guard<std::mutex> dblock(impl_->dbMutex);
                char* err = nullptr;
                int rc = sqlite3_exec(impl_->db, "BEGIN IMMEDIATE;", nullptr, nullptr, &err);
                if (rc != SQLITE_OK) {
                    LOG_WARN("EventStore(shutdown): BEGIN failed: %s", (err ? err : "unknown"));
                    if (err) sqlite3_free(err);
                }
                const char* sql = "INSERT OR IGNORE INTO reconciliation_events (timestamp, exchange, trading_pair, expected, actual, discrepancy, discrepancy_percent, auto_correct, message, external_id) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
                sqlite3_stmt* stmt = nullptr;
                rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
                if (rc == SQLITE_OK) {
                    for (auto &ev : batch) {
                        std::string externalId = ev.exchangeId.toString() + ":" + ev.tradingPair.toString() + ":" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(ev.timestamp.time_since_epoch()).count());
                        sqlite3_reset(stmt);
                        sqlite3_clear_bindings(stmt);
                        sqlite3_bind_int64(stmt, 1, std::chrono::duration_cast<std::chrono::milliseconds>(ev.timestamp.time_since_epoch()).count());
                        sqlite3_bind_text(stmt, 2, ev.exchangeId.toString().c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 3, ev.tradingPair.toString().c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_double(stmt, 4, ev.expectedQuantity);
                        sqlite3_bind_double(stmt, 5, ev.actualQuantity);
                        sqlite3_bind_double(stmt, 6, ev.actualQuantity - ev.expectedQuantity);
                        sqlite3_bind_double(stmt, 7, ev.discrepancyPercentage);
                        sqlite3_bind_int(stmt, 8, ev.autoCorrect ? 1 : 0);
                        sqlite3_bind_text(stmt, 9, ev.message.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 10, externalId.c_str(), -1, SQLITE_TRANSIENT);
                        int stepRc = sqlite3_step(stmt);
                        if (stepRc != SQLITE_DONE && stepRc != SQLITE_CONSTRAINT) {
                            LOG_WARN("EventStore(shutdown): insert failed: %s", sqlite3_errmsg(impl_->db));
                        }
                    }
                    sqlite3_finalize(stmt);
                } else {
                    LOG_WARN("EventStore(shutdown): prepare failed: %s", sqlite3_errmsg(impl_->db));
                }
                rc = sqlite3_exec(impl_->db, "COMMIT;", nullptr, nullptr, &err);
                if (rc != SQLITE_OK) {
                    LOG_WARN("EventStore(shutdown): COMMIT failed: %s", (err ? err : "unknown"));
                    if (err) sqlite3_free(err);
                }
                batch.clear();
            }
            lk.lock();
        }
    });
}

EventStore::EventStore(const std::string& dbPath, size_t queueCapacity, BackpressureMode mode, int busyTimeoutMs, size_t batchSize) : EventStore(dbPath) {
    if (impl_) {
        impl_->queueCapacity = queueCapacity;
        impl_->backpressureMode = mode;
        impl_->busyTimeoutMs = busyTimeoutMs;
        impl_->batchSize = batchSize;
        if (impl_->db) {
            sqlite3_busy_timeout(impl_->db, impl_->busyTimeoutMs);
        }
    }
}

EventStore::~EventStore() {
    if (impl_) {
        // request worker stop and notify
        impl_->stopRequested.store(true);
        impl_->queueCv.notify_all();
        if (impl_->worker.joinable()) impl_->worker.join();

        if (impl_->db) sqlite3_close(impl_->db);
        delete impl_;
        impl_ = nullptr;
    }
}

bool EventStore::initSchema() {
    if (!impl_ || !impl_->db) return false;

    const char* sql = R"SQL(
    CREATE TABLE IF NOT EXISTS reconciliation_events (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        timestamp INTEGER NOT NULL,
        exchange TEXT NOT NULL,
        trading_pair TEXT NOT NULL,
        expected REAL NOT NULL,
        actual REAL NOT NULL,
        discrepancy REAL NOT NULL,
        discrepancy_percent REAL NOT NULL,
        auto_correct INTEGER NOT NULL,
        message TEXT,
        external_id TEXT UNIQUE
    );

    -- Unique index is declared via column constraint above; keep a named index for compatibility
    CREATE UNIQUE INDEX IF NOT EXISTS idx_recon_external_id ON reconciliation_events(external_id);
    )SQL";

    char* err = nullptr;
    int rc = sqlite3_exec(impl_->db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        LOG_WARN("EventStore: initSchema failed: %s", (err ? err : "unknown"));
        if (err) sqlite3_free(err);
        return false;
    }
    return true;
}

std::optional<int64_t> EventStore::saveReconciliationEvent(const PositionReconciliationEvent& event) {
    if (!impl_ || !impl_->db) return std::nullopt;
    // For backward compatibility provide synchronous save by delegating to enqueue and returning empty (worker will persist)
    enqueueReconciliationEvent(event);
    return std::nullopt;
}

bool EventStore::isEventProcessed(const std::string& externalId) {
    if (!impl_ || !impl_->db) return false;
    const char* sql = "SELECT COUNT(1) FROM reconciliation_events WHERE external_id = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) { sqlite3_finalize(stmt); return false; }
    sqlite3_bind_text(stmt, 1, externalId.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    bool exists = false;
    if (rc == SQLITE_ROW) {
        exists = sqlite3_column_int(stmt, 0) > 0;
    }
    sqlite3_finalize(stmt);
    return exists;
}

bool EventStore::markEventProcessed(const std::string& externalId) {
    // No-op for now: events are recorded at save time; this function kept for API completeness
    return true;
}

bool EventStore::enqueueReconciliationEvent(const PositionReconciliationEvent& event) {
    if (!impl_) return false;

    // Fast path: unbounded
    if (impl_->queueCapacity == 0) {
        {
            std::lock_guard<std::mutex> lk(impl_->queueMutex);
            impl_->queue.push(event);
            impl_->queueSize.fetch_add(1, std::memory_order_relaxed);
        }
        impl_->queueCv.notify_one();
        return true;
    }

    // Bounded behavior
    std::unique_lock<std::mutex> lk(impl_->queueMutex);
    if (impl_->queue.size() < impl_->queueCapacity) {
        impl_->queue.push(event);
        impl_->queueSize.fetch_add(1, std::memory_order_relaxed);
        lk.unlock();
        impl_->queueCv.notify_one();
        return true;
    }

    // Queue is full
    switch (impl_->backpressureMode) {
        case EventStore::BackpressureMode::DROP_OLDEST: {
            // drop oldest then push
            if (!impl_->queue.empty()) impl_->queue.pop();
            impl_->queue.push(event);
            // queueSize unchanged (pop then push)
            lk.unlock();
            impl_->queueCv.notify_one();
            return true;
        }
        case EventStore::BackpressureMode::BLOCK: {
            // wait until space is available (with spurious wakeups handling)
            impl_->queueCv.wait(lk, [this]{ return impl_->stopRequested.load() || impl_->queue.size() < impl_->queueCapacity; });
            if (impl_->stopRequested.load()) return false;
            impl_->queue.push(event);
            impl_->queueSize.fetch_add(1, std::memory_order_relaxed);
            lk.unlock();
            impl_->queueCv.notify_one();
            return true;
        }
        case EventStore::BackpressureMode::FAIL: {
            // do not enqueue, return failure
            return false;
        }
        default: {
            return false;
        }
    }
}

size_t EventStore::queueSize() const {
    if (!impl_) return 0;
    return impl_->queueSize.load(std::memory_order_relaxed);
}

} // namespace trading::risk::storage
