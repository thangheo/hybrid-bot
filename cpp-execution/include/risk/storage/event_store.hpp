#pragma once

#include <string>
#include <optional>
#include <chrono>
#include "risk/storage/event_store.hpp"
#include "risk/risk_events.hpp"
namespace trading::risk::storage {

class EventStore {
public:
    enum class BackpressureMode { DROP_OLDEST, BLOCK, FAIL };

    // Simple constructor: default unbounded queue
    explicit EventStore(const std::string& dbPath);

    // Constructor with bounded queue, backpressure policy and DB tuning
    // busyTimeoutMs: sqlite busy_timeout in milliseconds
    // batchSize: number of inserts to group into a single transaction
    EventStore(const std::string& dbPath, size_t queueCapacity, BackpressureMode mode, int busyTimeoutMs = 1000, size_t batchSize = 50);
    ~EventStore();

    // Initialize DB schema (creates tables if missing)
    bool initSchema();

    // Persist a PositionReconciliationEvent, returns generated id or empty on failure
    std::optional<int64_t> saveReconciliationEvent(const PositionReconciliationEvent& event);

    // Async persistence: enqueue an event for background persistence.
    // Returns true if the event was queued; false if it was rejected (when in FAIL mode and full).
    bool enqueueReconciliationEvent(const PositionReconciliationEvent& event);

    // Current queue size (approximate)
    size_t queueSize() const;

    // Idempotency helpers
    bool isEventProcessed(const std::string& externalId);
    bool markEventProcessed(const std::string& externalId);
    // enqueueReconciliationEvent
    // bool enqueueReconciliationEvent(const PositionReconciliationEvent& event);
private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace trading::risk::storage
