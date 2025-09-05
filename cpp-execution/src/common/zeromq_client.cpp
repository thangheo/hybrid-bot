#include "common/zeromq_client.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>

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
void logWithTimestamp(const std::string& level, const std::string& message) {
    std::cout << "[" << getTimestamp() << "] [" << level << "] " << message << std::endl;
}

ZeroMQClient::ZeroMQClient(const ZeroMQConfig& config)
    : context_(1), config_(config), running_(false) {
    
    logWithTimestamp("INFO", "Initializing ZeroMQ client");
}

ZeroMQClient::ZeroMQClient(const std::string& subscriberEndpoint, const std::string& publisherEndpoint)
    : context_(1), running_(false) {
    
    config_.subscriberEndpoint = subscriberEndpoint;
    config_.publisherEndpoint = publisherEndpoint;
    
    logWithTimestamp("INFO", "Initializing ZeroMQ client with legacy constructor");
}

ZeroMQClient::~ZeroMQClient() {
    stop();
    
    // Ensure we clean up the sockets explicitly before the context
    subscriber_.reset();
    publisher_.reset();
    
    logWithTimestamp("INFO", "ZeroMQ client destroyed");
}

bool ZeroMQClient::start() {
    if (running_) {
        logWithTimestamp("WARNING", "ZeroMQ client already running");
        return true;
    }
    
    // Initialize publisher socket
    try {
        publisher_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PUB);
        
        // Configure socket options
        publisher_->set(zmq::sockopt::sndhwm, static_cast<int>(config_.sendHighWaterMark));
        publisher_->set(zmq::sockopt::linger, static_cast<int>(config_.linger ? config_.lingerTimeoutMs : 0));
        
        // Connect the socket
        publisher_->connect(config_.publisherEndpoint);
        publisherStatus_ = ConnectionStatus::CONNECTING;
        
        logWithTimestamp("INFO", "Publisher connecting to " + config_.publisherEndpoint);
    } catch (const zmq::error_t& e) {
        logError("Publisher initialization", e.what());
        publisherStatus_ = ConnectionStatus::FAILED;
        return false;
    }
    
    // Initialize subscriber socket
    try {
        subscriber_ = std::make_unique<zmq::socket_t>(context_, ZMQ_SUB);
        
        // Configure socket options
        subscriber_->set(zmq::sockopt::rcvhwm, static_cast<int>(config_.receiveHighWaterMark));
        subscriber_->set(zmq::sockopt::linger, static_cast<int>(config_.linger ? config_.lingerTimeoutMs : 0));
        
        // Connect the socket
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
        
        logWithTimestamp("INFO", "Subscriber connecting to " + config_.subscriberEndpoint);
    } catch (const zmq::error_t& e) {
        logError("Subscriber initialization", e.what());
        subscriberStatus_ = ConnectionStatus::FAILED;
        return false;
    }
    
    // Allow time for connections to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Start the receive thread
    running_ = true;
    receiveThread_ = std::thread(&ZeroMQClient::receiveWorker, this);
    
    logWithTimestamp("INFO", "ZeroMQ client started");
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
    
    logWithTimestamp("INFO", "ZeroMQ client stopped");
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
        
        logWithTimestamp("INFO", "Subscribed to topic: " + topic);
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
        
        logWithTimestamp("INFO", "Unsubscribed from topic: " + topic);
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
            
            logWithTimestamp("INFO", "Subscriber reconnected after " + 
                             std::to_string(attempts + 1) + " attempts");
            return true;
        } catch (const zmq::error_t& e) {
            attempts++;
            
            logWithTimestamp("WARNING", "Reconnection attempt " + 
                            std::to_string(attempts) + "/" + 
                            std::to_string(config_.maxReconnectAttempts) + 
                            " failed: " + e.what());
                            
            // Wait before retrying
            std::this_thread::sleep_for(config_.reconnectInterval);
        }
    }
    
    subscriberStatus_ = ConnectionStatus::FAILED;
    logWithTimestamp("ERROR", "Failed to reconnect subscriber after " + 
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
            
            logWithTimestamp("INFO", "Publisher reconnected after " + 
                             std::to_string(attempts + 1) + " attempts");
            return true;
        } catch (const zmq::error_t& e) {
            attempts++;
            
            logWithTimestamp("WARNING", "Reconnection attempt " + 
                            std::to_string(attempts) + "/" + 
                            std::to_string(config_.maxReconnectAttempts) + 
                            " failed: " + e.what());
                            
            // Wait before retrying
            std::this_thread::sleep_for(config_.reconnectInterval);
        }
    }
    
    publisherStatus_ = ConnectionStatus::FAILED;
    logWithTimestamp("ERROR", "Failed to reconnect publisher after " + 
                     std::to_string(attempts) + " attempts");
    return false;
}

void ZeroMQClient::receiveWorker() {
    logWithTimestamp("INFO", "Receive worker thread started");
    
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
                    logWithTimestamp("WARNING", "Received topic without message");
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
                    logWithTimestamp("INFO", "Subscriber connected successfully");
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
    
    logWithTimestamp("INFO", "Receive worker thread stopped");
}

void ZeroMQClient::logError(const std::string& context, const std::string& message) {
    std::string fullMessage = context + ": " + message;
    logWithTimestamp("ERROR", fullMessage);
    
    if (errorHandler_) {
        try {
            errorHandler_(context, message);
        } catch (...) {
            // Prevent errors in error handler from crashing
            logWithTimestamp("ERROR", "Exception in error handler");
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