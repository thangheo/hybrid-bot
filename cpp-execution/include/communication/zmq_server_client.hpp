#pragma once

#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <memory>
#include <future>
#include <chrono>
#include <random>
//publiser
namespace trading::communication {
// SERVER_REQUEST_ENDPOINT
// Defaults use loopback addresses so clients can connect using the same concrete
// endpoint. Change these values in configuration if you want binding on all
// interfaces (e.g. tcp://*:5555).
//now we use inproc all 
const std::string SERVER_REQUEST_ENDPOINT = "inproc://server_req";
const std::string SERVER_PUBLISHER_ENDPOINT = "inproc://server_pub";
const std::string SERVER_EXECUTION_PUBLISH_ENDPOINT = "inproc://server_exec_pub";
const std::string SERVER_EXECUTION_REQUEST_ENDPOINT = "inproc://server_exec_req";
// const std::string SERVER_EXECUTION_PUBLISH_ENDPOINT = "inproc://server_exec_pub";
const std::string SERVER_RISK_PUBLISH_ENDPOINT = "inproc://risk_pub";
const std::string SERVER_RISK_REQUEST_ENDPOINT = "inproc://risk_req";
// SERVER_STRATEGY_EVENTS_PUBLISH_ENDPOINT
const std::string SERVER_STRATEGY_EVENTS_PUBLISH_ENDPOINT = "inproc://strategy_events_pub";
const std::string SERVER_STRATEGY_EVENTS_REQUEST_ENDPOINT = "inproc://strategy_events_req";
// MARKET_DATA_TOPIC
const std::string MARKET_DATA_TOPIC = "market.*";
// ORDERBOOK_UPDATES_TOPIC
const std::string ORDERBOOK_UPDATES_TOPIC = "orderbook.*";
// TRADES_TOPIC
const std::string TRADES_TOPIC = "trade.*";
// EXECUTION_REPORTS_TOPIC
const std::string EXECUTION_REPORTS_TOPIC = "execution_reports";
//strategy events topic
const std::string STRATEGY_EVENTS_TOPIC = "strategy_events";
// Message priority enumeration
enum class MessagePriority {
    LOW = 0,
    NORMAL = 1,
    HIGH = 2,
    CRITICAL = 3
};

// Forward declarations
struct ZmqServerConfig;
struct ZmqClientConfig;
class GenericZmqServer;
class GenericZmqClient;

// Type aliases for handlers
using MessageHandler = std::function<void(const std::string& topic, const std::string& data, const std::unordered_map<std::string, std::string>& metadata)>;
using RequestHandler = std::function<std::string(const std::string& data, const std::unordered_map<std::string, std::string>& metadata)>;

// Configuration structures
struct ZmqServerConfig {
    std::string pub_endpoint = SERVER_PUBLISHER_ENDPOINT;
    std::string rep_endpoint = SERVER_REQUEST_ENDPOINT;
    int sndhwm = 10000;
    int rcvhwm = 10000;
    int sndtimeo = 1000;  // milliseconds
    int rcvtimeo = 1000;  // milliseconds
    int linger = 1000;    // milliseconds
    bool enable_monitoring = false;
    int io_threads = 1;
    int max_retries = 3;
    int retry_delay_ms = 100;
    
    // Reliability settings
    bool immediate = true;  // Don't queue messages to disconnected peers
    int heartbeat_ivl = 30000;  // 30 seconds
    int heartbeat_timeout = 90000;  // 90 seconds
    int heartbeat_ttl = 30000;  // 30 seconds
};

struct ZmqClientConfig {
    // Clients should connect to a concrete address (host:port). The server may bind to
    // a wildcard address like "tcp://*:5555" but clients must use a reachable host
    // (for local testing use loopback 127.0.0.1).
    std::vector<std::string> pub_endpoints = {"tcp://127.0.0.1:5555"};
    std::string req_endpoint = "tcp://127.0.0.1:5556";
    int rcvhwm = 10000;
    int sndhwm = 10000;
    int rcvtimeo = 5000;  // milliseconds
    int sndtimeo = 5000;  // milliseconds
    int linger = 1000;    // milliseconds
    int req_timeout = 10000;  // milliseconds
    int max_retries = 3;
    int retry_delay_ms = 100;
    bool auto_reconnect = true;
    int reconnect_delay_ms = 1000;
    
    // Reliability settings
    // Number of threads used to execute subscription handlers. Set to 0 to
    // execute handlers inline in the subscribe worker thread.
    int handler_thread_count = 4;
    
    int heartbeat_timeout = 90000;
    int heartbeat_ttl = 30000;
};

// Message structure for internal queuing
struct QueuedMessage {
    std::string topic;
    std::string data;
    std::unordered_map<std::string, std::string> metadata;
    MessagePriority priority;
    std::chrono::steady_clock::time_point timestamp;
    int retry_count = 0;
    
    QueuedMessage(const std::string& t, const std::string& d, 
                  const std::unordered_map<std::string, std::string>& m,
                  MessagePriority p)
        : topic(t), data(d), metadata(m), priority(p)
        , timestamp(std::chrono::steady_clock::now()) {}
};

// Priority queue comparator
struct MessagePriorityCompare {
    bool operator()(const std::shared_ptr<QueuedMessage>& a, 
                   const std::shared_ptr<QueuedMessage>& b) const {
        if (a->priority != b->priority) {
            return static_cast<int>(a->priority) < static_cast<int>(b->priority);
        }
        return a->timestamp > b->timestamp;  // Earlier timestamp has higher priority
    }
};

class GenericZmqServer {
public:
    explicit GenericZmqServer(const ZmqServerConfig& config, std::shared_ptr<zmq::context_t> shared_context = nullptr);
    ~GenericZmqServer();

    // Core lifecycle methods
    bool start();
    void stop();

    // Publishing methods
    bool publishAsync(const std::string& topic, std::string data, 
                     MessagePriority priority = MessagePriority::NORMAL, 
                     const std::string& correlation_id = "");
    
    bool publishSync(const std::string& topic, const std::string& data, 
                    MessagePriority priority = MessagePriority::NORMAL, 
                    const std::string& correlation_id = "");
    
    bool publishWithMetadata(const std::string& topic, const std::string& data, 
                           const std::unordered_map<std::string, std::string>& metadata, 
                           MessagePriority priority = MessagePriority::NORMAL);

    // Request handling
    void registerRequestHandler(const std::string& request_type, RequestHandler handler);
    void unregisterRequestHandler(const std::string& request_type);
    std::shared_ptr<zmq::context_t> getContext();
    // Topic filtering
    void addTopicFilter(const std::string& topic_pattern);
    void removeTopicFilter(const std::string& topic_pattern);

    // Status methods
    bool isRunning() const { return running_.load(); }
    std::string getLastError() const;
    size_t getQueueSize() const;
    uint64_t getMessagesSent() const { return messages_sent_.load(); }
    uint64_t getMessagesDropped() const { return messages_dropped_.load(); }
    //backward compatibility
    void publish(const std::string& topic, const std::string& message);
private:
    ZmqServerConfig config_;
    std::shared_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> pub_socket_;
    std::unique_ptr<zmq::socket_t> rep_socket_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> should_stop_{false};
    
    // Threading
    std::thread pub_thread_;
    std::thread rep_thread_;
    std::thread monitor_thread_;
    
    // Message queue and synchronization
    std::priority_queue<std::shared_ptr<QueuedMessage>, 
                       std::vector<std::shared_ptr<QueuedMessage>>,
                       MessagePriorityCompare> message_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Request handlers
    std::unordered_map<std::string, RequestHandler> request_handlers_;
    mutable std::mutex handlers_mutex_;
    
    // Topic filters
    std::vector<std::string> topic_filters_;
    mutable std::mutex filters_mutex_;
    
    // Statistics
    std::atomic<uint64_t> messages_sent_{0};
    std::atomic<uint64_t> messages_dropped_{0};
    
    // Error handling
    mutable std::mutex error_mutex_;
    std::string last_error_;
    
    // Internal methods
    void publishWorker();
    void requestWorker();
    void monitorWorker();
    bool initializeSockets();
    void cleanupSockets();
    bool sendMessage(const std::string& topic, const std::string& data, 
                    const std::unordered_map<std::string, std::string>& metadata);
    bool matchesTopicFilter(const std::string& topic) const;
    void setLastError(const std::string& error);
    std::string createMetadataFrame(const std::unordered_map<std::string, std::string>& metadata) const;
};

class GenericZmqClient {
public:
    explicit GenericZmqClient(const ZmqClientConfig& config, std::shared_ptr<zmq::context_t> shared_context = nullptr);
    ~GenericZmqClient();

    // Core lifecycle methods
    bool connect();
    void disconnect();

    // Subscription methods
    bool subscribe(const std::string& topic, MessageHandler handler);
    bool unsubscribe(const std::string& topic);
    bool subscribeMultiple(const std::vector<std::string>& topics, MessageHandler handler);
    void unsubscribeAll();
    void stop();
    bool start();
    // Request methods
    std::string sendRequest(const std::string& request_type, const std::string& data, 
                          const std::unordered_map<std::string, std::string>& metadata = {});
    
    std::future<std::string> sendRequestAsync(const std::string& request_type, 
                                             const std::string& data,
                                             const std::unordered_map<std::string, std::string>& metadata = {});

    // Status methods
    std::string getLastError() const;
    bool isConnected() const { return connected_.load(); }
    uint64_t getMessagesReceived() const { return messages_received_.load(); }
private:
    ZmqClientConfig config_;
    std::shared_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> sub_socket_;
    std::unique_ptr<zmq::socket_t> req_socket_;
    
    std::atomic<bool> connected_{false};
    std::atomic<bool> should_stop_{false};
    
    // Threading
    std::thread sub_thread_;
    std::thread monitor_thread_;
    // Handler pool for executing subscription handlers
    std::vector<std::thread> handler_threads_;
    std::queue<std::function<void()>> handler_queue_;
    mutable std::mutex handler_mutex_;
    std::condition_variable handler_cv_;
    // Condition variable to allow interruptible sleeps and fast shutdown
    mutable std::mutex worker_mutex_;
    mutable std::condition_variable worker_cv_;
    
    // Subscription management
    std::unordered_map<std::string, MessageHandler> subscriptions_;
    mutable std::mutex subscriptions_mutex_;
    
    // Statistics
    std::atomic<uint64_t> messages_received_{0};
    
    // Error handling
    mutable std::mutex error_mutex_;
    std::string last_error_;
    
    // Internal methods
    void subscribeWorker();
    void monitorWorker();
    void startHandlerPool();
    void stopHandlerPool();
    bool initializeSockets();
    void cleanupSockets();
    bool reconnect();
    void setLastError(const std::string& error);
    std::unordered_map<std::string, std::string> parseMetadataFrame(const std::string& metadata_json) const;
    std::string createRequestFrame(const std::string& request_type, const std::string& data,
                                 const std::unordered_map<std::string, std::string>& metadata) const;
};

// Utility functions
std::string generateCorrelationId();
bool isValidTopic(const std::string& topic);
std::vector<std::string> splitTopic(const std::string& topic);
bool matchesPattern(const std::string& topic, const std::string& pattern);

} // namespace trading::communication