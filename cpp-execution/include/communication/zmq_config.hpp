#pragma once

#include <string>
#include <vector>
#include <chrono>
#include "data_ingestion/orderbook/order_book_types.hpp"

// Include shared types so this config header does not re-declare them.
#include "communication/zmq_types.hpp"

//define endpoint constants for easy reference
constexpr const char* INTEL_ENDPOINT = "tcp://*:5560";
constexpr const char* INTERNAL_ENDPOINT = "tcp://*:5557";
constexpr const char* REQUEST_ENDPOINT = "tcp://*:5556";
// for client
constexpr const char* SERVER_INTEL_ENDPOINT = "tcp://localhost:5560";
constexpr const char* SERVER_INTERNAL_ENDPOINT = "tcp://localhost:5557";
constexpr const char* SERVER_REQUEST_ENDPOINT = "tcp://localhost:5556";
//risk
constexpr const char* SERVER_RISK_PUBLISH_ENDPOINT = "tcp://localhost:5558";

// execution endpoints
constexpr const char* SERVER_EXECUTION_REQUEST_ENDPOINT = "tcp://localhost:5565";
constexpr const char* SERVER_EXECUTION_PUBLISH_ENDPOINT = "tcp://localhost:5566";


using namespace trading::data_ingestion;

namespace trading::communication {

/**
 * Configuration for ZeroMQ (server and client settings)
 */
/**
 * Configuration for publisher component
 */
struct ZmqServerConfig {
    // Intel Publisher configuration (for Python - low frequency, protobuf)
    std::string intel_endpoint = "tcp://*:5560";
    int intel_hwm = 2000;                              // Very low - Python consumes slow
    int intel_linger = 0;                           // Don't wait on close
    bool intel_immediate = true;
    bool intel_conflate = true;                     // Only keep latest message
    
    // Internal Publisher configuration (for orderbook - high frequency, JSON)
    std::string internal_endpoint = "tcp://*:5557";
    int internal_hwm = 10000;                       // High for fast consumers
    int internal_linger = 1000;
    bool internal_immediate = true;
    bool internal_conflate = false;                 // Keep all messages
    
    // Request-Reply configuration
    std::string request_endpoint = "tcp://*:5556";
    int request_hwm = 1000;
    int request_timeout_ms = 30000;
    int request_linger = 1000;
    
    // Threading configuration
    int io_threads = 2;
    int intel_publisher_threads = 1;                // Low for intel
    int internal_publisher_threads = 1;             // Higher for internal
    int request_handler_threads = 1;
    
    // Queue configuration
    size_t max_intel_queue_size = 200;                // Small for intel
    size_t max_internal_queue_size = 10000;         // Large for internal
    
    // Rate limiting
    int max_intel_publish_rate = 30;                 // 60 messages per minute
    int max_internal_publish_rate = 1000;           // 1000 messages per second
    std::chrono::milliseconds intel_publish_interval{300};    // 300 milliseconds
    std::chrono::milliseconds internal_publish_interval{1};     // 1ms
    std::chrono::milliseconds queue_timeout{100};
    
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
        return !intel_endpoint.empty() &&
               !internal_endpoint.empty() &&
               !request_endpoint.empty() &&
               intel_hwm > 0 &&
               internal_hwm > 0 &&
               request_hwm > 0 &&
               intel_publisher_threads > 0 &&
               internal_publisher_threads > 0 &&
               request_handler_threads > 0 &&
               io_threads > 0 &&
               max_intel_queue_size > 0 &&
               max_internal_queue_size > 0;
    }
    
    // Helper methods
    bool isIntelEndpoint(const std::string& endpoint) const {
        return endpoint == intel_endpoint;
    }
    
    bool isInternalEndpoint(const std::string& endpoint) const {
        return endpoint == internal_endpoint;
    }
};

using RequestHandler = std::function<std::string(const std::string& request_data, 
                                                const std::unordered_map<std::string, std::string>& metadata)>;


/**
 * Client configuration structure
 */
struct ZmqClientConfig {
    std::string server_pub_endpoint = SERVER_INTEL_ENDPOINT;
    std::string server_req_endpoint = SERVER_REQUEST_ENDPOINT;
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
#define ZMQ_TOPIC_ORDER_BOOK "orderbook"
#define ZMQ_TOPIC_ORDERS "orders"
#define ZMQ_TOPIC_TRADES "trades"
#define ZMQ_TOPIC_POSITIONS "positions"
#define ZMQ_TOPIC_ACCOUNT "account"
#define ZMQ_TOPIC_RISK_ALERTS "risk.alerts"
#define ZMQ_TOPIC_EXECUTION_REPORTS "execution.reports"
#define ZMQ_TOPIC_TRADING_INTENTS "trading.intents" 
#define ZMQ_TOPIC_MARKET_INTELLIGENCE "MARKET_INTEL"




} // namespace trading::communication