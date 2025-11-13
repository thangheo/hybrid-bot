/**
 * Position Subscriber - Subscribes to position events from ZMQ bus
 * 
 * File: position_subscriber.hpp
 * Created: 2025-11-09
 */
#pragma once

#include "position_sync_types.hpp"
#include "communication/zmq_server_client.hpp"
#include "risk/risk_types.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <functional>

namespace trading {
namespace risk {
namespace sync {

/**
 * Position event callback function type
 */
using PositionEventCallback = std::function<void(const PositionEvent&)>;

/**
 * Position Subscriber subscribes to position events and maintains local cache
 * 
 * Features:
 * - Real-time position event processing
 * - Local position cache for fast lookups
 * - Event ordering and gap detection
 * - Cache TTL management
 * - Health monitoring
 */
class PositionSubscriber {
public:
    /**
     * Constructor
     * @param config Position sync configuration
     * @param zmqContext Shared ZMQ context
     */
    explicit PositionSubscriber(
        const PositionSyncConfig& config,
        std::shared_ptr<zmq::context_t> zmqContext);
    
    /**
     * Destructor
     */
    ~PositionSubscriber();
    
    // Disable copy/move
    PositionSubscriber(const PositionSubscriber&) = delete;
    PositionSubscriber& operator=(const PositionSubscriber&) = delete;
    PositionSubscriber(PositionSubscriber&&) = delete;
    PositionSubscriber& operator=(PositionSubscriber&&) = delete;
    
    /**
     * Initialize subscriber
     * @return true if successful
     */
    bool initialize();
    
    /**
     * Start subscription service
     * @return true if successful
     */
    bool start();
    
    /**
     * Stop subscription service
     */
    void stop();
    
    /**
     * Shutdown subscriber
     */
    void shutdown();
    
    /**
     * Register callback for position events
     * @param callback Event callback function
     */
    void registerEventCallback(const PositionEventCallback& callback);
    
    /**
     * Check if position exists in cache
     * @param symbol Trading symbol
     * @param exchangeId Exchange identifier
     * @return true if position exists
     */
    bool hasPosition(const std::string& symbol, const std::string& exchangeId = "") const;
    
    /**
     * Get position from cache
     * @param symbol Trading symbol  
     * @param exchangeId Exchange identifier
     * @return Position if found, nullopt otherwise
     */
    std::optional<Position> getPosition(const std::string& symbol, const std::string& exchangeId = "") const;
    
    /**
     * Get all positions from cache
     * @return Vector of all cached positions
     */
    std::vector<Position> getAllPositions() const;
    
    /**
     * Get positions for specific strategy
     * @param strategyId Strategy identifier
     * @return Vector of strategy positions
     */
    std::vector<Position> getStrategyPositions(const std::string& strategyId) const;
    
    /**
     * Get positions for specific symbol across all exchanges
     * @param symbol Trading symbol
     * @return Vector of positions for symbol
     */
    std::vector<Position> getSymbolPositions(const std::string& symbol) const;
    
    /**
     * Get subscriber status
     * @return Current status
     */
    PositionSyncStatus getStatus() const;
    
    /**
     * Check if subscriber is healthy
     * @return true if healthy
     */
    bool isHealthy() const;
    
    /**
     * Force cache refresh from position tracker
     * @return true if successful
     */
    bool refreshCache();

    /**
     * Deliver an event directly to the subscriber for synchronous processing
     * This bypasses ZMQ and updates the local cache immediately.
     */
    void deliverEventDirect(const PositionEvent& event);
    
    /**
     * Clear position cache
     */
    void clearCache();
    
    /**
     * Get cache statistics
     * @return Cache hit/miss statistics
     */
    struct CacheStats {
        size_t totalEntries;
        size_t validEntries;  
        size_t expiredEntries;
        size_t hits;
        size_t misses;
        double hitRate;
    };
    CacheStats getCacheStats() const;
    
private:
    // Configuration
    PositionSyncConfig config_;
    std::shared_ptr<zmq::context_t> zmqContext_;
    
    // ZMQ subscriber
    std::unique_ptr<zmq::socket_t> subscriber_;
    
    // State management
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> healthy_{true};
    
    // Sequence tracking
    std::atomic<uint64_t> lastSequenceNumber_{0};
    std::atomic<size_t> sequenceGaps_{0};
    
    // Position cache
    struct CachedPosition {
        Position position;
        std::chrono::system_clock::time_point cacheTime;
        std::chrono::system_clock::time_point lastUpdate;
        bool isValid;
        
        bool isExpired(int timeoutMs) const {
            auto now = std::chrono::system_clock::now();
            auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - cacheTime);
            return age.count() > timeoutMs;
        }
    };
    
    mutable std::shared_mutex cacheMutex_;
    std::unordered_map<std::string, CachedPosition> positionCache_;  // key: symbol + exchangeId
    
    // Event callbacks
    mutable std::mutex callbackMutex_;
    std::vector<PositionEventCallback> eventCallbacks_;
    
    // Worker thread
    std::thread workerThread_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    // Make these atomic so const methods can update/read them safely
    mutable std::atomic<size_t> receivedEvents_{0};
    mutable std::atomic<size_t> processedEvents_{0};
    mutable std::atomic<size_t> failedEvents_{0};
    mutable std::atomic<size_t> cacheHits_{0};
    mutable std::atomic<size_t> cacheMisses_{0};
    std::chrono::system_clock::time_point lastEventTime_;
    std::string lastError_;
    
    // Internal methods
    void workerLoop();
    void processEvent(const PositionEvent& event);
    void updateCache(const PositionEvent& event);
    void notifyCallbacks(const PositionEvent& event);
    
    // Cache management
    std::string createCacheKey(const std::string& symbol, const std::string& exchangeId) const;
    void cleanExpiredEntries();
    Position createPositionFromEvent(const PositionEvent& event);
    
    // Message handling
    bool receiveMessage(std::string& topic, std::string& data);
    PositionEvent deserializeEvent(const std::string& data);
    
    // Health monitoring
    void updateHealth();
    bool checkSequenceGap(uint64_t sequenceNumber);
    void handleSequenceGap(uint64_t expected, uint64_t received);
    
    // Validation
    bool validateEvent(const PositionEvent& event);
    bool isEventRecent(const PositionEvent& event);
};

} // namespace sync
} // namespace risk
} // namespace trading