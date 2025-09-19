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
    // Sanitize topic to conform to allowed characters
    // std::cout << "topic: " << topic << std::endl;
    std::string safe_topic = topic; //sanitizeTopic(topic); no need
    // std::cout << "safe_topic: " << safe_topic << std::endl;
    // print part of data
    // std::cout << "data (partial): " << data.substr(0, 50) << "..." << std::endl;

    if (!isValidTopic(safe_topic)) {
        logError("Invalid topic format: " + topic);
        return false;
    }

    if (!isTopicAllowed(safe_topic)) {
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
    GenericMessage message(safe_topic, data, priority);
        message.correlation_id = correlation_id;
        message_queue_.push(std::move(message));
        stats_.messages_queued++;
        // Diagnostic log: message queued

        // If the queue grows very large, process one message inline to unblock the system
        // This is a temporary smoke-test helper to verify publish/send paths.
        size_t inline_threshold = std::min<size_t>(std::max<size_t>(1, config_.max_queue_size / 4), 1024);
        if (message_queue_.size() > inline_threshold) {
            // Pop one message under the lock and process it outside the lock
            GenericMessage to_process = std::move(message_queue_.front());
            message_queue_.pop();
            // Unlock before processing to avoid deadlocks
            lock.unlock();
            bool ok = false;
           
            try {
                ok = processMessage(to_process);
            } catch (const std::exception& e) {
                logError(std::string("Error inline-processing message: ") + e.what());
            }
            if (ok) {
                stats_.messages_published++;
                stats_.bytes_sent += to_process.topic.size() + to_process.data.size();
            } else {
                stats_.messages_dropped++;
            }
            // Wake workers to continue processing remaining queue
            queue_cv_.notify_all();
            return true;
        }

        lock.unlock();
        // Wake all publisher threads; avoids missed wake-ups when multiple threads are used
        queue_cv_.notify_all();
        
        // if (logging_enabled_) {
        //     logInfo("Message queued for topic: " + safe_topic + " (correlation: " + correlation_id + ")");
        // }
        
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
    // Sanitize topic to conform to allowed characters
    // std::string safe_topic = sanitizeTopic(topic);
    std::string safe_topic = topic; // no need

    if (!isValidTopic(safe_topic)) {
        logError("Invalid topic format: " + topic);
        return false;
    }

    if (!isTopicAllowed(safe_topic)) {
        return true; // Silently succeed for filtered topics
    }
    
    try {
        // Create topic message
    zmq::message_t topic_msg(safe_topic.size());
    memcpy(topic_msg.data(), safe_topic.c_str(), safe_topic.size());
        
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
            stats_.bytes_sent += safe_topic.size() + serialized_data.size();
            updateStats("publish_sync", safe_topic.size() + serialized_data.size());
            
            if (logging_enabled_) {
                logInfo("Message published sync to topic: " + safe_topic);
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
    // Unconditional print so we see the thread even if logging flags change
    std::cout << "[THREAD] Publisher worker thread started id=" << std::this_thread::get_id() << std::endl;
    logInfo("Publisher worker thread started");
    
    while (state_.load() == ServerState::RUNNING) {
        try {
            std::unique_lock<std::mutex> lock(queue_mutex_);           

            // Wait for messages or shutdown signal (timed wait to avoid missed notifications)
            queue_cv_.wait_for(lock, std::chrono::seconds(1), [this]() {
                return !message_queue_.empty() || state_.load() != ServerState::RUNNING;
            });

            // Diagnostic: woke from wait
            try {
                logInfo(std::string("Diagnostic: Publisher worker woke (queue_size=") + std::to_string(message_queue_.size()) + ")");
            } catch(...) {}
            
            if (state_.load() != ServerState::RUNNING) {
                break;
            }
            
            // Process all available messages
            // while (!message_queue_.empty()) {
            //     GenericMessage message = std::move(message_queue_.front());
            //     message_queue_.pop();
            //     lock.unlock();
                
            //     // Process the message
            //     if (processMessage(message)) {
            //         stats_.messages_published++;
            //         stats_.bytes_sent += message.topic.size() + message.data.size();
            //     } else {
            //         stats_.messages_dropped++;
            //     }
                
            //     lock.lock();
            // }
            while (!message_queue_.empty()) {
                GenericMessage message = std::move(message_queue_.front());
                message_queue_.pop();
                
                lock.unlock();

                bool ok = false;
                try {
                    ok = processMessage(message);
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
            // if (logging_enabled_) {
            //     logInfo("Processed message for topic: " + message.topic);
            // }
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
    return message.dump();
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
    if (topic.empty() || topic.length() > 255) {
        return false;
    }
    if (topic.front() == '.' || topic.back() == '.') {
        return false;
    }
    
    // Manual validation - faster than regex
    for (char c : topic) {
        if (!std::isalnum(c) && c != '.' && c != '_' && c != '-') {
            return false;
        }
    }
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

} // namespace communication
} // namespace trading