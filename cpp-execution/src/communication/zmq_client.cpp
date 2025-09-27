/**
 * @file zmq_client.cpp
 * @brief Implementation of the ZeroMQ Client library
 * 
 * @author Trading System Team
 * @date September 17, 2025
 */

#include "communication/zmq_client.hpp"
#include <iostream>
#include <sstream>
#include <random>
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
        if (!initializeSockets()) {
            logError("Failed to initialize sockets");
            return false;
        }
        
        // Apply any pending subscriptions to the newly-created socket
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            if (subscriber_socket_) {
                for (const auto& [topic, handler] : message_handlers_) {
                    try {
                        subscriber_socket_->set(zmq::sockopt::subscribe, topic);
                        if (logging_enabled_) {
                            logInfo("Applied pending subscription for topic: " + topic);
                        }
                    } catch (const std::exception& e) {
                        logError(std::string("Failed to apply pending subscription for topic '") + topic + "': " + e.what());
                    }
                }
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

    // If already connected, apply the subscription to the socket immediately
    if (connected_.load() && subscriber_socket_) {
        try {
            subscriber_socket_->set(zmq::sockopt::subscribe, topic);
            logInfo("Subscribed to topic: " + topic);
            return true;
        } catch (const zmq::error_t& e) {
            logError("ZMQ error subscribing to topic '" + topic + "': " + std::string(e.what()));
            return false;
        } catch (const std::exception& e) {
            logError("Error subscribing to topic '" + topic + "': " + std::string(e.what()));
            return false;
        }
    }

    // Not connected yet — subscription stored and will be applied on connect()
    if (logging_enabled_) {
        logInfo("Stored subscription for topic (pending connect): " + topic);
    }
    return true;
}

bool GenericZmqClient::unsubscribe(const std::string& topic) {
    // Remove handler immediately
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        message_handlers_.erase(topic);
    }

    // If connected, also remove subscription filter from socket
    if (connected_.load() && subscriber_socket_) {
        try {
            subscriber_socket_->set(zmq::sockopt::unsubscribe, topic);
            logInfo("Unsubscribed from topic: " + topic);
            return true;
        } catch (const zmq::error_t& e) {
            logError("ZMQ error unsubscribing from topic '" + topic + "': " + std::string(e.what()));
            return false;
        } catch (const std::exception& e) {
            logError("Error unsubscribing from topic '" + topic + "': " + std::string(e.what()));
            return false;
        }
    }

    if (logging_enabled_) {
        logInfo("Removed pending subscription for topic (was not connected): " + topic);
    }
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
    
    for (const auto& [topic, handler] : message_handlers_) {
        try {
            if (subscriber_socket_) {
                subscriber_socket_->set(zmq::sockopt::unsubscribe, topic);
            }
        } catch (const std::exception& e) {
            logError("Error unsubscribing from topic '" + topic + "': " + std::string(e.what()));
        }
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
        
        if (logging_enabled_) {
            logInfo("Request processed successfully for type: " + request_type);
        }
        
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
    // Keep a shared_ptr reference so lifetime is managed by ZmqManager and users
    context_ = shared_ctx;

    // Create subscriber socket on shared context
    subscriber_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_SUB);
        // Optionally enable conflate on subscriber to keep only the latest message
        try {
            if (config_.subscriber_conflate) subscriber_socket_->set(zmq::sockopt::conflate, 1);
        } catch (...) {}
        // Use a reasonable receive timeout to avoid busy-waiting and partial-frame reads
        subscriber_socket_->set(zmq::sockopt::rcvtimeo, 1000);  // 1000ms
        subscriber_socket_->connect(config_.server_pub_endpoint);

        // Create request socket
    request_socket_ = std::make_unique<zmq::socket_t>(*context_, ZMQ_REQ);
        request_socket_->connect(config_.server_req_endpoint);

        logInfo("ZMQ sockets initialized successfully");
        return true;

    } catch (const zmq::error_t& e) {
        logError("GenericZmqClient Client error initializing sockets: " + std::string(e.what()));
        return false;
    } catch (const std::exception& e) {
        logError("GenericZmqClient error initializing sockets: " + std::string(e.what()));
        return false;
    }
}

void GenericZmqClient::cleanup() {
    // Close sockets
    if (subscriber_socket_) {
        subscriber_socket_->close();
        subscriber_socket_.reset();
    }
    
    if (request_socket_) {
        request_socket_->close();
        request_socket_.reset();
    }
    
    // Reset our shared_ptr reference to the shared context (manager owns actual lifecycle)
    context_.reset();
    
    // Clear handlers
    {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        message_handlers_.clear();
    }
    
    logInfo("ZMQ Client resources cleaned up");
}

// void GenericZmqClient::subscriberWorker() {
//     logInfo("Subscriber worker thread started");
    
//     while (running_.load()) {
//         try {
//             if (!subscriber_socket_) {
//                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
//                 continue;
//             }
            
//             zmq::message_t topic_msg;
//             zmq::message_t data_msg;

//             // Receive first frame (may be topic OR entire single-frame message)
//             // Use the pointer-based recv overload to avoid potential assertion
//             // in some cppzmq/libzmq combinations where sizes can mismatch.
//             bool first_result = false;
//             try {
//                 first_result = subscriber_socket_->recv(&topic_msg, 0);
//             } catch (const zmq::error_t& e) {
//                 logError(std::string("ZMQ error receiving first frame: ") + e.what());
//                 std::this_thread::sleep_for(std::chrono::milliseconds(10));
//                 continue;
//             }
//             if (!first_result) {
//                 // timeout or no message available
//                 continue;
//             }

//             // Check if there are more frames for this multipart message
//             bool more = false;
//             try {
//                 more = static_cast<bool>(subscriber_socket_->get(zmq::sockopt::rcvmore));
//             } catch (const zmq::error_t&) {
//                 // Older cppzmq/backends may throw; fall back to assuming single-frame
//                 more = false;
//             }

//             std::string topic;
//             std::string data;

//             if (more) {
//                 // Standard multipart: topic frame followed by data frame
//                 bool data_result = false;
//                 try {
//                     data_result = subscriber_socket_->recv(&data_msg, 0);
//                 } catch (const zmq::error_t& e) {
//                     logError(std::string("ZMQ error receiving data frame: ") + e.what());
//                     continue;
//                 }
//                 if (!data_result) {
//                     logError("Failed to receive data frame after topic frame (timeout)");
//                     continue;
//                 }
//                 topic.assign(static_cast<char*>(topic_msg.data()), topic_msg.size());
//                 data.assign(static_cast<char*>(data_msg.data()), data_msg.size());
//             } else {
//                 // Single-frame message. Try to split by first space (common "topic payload" format).
//                 std::string combined(static_cast<char*>(topic_msg.data()), topic_msg.size());
//                 auto pos = combined.find(' ');
//                 if (pos != std::string::npos) {
//                     topic = combined.substr(0, pos);
//                     data = combined.substr(pos + 1);
//                 } else {
//                     // No separator found. Treat as payload-only (no topic).
//                     topic.clear();
//                     data = std::move(combined);
//                 }
//             }

//             // If topic appears to be non-printable (binary protobuf landed in topic frame),
//             // try to recover a textual topic embedded at the start of the bytes. If we
//             // cannot find a textual topic, move the bytes into the data payload but keep
//             // a hex-dump for debugging.
//             auto is_printable = [](const std::string &s) {
//                 if (s.empty()) return false;
//                 for (unsigned char c : s) {
//                     // allow common whitespace that may appear in topics
//                     if (c == '\n' || c == '\r' || c == '\t') return false;
//                     if (c < 0x20 || c > 0x7E) return false;
//                 }
//                 return true;
//             };

//             if (!topic.empty() && !is_printable(topic)) {
//                 // Attempt to recover a textual topic if it exists at the start of the
//                 // combined bytes (e.g., when multipart frames landed in unexpected order)
//                 std::string raw_topic_bytes(static_cast<char*>(topic_msg.data()), topic_msg.size());
//                 std::string combined = raw_topic_bytes + data;

//                 // Look for a likely ASCII topic prefix like "orderbook." at the start
//                 auto pos = combined.find("orderbook.");
//                 if (pos == 0) {
//                     // Extract until first whitespace or null byte
//                     size_t end = combined.find_first_of(" \0\n\r\t");
//                     if (end == std::string::npos) end = combined.size();
//                     topic = combined.substr(0, end);
//                     data = combined.substr(end + (end < combined.size() ? 1 : 0));
//                 } else {
//                     // No textual topic found — prepend bytes to data and clear topic.
//                     data = std::move(combined);
//                     // Keep topic empty but log a short hex dump for diagnostics
//                     std::stringstream hex_dump;
//                     for (size_t i = 0; i < std::min<size_t>(32, raw_topic_bytes.size()); ++i) {
//                         hex_dump << std::hex << std::setw(2) << std::setfill('0')
//                                  << (static_cast<int>(static_cast<unsigned char>(raw_topic_bytes[i])) & 0xff) << " ";
//                     }
//                     logError(std::string("Received non-printable topic frame, moved into data (hex dump): ") + hex_dump.str());
//                     topic.clear();
//                 }
//             }

//             handleMessage(topic, data);

//             stats_.messages_received++;
//             stats_.last_message_time = std::chrono::steady_clock::now();
            
//         } catch (const zmq::error_t& e) {
//             if (e.num() != EAGAIN && e.num() != EINTR) {
//                 logError("ZMQ error in subscriber worker: " + std::string(e.what()));
//             }
//             std::this_thread::sleep_for(std::chrono::milliseconds(10));
//         } catch (const std::exception& e) {
//             logError("Error in subscriber worker: " + std::string(e.what()));
//             std::this_thread::sleep_for(std::chrono::milliseconds(100));
//         }
//     }
    
//     logInfo("Subscriber worker thread stopped");
// }
void GenericZmqClient::subscriberWorker() {
    logInfo("Subscriber worker thread started");
    
    while (running_.load()) {
        try {
            if (!subscriber_socket_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            
            // FIXED: Simplified message receiving logic
            std::string topic;
            std::string data;
            
            // Always expect multipart messages from server (topic + data)
            zmq::message_t topic_frame;
            zmq::message_t data_frame;
            
            // Receive first frame (should be topic)
            zmq::recv_result_t topic_result;
            try {
                topic_result = subscriber_socket_->recv(topic_frame, zmq::recv_flags::none);
            } catch (const zmq::error_t& e) {
                if (e.num() == EAGAIN || e.num() == EINTR) {
                    continue; // Timeout or interrupted
                }
                logError(std::string("ZMQ error receiving topic frame: ") + e.what());
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            
            if (!topic_result.has_value()) {
                continue; // No message available
            }
            
            // Check if there's a second frame
            bool has_more = false;
            try {
                has_more = subscriber_socket_->get(zmq::sockopt::rcvmore);
            } catch (const zmq::error_t& e) {
                logError(std::string("Error checking for more frames: ") + e.what());
                continue;
            }
            
            if (has_more) {
                // CORRECT: This is a proper multipart message (topic + data)
                zmq::recv_result_t data_result;
                try {
                    data_result = subscriber_socket_->recv(data_frame, zmq::recv_flags::none);
                } catch (const zmq::error_t& e) {
                    logError(std::string("ZMQ error receiving data frame: ") + e.what());
                    continue;
                }
                
                if (!data_result.has_value()) {
                    logError("Failed to receive data frame after topic frame");
                    continue;
                }
                
                // Extract topic and data from frames
                topic.assign(static_cast<char*>(topic_frame.data()), topic_frame.size());
                data.assign(static_cast<char*>(data_frame.data()), data_frame.size());
                
                // Validate topic is printable (should be text like "orderbook.BINANCE.BTC_USDT")
                if (!isValidTopicString(topic)) {
                    logError("Received invalid topic in multipart message: " + 
                            bytesToHex(topic.substr(0, std::min<size_t>(32, topic.size()))));
                    continue;
                }
                
            } else {
                // PROBLEMATIC: Single frame message - this shouldn't happen with your server
                // But we'll handle it gracefully
                std::string combined(static_cast<char*>(topic_frame.data()), topic_frame.size());
                
                logError("Received unexpected single-frame message (size=" + 
                        std::to_string(combined.size()) + "): " + 
                        bytesToHex(combined.substr(0, std::min<size_t>(32, combined.size()))));
                
                // Try to parse as "topic data" format, but this is likely wrong for protobuf
                auto space_pos = combined.find(' ');
                if (space_pos != std::string::npos && space_pos > 0) {
                    topic = combined.substr(0, space_pos);
                    data = combined.substr(space_pos + 1);
                    
                    if (!isValidTopicString(topic)) {
                        logError("Invalid topic extracted from single frame: " + topic);
                        continue;
                    }
                } else {
                    // No space found or invalid format - skip this message
                    logError("Cannot parse single-frame message format");
                    continue;
                }
            }
            
            // Additional validation: data should not be empty for orderbook messages
            if (data.empty()) {
                logError("Received empty data for topic: " + topic);
                continue;
            }
            
            // Additional validation: for protobuf messages, data should be binary
            if (topic.find("orderbook.") == 0) {
                // Protobuf data should not be printable text
                if (isPrintableText(data)) {
                    logError("Received text data instead of protobuf for topic: " + topic + 
                            " (data preview: " + data.substr(0, std::min<size_t>(50, data.size())) + ")");
                    continue;
                }
            }
            
            // Process the message
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

// ADDED: Helper functions for validation
bool GenericZmqClient::isValidTopicString(const std::string& topic) const {
    if (topic.empty() || topic.size() > 255) {
        return false;
    }
    
    // Topic should contain only printable ASCII characters
    for (unsigned char c : topic) {
        if (c < 0x20 || c > 0x7E) {
            return false; // Non-printable character
        }
    }
    
    // Basic format check for orderbook topics
    if (topic.find("orderbook.") == 0) {
        return topic.size() > 10 && topic.find_first_of("\r\n\t") == std::string::npos;
    }
    
    return true;
}

bool GenericZmqClient::isPrintableText(const std::string& data) const {
    if (data.empty()) return false;
    
    // Check first 100 bytes - if they're all printable, it's likely text not protobuf
    size_t check_size = std::min<size_t>(100, data.size());
    for (size_t i = 0; i < check_size; ++i) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        if (c < 0x20 && c != '\t' && c != '\r' && c != '\n') {
            return false; // Found binary data
        }
        if (c > 0x7E) {
            return false; // Found non-ASCII
        }
    }
    
    return true; // Looks like printable text
}

std::string GenericZmqClient::bytesToHex(const std::string& bytes) const {
    std::stringstream hex_stream;
    hex_stream << std::hex << std::setfill('0');
    
    for (unsigned char c : bytes) {
        hex_stream << std::setw(2) << static_cast<int>(c) << " ";
    }
    
    return hex_stream.str();
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
        
        // Then try pattern matching (simple prefix matching for now)
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
        if (logging_enabled_) {
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
    
    if (logging_enabled_) {
        std::cerr << "[ERROR] ZMQ Client: " << error_msg << std::endl;
    }
}

void GenericZmqClient::logInfo(const std::string& info_msg) {
    if (logging_enabled_) {
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
