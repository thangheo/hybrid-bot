/**
 * Position Publisher - Publishes position events to ZMQ bus
 * 
 * File: position_publisher.hpp
 * Created: 2025-11-09
 */
#pragma once

#include "position_sync_types.hpp"
#include "communication/zmq_server_client.hpp"
#include "risk/portfolio/position_tracker.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <condition_variable>

namespace trading {
namespace risk {
namespace sync {

/**
 * Position Publisher publishes position events to ZMQ message bus
 * 
 * Features:
 * - Event batching for performance
 * - Priority-based message handling  
 * - Sequence number management
 * - Health monitoring
 */
class PositionPublisher {
public:
    /**
     * Constructor
     * @param config Position sync configuration
     * @param zmqContext Shared ZMQ context
     */
    explicit PositionPublisher(
        const PositionSyncConfig& config,
        std::shared_ptr<zmq::context_t> zmqContext);
    
    /**
     * Destructor
     */
    ~PositionPublisher();
    
    // Disable copy/move
    PositionPublisher(const PositionPublisher&) = delete;
    PositionPublisher& operator=(const PositionPublisher&) = delete;
    PositionPublisher(PositionPublisher&&) = delete;
    PositionPublisher& operator=(PositionPublisher&&) = delete;
    
    /**
     * Initialize publisher
     * @return true if successful
     */
    bool initialize();
    
    /**
     * Start publishing service
     * @return true if successful
     */
    bool start();
    
    /**
     * Stop publishing service
     */
    void stop();
    
    /**
     * Shutdown publisher
     */
    void shutdown();
    
    /**
     * Connect to position tracker for automatic event publishing
     * @param positionTracker Shared position tracker instance
     */
    void connectToPositionTracker(std::shared_ptr<PositionTracker> positionTracker);
    
    /**
     * Manually publish a position event
     * @param event Position event to publish
     * @return true if successful
     */
    bool publishEvent(const PositionEvent& event);
    
    /**
     * Publish position snapshot for full sync
     * @param snapshot Position snapshot
     * @return true if successful  
     */
    bool publishSnapshot(const PositionSnapshot& snapshot);
    
    /**
     * Get publisher status
     * @return Current status
     */
    PositionSyncStatus getStatus() const;
    
    /**
     * Check if publisher is healthy
     * @return true if healthy
     */
    bool isHealthy() const;
    
    /**
     * Get next sequence number
     * @return Next sequence number
     */
    uint64_t getNextSequenceNumber();
    
private:
    // Configuration
    PositionSyncConfig config_;
    std::shared_ptr<zmq::context_t> zmqContext_;
    
    // ZMQ publisher
    std::unique_ptr<zmq::socket_t> publisher_;
    
    // Position tracker connection
    std::weak_ptr<PositionTracker> positionTracker_;
    
    // State management
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> healthy_{true};
    
    // Sequence number management  
    std::atomic<uint64_t> sequenceNumber_{0};
    
    // Event queue with priority
    struct PriorityEvent {
        PositionEvent event;
        std::chrono::system_clock::time_point queueTime;
        
        bool operator<(const PriorityEvent& other) const {
            // Higher priority first, then FIFO for same priority
            if (event.priority != other.event.priority) {
                return event.priority < other.event.priority;
            }
            return queueTime > other.queueTime; // Earlier time has higher priority
        }
    };
    
    mutable std::mutex queueMutex_;
    std::priority_queue<PriorityEvent> eventQueue_;
    std::condition_variable queueCondition_;
    
    // Worker thread
    std::thread workerThread_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    size_t publishedEvents_{0};
    size_t failedEvents_{0};
    size_t queuedEvents_{0};
    std::chrono::system_clock::time_point lastPublishTime_;
    std::string lastError_;
    
    // Internal methods
    void workerLoop();
    bool publishEventInternal(const PositionEvent& event);
    void processBatch();
    std::vector<PositionEvent> getBatch();
    
    // Position tracker callbacks
    void onPositionUpdate(const Position& position, const std::string& updateType);
    
    // Event creation helpers
    PositionEvent createPositionEvent(
        const Position& position, 
        PositionEventType eventType,
        const std::string& source = "position_tracker");
        
    // Health monitoring
    void updateHealth();
    bool checkZmqHealth();
    
    // JSON serialization helpers
    std::string serializeEvent(const PositionEvent& event);
    std::string serializeSnapshot(const PositionSnapshot& snapshot);
};

} // namespace sync
} // namespace risk
} // namespace trading