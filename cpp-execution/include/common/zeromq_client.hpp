#pragma once

#include <zmq.hpp>
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <chrono>
#include <queue>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace trading {
namespace common {

// Forward declaration
class Logger;

/**
 * Configuration for ZeroMQClient
 */
struct ZeroMQConfig {
    std::string subscriberEndpoint;       // Endpoint for subscriber socket
    std::string publisherEndpoint;        // Endpoint for publisher socket
    size_t receiveHighWaterMark = 1000;   // High water mark for receive queue
    size_t sendHighWaterMark = 1000;      // High water mark for send queue
    std::chrono::milliseconds pollTimeout{100};  // Timeout for polling
    std::chrono::milliseconds reconnectInterval{1000};  // Time between reconnection attempts
    unsigned int maxReconnectAttempts = 10;  // Maximum number of reconnection attempts
    bool linger = false;                  // Whether to linger on socket close
    int lingerTimeoutMs = 0;              // Linger timeout in milliseconds
};

/**
 * Status of ZeroMQClient
 */
enum class ConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    RECONNECTING,
    FAILED
};

/**
 * Statistics for ZeroMQClient
 */
struct ZeroMQStats {
    size_t messagesSent = 0;
    size_t messagesReceived = 0;
    size_t bytesReceived = 0;
    size_t bytesSent = 0;
    size_t publishErrors = 0;
    size_t receiveErrors = 0;
    size_t reconnectAttempts = 0;
};

/**
 * Enhanced ZeroMQClient for reliable messaging
 */
class ZeroMQClient {
public:
    using MessageHandler = std::function<void(const std::string&, const std::string&)>;
    using ErrorHandler = std::function<void(const std::string&, const std::string&)>;
    
    /**
     * Constructor with configuration
     */
    explicit ZeroMQClient(const ZeroMQConfig& config);
    
    /**
     * Constructor with subscriber and publisher endpoints
     * @deprecated Use the config-based constructor instead
     */
    ZeroMQClient(const std::string& subscriberEndpoint, const std::string& publisherEndpoint);
    
    /**
     * Destructor ensures clean shutdown
     */
    ~ZeroMQClient();
    
    // Disable copying
    ZeroMQClient(const ZeroMQClient&) = delete;
    ZeroMQClient& operator=(const ZeroMQClient&) = delete;
    
    /**
     * Subscribe to a topic
     */
    bool subscribe(const std::string& topic);
    
    /**
     * Unsubscribe from a topic
     */
    bool unsubscribe(const std::string& topic);
    
    /**
     * Publish a message to a topic
     */
    bool publish(const std::string& topic, const std::string& message);
    
    /**
     * Publish a message with binary data
     */
    bool publishBinary(const std::string& topic, const void* data, size_t size);
    
    /**
     * Set handler for received messages
     */
    void setMessageHandler(MessageHandler handler);
    
    /**
     * Set handler for errors
     */
    void setErrorHandler(ErrorHandler handler);
    
    /**
     * Start the client
     */
    bool start();
    
    /**
     * Stop the client
     */
    void stop();
    
    /**
     * Check if client is running
     */
    bool isRunning() const { return running_; }
    
    /**
     * Get connection status
     */
    ConnectionStatus getSubscriberStatus() const { return subscriberStatus_; }
    ConnectionStatus getPublisherStatus() const { return publisherStatus_; }
    
    /**
     * Get statistics
     */
    ZeroMQStats getStats() const;
    
    /**
     * Reset statistics
     */
    void resetStats();

private:
    // Worker for receiving messages
    void receiveWorker();
    
    // Attempt reconnection
    bool reconnectSubscriber();
    bool reconnectPublisher();
    
    // Log error and notify error handler
    void logError(const std::string& context, const std::string& message);
    
    // Check socket connection
    bool checkSocketConnection(zmq::socket_t& socket);
    
    // Update statistics
    void updateSendStats(size_t bytesSent);
    void updateReceiveStats(size_t bytesReceived);
    
    // ZMQ context and sockets
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> subscriber_;
    std::unique_ptr<zmq::socket_t> publisher_;
    
    // Configuration
    ZeroMQConfig config_;
    
    // Connection status
    std::atomic<ConnectionStatus> subscriberStatus_{ConnectionStatus::DISCONNECTED};
    std::atomic<ConnectionStatus> publisherStatus_{ConnectionStatus::DISCONNECTED};
    
    // Thread management
    std::atomic<bool> running_{false};
    std::thread receiveThread_;
    
    // Handlers
    MessageHandler messageHandler_;
    ErrorHandler errorHandler_;
    
    // List of subscribed topics
    std::mutex topicsMutex_;
    std::unordered_set<std::string> topics_;
    
    // Statistics
    mutable std::mutex statsMutex_;
    ZeroMQStats stats_;
};

} // namespace common
} // namespace trading