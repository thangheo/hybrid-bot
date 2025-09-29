#include "communication/zmq_server_client.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
namespace trading::communication {
GenericZmqServer::GenericZmqServer(const ZmqServerConfig& config, std::shared_ptr<zmq::context_t> shared_context)
    : config_(config) {
    // Use provided shared context if available; otherwise create our own
    if (shared_context) {
        context_ = shared_context;
    } else {
        context_ = std::make_shared<zmq::context_t>(config_.io_threads);
    }
}

GenericZmqServer::~GenericZmqServer() {
    if (running_.load()) {
        stop();
    }
}

bool GenericZmqServer::start() {
    if (running_.load()) {
        setLastError("Server is already running");
        return false;
    }

    try {
        if (!initializeSockets()) {
            return false;
        }

        should_stop_.store(false);
        running_.store(true);

        // Start worker threads
        pub_thread_ = std::thread(&GenericZmqServer::publishWorker, this);
        rep_thread_ = std::thread(&GenericZmqServer::requestWorker, this);
        
        if (config_.enable_monitoring) {
            monitor_thread_ = std::thread(&GenericZmqServer::monitorWorker, this);
        }

        return true;
    } catch (const zmq::error_t& e) {
        setLastError("Failed to start server: " + std::string(e.what()));
        running_.store(false);
        return false;
    }
}

void GenericZmqServer::stop() {
    if (!running_.load()) {
        return;
    }

    should_stop_.store(true);
    running_.store(false);

    // Wake up the publish worker
    queue_cv_.notify_all();

    // Join threads
    if (pub_thread_.joinable()) {
        pub_thread_.join();
    }
    if (rep_thread_.joinable()) {
        rep_thread_.join();
    }
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }

    cleanupSockets();
}

bool GenericZmqServer::publishAsync(const std::string& topic, std::string data, 
                                   MessagePriority priority, const std::string& correlation_id) {
    if (!running_.load()) {
        setLastError("Server is not running");
        return false;
    }

    if (!isValidTopic(topic)) {
        setLastError("Invalid topic: " + topic);
        return false;
    }

    // Create metadata with correlation ID
    std::unordered_map<std::string, std::string> metadata;
    if (!correlation_id.empty()) {
        metadata["correlation_id"] = correlation_id;
    }
    metadata["timestamp"] = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    try {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        
        // Check queue size to prevent unbounded growth
        if (message_queue_.size() >= static_cast<size_t>(config_.sndhwm)) {
            // Drop lowest priority message if queue is full
            if (!message_queue_.empty()) {
                message_queue_.pop();
                messages_dropped_.fetch_add(1);
            }
        }

        auto message = std::make_shared<QueuedMessage>(topic, std::move(data), metadata, priority);
        message_queue_.push(message);
        
        queue_cv_.notify_one();
        return true;
    } catch (const std::exception& e) {
        setLastError("Failed to queue message: " + std::string(e.what()));
        return false;
    }
}

bool GenericZmqServer::publishSync(const std::string& topic, const std::string& data, 
                                  MessagePriority priority, const std::string& correlation_id) {
    if (!running_.load()) {
        setLastError("Server is not running");
        return false;
    }

    if (!isValidTopic(topic)) {
        setLastError("Invalid topic: " + topic);
        return false;
    }

    // Create metadata with correlation ID
    std::unordered_map<std::string, std::string> metadata;
    if (!correlation_id.empty()) {
        metadata["correlation_id"] = correlation_id;
    }
    metadata["timestamp"] = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    return sendMessage(topic, data, metadata);
}

bool GenericZmqServer::publishWithMetadata(const std::string& topic, const std::string& data, 
                                          const std::unordered_map<std::string, std::string>& metadata, 
                                          MessagePriority priority) {
    if (!running_.load()) {
        setLastError("Server is not running");
        return false;
    }

    if (!isValidTopic(topic)) {
        setLastError("Invalid topic: " + topic);
        return false;
    }

    // Add timestamp to metadata
    auto extended_metadata = metadata;
    extended_metadata["timestamp"] = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    try {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        
        if (message_queue_.size() >= static_cast<size_t>(config_.sndhwm)) {
            if (!message_queue_.empty()) {
                message_queue_.pop();
                messages_dropped_.fetch_add(1);
            }
        }

        auto message = std::make_shared<QueuedMessage>(topic, data, extended_metadata, priority);
        message_queue_.push(message);
        
        queue_cv_.notify_one();
        return true;
    } catch (const std::exception& e) {
        setLastError("Failed to queue message with metadata: " + std::string(e.what()));
        return false;
    }
}

void GenericZmqServer::publish(const std::string& topic, const std::string& message) {
    publishAsync(topic, message, MessagePriority::NORMAL);
}

void GenericZmqServer::registerRequestHandler(const std::string& request_type, RequestHandler handler) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    request_handlers_[request_type] = std::move(handler);
}

void GenericZmqServer::unregisterRequestHandler(const std::string& request_type) {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    request_handlers_.erase(request_type);
}
std::shared_ptr<zmq::context_t> GenericZmqServer::getContext() {
    return context_;
}
void GenericZmqServer::addTopicFilter(const std::string& topic_pattern) {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    if (std::find(topic_filters_.begin(), topic_filters_.end(), topic_pattern) == topic_filters_.end()) {
        topic_filters_.push_back(topic_pattern);
    }
}

void GenericZmqServer::removeTopicFilter(const std::string& topic_pattern) {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    topic_filters_.erase(
        std::remove(topic_filters_.begin(), topic_filters_.end(), topic_pattern),
        topic_filters_.end());
}

std::string GenericZmqServer::getLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

size_t GenericZmqServer::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return message_queue_.size();
}

bool GenericZmqServer::initializeSockets() {
    try {
        // Create PUB socket
        pub_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_PUB);
        
        // Set socket options for reliability
        pub_socket_->set(zmq::sockopt::sndhwm, config_.sndhwm);
        pub_socket_->set(zmq::sockopt::sndtimeo, config_.sndtimeo);
        pub_socket_->set(zmq::sockopt::linger, config_.linger);
        pub_socket_->set(zmq::sockopt::immediate, config_.immediate ? 1 : 0);
        
        // Heartbeat settings for connection monitoring
        pub_socket_->set(zmq::sockopt::heartbeat_ivl, config_.heartbeat_ivl);
        pub_socket_->set(zmq::sockopt::heartbeat_timeout, config_.heartbeat_timeout);
        pub_socket_->set(zmq::sockopt::heartbeat_ttl, config_.heartbeat_ttl);
        
        pub_socket_->bind(config_.pub_endpoint);
    std::cout << "[ZMQ Server] PUB bound to '" << config_.pub_endpoint << "'" << std::endl;

        // Create REP socket
        rep_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_REP);
        
        rep_socket_->set(zmq::sockopt::rcvhwm, config_.rcvhwm);
        rep_socket_->set(zmq::sockopt::rcvtimeo, config_.rcvtimeo);
        rep_socket_->set(zmq::sockopt::sndtimeo, config_.sndtimeo);
        rep_socket_->set(zmq::sockopt::linger, config_.linger);
        
        rep_socket_->bind(config_.rep_endpoint);
    std::cout << "[ZMQ Server] REP bound to '" << config_.rep_endpoint << "'" << std::endl;

        return true;
    } catch (const zmq::error_t& e) {
        setLastError("Failed to initialize sockets: " + std::string(e.what()));
        cleanupSockets();
        return false;
    }
}

void GenericZmqServer::cleanupSockets() {
    pub_socket_.reset();
    rep_socket_.reset();
}

void GenericZmqServer::publishWorker() {
    while (!should_stop_.load()) {
        std::shared_ptr<QueuedMessage> message;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { 
                return !message_queue_.empty() || should_stop_.load(); 
            });
            
            if (should_stop_.load()) {
                break;
            }
            
            if (!message_queue_.empty()) {
                message = message_queue_.top();
                message_queue_.pop();
            }
        }
        
        if (message && matchesTopicFilter(message->topic)) {
            // Debug: log dequeued message info
            // try {
            //     std::cout << "[ZMQ Server] Dequeued message topic='" << message->topic << "' data_len=" << message->data.size()
            //               << " queue_size=" << getQueueSize() << std::endl;
            // } catch (...) {
            //     // swallow any logging errors
            // }

            if (sendMessage(message->topic, message->data, message->metadata)) {
                messages_sent_.fetch_add(1);
            } else {
                // Retry logic
                message->retry_count++;
                if (message->retry_count < config_.max_retries) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(config_.retry_delay_ms));
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    message_queue_.push(message);
                } else {
                    messages_dropped_.fetch_add(1);
                }
            }
        }
    }
}

void GenericZmqServer::requestWorker() {
    while (!should_stop_.load()) {
        try {
            zmq::message_t request;
            auto result = rep_socket_->recv(request, zmq::recv_flags::dontwait);
            
            if (result) {
                std::string request_str(static_cast<char*>(request.data()), request.size());
                std::cout << "[ZMQ Server] Received raw request: '" << request_str << "' size=" << request.size() << std::endl;
                
                // Parse request JSON
                nlohmann::json request_json;
                std::string response;
                
                try {
                    request_json = nlohmann::json::parse(request_str);
                    std::string request_type = request_json["type"];
                    std::string data = request_json["data"];
                    
                    std::unordered_map<std::string, std::string> metadata;
                    if (request_json.contains("metadata")) {
                        for (const auto& [key, value] : request_json["metadata"].items()) {
                            metadata[key] = value;
                        }
                    }
                    
                    // Find and execute handler
                    {
                        std::lock_guard<std::mutex> lock(handlers_mutex_);
                        auto it = request_handlers_.find(request_type);
                        if (it != request_handlers_.end()) {
                            response = it->second(data, metadata);
                        } else {
                            response = R"({"error": "Unknown request type"})";
                        }
                    }
                } catch (const std::exception& e) {
                    response = R"({"error": "Invalid request format"})";
                }
                
                // Send response
                zmq::message_t reply(response.size());
                memcpy(reply.data(), response.c_str(), response.size());
                rep_socket_->send(reply, zmq::send_flags::none);
                std::cout << "[ZMQ Server] Sent reply: '" << response << "'" << std::endl;
            } else {
                // No message received, sleep briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN) {  // EAGAIN is expected for non-blocking recv
                setLastError("Request worker error: " + std::string(e.what()));
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }
}

void GenericZmqServer::monitorWorker() {
    // Basic monitoring implementation
    while (!should_stop_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        // Log statistics
        std::cout << "Server stats - Messages sent: " << messages_sent_.load() 
                  << ", Messages dropped: " << messages_dropped_.load()
                  << ", Queue size: " << getQueueSize() << std::endl;
    }
}

bool GenericZmqServer::sendMessage(const std::string& topic, const std::string& data, 
                                  const std::unordered_map<std::string, std::string>& metadata) {
    try {
        // Create metadata frame
        std::string metadata_frame = createMetadataFrame(metadata);
        
        // Send multipart message: [topic][data][metadata]
        zmq::message_t topic_msg(topic.size());
        memcpy(topic_msg.data(), topic.c_str(), topic.size());
        
        // Normal operation: do not emit large binary dumps to stdout here.

        zmq::message_t data_msg(data.size());
        memcpy(data_msg.data(), data.c_str(), data.size());
        
        zmq::message_t metadata_msg(metadata_frame.size());
        memcpy(metadata_msg.data(), metadata_frame.c_str(), metadata_frame.size());
        
        // Send with proper multipart framing
        if (!pub_socket_->send(topic_msg, zmq::send_flags::sndmore)) {
            return false;
        }
        if (!pub_socket_->send(data_msg, zmq::send_flags::sndmore)) {
            return false;
        }
        if (!pub_socket_->send(metadata_msg, zmq::send_flags::none)) {
            return false;
        }
        
        return true;
    } catch (const zmq::error_t& e) {
        setLastError("Failed to send message: " + std::string(e.what()));
        return false;
    }
}

bool GenericZmqServer::matchesTopicFilter(const std::string& topic) const {
    std::lock_guard<std::mutex> lock(filters_mutex_);
    
    if (topic_filters_.empty()) {
        return true;  // No filters means all topics are allowed
    }
    
    for (const auto& pattern : topic_filters_) {
        if (matchesPattern(topic, pattern)) {
            return true;
        }
    }
    
    return false;
}

void GenericZmqServer::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
    std::cerr << "ZMQ Server Error: " << error << std::endl;
}

std::string GenericZmqServer::createMetadataFrame(const std::unordered_map<std::string, std::string>& metadata) const {
    nlohmann::json metadata_json = metadata;
    return metadata_json.dump();
}

}