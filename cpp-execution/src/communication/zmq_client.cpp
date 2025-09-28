#include "communication/zmq_server_client.hpp"
#include <iostream>
#include <algorithm>
namespace trading::communication {

// Compute the subscription key to pass to ZMQ. ZMQ subscriptions match prefixes,
// so if the user provides a pattern with wildcards (e.g. "news.*"), we subscribe
// to the prefix before the first wildcard ("news."). If the pattern starts with
// a wildcard or is exactly "*", subscribe to empty string to receive all messages.
static std::string computeSubscriptionKey(const std::string& pattern) {
    if (pattern.empty() || pattern == "*") {
        return std::string();
    }
    size_t pos_star = pattern.find('*');
    size_t pos_q = pattern.find('?');
    size_t pos = std::string::npos;
    if (pos_star != std::string::npos && pos_q != std::string::npos) {
        pos = std::min(pos_star, pos_q);
    } else if (pos_star != std::string::npos) {
        pos = pos_star;
    } else if (pos_q != std::string::npos) {
        pos = pos_q;
    }

    if (pos == 0) {
        // Starts with a wildcard, subscribe to everything
        return std::string();
    }

    if (pos == std::string::npos) {
        return pattern; // exact topic (no wildcards)
    }

    return pattern.substr(0, pos);
}


GenericZmqClient::GenericZmqClient(const ZmqClientConfig& config, std::shared_ptr<zmq::context_t> shared_context) 
    : config_(config) {
    // Use provided shared context if available; otherwise create our own
    if (shared_context) {
        context_ = shared_context;
    } else {
        context_ = std::make_shared<zmq::context_t>(1);
    }
}

GenericZmqClient::~GenericZmqClient() {
    if (connected_.load()) {
        disconnect();
    }
}

bool GenericZmqClient::connect() {
    if (connected_.load()) {
        setLastError("Client is already connected");
        return false;
    }

    try {
        if (!initializeSockets()) {
            return false;
        }

        should_stop_.store(false);
        connected_.store(true);

        // Start worker threads
        sub_thread_ = std::thread(&GenericZmqClient::subscribeWorker, this);
        
        if (config_.auto_reconnect) {
            monitor_thread_ = std::thread(&GenericZmqClient::monitorWorker, this);
        }

        return true;
    } catch (const zmq::error_t& e) {
        setLastError("Failed to connect: " + std::string(e.what()));
        connected_.store(false);
        return false;
    }
}

void GenericZmqClient::disconnect() {
    if (!connected_.load()) {
        return;
    }

    should_stop_.store(true);
    connected_.store(false);

    // Join threads
    // Avoid joining a thread from itself. If the current thread is the same as
    // the worker thread, detach instead to allow the object to be destroyed
    // without blocking. Joining the current thread leads to undefined
    // behavior / deadlocks.
    const auto self_id = std::this_thread::get_id();

    if (sub_thread_.joinable()) {
        if (sub_thread_.get_id() == self_id) {
            std::cerr << "GenericZmqClient::disconnect: detaching sub_thread_ because called from within that thread" << std::endl;
            try {
                sub_thread_.detach();
            } catch (...) {
                // If detach fails for any reason, swallow to avoid throwing from
                // a cleanup path.
            }
        } else {
            try {
                sub_thread_.join();
            } catch (const std::exception& e) {
                std::cerr << "Exception while joining sub_thread_: " << e.what() << std::endl;
            }
        }
    }

    if (monitor_thread_.joinable()) {
        if (monitor_thread_.get_id() == self_id) {
            std::cerr << "GenericZmqClient::disconnect: detaching monitor_thread_ because called from within that thread" << std::endl;
            try {
                monitor_thread_.detach();
            } catch (...) {
            }
        } else {
            try {
                monitor_thread_.join();
            } catch (const std::exception& e) {
                std::cerr << "Exception while joining monitor_thread_: " << e.what() << std::endl;
            }
        }
    }

    cleanupSockets();
}
void GenericZmqClient::stop() {
    disconnect();
}
bool GenericZmqClient::start() {
    return connect();
}
bool GenericZmqClient::subscribe(const std::string& topic, MessageHandler handler) {
    if (!connected_.load()) {
        setLastError("Client is not connected");
        return false;
    }

    if (!isValidTopic(topic)) {
        setLastError("Invalid topic: " + topic);
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);

        // Compute ZMQ subscription key (prefix up to wildcard)
        std::string sub_key = computeSubscriptionKey(topic);

        // Set ZMQ subscription (empty string subscribes to all)
        sub_socket_->set(zmq::sockopt::subscribe, sub_key);

        std::cout << "[ZMQ Client] Subscribed pattern='" << topic << "' -> key='" << sub_key << "'" << std::endl;

        // Store handler keyed by original pattern so matching still works
        subscriptions_[topic] = std::move(handler);

        return true;
    } catch (const zmq::error_t& e) {
        setLastError("Failed to subscribe to topic '" + topic + "': " + std::string(e.what()));
        return false;
    }
}

bool GenericZmqClient::unsubscribe(const std::string& topic) {
    if (!connected_.load()) {
        setLastError("Client is not connected");
        return false;
    }

    try {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);

        // Compute the subscription key used when subscribing
        std::string sub_key = computeSubscriptionKey(topic);

        // Remove ZMQ subscription
        sub_socket_->set(zmq::sockopt::unsubscribe, sub_key);

        // Remove handler
        subscriptions_.erase(topic);

        return true;
    } catch (const zmq::error_t& e) {
        setLastError("Failed to unsubscribe from topic '" + topic + "': " + std::string(e.what()));
        return false;
    }
}

bool GenericZmqClient::subscribeMultiple(const std::vector<std::string>& topics, MessageHandler handler) {
    if (!connected_.load()) {
        setLastError("Client is not connected");
        return false;
    }

    bool all_success = true;
    
    for (const auto& topic : topics) {
        if (!subscribe(topic, handler)) {
            all_success = false;
            // Continue with other topics
        }
    }
    
    return all_success;
}

void GenericZmqClient::unsubscribeAll() {
    if (!connected_.load()) {
        return;
    }

    std::lock_guard<std::mutex> lock(subscriptions_mutex_);
    
    try {
        for (const auto& [topic, handler] : subscriptions_) {
            std::string sub_key = computeSubscriptionKey(topic);
            sub_socket_->set(zmq::sockopt::unsubscribe, sub_key);
        }

        subscriptions_.clear();
    } catch (const zmq::error_t& e) {
        setLastError("Failed to unsubscribe from all topics: " + std::string(e.what()));
    }
}

std::string GenericZmqClient::sendRequest(const std::string& request_type, const std::string& data, 
                                         const std::unordered_map<std::string, std::string>& metadata) {
    if (!connected_.load()) {
        setLastError("Client is not connected");
        return "";
    }

    try {
        // Create request frame
        std::string request_frame = createRequestFrame(request_type, data, metadata);
        
        int retries = 0;
        while (retries < config_.max_retries) {
            try {
                // Send request
                zmq::message_t request(request_frame.size());
                memcpy(request.data(), request_frame.c_str(), request_frame.size());
                
                if (!req_socket_->send(request, zmq::send_flags::none)) {
                    throw zmq::error_t();
                }
                
                // Receive response
                zmq::message_t reply;
                auto result = req_socket_->recv(reply, zmq::recv_flags::none);
                
                if (result) {
                    return std::string(static_cast<char*>(reply.data()), reply.size());
                } else {
                    throw zmq::error_t();
                }
            } catch (const zmq::error_t& e) {
                retries++;
                if (retries < config_.max_retries) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(config_.retry_delay_ms));
                    
                    // Reconnect request socket
                    req_socket_.reset();
                    req_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_REQ);
                    req_socket_->set(zmq::sockopt::rcvtimeo, config_.rcvtimeo);
                    req_socket_->set(zmq::sockopt::sndtimeo, config_.sndtimeo);
                    req_socket_->set(zmq::sockopt::linger, config_.linger);
                    req_socket_->connect(config_.req_endpoint);
                } else {
                    setLastError("Failed to send request after " + std::to_string(config_.max_retries) + " retries: " + std::string(e.what()));
                    return "";
                }
            }
        }
        
        return "";
    } catch (const std::exception& e) {
        setLastError("Request failed: " + std::string(e.what()));
        return "";
    }
}

std::future<std::string> GenericZmqClient::sendRequestAsync(const std::string& request_type, 
                                                           const std::string& data,
                                                           const std::unordered_map<std::string, std::string>& metadata) {
    return std::async(std::launch::async, [this, request_type, data, metadata]() {
        return sendRequest(request_type, data, metadata);
    });
}

std::string GenericZmqClient::getLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

bool GenericZmqClient::initializeSockets() {
    try {
        // Create SUB socket
        sub_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        
        // Set socket options
        sub_socket_->set(zmq::sockopt::rcvhwm, config_.rcvhwm);
        sub_socket_->set(zmq::sockopt::rcvtimeo, config_.rcvtimeo);
        sub_socket_->set(zmq::sockopt::linger, config_.linger);
        
        // Heartbeat settings
        sub_socket_->set(zmq::sockopt::heartbeat_ivl, config_.heartbeat_ivl);
        sub_socket_->set(zmq::sockopt::heartbeat_timeout, config_.heartbeat_timeout);
        sub_socket_->set(zmq::sockopt::heartbeat_ttl, config_.heartbeat_ttl);
        
        // Connect to all publisher endpoints
        for (const auto& endpoint : config_.pub_endpoints) {
            sub_socket_->connect(endpoint);
        }

        // Create REQ socket
        req_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_REQ);
        
        req_socket_->set(zmq::sockopt::rcvtimeo, config_.rcvtimeo);
        req_socket_->set(zmq::sockopt::sndtimeo, config_.sndtimeo);
        req_socket_->set(zmq::sockopt::linger, config_.linger);
        
        req_socket_->connect(config_.req_endpoint);

        return true;
    } catch (const zmq::error_t& e) {
        setLastError("Failed to initialize sockets: " + std::string(e.what()));
        cleanupSockets();
        return false;
    }
}

void GenericZmqClient::cleanupSockets() {
    sub_socket_.reset();
    req_socket_.reset();
}

void GenericZmqClient::subscribeWorker() {
    while (!should_stop_.load()) {
        try {
            // Receive multipart message: [topic][data][metadata]
            std::vector<zmq::message_t> frames;
            zmq::recv_result_t result;
            
            // Receive topic frame
            zmq::message_t topic_frame;
            result = sub_socket_->recv(topic_frame, zmq::recv_flags::dontwait);
            
            if (!result) {
                // No message available, sleep briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            std::string topic(static_cast<char*>(topic_frame.data()), topic_frame.size());
            std::cout << "[ZMQ Client] Received topic frame='" << topic << "' size=" << topic_frame.size() << std::endl;
            
            // Check if there are more frames
            int more = 0;
            size_t more_size = sizeof(more);
            sub_socket_->getsockopt(ZMQ_RCVMORE, &more, &more_size);
            
            if (!more) {
                // Single frame message, treat as legacy format
                std::cout << "[ZMQ Client] No more frames after topic\n";
                continue;
            }
            
            // Receive data frame
            zmq::message_t data_frame;
            result = sub_socket_->recv(data_frame, zmq::recv_flags::none);
            if (!result) {
                continue;
            }
            
            std::string data(static_cast<char*>(data_frame.data()), data_frame.size());
            
            // Check for metadata frame
            sub_socket_->getsockopt(ZMQ_RCVMORE, &more, &more_size);
            
            std::unordered_map<std::string, std::string> metadata;
            if (more) {
                zmq::message_t metadata_frame;
                result = sub_socket_->recv(metadata_frame, zmq::recv_flags::none);
                if (result) {
                    std::string metadata_str(static_cast<char*>(metadata_frame.data()), metadata_frame.size());
                    metadata = parseMetadataFrame(metadata_str);
                }
            }
            
            // Find matching subscription handler
            MessageHandler handler;
            {
                std::lock_guard<std::mutex> lock(subscriptions_mutex_);
                // Debug: list current subscription patterns
                std::cout << "[ZMQ Client] Current subscriptions:";
                for (const auto& [pattern, h] : subscriptions_) {
                    std::cout << " '" << pattern << "'";
                }
                std::cout << std::endl;

                // Find exact match first
                auto it = subscriptions_.find(topic);
                if (it != subscriptions_.end()) {
                    handler = it->second;
                } else {
                    // Check for pattern matches and print diagnostics
                    for (const auto& [pattern, h] : subscriptions_) {
                        bool m = matchesPattern(topic, pattern);
                        std::cout << "[ZMQ Client] matching topic='" << topic << "' against pattern='" << pattern << "' -> " << (m ? "MATCH" : "NO") << std::endl;
                        if (m) {
                            handler = h;
                            break;
                        }
                    }
                }
            }
            
            if (handler) {
                messages_received_.fetch_add(1);
                
                // Execute handler in a separate thread to avoid blocking
                std::thread([handler = std::move(handler), topic, data, metadata]() {
                    try {
                        handler(topic, data, metadata);
                    } catch (const std::exception& e) {
                        std::cerr << "Handler exception for topic '" << topic << "': " << e.what() << std::endl;
                    }
                }).detach();
            }
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN) {  // EAGAIN is expected for non-blocking recv
                setLastError("Subscribe worker error: " + std::string(e.what()));
                
                if (config_.auto_reconnect && !should_stop_.load()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(config_.reconnect_delay_ms));
                    reconnect();
                }
            }
        }
    }
}

void GenericZmqClient::monitorWorker() {
    while (!should_stop_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        // Log statistics
        std::cout << "Client stats - Messages received: " << messages_received_.load() << std::endl;
        
        // Check connection health (simplified)
        if (connected_.load() && config_.auto_reconnect) {
            // Could implement more sophisticated health checks here
        }
    }
}

bool GenericZmqClient::reconnect() {
    std::cout << "Attempting to reconnect..." << std::endl;
    
    cleanupSockets();
    
    if (initializeSockets()) {
        // Restore subscriptions
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        try {
            for (const auto& [topic, handler] : subscriptions_) {
                std::string sub_key = computeSubscriptionKey(topic);
                sub_socket_->set(zmq::sockopt::subscribe, sub_key);
            }
            std::cout << "Reconnected successfully" << std::endl;
            return true;
        } catch (const zmq::error_t& e) {
            setLastError("Failed to restore subscriptions after reconnect: " + std::string(e.what()));
            return false;
        }
    }
    
    return false;
}

void GenericZmqClient::setLastError(const std::string& error) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error;
    std::cerr << "ZMQ Client Error: " << error << std::endl;
}

std::unordered_map<std::string, std::string> GenericZmqClient::parseMetadataFrame(const std::string& metadata_json) const {
    std::unordered_map<std::string, std::string> metadata;
    
    try {
        if (!metadata_json.empty()) {
            nlohmann::json json_obj = nlohmann::json::parse(metadata_json);
            for (const auto& [key, value] : json_obj.items()) {
                metadata[key] = value;
            }
        }
    } catch (const std::exception& e) {
        // Log parsing error but don't fail the message processing
        std::cerr << "Failed to parse metadata: " << e.what() << std::endl;
    }
    
    return metadata;
}

std::string GenericZmqClient::createRequestFrame(const std::string& request_type, const std::string& data,
                                                const std::unordered_map<std::string, std::string>& metadata) const {
    nlohmann::json request_json;
    request_json["type"] = request_type;
    request_json["data"] = data;
    request_json["metadata"] = metadata;
    request_json["correlation_id"] = generateCorrelationId();
    request_json["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    return request_json.dump();
}

} // namespace trading::communication