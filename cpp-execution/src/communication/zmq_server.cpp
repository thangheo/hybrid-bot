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
        
        // Start publisher threads
        publisher_threads_.reserve(config_.publisher_threads);
        for (int i = 0; i < config_.publisher_threads; ++i) {
            publisher_threads_.emplace_back(&GenericZmqServer::publisherWorker, this);
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
        logInfo("Publisher endpoint: " + config_.publisher_endpoint);
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
    
    // Notify all waiting threads
    queue_cv_.notify_all();
    
    // Join publisher threads
    for (auto& thread : publisher_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    publisher_threads_.clear();
    
    // Join request handler threads
    for (auto& thread : request_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    request_threads_.clear();
    
    // Join monitoring thread
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
    
    // Join heartbeat thread
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    // Cleanup resources
    cleanup();
    
    state_.store(ServerState::STOPPED);
    logInfo("Generic ZMQ Server stopped");
}

// =============================================================================
// PUBLISHING INTERFACE
// =============================================================================

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
    
    if (!isTopicAllowed(topic)) {
        // Silently drop filtered topics
        return true;
    }
    
    try {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        // Check queue capacity
        if (message_queue_.size() >= config_.max_queue_size) {
            stats_.queue_overflows++;
            
            // Drop low priority messages if queue is full
            GenericMessage new_msg(topic, data, priority);
            new_msg.correlation_id = correlation_id;
            
            if (shouldDropMessage(new_msg)) {
                stats_.messages_dropped++;
                logError("Message dropped due to queue overflow: " + topic);
                return false;
            }
            
            // Wait for space or timeout
            if (!queue_cv_.wait_for(lock, config_.queue_timeout, 
                                  [this]() { return message_queue_.size() < config_.max_queue_size || 
                                            state_.load() != ServerState::RUNNING; })) {
                stats_.messages_dropped++;
                logError("Message timeout while waiting for queue space: " + topic);
                return false;
            }
        }
        
        if (state_.load() != ServerState::RUNNING) {
            return false;
        }
        
        // Add message to queue
        GenericMessage message(topic, data, priority);
        message.correlation_id = correlation_id;
        message_queue_.push(std::move(message));
        stats_.messages_queued++;
        
        lock.unlock();
        queue_cv_.notify_one();
        
        if (logging_enabled_) {
            logInfo("Message queued for topic: " + topic + " (correlation: " + correlation_id + ")");
        }
        
        return true;
        
    } catch (const std::exception& e) {
        logError("Error publishing async message: " + std::string(e.what()));
        return false;
    }
}

bool GenericZmqServer::publishSync(const std::string& topic, const std::string& data,
                                  MessagePriority priority, const std::string& correlation_id) {
    if (state_.load() != ServerState::RUNNING || !publisher_socket_) {
        logError("Cannot publish sync - server not running or socket not available");
        return false;
    }
    
    if (!isValidTopic(topic)) {
        logError("Invalid topic format: " + topic);
        return false;
    }
    
    if (!isTopicAllowed(topic)) {
        return true; // Silently succeed for filtered topics
    }
    
    try {
        // Create topic message
        zmq::message_t topic_msg(topic.size());
        memcpy(topic_msg.data(), topic.c_str(), topic.size());
        
        // Create data message with metadata
        std::unordered_map<std::string, std::string> metadata;
        metadata["correlation_id"] = correlation_id.empty() ? GenericMessage::generateCorrelationId() : correlation_id;
        metadata["priority"] = std::to_string(static_cast<int>(priority));
        metadata["timestamp"] = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
        
        std::string serialized_data = serializeWithMetadata(data, metadata);
        zmq::message_t data_msg(serialized_data.size());
        memcpy(data_msg.data(), serialized_data.c_str(), serialized_data.size());
        
        // Send messages (topic first, then data)
        auto topic_result = publisher_socket_->send(topic_msg, zmq::send_flags::sndmore);
        auto data_result = publisher_socket_->send(data_msg, zmq::send_flags::none);
        
        if (topic_result.has_value() && data_result.has_value()) {
            stats_.messages_published++;
            stats_.bytes_sent += topic.size() + serialized_data.size();
            updateStats("publish_sync", topic.size() + serialized_data.size());
            
            if (logging_enabled_) {
                logInfo("Message published sync to topic: " + topic);
            }
            return true;
        } else {
            logError("Failed to send sync message to topic: " + topic);
            return false;
        }
        
    } catch (const zmq::error_t& e) {
        logError("ZMQ error in sync publish: " + std::string(e.what()));
        stats_.connection_errors++;
        return false;
    } catch (const std::exception& e) {
        logError("Error in sync publish: " + std::string(e.what()));
        return false;
    }
}

bool GenericZmqServer::publishWithMetadata(const std::string& topic, const std::string& data,
                                         const std::unordered_map<std::string, std::string>& metadata,
                                         MessagePriority priority) {
    
    // Extract correlation ID from metadata if present
    std::string correlation_id;
    auto it = metadata.find("correlation_id");
    if (it != metadata.end()) {
        correlation_id = it->second;
    }
    
    // For metadata-rich messages, always use sync publishing for immediate delivery
    return publishSync(topic, serializeWithMetadata(data, metadata), priority, correlation_id);
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
    if (!publisher_socket_ || !request_socket_) {
        return false;
    }
    
    // Check queue utilization
    if (getQueueUtilization() > 0.95) {  // 95% threshold
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
        {"queue_size", getQueueSize()},
        {"queue_utilization", getQueueUtilization()}
    };
    
    publishSync("system.heartbeat", heartbeat.dump(), MessagePriority::HIGH, "heartbeat");
}

// =============================================================================
// PRIVATE METHODS - INITIALIZATION
// =============================================================================

bool GenericZmqServer::initializeSockets() {
    try {
        // Create ZeroMQ context
        context_ = std::make_unique<zmq::context_t>(config_.io_threads);
        
        // Create and configure publisher socket
        publisher_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        
        // Set publisher socket options
        publisher_socket_->set(zmq::sockopt::sndhwm, config_.publisher_hwm);
        publisher_socket_->set(zmq::sockopt::linger, config_.publisher_linger);
        if (config_.publisher_immediate) {
            publisher_socket_->set(zmq::sockopt::immediate, 1);
        }
        
        // Bind publisher socket
        publisher_socket_->bind(config_.publisher_endpoint);
        
        // Create and configure request socket
        request_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_ROUTER);
        
        // Set request socket options
        request_socket_->set(zmq::sockopt::rcvhwm, config_.request_hwm);
        request_socket_->set(zmq::sockopt::linger, 1000);
        request_socket_->set(zmq::sockopt::rcvtimeo, 100);  // 100ms timeout for non-blocking
        
        // Bind request socket
        request_socket_->bind(config_.request_endpoint);
        
        logInfo("ZeroMQ sockets initialized successfully");
        return true;
        
    } catch (const zmq::error_t& e) {
        logError("ZMQ error initializing sockets: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        logError("Error initializing sockets: " + std::string(e.what()));
        return false;
    }
}

void GenericZmqServer::cleanup() {
    // Close sockets
    if (publisher_socket_) {
        publisher_socket_->close();
        publisher_socket_.reset();
    }
    
    if (request_socket_) {
        request_socket_->close();
        request_socket_.reset();
    }
    
    // Close context
    if (context_) {
        context_->close();
        context_.reset();
    }
    
    // Clear queues
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::queue<GenericMessage> empty_queue;
        message_queue_.swap(empty_queue);
        
        // Clear priority queue
        std::priority_queue<GenericMessage, std::vector<GenericMessage>,
                           std::function<bool(const GenericMessage&, const GenericMessage&)>> empty_priority;
        priority_queue_.swap(empty_priority);
    }
    
    logInfo("ZMQ Server resources cleaned up");
}

// =============================================================================
// PRIVATE METHODS - WORKER THREADS
// =============================================================================

void GenericZmqServer::publisherWorker() {
    logInfo("Publisher worker thread started");
    
    while (state_.load() == ServerState::RUNNING) {
        try {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // Wait for messages or shutdown signal
            queue_cv_.wait(lock, [this]() {
                return !message_queue_.empty() || state_.load() != ServerState::RUNNING;
            });
            
            if (state_.load() != ServerState::RUNNING) {
                break;
            }
            
            // Process all available messages
            while (!message_queue_.empty()) {
                GenericMessage message = std::move(message_queue_.front());
                message_queue_.pop();
                lock.unlock();
                
                // Process the message
                if (processMessage(message)) {
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
    
    logInfo("Publisher worker thread stopped");
}

void GenericZmqServer::requestWorker() {
    logInfo("Request worker thread started");
    
    while (state_.load() == ServerState::RUNNING) {
        try {
            if (!request_socket_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            zmq::message_t identity;
            zmq::message_t request;
            
            // Try to receive request with timeout
            if (request_socket_->recv(identity, zmq::recv_flags::dontwait) &&
                request_socket_->recv(request, zmq::recv_flags::dontwait)) {
                
                handleRequest(identity, request);
                stats_.requests_processed++;
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN && e.num() != EINTR) {
                logError("ZMQ error in request worker: " + std::string(e.what()));
                stats_.connection_errors++;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } catch (const std::exception& e) {
            logError("Error in request worker: " + std::string(e.what()));
            stats_.requests_failed++;
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
                logInfo("Queue Size: " + std::to_string(getQueueSize()));
                logInfo("Queue Utilization: " + std::to_string(getQueueUtilization() * 100) + "%");
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

// =============================================================================
// PRIVATE METHODS - MESSAGE PROCESSING
// =============================================================================

bool GenericZmqServer::processMessage(const GenericMessage& message) {
    if (!publisher_socket_) {
        return false;
    }
    
    try {
        // Create topic message
        zmq::message_t topic_msg(message.topic.size());
        memcpy(topic_msg.data(), message.topic.c_str(), message.topic.size());
        
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
        auto topic_result = publisher_socket_->send(topic_msg, zmq::send_flags::sndmore);
        auto data_result = publisher_socket_->send(data_msg, zmq::send_flags::none);
        
        if (topic_result.has_value() && data_result.has_value()) {
            if (logging_enabled_) {
                logInfo("Processed message for topic: " + message.topic);
            }
            return true;
        }
        
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
    nlohmann::json message;
    message["data"] = data;
    message["metadata"] = metadata;
    return message.dump();
}

std::pair<std::string, std::unordered_map<std::string, std::string>>
GenericZmqServer::deserializeWithMetadata(const std::string& serialized_data) const {
    try {
        auto message = nlohmann::json::parse(serialized_data);
        
        std::string data = message.value("data", "");
        std::unordered_map<std::string, std::string> metadata;
        
        if (message.contains("metadata") && message["metadata"].is_object()) {
            for (auto& [key, value] : message["metadata"].items()) {
                if (value.is_string()) {
                    metadata[key] = value.get<std::string>();
                }
            }
        }
        
        return {data, metadata};
        
    } catch (const std::exception&) {
        // If parsing fails, treat entire input as data
        return {serialized_data, {}};
    }
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
        return getQueueUtilization() > 0.9;  // Drop if over 90% utilization
    }
    
    // Never drop high priority or critical messages
    return false;
}

bool GenericZmqServer::reconnectSocket(zmq::socket_t& socket, const std::string& endpoint) {
    try {
        socket.close();
        
        // Recreate socket
        if (&socket == publisher_socket_.get()) {
            publisher_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
            publisher_socket_->set(zmq::sockopt::sndhwm, config_.publisher_hwm);
            publisher_socket_->set(zmq::sockopt::linger, config_.publisher_linger);
        } else if (&socket == request_socket_.get()) {
            request_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_ROUTER);
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
    // Topic validation rules:
    // - Not empty
    // - Length between 1 and 255 characters
    // - Contains only alphanumeric, dots, underscores, and hyphens
    // - Doesn't start or end with a dot
    
    if (topic.empty() || topic.length() > 255) {
        return false;
    }
    
    if (topic.front() == '.' || topic.back() == '.') {
        return false;
    }
    
    static const std::regex topic_regex(R"([a-zA-Z0-9._-]+)");
    return std::regex_match(topic, topic_regex);
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

} // namespace communication
} // namespace trading