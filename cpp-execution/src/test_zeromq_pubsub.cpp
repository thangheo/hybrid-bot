/**
 * @file test_zeromq_pubsub.cpp
 * @brief Test program for ZeroMQ pub/sub market data flow
 */

#include "communication/market_data_subscriber.hpp"
#include "common/logger.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace trading::communication;
using namespace trading::common;

int main() {
    Logger::getLogger()->setLevel(LogLevel::INFO);
    
    LOG_INFO("Starting ZeroMQ pub/sub test");
    
    // Create subscriber
    MarketDataSubscriber subscriber("tcp://localhost:5555");
    
    // Set up callbacks
    subscriber.setOrderBookCallback([](const OrderBookData& orderBook) {
        LOG_INFO("Received OrderBook: " + orderBook.symbol + 
                " Bids: " + std::to_string(orderBook.bids.size()) +
                " Asks: " + std::to_string(orderBook.asks.size()) +
                " Sequence: " + std::to_string(orderBook.sequence));
    });
    
    subscriber.setIntelligenceCallback([](const MarketIntelligenceData& intel) {
        LOG_INFO("Received Intelligence: " + intel.symbol + 
                " Spread: " + std::to_string(intel.bidAskSpread) +
                " Imbalance: " + std::to_string(intel.orderBookImbalance) +
                " Momentum: " + std::to_string(intel.priceMomentum));
    });
    
    // Start subscriber
    if (!subscriber.start()) {
        LOG_ERROR("Failed to start subscriber");
        return 1;
    }
    
    // Subscribe to specific symbols
    std::vector<std::string> symbols = {"BTCUSDT", "ETHUSDT"};
    subscriber.subscribeToOrderBooks(symbols);
    subscriber.subscribeToIntelligence(symbols);
    
    LOG_INFO("Subscriber started, waiting for messages...");
    LOG_INFO("Make sure data_ingestion_publisher is running: ./build/data_ingestion_publisher");
    
    // Run for 60 seconds
    auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(60)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Print stats every 10 seconds
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count();
        
        if (elapsed % 10 == 0) {
            LOG_INFO("Stats - OrderBooks: " + std::to_string(subscriber.getOrderBooksReceived()) +
                    " Intelligence: " + std::to_string(subscriber.getIntelligenceReceived()));
        }
    }
    
    LOG_INFO("Test completed");
    subscriber.stop();
    
    LOG_INFO("Final Stats - OrderBooks: " + std::to_string(subscriber.getOrderBooksReceived()) +
            " Intelligence: " + std::to_string(subscriber.getIntelligenceReceived()));
    
    return 0;
}
