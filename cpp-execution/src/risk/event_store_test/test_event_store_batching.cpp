#include <gtest/gtest.h>
#include <filesystem>
#include <chrono>
#include <thread>
#include <sqlite3.h>

#include "risk/storage/event_store.hpp"
#include "risk/risk_events.hpp"

using namespace trading::risk::storage;
using namespace trading::risk;

// Helper: create a simple PositionReconciliationEvent
static PositionReconciliationEvent makeEvent(const std::string& exchange, const std::string& pair, int64_t tsMillis, double expected, double actual, const std::string& msg = "") {
    trading::risk::Timestamp ts = std::chrono::system_clock::time_point(std::chrono::milliseconds(tsMillis));
    auto exch = ExchangeId::fromString(exchange);
    auto tp = TradingPair::fromString(pair);
    double discrepancy = (actual - expected) / (expected == 0 ? 1.0 : expected) * 100.0;
    return PositionReconciliationEvent(ts, exch, tp, expected, actual, discrepancy, false, msg);
}

TEST(EventStoreBatchingTest, EnqueueAndDeduplicate) {
    // Temp DB path
    std::string tmp = "/tmp/event_store_test_batching_" + std::to_string(::getpid()) + ".db";
    if (std::filesystem::exists(tmp)) std::filesystem::remove(tmp);

    // Create EventStore with small batch size to force batching
    size_t queueCap = 1000;
    int busyMs = 500;
    size_t batchSize = 5;
    EventStore store(tmp, queueCap, EventStore::BackpressureMode::BLOCK, busyMs, batchSize);
    ASSERT_TRUE(store.initSchema());

    // Enqueue 12 events, with two duplicates (same external id)
    int64_t baseTs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (int i = 0; i < 10; ++i) {
        // Use a supported exchange string (binance) instead of the unsupported EX1
        auto ev = makeEvent("binance", "BTCUSD", baseTs + i, 1.0 + i, 1.0 + i + 0.1, "ev" + std::to_string(i));
        bool ok = store.enqueueReconciliationEvent(ev);
        ASSERT_TRUE(ok);
    }
    // duplicate of event 3
    auto dup = makeEvent("binance", "BTCUSD", baseTs + 3, 4.0, 4.1, "dup");
    ASSERT_TRUE(store.enqueueReconciliationEvent(dup));
    // duplicate of event 7
    auto dup2 = makeEvent("binance", "BTCUSD", baseTs + 7, 8.0, 8.1, "dup2");
    ASSERT_TRUE(store.enqueueReconciliationEvent(dup2));

    // Wait for worker to process queue (polling)
    int waitMs = 0;
    while (store.queueSize() > 0 && waitMs < 5000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        waitMs += 50;
    }
    ASSERT_EQ(store.queueSize(), 0u);

    // Open DB and count rows — should be 10 unique events
    sqlite3* db = nullptr;
    int rc = sqlite3_open(tmp.c_str(), &db);
    ASSERT_EQ(rc, SQLITE_OK);
    const char* sql = "SELECT COUNT(1) FROM reconciliation_events;";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    ASSERT_EQ(rc, SQLITE_OK);
    rc = sqlite3_step(stmt);
    ASSERT_EQ(rc, SQLITE_ROW);
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    EXPECT_EQ(count, 10);

    // cleanup
    std::filesystem::remove(tmp);
}
