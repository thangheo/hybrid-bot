/**
 * @file zmq_server.hpp
 * @brief Generic, production-ready ZeroMQ server for all trading system communications
 * 
 * This server handles multiple message types across different topics with:
 * - High-performance message routing
 * - Comprehensive error handling and recovery
 * - Metrics and monitoring
 * - Thread-safe operations
 * - Configurable endpoints and topics
 * 
 * @author Trading System Team
 * @date September 17, 2025
 */

#pragma once

#include <zmq.hpp>
#include <thread>
#include <chrono>
#include <string>
#include <memory>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <vector>
#include <optional>
#include <any>
#include <typeinfo>
#include <type_traits>

// System headers
#include <common/logger.hpp>
#include <common/types.hpp>
#include "communication/zmq_config.hpp"
#include <google/protobuf/message.h>
#include <google/protobuf/message_lite.h>

namespace trading {
namespace communication {


/**
 * Request handler callback type
 */
/**
 * Generic ZeroMQ Server for all trading system communications
 */
class GenericZmqServer {
public:
    std::string base64_decode_safe(const std::string& input) const ;

    /**
     * Constructor with configuration
     */
    explicit GenericZmqServer(const ZmqServerConfig& config = ZmqServerConfig{});
    
    /**
     * Destructor - ensures clean shutdown
     */
    ~GenericZmqServer();
    
    // Disable copy operations
    GenericZmqServer(const GenericZmqServer&) = delete;
    GenericZmqServer& operator=(const GenericZmqServer&) = delete;
    
    // Enable move operations
    GenericZmqServer(GenericZmqServer&&) noexcept = default;
    GenericZmqServer& operator=(GenericZmqServer&&) noexcept = default;
    
    /**
     * Start the server
     */
    bool start();
    
    /**
     * Stop the server gracefully
     */
    void stop();
    
    /**
     * Get current server state
     */
    ServerState getState() const { return state_.load(); }
    
    /**
     * Check if server is running
     */
    bool isRunning() const { return state_.load() == ServerState::RUNNING; }
    
    // =============================================================================
    // PUBLISHING INTERFACE
    // =============================================================================
    /**
     * Protobuf message handler type
     */
    // =============================================================================
    // PROTOBUF PUBLISHING INTERFACE
    // =============================================================================

    /**
     * Publish a protobuf message
     */
    template<typename ProtoMsg>
    bool publishProtobuf(const std::string& topic, const ProtoMsg& message,
                        MessagePriority priority = MessagePriority::NORMAL,
                        const std::string& correlation_id = "");

    /**
     * Publish protobuf with type information for easier deserialization
     */
    template<typename ProtoMsg>
    bool publishProtobufTyped(const std::string& topic, const ProtoMsg& message,
                            MessagePriority priority = MessagePriority::NORMAL,
                            const std::string& correlation_id = "");
    /**
     * Publish a generic message (non-blocking)
     */
    bool publishAsync(const std::string& topic, const std::string& data, 
                     MessagePriority priority = MessagePriority::NORMAL,
                     const std::string& correlation_id = "");
    // pushToIntelQueue
    bool pushToIntelQueue(GenericMessage&& message, const std::string& correlation_id);
    // pushToInternalQueue
    bool pushToInternalQueue(GenericMessage&& message, const std::string& correlation_id);
    /**
     * Publish a message synchronously (blocking until sent)
     */
    bool publishSync(const std::string& topic, const std::string& data, 
                    MessagePriority priority = MessagePriority::NORMAL,
                    const std::string& correlation_id = "");
    
    /**
     * Template method for publishing typed messages
     */
    template<typename T>
    bool publish(const std::string& topic, const T& message, 
                MessagePriority priority = MessagePriority::NORMAL,
                const std::string& correlation_id = "");
    
    /**
     * Publish with metadata
     */
    bool publishWithMetadata(const std::string& topic, const std::string& data,
                           const std::unordered_map<std::string, std::string>& metadata,
                           MessagePriority priority = MessagePriority::NORMAL);
    // publish protobuf intel message
    
    // =============================================================================
    // REQUEST-REPLY INTERFACE
    // =============================================================================
    
    /**
     * Register a request handler for a specific request type
     */
    void registerRequestHandler(const std::string& request_type, RequestHandler handler);
    
    /**
     * Unregister a request handler
     */
    void unregisterRequestHandler(const std::string& request_type);
    
    // =============================================================================
    // TOPIC MANAGEMENT
    // =============================================================================
    
    /**
     * Register a topic filter (only matching topics will be published)
     */
    void addTopicFilter(const std::string& topic_pattern);
    
    /**
     * Remove a topic filter
     */
    void removeTopicFilter(const std::string& topic_pattern);
    
    /**
     * Clear all topic filters
     */
    void clearTopicFilters();
    
    /**
     * Check if topic matches filters
     */
    bool isTopicAllowed(const std::string& topic) const;
    
    // =============================================================================
    // MONITORING AND STATISTICS
    // =============================================================================
    
    /**
     * Get server statistics
     */
    const ServerStatistics& getStatistics() const { return stats_; }
    
    /**
     * Reset statistics
     */
    void resetStatistics() { stats_.reset(); }
    
    /**
     * Get queue size
     */
    size_t getQueueSize(std::string queue_type) const {
        // std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_type == "intel") {
            return intel_priority_queue_.size();
        } else if (queue_type == "internal") {
            return internal_priority_queue_.size();
        }
        return 0;
    }
    
    /**
     * Get queue utilization percentage (0.0 - 1.0)
     */
    double getQueueUtilization(std::string queue_type) const {
        //for intel and internal queues
        std::lock_guard<std::mutex> lock(intel_queue_mutex_);
        std::lock_guard<std::mutex> lock2(internal_queue_mutex_);
        if (queue_type == "intel") {
            return static_cast<double>(getQueueSize("intel")) / config_.max_intel_queue_size;
        } else if (queue_type == "internal") {
            return static_cast<double>(getQueueSize("internal")) / config_.max_internal_queue_size;
        }
        return 0.0;
    }
    
    /**
     * Enable/disable detailed logging
     */
    void setLoggingEnabled(bool enabled) { logging_enabled_ = enabled; }
    
    // =============================================================================
    // HEALTH AND DIAGNOSTICS
    // =============================================================================
    
    /**
     * Perform health check
     */
    bool healthCheck() const;
    
    /**
     * Get last error message
     */
    std::string getLastError() const {
        std::lock_guard<std::mutex> lock(error_mutex_);
        return last_error_;
    }
    
    /**
     * Send heartbeat message
     */
    void sendHeartbeat();

private:

    // =============================================================================
    // CONFIGURATION AND STATE
    // =============================================================================
    ZmqServerConfig config_;
    std::atomic<ServerState> state_{ServerState::STOPPED};
    mutable ServerStatistics stats_;
    
    // =============================================================================
    // ZEROMQ COMPONENTS
    // =============================================================================
    // std::unique_ptr<zmq::context_t> context_;
    //use shared_ptr for context to allow sharing between multiple server instances if needed
    // std::shared_ptr<zmq::context_t> context_;
    //we should have separate contexts for intel and internal publishers
    std::shared_ptr<zmq::context_t> intel_context_;
    std::shared_ptr<zmq::context_t> internal_context_;
    //separate publisher for internal high-frequency messages and market intel
    std::unique_ptr<zmq::socket_t> intel_publisher_socket_;
    std::unique_ptr<zmq::socket_t> internal_publisher_socket_;
    std::unique_ptr<zmq::socket_t> request_socket_;
    std::mutex intel_send_mutex_;
    // static std::mutex internal_send_mutex_;

    // =============================================================================
    // THREADING
    // =============================================================================
    // std::vector<std::thread> publisher_threads_;
    //should have separate threads for intel and internal publishers
    std::vector<std::thread> intel_threads_;
    std::vector<std::thread> internal_threads_;
    std::vector<std::thread> request_threads_;
    std::thread monitoring_thread_;
    std::thread heartbeat_thread_;
    
    // =============================================================================
    // MESSAGE QUEUING
    // =============================================================================
    // mutable std::mutex queue_mutex_;
    //should have separate mutexes for intel and internal queues
    mutable std::mutex intel_queue_mutex_;
    mutable std::mutex internal_queue_mutex_;
    // std::queue<GenericMessage> message_queue_;
    //we create 2 priority queues for intel and internal messages
    // std::queue<GenericMessage> message_queue_; // For normal priority messages
    std::queue<GenericMessage> intel_priority_queue_; // For high-priority intel messages
    std::queue<GenericMessage> internal_priority_queue_; // For high-priority internal messages
    // std::condition_variable queue_cv_;
    std::condition_variable intel_queue_cv_;
    std::condition_variable internal_queue_cv_;
    
    // Priority queues for different message types
    std::priority_queue<GenericMessage, std::vector<GenericMessage>, 
                       std::function<bool(const GenericMessage&, const GenericMessage&)>> priority_queue_;
    
    // =============================================================================
    // REQUEST HANDLING
    // =============================================================================
    mutable std::mutex handlers_mutex_;
    std::unordered_map<std::string, RequestHandler> request_handlers_;
    
    // =============================================================================
    // TOPIC FILTERING
    // =============================================================================
    mutable std::mutex filters_mutex_;
    std::vector<std::string> topic_filters_;
    bool use_topic_filters_ = false;
    
    // =============================================================================
    // ERROR HANDLING AND LOGGING
    // =============================================================================
    mutable std::mutex error_mutex_;
    std::string last_error_;
    std::atomic<bool> logging_enabled_{false};
    
    // =============================================================================
    // PRIVATE METHODS
    // =============================================================================
    
    /**
     * Initialize ZeroMQ sockets
     */
    bool initializeSockets();
    
    /**
     * Cleanup resources
     */
    void cleanup();
    
    /**
     * Publisher worker thread function
     */
    void intelPublisherWorker();
    void internalPublisherWorker();
    // calculateInlineThreshold
    size_t calculateInlineThreshold() const;
    /**
     * Request handler worker thread function
     */
    void requestWorker();
    
    /**
     * Monitoring thread function
     */
    void monitoringWorker();
    
    /**
     * Heartbeat thread function
     */
    void heartbeatWorker();
    
    /**
     * Process a single message from queue
     */
    // bool processMessage(const GenericMessage& message);
    // bool processMessage(const GenericMessage& message, zmq::socket_t& socket);
    bool processIntelMessage(const GenericMessage& message);
    bool processInternalMessage(const GenericMessage& message);
    
    /**
     * Handle incoming request
     */
    void handleRequest(zmq::message_t& identity, zmq::message_t& request);
    
    /**
     * Send error response
     */
    void sendErrorResponse(zmq::message_t& identity, const std::string& error_msg,
                          const std::string& request_id = "");
    
    /**
     * Serialize message with metadata
     */
    std::string serializeWithMetadata(const std::string& data,
                                    const std::unordered_map<std::string, std::string>& metadata) const;
    
    /**
     * Deserialize message with metadata
     */
    std::pair<std::string, std::unordered_map<std::string, std::string>>
    deserializeWithMetadata(const std::string& serialized_data) const;
    
    /**
     * Log error message
     */
    void logError(const std::string& error_msg);
    
    /**
     * Log info message
     */
    void logInfo(const std::string& info_msg);
    
    /**
     * Update statistics
     */
    void updateStats(const std::string& operation, size_t bytes = 0);
    
    /**
     * Check if message should be dropped due to queue pressure
     */
    bool shouldDropMessage(const GenericMessage& message) const;
    
    /**
     * Reconnect socket after failure
     */
    bool reconnectSocket(zmq::socket_t& socket, const std::string& endpoint);
    
    /**
     * Validate topic format
     */
    static bool isValidTopic(const std::string& topic);
    
    /**
     * Create message priority comparator
     */
    static std::function<bool(const GenericMessage&, const GenericMessage&)> 
    createPriorityComparator();
};

// =============================================================================
// TEMPLATE IMPLEMENTATIONS
// =============================================================================

template<typename T>
bool GenericZmqServer::publish(const std::string& topic, const T& message, 
                              MessagePriority priority, const std::string& correlation_id) {
    static_assert(std::is_trivially_copyable_v<T> || 
                  std::is_same_v<T, std::string> ||
                  requires(const T& t) { t.SerializeAsString(); } ||
                  requires(const T& t) { std::to_string(t); },
                  "Type T must be serializable");
    
    std::string serialized_data;
    
    // Handle different serialization methods
    if constexpr (std::is_same_v<T, std::string>) {
        serialized_data = message;
    } else if constexpr (requires(const T& t) { t.SerializeAsString(); }) {
        // Protocol Buffers serialization
        serialized_data = message.SerializeAsString();
    } else if constexpr (requires(const T& t) { std::to_string(t); }) {
        // Built-in types that can be converted to string
        serialized_data = std::to_string(message);
    } else {
        // Fallback for trivially copyable types
        serialized_data = std::string(reinterpret_cast<const char*>(&message), sizeof(T));
    }
    
    return publishAsync(topic, serialized_data, priority, correlation_id);
}

// =============================================================================
// Protobuf template implementations
// =============================================================================
template<typename ProtoMsg>
bool GenericZmqServer::publishProtobuf(const std::string& topic, const ProtoMsg& message,
                                      MessagePriority priority, const std::string& correlation_id) {
    static_assert(std::is_base_of_v<google::protobuf::MessageLite, ProtoMsg>,
                  "ProtoMsg must be a protobuf message type");

    std::string serialized_data;
    if (!message.SerializeToString(&serialized_data)) {
        logError("Failed to serialize protobuf message for topic: " + topic);
        return false;
    }

    return publishAsync(topic, serialized_data, priority, correlation_id);
}

template<typename ProtoMsg>
bool GenericZmqServer::publishProtobufTyped(const std::string& topic, const ProtoMsg& message,
                                           MessagePriority priority, const std::string& correlation_id) {
    static_assert(std::is_base_of_v<google::protobuf::MessageLite, ProtoMsg>,
                  "ProtoMsg must be a protobuf message type");

    std::string serialized_data;
    if (!message.SerializeToString(&serialized_data)) {
        logError("Failed to serialize protobuf message for topic: " + topic);
        return false;
    }

    // Add type information to metadata
    std::unordered_map<std::string, std::string> metadata;
    metadata["message_type"] = message.GetTypeName();
    metadata["encoding"] = "protobuf";
    metadata["correlation_id"] = correlation_id.empty() ? GenericMessage::generateCorrelationId() : correlation_id;
    metadata["priority"] = std::to_string(static_cast<int>(priority));

    return publishWithMetadata(topic, serialized_data, metadata, priority);
}

} // namespace communication
} // namespace trading