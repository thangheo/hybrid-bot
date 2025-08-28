/**
 * @file example_data_source_usage.cpp
 * @brief Example showing how to use DataSourceManager
 * 
 * This example demonstrates:
 * 1. Setting up the DataSourceManager
 * 2. Subscribing to market data
 * 3. Monitoring the data flow
 */

#include "data_ingestion/data_source_manager.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace trading::data_ingestion;
using namespace trading::core;

int main() {
    LOG_INFO("Starting market data collection example");
    
    // 1. Configure ZeroMQ publisher
    PublisherConfig publisherConfig;
    publisherConfig.zmq_endpoint = "tcp://*:5555";
    publisherConfig.max_queue_size = 10000;
    publisherConfig.heartbeat_interval_ms = 5000;
    
    // 2. Create DataSourceManager for Binance
    std::vector<ExchangeId> exchanges = {"binance"};
    auto manager = createDataSourceManager(exchanges, publisherConfig);
    
    // 3. Initialize and start the manager
    if (!manager->initialize()) {
        LOG_ERROR("Failed to initialize DataSourceManager");
        return 1;
    }
    
    if (!manager->start()) {
        LOG_ERROR("Failed to start DataSourceManager");
        return 1;
    }
    
    LOG_INFO("DataSourceManager started successfully");
    
    // 4. Subscribe to market data
    DataSubscription subscription;
    subscription.exchangeId = "binance";
    subscription.symbols = {
        Symbol("BTC", "USDT"),
        Symbol("ETH", "USDT"),
        Symbol("BNB", "USDT")
    };
    subscription.dataTypes = {
        DataType::ORDER_BOOK,
        DataType::TRADES,
        DataType::TICKER
    };
    
    if (!manager->subscribe(subscription)) {
        LOG_ERROR("Failed to subscribe to market data");
        return 1;
    }
    
    LOG_INFO("Successfully subscribed to market data for " + 
             std::to_string(subscription.symbols.size()) + " symbols");
    
    // 5. Monitor data flow for 30 seconds
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(30)) {
        
        // Print statistics every 5 seconds
        auto stats = manager->getStats();
        std::cout << "\n=== Data Source Statistics ===" << std::endl;
        std::cout << "Messages Received: " << stats.messagesReceived << std::endl;
        std::cout << "Messages Published: " << stats.messagesPublished << std::endl;
        std::cout << "Messages Dropped: " << stats.messagesDropped << std::endl;
        std::cout << "Bytes Received: " << stats.bytesReceived << std::endl;
        std::cout << "Bytes Published: " << stats.bytesPublished << std::endl;
        
        // Show exchange-specific stats
        auto binanceStats = manager->getExchangeStats("binance");
        std::cout << "\nBinance Stats:" << std::endl;
        std::cout << "  Messages: " << binanceStats.messagesReceived << std::endl;
        std::cout << "  Bytes: " << binanceStats.bytesReceived << std::endl;
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    
    // 6. Stop the manager
    LOG_INFO("Stopping DataSourceManager");
    manager->stop();
    
    LOG_INFO("Example completed successfully");
    return 0;
}

/**
 * Expected ZeroMQ Topics Created:
 * 
 * market.orderbook.binance.BTC/USDT
 * market.orderbook.binance.ETH/USDT  
 * market.orderbook.binance.BNB/USDT
 * 
 * market.trade.binance.BTC/USDT
 * market.trade.binance.ETH/USDT
 * market.trade.binance.BNB/USDT
 * 
 * market.ticker.binance.BTC/USDT
 * market.ticker.binance.ETH/USDT
 * market.ticker.binance.BNB/USDT
 */

/**
 * How Your Risk System Would Subscribe:
 * 
 * // In your risk system
 * zmq::socket_t subscriber(context, ZMQ_SUB);
 * subscriber.connect("tcp://localhost:5555");
 * 
 * // Subscribe to all market data
 * subscriber.set(zmq::sockopt::subscribe, "market.");
 * 
 * // Or subscribe to specific data
 * subscriber.set(zmq::sockopt::subscribe, "market.ticker.binance.BTC/USDT");
 * 
 * // Receive messages
 * zmq::message_t message;
 * subscriber.recv(message);
 * // Parse the message data...
 */