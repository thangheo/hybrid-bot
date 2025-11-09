/**
 * Position Synchronization Types and Events
 * 
 * File: position_sync_types.hpp
 * Created: 2025-11-09
 */
#pragma once

#include "core/market_types.hpp"
#include "risk/risk_types.hpp"
#include <string>
#include <chrono>
#include <nlohmann/json.hpp>

namespace trading {
namespace risk {
namespace sync {

/**
 * Position event types for synchronization
 */
enum class PositionEventType {
    POSITION_OPENED = 0,     // Khi mở position mới
    POSITION_UPDATED = 1,    // Khi update quantity/price
    POSITION_CLOSED = 2,     // Khi đóng position
    POSITION_RECONCILED = 3, // Khi reconcile với exchange
    POSITION_PARTIAL_FILL = 4, // Khi có partial fill
    POSITION_ERROR = 5       // Khi có lỗi position
};

/**
 * Position event priority levels
 */
enum class PositionEventPriority {
    LOW = 0,        // Reconciliation, periodic updates
    NORMAL = 1,     // Regular quantity updates
    HIGH = 2,       // Position opened/closed  
    CRITICAL = 3    // Emergency exits, errors
};

/**
 * Position synchronization event
 */
struct PositionEvent {
    std::string eventId;                    // Unique event identifier
    uint64_t sequenceNumber;                // For ordering guarantees
    PositionEventType eventType;            // Event type
    PositionEventPriority priority;         // Event priority
    
    // Position identification
    std::string symbol;                     // Trading symbol
    std::string exchangeId;                 // Exchange identifier  
    std::string strategyId;                 // Strategy identifier
    
    // Position data
    double quantity;                        // Current position quantity
    double avgPrice;                        // Average entry price
    double lastPrice;                       // Last known market price
    double unrealizedPnl;                   // Unrealized P&L
    double realizedPnl;                     // Realized P&L (for closed positions)
    
    // Timing
    std::chrono::system_clock::time_point timestamp;       // Event timestamp
    std::chrono::system_clock::time_point lastUpdated;     // Last position update
    
    // Metadata
    std::string metadata;                   // JSON metadata
    std::string source;                     // Event source (strategy, reconciliation, etc.)
    std::string correlationId;              // For tracking related events
    
    // Convert to/from JSON for ZMQ transport
    nlohmann::json toJson() const;
    static PositionEvent fromJson(const nlohmann::json& j);
    
    // Validation
    bool isValid() const;
    
    // Create position key for caching
    std::string getPositionKey() const;
};

/**
 * Position snapshot for full state sync
 */
struct PositionSnapshot {
    std::string snapshotId;
    std::chrono::system_clock::time_point timestamp;
    std::vector<Position> positions;        // All current positions
    std::string checksum;                   // Data integrity check
    
    nlohmann::json toJson() const;
    static PositionSnapshot fromJson(const nlohmann::json& j);
    
    bool isValid() const;
    std::string calculateChecksum() const;
};

/**
 * Position synchronization status
 */
struct PositionSyncStatus {
    bool isHealthy;
    std::chrono::system_clock::time_point lastSync;
    uint64_t lastSequenceNumber;
    size_t pendingEvents;
    size_t processedEvents;
    size_t errorCount;
    std::string lastError;
    
    // Cache statistics
    size_t cacheSize;
    size_t cacheHits;
    size_t cacheMisses;
    
    nlohmann::json toJson() const;
};

/**
 * Position synchronization configuration
 */
struct PositionSyncConfig {
    // Publisher settings
    std::string publishEndpoint = "inproc://position_pub";
    int publisherHwm = 10000;
    int batchSize = 10;
    int batchTimeoutMs = 100;
    bool enablePriority = true;
    
    // Subscriber settings
    std::vector<std::string> subscribeEndpoints = {"inproc://position_pub"};
    int subscriberHwm = 10000;
    int cacheTimeout_ms = 30000;
    int maxQueueSize = 1000;
    
    // Reconciliation settings
    int reconciliationIntervalSeconds = 300;  // 5 minutes
    int reconciliationTimeoutMs = 5000;
    int maxRetries = 3;
    int retryDelayMs = 1000;
    
    // Health check settings
    int healthCheckIntervalMs = 10000;        // 10 seconds
    int maxSequenceGap = 100;
    int maxEventAge_ms = 60000;               // 1 minute
    
    nlohmann::json toJson() const;
    static PositionSyncConfig fromJson(const nlohmann::json& j);
};

/**
 * Position update request/response for direct queries
 */
struct PositionRequest {
    enum Type {
        GET_POSITION,
        GET_ALL_POSITIONS,
        GET_STRATEGY_POSITIONS,
        GET_SYMBOL_POSITIONS,
        FORCE_RECONCILE
    };
    
    std::string requestId;
    Type requestType;
    std::string symbol;
    std::string exchangeId;
    std::string strategyId;
    
    nlohmann::json toJson() const;
    static PositionRequest fromJson(const nlohmann::json& j);
};

struct PositionResponse {
    std::string requestId;
    bool success;
    std::string error;
    std::vector<Position> positions;
    PositionSyncStatus status;
    
    nlohmann::json toJson() const;
    static PositionResponse fromJson(const nlohmann::json& j);
};

} // namespace sync
} // namespace risk  
} // namespace trading