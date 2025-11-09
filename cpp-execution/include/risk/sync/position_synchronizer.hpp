/**
 * Position Synchronizer - Coordinates position synchronization between modules
 * 
 * File: position_synchronizer.hpp  
 * Created: 2025-11-09
 */
#pragma once

#include "position_publisher.hpp"
#include "position_subscriber.hpp"
#include "position_sync_types.hpp"
#include "risk/portfolio/position_tracker.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

namespace trading {
namespace risk {
namespace sync {

/**
 * Position Synchronizer coordinates position sync between modules
 * 
 * Features:
 * - Manages publisher and subscriber lifecycle
 * - Periodic reconciliation between modules  
 * - Health monitoring and recovery
 * - Performance metrics
 */
class PositionSynchronizer {
public:
    /**
     * Constructor
     * @param config Position sync configuration
     * @param zmqContext Shared ZMQ context
     */
    explicit PositionSynchronizer(
        const PositionSyncConfig& config,
        std::shared_ptr<zmq::context_t> zmqContext);
    
    /**
     * Destructor
     */
    ~PositionSynchronizer();
    
    // Disable copy/move
    PositionSynchronizer(const PositionSynchronizer&) = delete;
    PositionSynchronizer& operator=(const PositionSynchronizer&) = delete;
    PositionSynchronizer(PositionSynchronizer&&) = delete;
    PositionSynchronizer& operator=(PositionSynchronizer&&) = delete;
    
    /**
     * Initialize synchronizer
     * @return true if successful
     */
    bool initialize();
    
    /**
     * Start synchronization service
     * @param positionTracker Shared position tracker instance
     * @return true if successful
     */
    bool start(std::shared_ptr<PositionTracker> positionTracker);
    
    /**
     * Stop synchronization service
     */
    void stop();
    
    /**
     * Shutdown synchronizer
     */
    void shutdown();
    
    /**
     * Get publisher instance
     * @return Publisher instance
     */
    std::shared_ptr<PositionPublisher> getPublisher() const;
    
    /**
     * Get subscriber instance
     * @return Subscriber instance
     */
    std::shared_ptr<PositionSubscriber> getSubscriber() const;
    
    /**
     * Register position event callback (delegates to subscriber)
     * @param callback Event callback function
     */
    void registerPositionEventCallback(const PositionEventCallback& callback);
    
    /**
     * Check if position exists (delegates to subscriber)
     * @param symbol Trading symbol
     * @param exchangeId Exchange identifier
     * @return true if position exists
     */
    bool hasPosition(const std::string& symbol, const std::string& exchangeId = "") const;
    
    /**
     * Get position (delegates to subscriber)
     * @param symbol Trading symbol
     * @param exchangeId Exchange identifier  
     * @return Position if found, nullopt otherwise
     */
    std::optional<Position> getPosition(const std::string& symbol, const std::string& exchangeId = "") const;
    
    /**
     * Get all positions (delegates to subscriber)
     * @return Vector of all positions
     */
    std::vector<Position> getAllPositions() const;
    
    /**
     * Force reconciliation between modules
     * @return true if successful
     */
    bool forceReconciliation();
    
    /**
     * Get overall synchronization status
     * @return Sync status combining publisher and subscriber
     */
    PositionSyncStatus getOverallStatus() const;
    
    /**
     * Check if synchronizer is healthy
     * @return true if healthy
     */
    bool isHealthy() const;
    
    /**
     * Get performance metrics
     */
    struct SyncMetrics {
        // Publisher metrics
        size_t publishedEvents;
        size_t publishFailures;
        double publishRate;  // events per second
        
        // Subscriber metrics  
        size_t receivedEvents;
        size_t processedEvents;
        size_t cacheHits;
        size_t cacheMisses;
        double cacheHitRate;
        
        // Overall metrics
        std::chrono::milliseconds avgLatency;
        std::chrono::milliseconds maxLatency;
        size_t sequenceGaps;
        size_t reconciliationCount;
        std::chrono::system_clock::time_point lastReconciliation;
        
        nlohmann::json toJson() const;
    };
    SyncMetrics getMetrics() const;
    
private:
    // Configuration
    PositionSyncConfig config_;
    std::shared_ptr<zmq::context_t> zmqContext_;
    
    // Components
    std::atomic<std::shared_ptr<PositionPublisher>> publisher_{nullptr};
    std::atomic<std::shared_ptr<PositionSubscriber>> subscriber_{nullptr};
    std::weak_ptr<PositionTracker> positionTracker_;
    
    // State management
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> healthy_{true};
    // Track number of in-flight calls into the synchronizer (getPosition/hasPosition/etc.)
    mutable std::atomic<size_t> inFlightCalls_{0};
    
    // Reconciliation thread
    std::thread reconciliationThread_;
    std::atomic<bool> stopReconciliation_{false};
    
    // Metrics tracking
    mutable std::mutex metricsMutex_;
    size_t reconciliationCount_{0};
    std::chrono::system_clock::time_point lastReconciliationTime_;
    std::chrono::system_clock::time_point startTime_;
    
    // Internal methods
    void reconciliationLoop();
    bool performReconciliation();
    bool comparePositions();
    void handleDiscrepancy(const std::string& positionKey, 
                          const Position& trackerPosition,
                          const std::optional<Position>& subscriberPosition);
    
    // Health monitoring
    void updateOverallHealth();
    bool checkComponentsHealth();
    
    // Metrics calculation
    SyncMetrics calculateMetrics() const;
    
    // Configuration validation
    bool validateConfig() const;
};

/**
 * Factory function to create position synchronizer
 * @param config Position sync configuration
 * @param zmqContext Shared ZMQ context
 * @return Unique pointer to position synchronizer
 */
std::unique_ptr<PositionSynchronizer> createPositionSynchronizer(
    const PositionSyncConfig& config,
    std::shared_ptr<zmq::context_t> zmqContext);

} // namespace sync
} // namespace risk
} // namespace trading