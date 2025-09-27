/**
 * @file zmq_client.hpp
 * @brief Clean ZeroMQ Client library for trading system communications
 * 
 * This client provides both subscriber and request-reply functionality
 * with robust message handling and proper multipart message support.
 * 
 * @author Trading System Team
 * @date September 27, 2025
 */

#pragma once

#include <zmq.hpp>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <memory>
#include <future>
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include "communication/zmq_config.hpp"

namespace trading {
namespace communication {

/**
 * Message handler callback type for subscriptions
 */
using MessageHandler = std::function<void(const std::string& topic, 
                                        const std::string& data,
                                        const std::unordered_map<std::string, std::string>& metadata)>;

/**
 * Client statistics for monitoring
 */
struct ClientStats {
    std::atomic<uint64_t> messages_received{0};
    std::atomic<uint64_t> requests_sent{0};
    std::atomic<uint64_t> requests_failed{0};
    std::atomic<uint64_t> connection_attempts{0};
    std::chrono::steady_clock::time_point last_message_time;
    std::chrono::steady_clock::time_point connection_time;
};

/**
 * Clean ZeroMQ Client for trading system communications
 */
class GenericZmqClient {
public:
    /**
     * Constructor with configuration
     */
    explicit GenericZmqClient(const ZmqClientConfig& config = ZmqClientConfig{});
    
    /**
     * Destructor
     */
    ~GenericZmqClient();
    
    // Disable copy operations
    GenericZmqClient(const GenericZmqClient&) = delete;
    GenericZmqClient& operator=(const GenericZmqClient&) = delete;
    
    // =============================================================================
    // CONNECTION MANAGEMENT
    // =============================================================================
    
    /**
     * Connect to the ZMQ server
     */
    bool connect();
    
    /**
     * Disconnect from the ZMQ server
     */
    void disconnect();
    
    /**
     * Check if client is connected
     */
    bool isConnected() const { return connected_.load(); }
    
    // Convenience aliases
    bool start() { return connect(); }
    bool stop() { disconnect(); return true; }
    
    // =============================================================================
    // SUBSCRIPTION INTERFACE
    // =============================================================================
    
    /**
     * Subscribe to a specific topic with a message handler
     */
    bool subscribe(const std::string& topic, MessageHandler handler);
    
    /**
     * Unsubscribe from a topic
     */
    bool unsubscribe(const std::string& topic);
    
    /**
     * Subscribe to multiple topics with the same handler
     */
    bool subscribeMultiple(const std::vector<std::string>& topics, MessageHandler handler);
    
    /**
     * Unsubscribe from all topics
     */
    void unsubscribeAll();
    
    // =============================================================================
    // REQUEST-REPLY INTERFACE
    // =============================================================================
    
    /**
     * Send synchronous request and wait for response
     */
    std::string sendRequest(const std::string& request_type, 
                           const std::string& data,
                           const std::unordered_map<std::string, std::string>& metadata = {});
    
    /**
     * Send asynchronous request and return future
     */
    std::future<std::string> sendRequestAsync(const std::string& request_type,
                                             const std::string& data,
                                             const std::unordered_map<std::string, std::string>& metadata = {});
    
    /**
     * Send request with JSON data
     */
    template<typename T>
    std::string sendJsonRequest(const std::string& request_type, const T& json_data,
                               const std::unordered_map<std::string, std::string>& metadata = {});
    
    // =============================================================================
    // UTILITY METHODS
    // =============================================================================
    
    /**
     * Get connection statistics
     */
    const ClientStats& getStats() const { return stats_; }
    
    /**
     * Get last error message
     */
    std::string getLastError() const;
    
    /**
     * Enable/disable logging
     */
    void setLoggingEnabled(bool enabled) { logging_enabled_ = enabled; }
    bool isLoggingEnabled() const { return logging_enabled_.load(); }

private:
    // Configuration
    ZmqClientConfig config_;
    
    // ZeroMQ components
    std::shared_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> subscriber_socket_;
    std::unique_ptr<zmq::socket_t> request_socket_;
    
    // Connection state
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> logging_enabled_{true};
    
    // Subscriber thread and handlers
    std::thread subscriber_thread_;
    std::unordered_map<std::string, MessageHandler> message_handlers_;
    mutable std::mutex handlers_mutex_;
    
    // Statistics and monitoring
    mutable ClientStats stats_;
    mutable std::mutex error_mutex_;
    std::string last_error_;
    
    // =============================================================================
    // PRIVATE METHODS
    // =============================================================================
    
    bool initializeSockets();
    void cleanup();
    void subscriberWorker();
    void handleMessage(const std::string& topic, const std::string& data);
    
    // Message handling
    std::pair<std::string, std::unordered_map<std::string, std::string>> 
    parseMessage(const std::string& raw_data) const;
    
    std::string createRequestMessage(const std::string& request_type,
                                   const std::string& data,
                                   const std::unordered_map<std::string, std::string>& metadata) const;
    
    // Logging and utilities
    void logError(const std::string& error_msg);
    void logInfo(const std::string& info_msg);
    std::string generateRequestId() const;
};

// =============================================================================
// TEMPLATE IMPLEMENTATIONS
// =============================================================================

template<typename T>
std::string GenericZmqClient::sendJsonRequest(const std::string& request_type, const T& json_data,
                                             const std::unordered_map<std::string, std::string>& metadata) {
    nlohmann::json data;
    
    if constexpr (std::is_same_v<T, nlohmann::json>) {
        data = json_data;
    } else if constexpr (std::is_same_v<T, std::string>) {
        data = nlohmann::json::parse(json_data);
    } else {
        data = json_data;  // Assume T is json-serializable
    }
    
    return sendRequest(request_type, data.dump(), metadata);
}

} // namespace communication
} // namespace trading