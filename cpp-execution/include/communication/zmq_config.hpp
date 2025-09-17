#pragma once

#include <string>
#include <vector>
#include <chrono>
#include "data_ingestion/orderbook/order_book_types.hpp"
#include "communication/zmq_server.hpp"
using namespace trading::data_ingestion;

namespace trading::communication {

/**
 * Configuration for ZeroMQ
 */
struct ZMQConfig {
    std::string endpoint = "tcp://*:5555";
    int linger = 1000;
    int high_water_mark = 1000;
};
struct GenericMessage {
    std::string topic;
    std::string data;
    MessagePriority priority;
    std::chrono::steady_clock::time_point timestamp;
    std::string correlation_id;
    std::unordered_map<std::string, std::string> metadata;
    
    GenericMessage(const std::string& t, const std::string& d, 
                   MessagePriority p = MessagePriority::NORMAL)
        : topic(t), data(d), priority(p), 
          timestamp(std::chrono::steady_clock::now()) {
        
        // Generate correlation ID if not provided
        if (correlation_id.empty()) {
            correlation_id = generateCorrelationId();
        }
    }
    
// private:
    static std::string generateCorrelationId();
};

/**
 * Configuration for publisher component
 */
struct ZmqServerConfig {
    // Publisher configuration
    std::string publisher_endpoint = "tcp://*:5555";
    int publisher_hwm = 10000;
    int publisher_linger = 1000;
    bool publisher_immediate = true;
    
    // Request-Reply configuration  
    std::string request_endpoint = "tcp://*:5556";
    int request_hwm = 1000;
    int request_timeout_ms = 30000;
    
    // Threading configuration
    int publisher_threads = 2;
    int request_handler_threads = 4;
    int io_threads = 2;
    
    // Queue configuration
    size_t max_queue_size = 100000;
    // max_publish_rate
    int max_publish_rate = 1000;  // messages per second
    std::chrono::milliseconds publish_interval{100};
    std::chrono::milliseconds queue_timeout{100};
    // publish_interval_ms
    // Monitoring configuration
    bool enable_monitoring = true;
    std::chrono::seconds stats_interval{60};
    
    // Reliability configuration
    bool enable_heartbeat = true;
    std::chrono::seconds heartbeat_interval{30};
    int max_reconnect_attempts = 5;
    std::chrono::milliseconds reconnect_delay{1000};
    
    // Validation
    bool validate() const {
        return !publisher_endpoint.empty() && 
               !request_endpoint.empty() &&
               publisher_hwm > 0 &&
               request_hwm > 0 &&
               publisher_threads > 0 &&
               request_handler_threads > 0 &&
               io_threads > 0 &&
               max_queue_size > 0;
    }
};

using RequestHandler = std::function<std::string(const std::string& request_data, 
                                                const std::unordered_map<std::string, std::string>& metadata)>;


/**
 * Client configuration structure
 */
struct ZmqClientConfig {
    std::string server_pub_endpoint = "tcp://localhost:5555";
    std::string server_req_endpoint = "tcp://localhost:5556";
    std::chrono::milliseconds request_timeout{30000};  // 30 seconds
    int io_threads = 1;
    bool auto_reconnect = true;
    std::chrono::milliseconds reconnect_interval{5000};  // 5 seconds
    
    bool validate() const {
        return !server_pub_endpoint.empty() && 
               !server_req_endpoint.empty() &&
               request_timeout.count() > 0 &&
               io_threads > 0;
    }
};

#define ZMQ_TOPIC_MARKET_DATA "market.data"
#define ZMQ_TOPIC_ORDERS "orders"
#define ZMQ_TOPIC_TRADES "trades"
#define ZMQ_TOPIC_POSITIONS "positions"
#define ZMQ_TOPIC_ACCOUNT "account"
#define ZMQ_TOPIC_RISK_ALERTS "risk.alerts"
#define ZMQ_TOPIC_EXECUTION_REPORTS "execution.reports"
#define ZMQ_TOPIC_TRADING_INTENTS "trading.intents" 
#define ZMQ_TOPIC_MARKET_INTELLIGENCE "MARKET_INTEL"




} // namespace trading::common