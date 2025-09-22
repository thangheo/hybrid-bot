/**
 * @file zmq_server.cpp
 * @brief Implementation of Generic ZeroMQ Server for trading system communications
 * 
 * @author Trading System Team  
 * @date September 17, 2025
 */

#include "communication/zmq_server.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <regex>
#include <random>
#include <nlohmann/json.hpp>
#include <regex>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace trading {
namespace communication {
    

// =============================================================================
// HELPER FUNCTIONS
// =============================================================================

std::string GenericMessage::generateCorrelationId() {
    static thread_local std::mt19937 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<uint64_t> dis;
    
    std::ostringstream oss;
    oss << std::hex << dis(gen);
    return oss.str();
}

// Replace characters not allowed in topic names with underscore
static std::string sanitizeTopic(const std::string& topic) {
    try {
        static const std::regex invalid(R"([^a-zA-Z0-9._-])");
        return std::regex_replace(topic, invalid, "_");
    } catch (const std::regex_error&) {
        // Fallback: simple character replace for common problematic chars
        std::string out = topic;
        for (auto &c : out) {
            if (!( (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) {
                c = '_';
            }
        }
        return out;
    }
}


// =============================================================================
// CONSTRUCTOR AND DESTRUCTOR
// =============================================================================

GenericZmqServer::GenericZmqServer(const ZmqServerConfig& config) 
    : config_(config), priority_queue_(createPriorityComparator()) {

    if (!config_.validate()) {
        throw std::invalid_argument("Invalid ZeroMQ server configuration");
    }
    
    logInfo("Generic ZMQ Server created with config validation passed");
}

GenericZmqServer::~GenericZmqServer() {
    if (state_.load() != ServerState::STOPPED) {
        stop();
    }
}

// =============================================================================
// SERVER LIFECYCLE
// =============================================================================

bool GenericZmqServer::start() {
    if (state_.load() != ServerState::STOPPED) {
        logError("Cannot start server - not in STOPPED state");
        return false;
    }
    
    state_.store(ServerState::STARTING);
    logInfo("Starting Generic ZMQ Server...");
    
    try {
        // Initialize ZeroMQ context and sockets
        if (!initializeSockets()) {
            state_.store(ServerState::ERROR);
            logError("Failed to initialize ZeroMQ sockets");
            return false;
        }
        state_.store(ServerState::RUNNING);
        stats_.start_time = std::chrono::steady_clock::now();
        // Start publisher threads
        intel_threads_.reserve(config_.intel_publisher_threads);
        for (int i = 0; i < config_.intel_publisher_threads; ++i) {
            intel_threads_.emplace_back(&GenericZmqServer::intelPublisherWorker, this);
        }
        internal_threads_.reserve(config_.internal_publisher_threads);
        for (int i = 0; i < config_.internal_publisher_threads; ++i) {
            internal_threads_.emplace_back(&GenericZmqServer::internalPublisherWorker, this);
        }

        // Start request handler threads
        request_threads_.reserve(config_.request_handler_threads);
        for (int i = 0; i < config_.request_handler_threads; ++i) {
            request_threads_.emplace_back(&GenericZmqServer::requestWorker, this);
        }
        
        // Start monitoring thread if enabled
        if (config_.enable_monitoring) {
            monitoring_thread_ = std::thread(&GenericZmqServer::monitoringWorker, this);
        }
        
        // Start heartbeat thread if enabled
        if (config_.enable_heartbeat) {
            heartbeat_thread_ = std::thread(&GenericZmqServer::heartbeatWorker, this);
        }
        
        state_.store(ServerState::RUNNING);
        stats_.start_time = std::chrono::steady_clock::now();
        
        logInfo("Generic ZMQ Server started successfully");
        logInfo("Intel publisher endpoint: " + config_.intel_endpoint);
        logInfo("Internal publisher endpoint: " + config_.internal_endpoint);
        logInfo("Request endpoint: " + config_.request_endpoint);
        
        return true;
        
    } catch (const std::exception& e) {
        state_.store(ServerState::ERROR);
        logError("Failed to start server: " + std::string(e.what()));
        cleanup();
        return false;
    }
}

void GenericZmqServer::stop() {
    if (state_.load() == ServerState::STOPPED) {
        return;
    }
    
    logInfo("Stopping Generic ZMQ Server...");
    state_.store(ServerState::STOPPING);
    
    // Signal context termination to wake up any blocking operations
    if (intel_context_) {
        try {
            intel_context_->shutdown();
        } catch (const zmq::error_t& e) {
            logError("Error shutting down Intel context: " + std::string(e.what()));
        }
    }

    if (internal_context_) {
        try {
            internal_context_->shutdown();
        } catch (const zmq::error_t& e) {
            logError("Error shutting down Internal context: " + std::string(e.what()));
        }
    }
    
    
    // Notify all waiting threads
    intel_queue_cv_.notify_all();
    internal_queue_cv_.notify_all();

    // Give threads time to see the state change and exit gracefully
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Join publisher threads first
    logInfo("Stopping publisher threads...");
    for (auto& thread : intel_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    intel_threads_.clear();
    
    for (auto& thread : internal_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    internal_threads_.clear();

    // Join request handler threads
    logInfo("Stopping request threads...");
    for (auto& thread : request_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    request_threads_.clear();
    
    // Join monitoring thread
    logInfo("Stopping monitoring thread...");
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    // Join heartbeat thread
    logInfo("Stopping heartbeat thread...");
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }

    // Only cleanup after ALL worker threads have stopped
    logInfo("Cleaning up resources...");
    cleanup();

    state_.store(ServerState::STOPPED);
    logInfo("Generic ZMQ Server stopped");
}

// =============================================================================
// PUBLISHING INTERFACE
// =============================================================================
// ...existing code...
bool GenericZmqServer::pushToIntelQueue(GenericMessage&& message, const std::string& correlation_id) {
    std::unique_lock<std::mutex> lock(intel_queue_mutex_);
    if (intel_priority_queue_.size() >= config_.max_intel_queue_size) {
        stats_.queue_overflows++;
        logInfo("Intel queue overflow detected");
        message.correlation_id = correlation_id;
        if (shouldDropMessage(message)) {
            stats_.messages_dropped++;
            logError("Intel message dropped due to queue overflow: " + message.topic);
            return false;
        }
        if (!intel_queue_cv_.wait_for(lock, config_.queue_timeout,
                [this]() { return intel_priority_queue_.size() < config_.max_intel_queue_size || state_.load() != ServerState::RUNNING; })) {
            stats_.messages_dropped++;
            logError("Intel message timeout while waiting for queue space: " + message.topic);
            return false;
        }
    }
    if (state_.load() != ServerState::RUNNING) {
        logError("Server stopped while waiting for intel queue space");
        return false;
    }
    intel_priority_queue_.push(std::move(message));
    intel_queue_cv_.notify_one();
    stats_.messages_queued++;
    return true;
}

bool GenericZmqServer::pushToInternalQueue(GenericMessage&& message, const std::string& correlation_id) {
    std::unique_lock<std::mutex> lock(internal_queue_mutex_);
    if (internal_priority_queue_.size() >= config_.max_internal_queue_size) {
        stats_.queue_overflows++;
        logInfo("Internal queue overflow detected");
        message.correlation_id = correlation_id;
        if (shouldDropMessage(message)) {
            stats_.messages_dropped++;
            logError("Message dropped due to queue overflow: " + message.topic);
            return false;
        }
        if (!internal_queue_cv_.wait_for(lock, config_.queue_timeout,
                [this]() { return internal_priority_queue_.size() < config_.max_internal_queue_size || state_.load() != ServerState::RUNNING; })) {
            stats_.messages_dropped++;
            logError("Message timeout while waiting for internal queue space: " + message.topic);
            return false;
        }
    }
    if (state_.load() != ServerState::RUNNING) {
        logError("Server stopped while waiting for internal queue space");
        return false;
    }
    internal_priority_queue_.push(std::move(message));
    internal_queue_cv_.notify_one();
    stats_.messages_queued++;
    return true;
}

bool GenericZmqServer::publishAsync(const std::string& topic, const std::string& data,
                                   MessagePriority priority, const std::string& correlation_id) {
    if (state_.load() != ServerState::RUNNING) {
        logError("Cannot publish - server not running");
        return false;
    }

    if (!isValidTopic(topic)) {
        logError("Invalid topic format: " + topic);
        return false;
    }

    std::string safe_topic = topic;
    GenericMessage message(std::move(safe_topic), data, priority);
    message.correlation_id = correlation_id;

    logInfo("Async Publishing message: " + topic + " with priority " + std::to_string(static_cast<int>(priority)));

    if (topic == "MARKET_INTEL") {
        return pushToIntelQueue(std::move(message), correlation_id);
    } else {
        return pushToInternalQueue(std::move(message), correlation_id);
    }
}

// ...existing code...
// bool GenericZmqServer::publishAsync(const std::string& topic, const std::string& data,
//                                    MessagePriority priority, const std::string& correlation_id) {
//     if (state_.load() != ServerState::RUNNING) {
//         logError("Cannot publish - server not running");
//         return false;
//     }
  
//     std::string safe_topic = topic;
   
//     if (!isValidTopic(safe_topic)) {
//         logError("Invalid topic format: " + topic);
//         return false;
//     }
//     // logInfo("Publishing async message on topic: " + safe_topic);
//     try {
//         // std::unique_lock<std::mutex> lock(queue_mutex_);
//         //should have separate locks for intel and internal queues
//         std::unique_lock<std::mutex> lock(internal_queue_mutex_, std::defer_lock);
//         std::unique_lock<std::mutex> lock2(intel_queue_mutex_, std::defer_lock);
//         if (topic == "MARKET_INTEL") {
//             logInfo("MARKET_INTEL Publishing Intel message: " + topic);
//             lock2.lock();
//         } else {
//             // logInfo("Publishing internal message: " + topic);
//             lock.lock();
//         }
        
//         // this is for internal messages
//         if (internal_priority_queue_.size() >= config_.max_internal_queue_size && topic != "MARKET_INTEL") {
//             stats_.queue_overflows++;
//             logInfo("Internal queue overflow detected");
//             // Drop low priority messages if queue is full
//             GenericMessage new_msg(topic, data, priority);
//             new_msg.correlation_id = correlation_id;
            
//             if (shouldDropMessage(new_msg)) {
//                 stats_.messages_dropped++;
//                 logError("Message dropped due to queue overflow: " + topic);
//                 return false;
//             }
            
//             // Wait for space or timeout
//             if (!internal_queue_cv_.wait_for(lock, config_.queue_timeout,
//                                   [this]() { return internal_priority_queue_.size() < config_.max_internal_queue_size ||
//                                             state_.load() != ServerState::RUNNING; })) {
//                 stats_.messages_dropped++;
//                 logError("Message timeout while waiting for queue space: " + topic);
//                 return false;
//             }
//         }
//         //this is for intel messages
//         else if (intel_priority_queue_.size() >= config_.max_intel_queue_size && topic == "MARKET_INTEL") {
//             stats_.queue_overflows++;
//             logInfo("Intel queue overflow detected");
//             // Drop low priority messages if queue is full
//             GenericMessage new_msg(topic, data, priority);
//             new_msg.correlation_id = correlation_id;
            
//             if (shouldDropMessage(new_msg)) {
//                 stats_.messages_dropped++;
//                 logError("Intel message dropped due to queue overflow: " + topic);
//                 return false;
//             }
            
//             // Wait for space or timeout
//             if (!intel_queue_cv_.wait_for(lock2, config_.queue_timeout,
//                                   [this]() { return intel_priority_queue_.size() < config_.max_intel_queue_size ||
//                                             state_.load() != ServerState::RUNNING; })) {
//                 stats_.messages_dropped++;
//                 logError("Intel message timeout while waiting for queue space: " + topic);
//                 return false;
//             }
//         }        
//         if (state_.load() != ServerState::RUNNING) {
//             // Server stopped while waiting
//             logError("Server stopped while waiting for queue space: " + topic);
//             return false;
//         }
        
//         // Add message to queue
//         GenericMessage message(std::move(safe_topic), std::move(data), priority);
//         // GenericMessage message(topic, data, priority);  // No moves at all for debugging
//         message.correlation_id = correlation_id;
//         logInfo("Async Publishing message: " + topic + " with priority " + std::to_string(static_cast<int>(priority)));
//         if (topic == "MARKET_INTEL") {
//             // logInfo("Publishing Intel message: " + topic);
//             intel_priority_queue_.push(std::move(message));
//             intel_queue_cv_.notify_one();
//         } else {
//             // logInfo("Publishing internal message: " + topic);
//             internal_priority_queue_.push(std::move(message));
//             internal_queue_cv_.notify_one();
//         }
//         stats_.messages_queued++;
        
//         return true;

//     } catch (const std::exception& e) {
//         logError("Error publishing async message: " + std::string(e.what()));
//         return false;
//     }
// }
bool GenericZmqServer::publishSync(const std::string& topic, const std::string& data,
                                  MessagePriority priority, const std::string& correlation_id) {
    if (state_.load() != ServerState::RUNNING || !intel_publisher_socket_ || !internal_publisher_socket_) {
        logError("Cannot publish sync - server not running or socket not available");
        return false;
    }

    // Pre-validate
    if (!isValidTopic(topic) || !isTopicAllowed(topic)) {
        return topic.empty() ? false : true; // Silent succeed for filtered
    }

    try {
        // Reuse static buffers (thread-local for thread safety)
        static thread_local std::string serialized_buffer;
        static thread_local std::unordered_map<std::string, std::string> metadata_buffer;
        
        // Clear and reuse
        metadata_buffer.clear();
        metadata_buffer["correlation_id"] = correlation_id.empty() ? 
            GenericMessage::generateCorrelationId() : correlation_id;
        metadata_buffer["priority"] = std::to_string(static_cast<int>(priority));
        
        // Cache timestamp (update less frequently)
        static thread_local auto last_ts_update = std::chrono::steady_clock::now();
        static thread_local std::string cached_timestamp;
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_ts_update).count() > 100) {
            cached_timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count());
            last_ts_update = now;
        }
        metadata_buffer["timestamp"] = cached_timestamp;

        // Serialize to reused buffer
        serialized_buffer.clear();
        serialized_buffer = serializeWithMetadata(data, metadata_buffer);

        // Create messages with move semantics
        zmq::message_t topic_msg(topic.data(), topic.size());
        zmq::message_t data_msg(serialized_buffer.data(), serialized_buffer.size());

        // Send with non-blocking for better performance
        if (topic.empty() || data.empty()) {
            logError("Empty topic or data in sync publish");
            return false;
        }
        if (topic == "MARKET_INTEL") {
            // High-frequency market intelligence goes to dedicated socket
            auto topic_result = intel_publisher_socket_->send(topic_msg, zmq::send_flags::sndmore | zmq::send_flags::dontwait);
            auto data_result = intel_publisher_socket_->send(data_msg, zmq::send_flags::dontwait);
            if (topic_result.has_value() && data_result.has_value()) {
                // Batch stats updates
                stats_.messages_published++;
                stats_.bytes_sent += topic.size() + serialized_buffer.size();
                return true;
            }
            return false;
        }
        auto topic_result = internal_publisher_socket_->send(topic_msg, zmq::send_flags::sndmore | zmq::send_flags::dontwait);
        auto data_result = internal_publisher_socket_->send(data_msg, zmq::send_flags::dontwait);

        if (topic_result.has_value() && data_result.has_value()) {
            // Batch stats updates
            stats_.messages_published++;
            stats_.bytes_sent += topic.size() + serialized_buffer.size();
            return true;
        }
        return false;

    } catch (const zmq::error_t& e) {
        if (e.num() != EAGAIN) { // Don't log EAGAIN (queue full)
            logError("ZMQ error: " + std::string(e.what()));
        }
        return false;
    }
}


// Template publishProtobuf implementations moved to header (for proper instantiation)
bool GenericZmqServer::publishWithMetadata(const std::string& topic, const std::string& data,
                                         const std::unordered_map<std::string, std::string>& metadata,
                                         MessagePriority priority) {
    // Extract correlation ID from metadata if present
    std::string correlation_id;
    auto it = metadata.find("correlation_id");
    if (it != metadata.end()) {
        correlation_id = it->second;
    }

    // Sanitize topic and use sync publishing for metadata-rich messages
    std::string safe_topic = topic; //sanitizeTopic(topic); no need
    return publishSync(safe_topic, serializeWithMetadata(data, metadata), priority, correlation_id);
}

// =============================================================================
// REQUEST-REPLY INTERFACE  
// =============================================================================

void GenericZmqServer::registerRequestHandler(const std::string& request_type, RequestHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    request_handlers_[request_type] = std::move(handler);
    logInfo("Registered request handler for type: " + request_type);
}

void GenericZmqServer::unregisterRequestHandler(const std::string& request_type) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    auto it = request_handlers_.find(request_type);
    if (it != request_handlers_.end()) {
        request_handlers_.erase(it);
        logInfo("Unregistered request handler for type: " + request_type);
    }
}

// =============================================================================
// TOPIC MANAGEMENT
// =============================================================================

void GenericZmqServer::addTopicFilter(const std::string& topic_pattern) {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    topic_filters_.push_back(topic_pattern);
    use_topic_filters_ = true;
    logInfo("Added topic filter: " + topic_pattern);
}

void GenericZmqServer::removeTopicFilter(const std::string& topic_pattern) {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    auto it = std::find(topic_filters_.begin(), topic_filters_.end(), topic_pattern);
    if (it != topic_filters_.end()) {
        topic_filters_.erase(it);
        use_topic_filters_ = !topic_filters_.empty();
        logInfo("Removed topic filter: " + topic_pattern);
    }
}

void GenericZmqServer::clearTopicFilters() {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    topic_filters_.clear();
    use_topic_filters_ = false;
    logInfo("Cleared all topic filters");
}

bool GenericZmqServer::isTopicAllowed(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    
    if (!use_topic_filters_) {
        return true;
    }
    
    // Check if topic matches any filter pattern
    for (const auto& pattern : topic_filters_) {
        try {
            std::regex regex_pattern(pattern);
            if (std::regex_match(topic, regex_pattern)) {
                return true;
            }
        } catch (const std::regex_error&) {
            // If regex is invalid, do simple string comparison
            if (topic.find(pattern) != std::string::npos) {
                return true;
            }
        }
    }
    
    return false;
}

// =============================================================================
// HEALTH AND DIAGNOSTICS
// =============================================================================

bool GenericZmqServer::healthCheck() const {
    if (state_.load() != ServerState::RUNNING) {
        return false;
    }
    
    // Check if sockets are available
    if (!intel_publisher_socket_ || !request_socket_ || ! internal_publisher_socket_) {
        return false;
    }
    
    // Check queue utilization
    if (getQueueUtilization("intel") > 0.9 || getQueueUtilization("internal") > 0.9) {
        return false;
    }
    
    // Check error rate (simple heuristic)
    uint64_t total_operations = stats_.messages_published + stats_.requests_processed;
    uint64_t total_errors = stats_.messages_dropped + stats_.requests_failed + stats_.connection_errors;
    
    if (total_operations > 100 && (static_cast<double>(total_errors) / total_operations) > 0.1) {  // 10% error rate
        return false;
    }
    
    return true;
}

void GenericZmqServer::sendHeartbeat() {
    if (state_.load() != ServerState::RUNNING) {
        return;
    }
    
    nlohmann::json heartbeat;
    heartbeat["type"] = "heartbeat";
    heartbeat["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    heartbeat["state"] = static_cast<int>(state_.load());
    heartbeat["stats"] = {
        {"messages_published", stats_.messages_published.load()},
        {"requests_processed", stats_.requests_processed.load()},
        {"queue_size", getQueueSize("intel")},
        {"queue_utilization", getQueueUtilization("intel")},
        {"connection_errors", stats_.connection_errors.load()}
    };
    
    publishSync("system.heartbeat", heartbeat.dump(), MessagePriority::HIGH, "heartbeat");
}

// =============================================================================
// PRIVATE METHODS - INITIALIZATION
// =============================================================================
bool GenericZmqServer::initializeSockets() {
    try {
        logInfo("Starting socket initialization...");
        
        // Validate config first
        if (!config_.validate()) {
            logError("Invalid configuration");
            return false;
        }
        
        logInfo("Config validation passed");
        logInfo("Intel endpoint: " + config_.intel_endpoint);
        logInfo("Internal endpoint: " + config_.internal_endpoint);
        logInfo("Request endpoint: " + config_.request_endpoint);
        
        // Create ZeroMQ context with proper shutdown behavior
        logInfo("Creating ZMQ context with " + std::to_string(config_.io_threads) + " IO threads");
        intel_context_ = std::make_unique<zmq::context_t>(1);  // Single thread for intel
        internal_context_ = std::make_unique<zmq::context_t>(config_.io_threads);

        // Set context options for safer shutdown
        intel_context_->set(zmq::ctxopt::blocky, false);  // Non-blocking context termination
        internal_context_->set(zmq::ctxopt::blocky, false);  // Non-blocking context termination

        // Create and configure intel publisher socket
        logInfo("Creating intel publisher socket...");
        intel_publisher_socket_ = std::make_unique<zmq::socket_t>(*intel_context_, ZMQ_PUB);
        
        logInfo("Setting intel socket options...");
        intel_publisher_socket_->set(zmq::sockopt::sndhwm, config_.intel_hwm);
        intel_publisher_socket_->set(zmq::sockopt::linger, 0);  // Immediate close
        intel_publisher_socket_->set(zmq::sockopt::sndtimeo, 1000);  // Send timeout
        intel_publisher_socket_->set(zmq::sockopt::sndbuf, 0);  // OS buffer size 0 to drop messages when HWM is reached
        
        if (config_.intel_immediate) {
            intel_publisher_socket_->set(zmq::sockopt::immediate, 1);
        }
        
        if (config_.intel_conflate) {
            intel_publisher_socket_->set(zmq::sockopt::conflate, 1);
        }
        
        intel_publisher_socket_->bind(config_.intel_endpoint);
        logInfo("Intel publisher socket bound successfully to: " + config_.intel_endpoint);
       
        // Create and configure internal publisher socket
        logInfo("Creating internal publisher socket...");
        internal_publisher_socket_ = std::make_unique<zmq::socket_t>(*internal_context_, ZMQ_PUB);
        
        logInfo("Setting internal socket options...");
        internal_publisher_socket_->set(zmq::sockopt::sndhwm, config_.internal_hwm);
        internal_publisher_socket_->set(zmq::sockopt::linger, 0);  // Immediate close
        internal_publisher_socket_->set(zmq::sockopt::sndtimeo, 1000);  // Send timeout
        internal_publisher_socket_->set(zmq::sockopt::sndbuf, 0);  // OS buffer size 0 to drop messages when HWM is reached
        
        if (config_.internal_immediate) {
            internal_publisher_socket_->set(zmq::sockopt::immediate, 1);
        }
        
        if (config_.internal_conflate) {
            internal_publisher_socket_->set(zmq::sockopt::conflate, 1);
        }
        
        internal_publisher_socket_->bind(config_.internal_endpoint);
        logInfo("Internal publisher socket bound successfully to: " + config_.internal_endpoint);
        
        // Create and configure request socket
        logInfo("Creating request socket...");
        request_socket_ = std::make_unique<zmq::socket_t>(*internal_context_, ZMQ_ROUTER); // trading intent is from python
        
        logInfo("Setting request socket options...");
        request_socket_->set(zmq::sockopt::rcvhwm, config_.request_hwm);
        request_socket_->set(zmq::sockopt::sndhwm, config_.request_hwm);
        request_socket_->set(zmq::sockopt::linger, 0);  // Immediate close
        request_socket_->set(zmq::sockopt::rcvtimeo, 100);  // Receive timeout
        request_socket_->set(zmq::sockopt::sndtimeo, 1000);  // Send timeout
        request_socket_->set(zmq::sockopt::rcvbuf, 0);  // Receive buffer
        
        request_socket_->bind(config_.request_endpoint);
        logInfo("Request socket bound successfully to: " + config_.request_endpoint);
        
        logInfo("All ZeroMQ sockets initialized successfully");
        return true;
        
    } catch (const zmq::error_t& e) {
        logError("GenericZmqServer server error initializing sockets: " + std::string(e.what()) + 
                " (errno: " + std::to_string(e.num()) + ")");
        return false;
    } catch (const std::exception& e) {
        logError("GenericZmqServer server error initializing sockets: " + std::string(e.what()));
        return false;
    } catch (...) {
        logError("GenericZmqServer server unknown error initializing sockets");
        return false;
    }
}

void GenericZmqServer::cleanup() {
    logInfo("Starting cleanup...");
    
    // Close sockets safely with error handling
    if (intel_publisher_socket_) {
        try {
            intel_publisher_socket_->set(zmq::sockopt::linger, 0);
            intel_publisher_socket_->close();
        } catch (const zmq::error_t& e) {
            logError("Error closing intel publisher socket: " + std::string(e.what()));
        }
        intel_publisher_socket_.reset();
    }
    
    if (internal_publisher_socket_) {
        try {
            internal_publisher_socket_->set(zmq::sockopt::linger, 0);
            internal_publisher_socket_->close();
        } catch (const zmq::error_t& e) {
            logError("Error closing internal publisher socket: " + std::string(e.what()));
        }
        internal_publisher_socket_.reset();
    }
    
    if (request_socket_) {
        try {
            request_socket_->set(zmq::sockopt::linger, 0);
            request_socket_->close();
        } catch (const zmq::error_t& e) {
            logError("Error closing request socket: " + std::string(e.what()));
        }
        request_socket_.reset();
    }
    
    // Close context
    if (intel_context_) {
        try {
            intel_context_->close();
        } catch (const zmq::error_t& e) {
            logError("Error closing Intel ZMQ context: " + std::string(e.what()));
        }
    }

    if (internal_context_) {
        try {
            internal_context_->close();
        } catch (const zmq::error_t& e) {
            logError("Error closing Internal ZMQ context: " + std::string(e.what()));
        }
    }
    internal_context_.reset();
    intel_context_.reset();
    // Clear queues
    {
        std::lock_guard<std::mutex> lock1(intel_queue_mutex_);
        std::lock_guard<std::mutex> lock2(internal_queue_mutex_);
        
        // Clear internal queue
        std::queue<GenericMessage> empty_queue;
        internal_priority_queue_.swap(empty_queue);
        
        // Clear intel priority queue
        std::queue<GenericMessage> empty_intel_queue;
        intel_priority_queue_.swap(empty_intel_queue);
        
        // Clear general priority queue
        std::priority_queue<GenericMessage, std::vector<GenericMessage>,
                           std::function<bool(const GenericMessage&, const GenericMessage&)>> empty_priority;
        priority_queue_.swap(empty_priority);
    }
    
    logInfo("ZMQ Server resources cleaned up");
}

// =============================================================================
// PRIVATE METHODS - WORKER THREADS
// =============================================================================

void GenericZmqServer::intelPublisherWorker() {
    // Unconditional print so we see the thread even if logging flags change
    std::cout << "[THREAD] Intel publisher worker thread started id=" << std::this_thread::get_id() << std::endl;
    logInfo("Intel publisher worker thread started");

    while (state_.load() == ServerState::RUNNING) {
        try {
            std::unique_lock<std::mutex> lock(intel_queue_mutex_);

            // Wait for messages or shutdown signal (timed wait to avoid missed notifications)
            logInfo("Intel worker: About to wait for messages");
            intel_queue_cv_.wait_for(lock, std::chrono::seconds(1), [this]() {
                return !intel_priority_queue_.empty() || state_.load() != ServerState::RUNNING;
            });
            // Diagnostic:  
                      
            while (!intel_priority_queue_.empty()) {
                // logInfo("Publishing Intel message: " + intel_priority_queue_.front().topic);
                GenericMessage message = std::move(intel_priority_queue_.front());
                intel_priority_queue_.pop();                
                lock.unlock();

                bool ok = false;
                try {
                    logInfo("Processing Intel message: " + message.topic);
                    ok = processIntelMessage(message);
                } catch (const std::exception& e) {
                    logError("Error processing message: " + std::string(e.what()));
                }

                if (ok) {
                    stats_.messages_published++;
                    stats_.bytes_sent += message.topic.size() + message.data.size();
                } else {
                    stats_.messages_dropped++;
                }

                lock.lock();
            }

        } catch (const std::exception& e) {
            logError("Error in publisher worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    logInfo("Intel publisher worker thread stopped");
}
// internalPublisherWorker
void GenericZmqServer::internalPublisherWorker() {
    // Unconditional print so we see the thread even if logging flags change
    std::cout << "[THREAD] Internal publisher worker thread started id=" << std::this_thread::get_id() << std::endl;
    logInfo("Internal publisher worker thread started");

    while (state_.load() == ServerState::RUNNING) {
        try {
            std::unique_lock<std::mutex> lock(internal_queue_mutex_);
            // logInfo("Internal worker in main loop, state=" + std::to_string(static_cast<int>(state_.load())));
            // Wait for messages or shutdown signal (timed wait to avoid missed notifications)
            internal_queue_cv_.wait_for(lock, std::chrono::seconds(1), [this]() {
                return !internal_priority_queue_.empty() || state_.load() != ServerState::RUNNING;
            });

            // Diagnostic: woke from wait
            // try {
            //     logInfo(std::string("Diagnostic: Publisher worker woke (queue_size=") + std::to_string(internal_priority_queue_.size()) + ")");
            // } catch(...) {}
            
            // if (state_.load() != ServerState::RUNNING) {
            //     break;
            // }
            
            while (!internal_priority_queue_.empty()) {
                GenericMessage message = std::move(internal_priority_queue_.front());
                internal_priority_queue_.pop();
                
                lock.unlock();

                bool ok = false;
                try {
                    ok = processInternalMessage(message);
                } catch (const std::exception& e) {
                    logError("Error processing message: " + std::string(e.what()));
                }

                if (ok) {
                    stats_.messages_published++;
                    stats_.bytes_sent += message.topic.size() + message.data.size();
                } else {
                    stats_.messages_dropped++;
                }

                lock.lock();
            }

        } catch (const std::exception& e) {
            logError("Error in publisher worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    logInfo("Internal publisher worker thread stopped");
}

void GenericZmqServer::requestWorker() {
    logInfo("Request worker thread started");
    
    while (state_.load() == ServerState::RUNNING) {
        try {
            // Check socket validity without accessing it
            if (state_.load() != ServerState::RUNNING) {
                break;
            }
            
            // Use a simple sleep-based approach instead of polling
            // This is less efficient but much safer for shutdown
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            // Only proceed if we're still running and have a valid socket
            if (state_.load() != ServerState::RUNNING || !request_socket_) {
                continue;
            }
            
            zmq::message_t identity;
            zmq::message_t request;
            
            try {
                // Use non-blocking recv to check for messages
                auto id_result = request_socket_->recv(identity, zmq::recv_flags::dontwait);
                if (!id_result.has_value()) {
                    continue; // No message available
                }
                
                auto req_result = request_socket_->recv(request, zmq::recv_flags::dontwait);
                if (!req_result.has_value()) {
                    logError("Received identity but no request data");
                    continue;
                }
                
                // Process the request
                handleRequest(identity, request);
                stats_.requests_processed++;
                
            } catch (const zmq::error_t& e) {
                if (e.num() == ETERM || e.num() == ENOTSOCK) {
                    logInfo("Socket terminated during recv, exiting worker");
                    break;
                } else if (e.num() == EAGAIN) {
                    // No message available, continue
                    continue;
                } else {
                    logError("Recv error: " + std::string(e.what()) + " (errno: " + std::to_string(e.num()) + ")");
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
            
        } catch (const std::exception& e) {
            logError("Error in request worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    logInfo("Request worker thread stopped");
}

void GenericZmqServer::monitoringWorker() {
    logInfo("Monitoring worker thread started");
    
    auto last_stats_time = std::chrono::steady_clock::now();
    
    while (state_.load() == ServerState::RUNNING) {
        try {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            auto now = std::chrono::steady_clock::now();
            if (now - last_stats_time >= config_.stats_interval) {
                // Log statistics
                auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - stats_.start_time).count();
                
                logInfo("=== Server Statistics ===");
                logInfo("Uptime: " + std::to_string(uptime) + "s");
                logInfo("State: " + std::to_string(static_cast<int>(state_.load())));
                logInfo("Messages Published: " + std::to_string(stats_.messages_published.load()));
                logInfo("Messages Queued: " + std::to_string(stats_.messages_queued.load()));
                logInfo("Messages Dropped: " + std::to_string(stats_.messages_dropped.load()));
                logInfo("Requests Processed: " + std::to_string(stats_.requests_processed.load()));
                logInfo("Requests Failed: " + std::to_string(stats_.requests_failed.load()));
                logInfo("Queue Size: " + std::to_string(getQueueSize("intel")) + " (intel), " +
                        std::to_string(getQueueSize("internal")) + " (internal)");
                logInfo("Queue Utilization: " + std::to_string(getQueueUtilization("intel")) + "% (intel), " +
                        std::to_string(getQueueUtilization("internal")) + "% (internal)");
                logInfo("Bytes Sent: " + std::to_string(stats_.bytes_sent.load()));
                logInfo("Bytes Received: " + std::to_string(stats_.bytes_received.load()));
                logInfo("========================");
                
                last_stats_time = now;
            }
            
        } catch (const std::exception& e) {
            logError("Error in monitoring worker: " + std::string(e.what()));
        }
    }
    
    logInfo("Monitoring worker thread stopped");
}

void GenericZmqServer::heartbeatWorker() {
    logInfo("Heartbeat worker thread started");
    
    while (state_.load() == ServerState::RUNNING) {
        try {
            sendHeartbeat();
            std::this_thread::sleep_for(config_.heartbeat_interval);
        } catch (const std::exception& e) {
            logError("Error in heartbeat worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    logInfo("Heartbeat worker thread stopped");
}

bool GenericZmqServer::processIntelMessage(const GenericMessage& message) {
    if (!intel_publisher_socket_) {
        logError("Intel publisher socket not available");
        return false;
    }

    try {
        // DO NOT lock the intel_queue_mutex_ here — the worker already popped and unlocked.
        // Use a dedicated send mutex to serialize multipart sends so topic+data don't interleave.
        if (message.data.empty()) {
            logError("Empty data in processIntelMessage");
            return false;
        }

        std::string topic = sanitizeTopic(message.topic);
        if (topic.empty() || message.data.empty()) {
            logError("Empty topic or data in processIntelMessage");
            return false;
        }

        zmq::message_t topic_msg(topic.data(), topic.size());
        zmq::message_t data_msg(message.data.data(), message.data.size());

        std::lock_guard<std::mutex> send_guard(intel_send_mutex_);

        // Send multipart: topic then data. Use blocking sends (no dontwait) to avoid partial sends.
        intel_publisher_socket_->send(topic_msg, zmq::send_flags::sndmore);
        intel_publisher_socket_->send(data_msg, zmq::send_flags::none);

        logInfo("Sent MARKET_INTEL message: " + topic);
        return true;
    } catch (const zmq::error_t& e) {
        logError("ZMQ error processing message: " + std::string(e.what()));
        stats_.connection_errors++;
        return false;
    } catch (const std::exception& e) {
        logError("Error processing message: " + std::string(e.what()));
        return false;
    }
}

bool GenericZmqServer::processInternalMessage(const GenericMessage& message) {
    if (!internal_publisher_socket_) {
        logError("Internal publisher socket not available");
        return false;
    }

    try {
        // Create topic message
        //sanitize topic
        std::string topic = sanitizeTopic(message.topic);   
        zmq::message_t topic_msg(topic.size());
        memcpy(topic_msg.data(), topic.c_str(), topic.size());

        // Create metadata
        std::unordered_map<std::string, std::string> metadata;
        metadata["correlation_id"] = message.correlation_id;
        metadata["priority"] = std::to_string(static_cast<int>(message.priority));
        metadata["timestamp"] = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            message.timestamp.time_since_epoch()).count());

        // Serialize data with metadata
        std::string serialized_data = serializeWithMetadata(message.data, metadata);
        zmq::message_t data_msg(serialized_data.size());
        memcpy(data_msg.data(), serialized_data.c_str(), serialized_data.size());

        // Send messages
        if (message.topic.empty() || message.data.empty()) {
            logError("Empty topic or data in processMessage");
            return false;
        }
        
        logInfo("Processing message for topic: " + message.topic);
        auto topic_result = internal_publisher_socket_->send(topic_msg, zmq::send_flags::sndmore);
        auto data_result = internal_publisher_socket_->send(data_msg, zmq::send_flags::none);
        logInfo("Sent internal message");
        if (topic_result.has_value() && data_result.has_value()) {
            return true;
        }
        logError("Failed to publish internal message: " + message.topic);
        return false;
    } catch (const zmq::error_t& e) {
        logError("ZMQ error processing message: " + std::string(e.what()));
        stats_.connection_errors++;
        return false;
    } catch (const std::exception& e) {
        logError("Error processing message: " + std::string(e.what()));
        return false;
    }
}
 
void GenericZmqServer::handleRequest(zmq::message_t& identity, zmq::message_t& request) {
    try {
        // Parse request data
        std::string request_data(static_cast<char*>(request.data()), request.size());
        auto [data, metadata] = deserializeWithMetadata(request_data);
        
        // Extract request type from metadata or data
        std::string request_type = "default";
        auto type_it = metadata.find("type");
        if (type_it != metadata.end()) {
            request_type = type_it->second;
        }
        
        // Find handler
        std::string response_data;
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            auto handler_it = request_handlers_.find(request_type);
            
            if (handler_it != request_handlers_.end()) {
                // Call handler
                try {
                    response_data = handler_it->second(data, metadata);
                } catch (const std::exception& e) {
                    sendErrorResponse(identity, "Handler error: " + std::string(e.what()),
                                    metadata.count("request_id") ? metadata["request_id"] : "");
                    return;
                }
            } else {
                sendErrorResponse(identity, "Unknown request type: " + request_type,
                                metadata.count("request_id") ? metadata["request_id"] : "");
                return;
            }
        }
        
        // Send response
        zmq::message_t identity_copy;
        identity_copy.rebuild(identity.size());
        memcpy(identity_copy.data(), identity.data(), identity.size());
        
        zmq::message_t response(response_data.size());
        memcpy(response.data(), response_data.c_str(), response_data.size());
        
        request_socket_->send(identity_copy, zmq::send_flags::sndmore);
        request_socket_->send(response, zmq::send_flags::none);
        
        stats_.bytes_received += request_data.size();
        stats_.bytes_sent += response_data.size();
        
    } catch (const std::exception& e) {
        logError("Error handling request: " + std::string(e.what()));
        sendErrorResponse(identity, "Internal server error");
        stats_.requests_failed++;
    }
}

void GenericZmqServer::sendErrorResponse(zmq::message_t& identity, const std::string& error_msg,
                                       const std::string& request_id) {
    try {
        nlohmann::json error_response;
        error_response["error"] = true;
        error_response["message"] = error_msg;
        if (!request_id.empty()) {
            error_response["request_id"] = request_id;
        }
        error_response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        
        std::string response_data = error_response.dump();
        
        zmq::message_t identity_copy;
        identity_copy.rebuild(identity.size());
        memcpy(identity_copy.data(), identity.data(), identity.size());
        
        zmq::message_t response(response_data.size());
        memcpy(response.data(), response_data.c_str(), response_data.size());
        
        request_socket_->send(identity_copy, zmq::send_flags::sndmore);
        request_socket_->send(response, zmq::send_flags::none);
        
    } catch (const std::exception& e) {
        logError("Failed to send error response: " + std::string(e.what()));
    }
}

// =============================================================================
// PRIVATE METHODS - UTILITY
// =============================================================================

std::string GenericZmqServer::serializeWithMetadata(const std::string& data,
                                                   const std::unordered_map<std::string, std::string>& metadata) const {
    // Helper: check if buffer is valid UTF-8
    auto isValidUtf8 = [](const std::string& s) {
        const unsigned char* bytes = reinterpret_cast<const unsigned char*>(s.c_str());
        size_t len = s.size();
        size_t i = 0;
        while (i < len) {
            unsigned char c = bytes[i];
            if (c <= 0x7F) { // ASCII
                i++;
            } else if ((c >> 5) == 0x6) { // 110xxxxx 10xxxxxx
                if (i + 1 >= len) return false;
                if ((bytes[i+1] >> 6) != 0x2) return false;
                i += 2;
            } else if ((c >> 4) == 0xE) { // 1110xxxx 10xxxxxx 10xxxxxx
                if (i + 2 >= len) return false;
                if ((bytes[i+1] >> 6) != 0x2 || (bytes[i+2] >> 6) != 0x2) return false;
                i += 3;
            } else if ((c >> 3) == 0x1E) { // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
                if (i + 3 >= len) return false;
                if ((bytes[i+1] >> 6) != 0x2 || (bytes[i+2] >> 6) != 0x2 || (bytes[i+3] >> 6) != 0x2) return false;
                i += 4;
            } else {
                return false;
            }
        }
        return true;
    };

    // Basic base64 encode/decode helpers
    auto base64_encode = [](const std::string &in) {
        static const char* b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        int val=0, valb=-6;
        for (unsigned char c : in) {
            val = (val<<8) + c;
            valb += 8;
            while (valb>=0) {
                out.push_back(b64_chars[(val>>valb)&0x3F]);
                valb-=6;
            }
        }
        if (valb>-6) out.push_back(b64_chars[((val<<8)>>(valb+8))&0x3F]);
        while (out.size()%4) out.push_back('=');
        return out;
    };

    auto base64_decode = [](const std::string &in) {
        static const std::vector<int> T = [](){
            std::vector<int> t(256, -1);
            for (int i=0;i<64;i++) t[static_cast<unsigned char>(std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/")[i])] = i;
            return t; }();
        std::string out;
        std::vector<int> val(4,0);
        int valb= -8;
        for (unsigned char c : in) {
            if (T[c] == -1) break;
            valb += 6;
            int v = T[c];
            if (valb >= 0) {
                out.push_back(char((v << (valb%8)) & 0xFF));
            }
        }
        // fallback: use a simpler decode (robustness over performance)
        std::string ret;
        std::vector<int> state(256, -1);
        for (int i=0;i<64;i++) state[(unsigned char)std::string("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/")[i]] = i;
        int val2=0, valb2=-8;
        for (unsigned char c : in) {
            if (state[c] == -1) break;
            val2 = (val2<<6) + state[c];
            valb2 += 6;
            if (valb2>=0) {
                ret.push_back(char((val2>>valb2)&0xFF));
                valb2-=8;
            }
        }
        return ret;
    };

    nlohmann::json message;
    std::unordered_map<std::string, std::string> meta = metadata; // copy so we can mutate
    if (isValidUtf8(data)) {
        message["data"] = data;
    } else {
        message["data"] = base64_encode(data);
        meta["data_encoding"] = "base64";
    }
    message["metadata"] = meta;
    std::string result = message.dump();
    message.clear(); // Explicitly clear to free memory
    return result;
}

// 1. FIX: JSON parsing with proper exception safety
std::pair<std::string, std::unordered_map<std::string, std::string>>
GenericZmqServer::deserializeWithMetadata(const std::string& serialized_data) const {
    try {
        // Reserve memory to avoid reallocation during parsing
        std::string data;
        data.reserve(serialized_data.size());
        
        // Parse with explicit error handling
        nlohmann::json message;
        try {
            message = nlohmann::json::parse(serialized_data);
        } catch (const nlohmann::json::parse_error& e) {
            // Fallback: treat as raw data to prevent parser leak
            return {serialized_data, {}};
        }
        
        data = message.value("data", "");
        std::unordered_map<std::string, std::string> metadata;
        
        if (message.contains("metadata") && message["metadata"].is_object()) {
            // Reserve space to prevent rehashing
            metadata.reserve(message["metadata"].size());
            for (const auto& [key, value] : message["metadata"].items()) {
                if (value.is_string()) {
                    metadata.emplace(key, value.get<std::string>());
                }
            }
        }
        
        // Clear message explicitly to free JSON memory
        message.clear();
        
        // Handle base64 decoding with memory safety
        auto it = metadata.find("data_encoding");
        if (it != metadata.end() && it->second == "base64") {
            std::string decoded = base64_decode_safe(data);
            return {std::move(decoded), std::move(metadata)};
        }
        
        return {std::move(data), std::move(metadata)};
        
    } catch (const std::exception&) {
        return {serialized_data, {}};
    }
}

// 2. FIX: Memory-safe base64 decode
std::string GenericZmqServer::base64_decode_safe(const std::string& input) const {
    if (input.empty()) return {};
    
    // Pre-calculate output size to avoid reallocations
    size_t output_size = (input.size() * 3) / 4;
    std::string result;
    result.reserve(output_size);
    
    static const std::array<int, 256> decode_table = []() {
        std::array<int, 256> table;
        table.fill(-1);
        const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < chars.size(); ++i) {
            table[static_cast<unsigned char>(chars[i])] = static_cast<int>(i);
        }
        return table;
    }();
    
    int val = 0, valb = -8;
    for (unsigned char c : input) {
        if (decode_table[c] == -1) break;
        val = (val << 6) + decode_table[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    
    return result;
}


void GenericZmqServer::logError(const std::string& error_msg) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error_msg;
    
    if (logging_enabled_) {
        std::cerr << "[ERROR] GenericZmqServer: " << error_msg << std::endl;
    }
}

void GenericZmqServer::logInfo(const std::string& info_msg) {
    if (logging_enabled_) {
        std::cout << "[INFO] GenericZmqServer: " << info_msg << std::endl;
    }
}

void GenericZmqServer::updateStats(const std::string& operation, size_t bytes) {
    // This method can be extended to track operation-specific statistics
    stats_.bytes_sent += bytes;
}

bool GenericZmqServer::shouldDropMessage(const GenericMessage& message) const {
    // Drop low priority messages when queue is under pressure
    if (message.priority == MessagePriority::LOW || message.priority == MessagePriority::NORMAL) {
        return getQueueUtilization("internal") > 0.9;  // Drop if over 90% utilization
    }
    
    // Never drop high priority or critical messages
    return false;
}

bool GenericZmqServer::reconnectSocket(zmq::socket_t& socket, const std::string& endpoint) {
    try {
        socket.close();
        
        // Recreate socket
        if (&socket == intel_publisher_socket_.get()) {
            intel_publisher_socket_ = std::make_unique<zmq::socket_t>(*intel_context_, ZMQ_PUB);
            intel_publisher_socket_->set(zmq::sockopt::sndhwm, config_.intel_hwm);
            intel_publisher_socket_->set(zmq::sockopt::linger, config_.intel_linger);
        } else if (&socket == internal_publisher_socket_.get()) {
            internal_publisher_socket_ = std::make_unique<zmq::socket_t>(*internal_context_, ZMQ_PUB);
            internal_publisher_socket_->set(zmq::sockopt::sndhwm, config_.internal_hwm);
            internal_publisher_socket_->set(zmq::sockopt::linger, config_.internal_linger);
        } else if (&socket == request_socket_.get()) {
            request_socket_ = std::make_unique<zmq::socket_t>(*intel_context_, ZMQ_ROUTER);
            request_socket_->set(zmq::sockopt::rcvhwm, config_.request_hwm);
            request_socket_->set(zmq::sockopt::linger, 1000);
        }
        
        socket.bind(endpoint);
        logInfo("Socket reconnected to: " + endpoint);
        return true;
        
    } catch (const zmq::error_t& e) {
        logError("Failed to reconnect socket: " + std::string(e.what()));
        return false;
    }
}

bool GenericZmqServer::isValidTopic(const std::string& topic) {
    if (topic.empty() || topic.length() > 255) {
        return false;
    }
    if (topic.front() == '.' || topic.back() == '.') {
        return false;
    }
    
    // Manual validation - faster than regex
    // for (char c : topic) {
    //     if (!std::isalnum(c) && c != '.' && c != '_' && c != '-') {
    //         return false;
    //     }
    // }
    return true;
}

std::function<bool(const GenericMessage&, const GenericMessage&)>
GenericZmqServer::createPriorityComparator() {
    return [](const GenericMessage& a, const GenericMessage& b) {
        // Higher priority messages should be processed first
        if (a.priority != b.priority) {
            return static_cast<int>(a.priority) < static_cast<int>(b.priority);
        }
        // If same priority, older messages first
        return a.timestamp > b.timestamp;
    };
}

/**
 * @brief Calculates the threshold for inline message processing to prevent queue overload.
 * Processes one message inline if the queue exceeds this threshold.
 */
size_t GenericZmqServer::calculateInlineThreshold() const {
    // Minimum 1, up to 1/4 of max_internal_queue_size, capped at 1024
    return std::min<size_t>(std::max<size_t>(1, config_.max_internal_queue_size / 4), 1024);
}

} // namespace communication
} // namespace trading