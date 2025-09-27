/**
 * @file zmq_client.cpp
 * @brief Clean implementation of the ZeroMQ Client library - FIXED VERSION
 * 
 * @author Trading System Team
 * @date September 27, 2025
 * @fix Fixed double-free corruption in subscriberWorker message handling
 */

#include "communication/zmq_client.hpp"
#include <iostream>
#include <sstream>
#include <random>
#include <iomanip>
#include <deque>
#include "communication/zmq_manager.hpp"

namespace trading {
namespace communication {

// =============================================================================
// CONSTRUCTOR AND DESTRUCTOR
// =============================================================================

GenericZmqClient::GenericZmqClient(const ZmqClientConfig& config) : config_(config) {
    if (!config_.validate()) {
        throw std::invalid_argument("Invalid ZMQ client configuration");
    }
    
    logInfo("ZMQ Client created");
}

GenericZmqClient::~GenericZmqClient() {
    disconnect();
}

// =============================================================================
// CONNECTION MANAGEMENT
// =============================================================================

bool GenericZmqClient::connect() {
    if (connected_.load()) {
        logInfo("Client already connected");
        return true;
    }
    
    try {
        stats_.connection_attempts++;
        
        if (!initializeSockets()) {
            logError("Failed to initialize sockets");
            return false;
        }
        
        // Queue all existing subscriptions for the subscriber thread to apply
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            std::lock_guard<std::mutex> sub_lock(pending_sub_mutex_);
            for (const auto& [topic, handler] : message_handlers_) {
                pending_subscriptions_.emplace_back(true, topic);
                logInfo("Queued subscription for topic: " + topic);
            }
        }

        // Start subscriber thread
        running_.store(true);
        subscriber_thread_ = std::thread(&GenericZmqClient::subscriberWorker, this);

        connected_.store(true);
        stats_.connection_time = std::chrono::steady_clock::now();
        
        logInfo("ZMQ Client connected successfully");
        logInfo("Publisher endpoint: " + config_.server_pub_endpoint);
        logInfo("Request endpoint: " + config_.server_req_endpoint);
        
        return true;
        
    } catch (const std::exception& e) {
        logError("Failed to connect: " + std::string(e.what()));
        cleanup();
        return false;
    }
}

void GenericZmqClient::disconnect() {
    if (!connected_.load()) {
        return;
    }
    
    logInfo("Disconnecting ZMQ Client...");
    
    // Stop subscriber thread
    running_.store(false);
    connected_.store(false);
    
    if (subscriber_thread_.joinable()) {
        subscriber_thread_.join();
    }
    
    // Cleanup resources
    cleanup();
    
    logInfo("ZMQ Client disconnected");
}

// =============================================================================
// SUBSCRIPTION INTERFACE
// =============================================================================

bool GenericZmqClient::subscribe(const std::string& topic, MessageHandler handler) {
    // Always store the handler so callers can subscribe before connect()
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        message_handlers_[topic] = std::move(handler);
    }

    // Queue subscription command for subscriber thread to apply
    {
        std::lock_guard<std::mutex> lk(pending_sub_mutex_);
        pending_subscriptions_.emplace_back(true, topic);
    }
    
    logInfo("Queued subscribe command for topic: " + topic);
    return true;
}

bool GenericZmqClient::unsubscribe(const std::string& topic) {
    // Remove handler immediately
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        message_handlers_.erase(topic);
    }

    // Queue unsubscribe command for subscriber thread
    {
        std::lock_guard<std::mutex> lk(pending_sub_mutex_);
        pending_subscriptions_.emplace_back(false, topic);
    }
    
    logInfo("Queued unsubscribe command for topic: " + topic);
    return true;
}

bool GenericZmqClient::subscribeMultiple(const std::vector<std::string>& topics, MessageHandler handler) {
    bool all_success = true;
    
    for (const auto& topic : topics) {
        if (!subscribe(topic, handler)) {
            all_success = false;
        }
    }
    
    return all_success;
}

void GenericZmqClient::unsubscribeAll() {
    std::lock_guard<std::mutex> lock(handlers_mutex_);
    std::lock_guard<std::mutex> sub_lock(pending_sub_mutex_);
    
    for (const auto& [topic, handler] : message_handlers_) {
        pending_subscriptions_.emplace_back(false, topic);
    }
    
    message_handlers_.clear();
    logInfo("Unsubscribed from all topics");
}

// =============================================================================
// REQUEST-REPLY INTERFACE
// =============================================================================

std::string GenericZmqClient::sendRequest(const std::string& request_type,
                                         const std::string& data,
                                         const std::unordered_map<std::string, std::string>& metadata) {
    if (!connected_.load() || !request_socket_) {
        logError("Cannot send request - client not connected");
        return "";
    }
    
    try {
        // Create request message
        std::string request_msg = createRequestMessage(request_type, data, metadata);
        
        // Send request
        zmq::message_t request(request_msg.size());
        memcpy(request.data(), request_msg.c_str(), request_msg.size());
        
        auto send_result = request_socket_->send(request, zmq::send_flags::none);
        if (!send_result.has_value()) {
            logError("Failed to send request");
            stats_.requests_failed++;
            return "";
        }
        
        // Wait for response with timeout
        zmq::message_t response;
        request_socket_->set(zmq::sockopt::rcvtimeo, static_cast<int>(config_.request_timeout.count()));
        
        auto recv_result = request_socket_->recv(response, zmq::recv_flags::none);
        if (!recv_result.has_value()) {
            logError("Request timeout or receive failed");
            stats_.requests_failed++;
            return "";
        }
        
        stats_.requests_sent++;
        
        std::string response_data(static_cast<char*>(response.data()), response.size());
        
        logInfo("Request processed successfully for type: " + request_type);
        
        return response_data;
        
    } catch (const zmq::error_t& e) {
        logError("ZMQ error sending request: " + std::string(e.what()));
        stats_.requests_failed++;
        return "";
    } catch (const std::exception& e) {
        logError("Error sending request: " + std::string(e.what()));
        stats_.requests_failed++;
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

// =============================================================================
// UTILITY METHODS
// =============================================================================

std::string GenericZmqClient::getLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

// =============================================================================
// PRIVATE METHODS
// =============================================================================

bool GenericZmqClient::initializeSockets() {
    try {
        // Use shared context from ZmqManager to avoid per-component contexts
        auto shared_ctx = trading::communication::ZmqManager::instance().getContext(config_.io_threads);
        context_ = shared_ctx;

        // Create subscriber socket
        subscriber_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        
        // Configure subscriber socket
        if (config_.subscriber_conflate) {
            subscriber_socket_->set(zmq::sockopt::conflate, 1);
        }
        subscriber_socket_->set(zmq::sockopt::rcvtimeo, 1000);  // 1 second timeout
        subscriber_socket_->connect(config_.server_pub_endpoint);

        // Create request socket
        request_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_REQ);
        request_socket_->connect(config_.server_req_endpoint);

        logInfo("ZMQ sockets initialized successfully");
        return true;

    } catch (const zmq::error_t& e) {
        logError("ZMQ error initializing sockets: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        logError("Error initializing sockets: " + std::string(e.what()));
        return false;
    }
}

void GenericZmqClient::cleanup() {
    // Clear pending subscriptions
    {
        std::lock_guard<std::mutex> lk(pending_sub_mutex_);
        pending_subscriptions_.clear();
    }
    
    // Close sockets
    if (subscriber_socket_) {
        subscriber_socket_->close();
        subscriber_socket_.reset();
    }
    
    if (request_socket_) {
        request_socket_->close();
        request_socket_.reset();
    }
    
    // Reset context reference
    context_.reset();
    
    // Clear handlers
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        message_handlers_.clear();
    }
    
    logInfo("ZMQ Client resources cleaned up");
}

void GenericZmqClient::subscriberWorker() {
    logInfo("Subscriber worker thread started");
    
    while (running_.load()) {
        try {
            if (!subscriber_socket_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }

            // Apply any pending subscription/unsubscribe commands queued by
            // other threads. Doing this here ensures only the subscriber
            // thread calls socket->set(), avoiding concurrent socket use.
            {
                std::deque<std::pair<bool, std::string>> to_apply;
                {
                    std::lock_guard<std::mutex> lk(pending_sub_mutex_);
                    to_apply.swap(pending_subscriptions_);
                }

                for (const auto &cmd : to_apply) {
                    try {
                        if (cmd.first) {
                            subscriber_socket_->set(zmq::sockopt::subscribe, cmd.second);
                            logInfo("Applied subscribe for topic: " + cmd.second);
                        } else {
                            subscriber_socket_->set(zmq::sockopt::unsubscribe, cmd.second);
                            logInfo("Applied unsubscribe for topic: " + cmd.second);
                        }
                    } catch (const std::exception &e) {
                        logError(std::string("Failed to apply subscription command for topic '") + cmd.second + "': " + e.what());
                    }
                }
            }
            
            // **FIXED: Use safer message handling with proper RAII and explicit cleanup**
            std::string topic;
            std::string data;
            
            // Create messages on the stack with proper cleanup
            {
                zmq::message_t firstMsg;
                auto recv_res = subscriber_socket_->recv(firstMsg, zmq::recv_flags::none);
                if (!recv_res.has_value()) {
                    // no message available within timeout or interrupted
                    continue;
                }

                // **FIXED: Immediately copy data out of ZMQ message to avoid double-free**
                std::string firstStr(static_cast<char*>(firstMsg.data()), firstMsg.size());
                
                // Check for more parts - do this BEFORE the message goes out of scope
                bool has_more = false;
                try {
                    has_more = subscriber_socket_->get(zmq::sockopt::rcvmore);
                } catch (const zmq::error_t& /*e*/) {
                    has_more = false;
                }

                if (has_more) {
                    zmq::message_t secondMsg;
                    auto recv_res2 = subscriber_socket_->recv(secondMsg, zmq::recv_flags::none);
                    if (!recv_res2.has_value()) {
                        // Unexpected: second frame missing, treat first as data
                        topic.clear();
                        data = std::move(firstStr);
                    } else {
                        // **FIXED: Immediately copy data out of second message**
                        topic = std::move(firstStr);
                        data.assign(static_cast<char*>(secondMsg.data()), secondMsg.size());
                        // secondMsg will be destroyed here safely
                    }
                    // firstMsg will be destroyed here safely
                } else {
                    // Single-frame message: try to parse topic|data convention
                    size_t delimiter_pos = firstStr.find('|');
                    if (delimiter_pos != std::string::npos) {
                        topic = firstStr.substr(0, delimiter_pos);
                        data = firstStr.substr(delimiter_pos + 1);
                    } else {
                        topic.clear();
                        data = std::move(firstStr);
                    }
                    // firstMsg will be destroyed here safely
                }
            } // **FIXED: All zmq::message_t objects are destroyed here with proper cleanup**
            
            // Basic validation
            if (data.empty()) {
                continue; // Skip empty messages
            }

            // Process the message using copied data (no ZMQ objects involved)
            handleMessage(topic, data);
            stats_.messages_received++;
            stats_.last_message_time = std::chrono::steady_clock::now();
            
        } catch (const zmq::error_t& e) {
            if (e.num() != EAGAIN && e.num() != EINTR) {
                logError("ZMQ error in subscriber worker: " + std::string(e.what()));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } catch (const std::exception& e) {
            logError("Error in subscriber worker: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    logInfo("Subscriber worker thread stopped");
}

void GenericZmqClient::handleMessage(const std::string& topic, const std::string& raw_data) {
    try {
        // Parse message to extract data and metadata
        auto [data, metadata] = parseMessage(raw_data);
        
        // Find appropriate handler
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        
        // First try exact match
        auto handler_it = message_handlers_.find(topic);
        if (handler_it != message_handlers_.end()) {
            try {
                handler_it->second(topic, data, metadata);
                return;
            } catch (const std::exception& e) {
                logError("Error in message handler for topic '" + topic + "': " + std::string(e.what()));
                return;
            }
        }
        
        // Then try pattern matching (simple prefix matching)
        for (const auto& [pattern, handler] : message_handlers_) {
            if (topic.find(pattern) == 0 || pattern == "*") {
                try {
                    handler(topic, data, metadata);
                    return;
                } catch (const std::exception& e) {
                    logError("Error in pattern handler for topic '" + topic + "': " + std::string(e.what()));
                    return;
                }
            }
        }
        
        // If no handler found, log debug message
        if (logging_enabled_.load()) {
            logInfo("No handler found for topic: " + topic);
        }
        
    } catch (const std::exception& e) {
        logError("Error handling message for topic '" + topic + "': " + std::string(e.what()));
    }
}

std::pair<std::string, std::unordered_map<std::string, std::string>>
GenericZmqClient::parseMessage(const std::string& raw_data) const {
    try {
        // Try to parse as JSON with metadata
        auto message = nlohmann::json::parse(raw_data);
        
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
        return {raw_data, {}};
    }
}

std::string GenericZmqClient::createRequestMessage(const std::string& request_type,
                                                  const std::string& data,
                                                  const std::unordered_map<std::string, std::string>& metadata) const {
    nlohmann::json message;
    message["data"] = data;
    
    // Add metadata
    auto full_metadata = metadata;
    full_metadata["type"] = request_type;
    full_metadata["request_id"] = generateRequestId();
    full_metadata["timestamp"] = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    
    message["metadata"] = full_metadata;
    
    return message.dump();
}

void GenericZmqClient::logError(const std::string& error_msg) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    last_error_ = error_msg;
    
    if (logging_enabled_.load()) {
        std::cerr << "[ERROR] ZMQ Client: " << error_msg << std::endl;
    }
}

void GenericZmqClient::logInfo(const std::string& info_msg) {
    if (logging_enabled_.load()) {
        std::cout << "[INFO] ZMQ Client: " << info_msg << std::endl;
    }
}

std::string GenericZmqClient::generateRequestId() const {
    static thread_local std::mt19937 gen(std::random_device{}());
    static thread_local std::uniform_int_distribution<uint64_t> dis;
    
    std::ostringstream oss;
    oss << "req_" << std::hex << dis(gen);
    return oss.str();
}

} // namespace communication
} // namespace trading