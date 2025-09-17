#pragma once

#include <string>
#include <vector>
#include <chrono>
#include "data_ingestion/orderbook/order_book_types.hpp"
#include "communication/zmq_server.hpp"
using namespace trading::data_ingestion;

namespace trading::common {

/**
 * Configuration for ZeroMQ
 */
struct ZMQConfig {
    std::string endpoint = "tcp://*:5555";
    int linger = 1000;
    int high_water_mark = 1000;
};

/**
 * Configuration for publisher component
 */
struct PublisherConfig {
    // ZeroMQ configuration
    std::string market_intel_endpoint = "tcp://*:5555";
    std::string trading_intent_endpoint = "tcp://*:5556";  // Alias for compatibility
    std::string orderbook_endpoint = "tcp://*:5557";
    std::string publisherType = "zeromq";
    
    // Buffer and rate limiting
    int bufferSize = 1000;
    int max_queue_size = 1000;
    int max_publish_rate = 1000;  // messages per second
    
    // Timing configuration
    std::chrono::milliseconds publishInterval{100};
    int publish_interval_ms = 100;
    int heartbeat_interval_ms = 30000;
    
    // ZeroMQ specific settings
    int zmq_linger_ms = 1000;
    int zmq_hwm = 1000;  // High Water Mark
    
    // Data publishing options
    bool enableCompression = false;
    bool publish_raw_data = true;
    bool publish_normalized_data = true;
    
    // Topics and compression
    std::vector<std::string> topics;
    CompressionAlgorithm compression = CompressionAlgorithm::NONE;
};
// struct SubscriberConfig {
//     std::string trading_intent_endpoint = "tcp://*:5558";
//     int zmq_linger_ms = 1000;
//     int zmq_hwm = 1000;  // High Water Mark
// };


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