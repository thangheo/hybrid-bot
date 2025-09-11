#include "common/zeromq_client.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include "common/logger.hpp"
namespace trading {
namespace common {

// Helper function to get current timestamp for logging
std::string getTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
        
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// Helper to log with timestamp
// void LOG_INFO(const std::string& level, const std::string& message) {
//     std::cout << "[" << getTimestamp() << "] [" << level << "] " << message << std::endl;
// }

ZeroMQClient::ZeroMQClient(const ZeroMQConfig& config)
    : context_(1), config_(config), running_(false) {
    
    LOG_INFO("INFO", "Initializing ZeroMQ client");
}

ZeroMQClient::ZeroMQClient(const std::string& subscriberEndpoint, const std::string& publisherEndpoint)
    : context_(1), running_(false) {
    
    config_.subscriberEndpoint = subscriberEndpoint;
    config_.publisherEndpoint = publisherEndpoint;
    
    LOG_INFO("INFO", "Initializing ZeroMQ client with legacy constructor");
}

ZeroMQClient::~ZeroMQClient() {
    stop();
    
    // Ensure we clean up the sockets explicitly before the context
    subscriber_.reset();
    publisher_.reset();
    
    LOG_INFO("INFO", "ZeroMQ client destroyed");
}

bool ZeroMQClient::start() {
    if (running_) {
        LOG_INFO("WARNING", "ZeroMQ client already running");
        return true;
    }
    
    // Only initialize publisher socket if endpoint is provided
    if (!config_.publisherEndpoint.empty()) {
        try {
            publisher_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PUB);
            publisher_->set(zmq::sockopt::sndhwm, static_cast<int>(config_.sendHighWaterMark));
            publisher_->set(zmq::sockopt::linger, static_cast<int>(config_.linger ? config_.lingerTimeoutMs : 0));
            publisher_->connect(config_.publisherEndpoint);
            publisherStatus_ = ConnectionStatus::CONNECTING;
            LOG_INFO("INFO", "Publisher connecting to " + config_.publisherEndpoint);
        } catch (const zmq::error_t& e) {
            logError("Publisher initialization", e.what());
            publisherStatus_ = ConnectionStatus::FAILED;
            return false;
        }
    } else {
        LOG_INFO("INFO", "No publisher endpoint provided - subscriber only mode");
        publisherStatus_ = ConnectionStatus::DISCONNECTED; // Not an error, just not used
    }
    
    // Only initialize subscriber socket if endpoint is provided
    if (!config_.subscriberEndpoint.empty()) {
        try {
            subscriber_ = std::make_unique<zmq::socket_t>(context_, ZMQ_SUB);
            subscriber_->set(zmq::sockopt::rcvhwm, static_cast<int>(config_.receiveHighWaterMark));
            subscriber_->set(zmq::sockopt::linger, static_cast<int>(config_.linger ? config_.lingerTimeoutMs : 0));
            subscriber_->connect(config_.subscriberEndpoint);
            subscriberStatus_ = ConnectionStatus::CONNECTING;
            
            // Resubscribe to all topics
            {
                std::lock_guard<std::mutex> lock(topicsMutex_);
                for (const auto& topic : topics_) {
                    try {
                        subscriber_->set(zmq::sockopt::subscribe, topic);
                    } catch (const zmq::error_t& e) {
                        logError("Topic resubscription", "Failed to resubscribe to " + topic + ": " + e.what());
                    }
                }
            }
            
            LOG_INFO("INFO", "Subscriber connecting to " + config_.subscriberEndpoint);
        } catch (const zmq::error_t& e) {
            logError("Subscriber initialization", e.what());
            subscriberStatus_ = ConnectionStatus::FAILED;
            return false;
        }
    } else {
        LOG_INFO("INFO", "No subscriber endpoint provided - publisher only mode");
        subscriberStatus_ = ConnectionStatus::DISCONNECTED; // Not an error, just not used
    }
    
    // Only start receive thread if we have a subscriber
    if (subscriber_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        running_ = true;
        receiveThread_ = std::thread(&ZeroMQClient::receiveWorker, this);
    }
    
    LOG_INFO("INFO", "ZeroMQ client started");
    return true;
}
bool ZeroMQClient::isConnected(){
    return (running_);
}

void ZeroMQClient::stop() {
    if (!running_) return;
    
    // Signal worker thread to stop
    running_ = false;
    
    // Wait for thread to finish
    if (receiveThread_.joinable()) {
        receiveThread_.join();
    }
    
    // Close sockets
    try {
        if (subscriber_) {
            subscriber_.reset();
        }
        
        if (publisher_) {
            publisher_.reset();
        }
    } catch (const zmq::error_t& e) {
        logError("Socket shutdown", e.what());
    }
    
    subscriberStatus_ = ConnectionStatus::DISCONNECTED;
    publisherStatus_ = ConnectionStatus::DISCONNECTED;
    
    LOG_INFO("INFO", "ZeroMQ client stopped");
}

bool ZeroMQClient::subscribe(const std::string& topic) {
    if (!subscriber_) {
        logError("Subscribe", "Subscriber socket not initialized");
        return false;
    }
    
    try {
        subscriber_->set(zmq::sockopt::subscribe, topic);
        
        {
            std::lock_guard<std::mutex> lock(topicsMutex_);
            topics_.insert(topic);
        }
        
        LOG_INFO("INFO", "Subscribed to topic: " + topic);
        return true;
    } catch (const zmq::error_t& e) {
        logError("Subscribe", "Failed to subscribe to topic " + topic + ": " + e.what());
        return false;
    }
}

bool ZeroMQClient::unsubscribe(const std::string& topic) {
    if (!subscriber_) {
        logError("Unsubscribe", "Subscriber socket not initialized");
        return false;
    }
    
    try {
        subscriber_->set(zmq::sockopt::unsubscribe, topic);
        
        {
            std::lock_guard<std::mutex> lock(topicsMutex_);
            topics_.erase(topic);
        }
        
        LOG_INFO("INFO", "Unsubscribed from topic: " + topic);
        return true;
    } catch (const zmq::error_t& e) {
        logError("Unsubscribe", "Failed to unsubscribe from topic " + topic + ": " + e.what());
        return false;
    }
}

bool ZeroMQClient::publish(const std::string& topic, const std::string& message) {
    return publishBinary(topic, message.data(), message.size());
}

bool ZeroMQClient::publishBinary(const std::string& topic, const void* data, size_t size) {
    if (!publisher_) {
        logError("Publish", "Publisher socket not initialized");
        return false;
    }
    
    // Check if we're still in CONNECTING state and convert to CONNECTED after first publish
    if (publisherStatus_ == ConnectionStatus::CONNECTING) {
        publisherStatus_ = ConnectionStatus::CONNECTED;
    }
    
    try {
        // Send topic
        zmq::message_t topicMsg(topic.data(), topic.size());
        auto topicSent = publisher_->send(topicMsg, zmq::send_flags::sndmore);
        
        if (!topicSent) {
            logError("Publish", "Failed to send topic");
            return false;
        }
        
        // Send message
        zmq::message_t dataMsg(data, size);
        auto dataSent = publisher_->send(dataMsg, zmq::send_flags::none);
        
        if (!dataSent) {
            logError("Publish", "Failed to send message data");
            return false;
        }
        
        // Update statistics
        updateSendStats(topic.size() + size);
        
        return true;
    } catch (const zmq::error_t& e) {
        logError("Publish", "Failed to publish message: " + std::string(e.what()));
        
        // Mark for reconnection on next attempt
        if (e.num() == ECONNREFUSED || e.num() == EAGAIN) {
            publisherStatus_ = ConnectionStatus::RECONNECTING;
        }
        
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.publishErrors++;
        
        return false;
    }
}

void ZeroMQClient::setMessageHandler(MessageHandler handler) {
    messageHandler_ = handler;
}

void ZeroMQClient::setErrorHandler(ErrorHandler handler) {
    errorHandler_ = handler;
}

ZeroMQStats ZeroMQClient::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

void ZeroMQClient::resetStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = ZeroMQStats{};
}

bool ZeroMQClient::reconnectSubscriber() {
    if (!subscriber_) return false;
    
    unsigned int attempts = 0;
    subscriberStatus_ = ConnectionStatus::RECONNECTING;
    
    while (running_ && attempts < config_.maxReconnectAttempts) {
        try {
            // Create a new socket
            subscriber_.reset(new zmq::socket_t(context_, ZMQ_SUB));
            
            // Configure socket options
            subscriber_->set(zmq::sockopt::rcvhwm, static_cast<int>(config_.receiveHighWaterMark));
            subscriber_->set(zmq::sockopt::linger, static_cast<int>(config_.linger ? config_.lingerTimeoutMs : 0));
            
            // Connect the socket
            subscriber_->connect(config_.subscriberEndpoint);
            
            // Resubscribe to all topics
            {
                std::lock_guard<std::mutex> lock(topicsMutex_);
                for (const auto& topic : topics_) {
                    subscriber_->set(zmq::sockopt::subscribe, topic);
                }
            }
            
            subscriberStatus_ = ConnectionStatus::CONNECTED;
            
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.reconnectAttempts++;
            }
            
            LOG_INFO("INFO", "Subscriber reconnected after " + 
                             std::to_string(attempts + 1) + " attempts");
            return true;
        } catch (const zmq::error_t& e) {
            attempts++;
            
            LOG_INFO("WARNING", "Reconnection attempt " + 
                            std::to_string(attempts) + "/" + 
                            std::to_string(config_.maxReconnectAttempts) + 
                            " failed: " + e.what());
                            
            // Wait before retrying
            std::this_thread::sleep_for(config_.reconnectInterval);
        }
    }
    
    subscriberStatus_ = ConnectionStatus::FAILED;
    LOG_INFO("ERROR", "Failed to reconnect subscriber after " + 
                     std::to_string(attempts) + " attempts");
    return false;
}

bool ZeroMQClient::reconnectPublisher() {
    if (!publisher_) return false;
    
    unsigned int attempts = 0;
    publisherStatus_ = ConnectionStatus::RECONNECTING;
    
    while (running_ && attempts < config_.maxReconnectAttempts) {
        try {
            // Create a new socket
            publisher_.reset(new zmq::socket_t(context_, ZMQ_PUB));
            
            // Configure socket options
            publisher_->set(zmq::sockopt::sndhwm, static_cast<int>(config_.sendHighWaterMark));
            publisher_->set(zmq::sockopt::linger, static_cast<int>(config_.linger ? config_.lingerTimeoutMs : 0));
            
            // Connect the socket
            publisher_->connect(config_.publisherEndpoint);
            
            publisherStatus_ = ConnectionStatus::CONNECTED;
            
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.reconnectAttempts++;
            }
            
            LOG_INFO("INFO", "Publisher reconnected after " + 
                             std::to_string(attempts + 1) + " attempts");
            return true;
        } catch (const zmq::error_t& e) {
            attempts++;
            
            LOG_INFO("WARNING", "Reconnection attempt " + 
                            std::to_string(attempts) + "/" + 
                            std::to_string(config_.maxReconnectAttempts) + 
                            " failed: " + e.what());
                            
            // Wait before retrying
            std::this_thread::sleep_for(config_.reconnectInterval);
        }
    }
    
    publisherStatus_ = ConnectionStatus::FAILED;
    LOG_INFO("ERROR", "Failed to reconnect publisher after " + 
                     std::to_string(attempts) + " attempts");
    return false;
}

void ZeroMQClient::receiveWorker() {
    LOG_INFO("INFO", "Receive worker thread started");
    
    while (running_) {
        try {
            // Check if we need to reconnect
            if (subscriberStatus_ == ConnectionStatus::RECONNECTING) {
                if (!reconnectSubscriber()) {
                    // Failed to reconnect, wait before trying again
                    std::this_thread::sleep_for(config_.reconnectInterval);
                    continue;
                }
            }
            
            if (publisherStatus_ == ConnectionStatus::RECONNECTING) {
                if (!reconnectPublisher()) {
                    // Failed to reconnect, wait before trying again
                    std::this_thread::sleep_for(config_.reconnectInterval);
                    continue;
                }
            }
            
            // Setup polling
            zmq::pollitem_t items[] = {
                { static_cast<void*>(*subscriber_), 0, ZMQ_POLLIN, 0 }
            };
            
            zmq::poll(items, 1, config_.pollTimeout);
            
            if (items[0].revents & ZMQ_POLLIN) {
                // Receive topic
                zmq::message_t topicMsg;
                auto topicRecv = subscriber_->recv(topicMsg, zmq::recv_flags::none);
                
                if (!topicRecv) continue;
                
                // Check if there's more (the message)
                if (!subscriber_->get(zmq::sockopt::rcvmore)) {
                    LOG_INFO("WARNING", "Received topic without message");
                    continue;
                }
                
                // Receive message
                zmq::message_t dataMsg;
                auto dataRecv = subscriber_->recv(dataMsg, zmq::recv_flags::none);
                
                if (!dataRecv) continue;
                
                std::string topic(static_cast<char*>(topicMsg.data()), topicMsg.size());
                std::string message(static_cast<char*>(dataMsg.data()), dataMsg.size());
                
                // Update statistics
                updateReceiveStats(topicMsg.size() + dataMsg.size());
                
                // Update connection status if needed
                if (subscriberStatus_ == ConnectionStatus::CONNECTING) {
                    subscriberStatus_ = ConnectionStatus::CONNECTED;
                    LOG_INFO("INFO", "Subscriber connected successfully");
                }
                
                // Invoke handler if set
                if (messageHandler_) {
                    try {
                        messageHandler_(topic, message);
                    } catch (const std::exception& e) {
                        logError("Message handler", "Exception in message handler: " + std::string(e.what()));
                    } catch (...) {
                        logError("Message handler", "Unknown exception in message handler");
                    }
                }
            }
        } catch (const zmq::error_t& e) {
            logError("Receive worker", "ZeroMQ error: " + std::string(e.what()));
            
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                stats_.receiveErrors++;
            }
            
            // Detect connection failures and mark for reconnection
            if (e.num() == ECONNREFUSED || e.num() == EAGAIN || e.num() == ETERM) {
                subscriberStatus_ = ConnectionStatus::RECONNECTING;
            }
            
            // Small sleep to prevent CPU spinning on errors
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (const std::exception& e) {
            logError("Receive worker", "STL exception: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (...) {
            logError("Receive worker", "Unknown exception");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    LOG_INFO("INFO", "Receive worker thread stopped");
}

void ZeroMQClient::logError(const std::string& context, const std::string& message) {
    std::string fullMessage = context + ": " + message;
    LOG_INFO("ERROR", fullMessage);
    
    if (errorHandler_) {
        try {
            errorHandler_(context, message);
        } catch (...) {
            // Prevent errors in error handler from crashing
            LOG_INFO("ERROR", "Exception in error handler");
        }
    }
}
//checkSocketConnection
bool ZeroMQClient::checkSocketConnection(zmq::socket_t& socket) {
    try {
        zmq::message_t msg;
        socket.recv(msg, zmq::recv_flags::dontwait);
        return true;
    } catch (const zmq::error_t& e) {
        if (e.num() == EAGAIN) {
            return false; // No message available
        }
        return false; // Other error
    }
}
void ZeroMQClient::updateSendStats(size_t bytesSent) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.messagesSent++;
    stats_.bytesSent += bytesSent;
}

void ZeroMQClient::updateReceiveStats(size_t bytesReceived) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.messagesReceived++;
    stats_.bytesReceived += bytesReceived;
}

} // namespace common
} // namespace trading