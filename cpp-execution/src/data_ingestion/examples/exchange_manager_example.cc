// src/data_ingestion/examples/exchange_manager_example.cpp
#include "data_ingestion/exchange_manager.hpp"
#include "data_ingestion/websocket/exchanges/binance_client_factory.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <functional>
#include <iomanip>

using namespace trading::data_ingestion;

// Signal handler to gracefully exit the program
std::atomic<bool> running{true};

void signalHandler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    running = false;
}

// Callback for normalized order book updates
void onOrderBookUpdate(const OrderBookUpdate& update) {
    // Get current timestamp for latency calculation
    auto now = std::chrono::system_clock::now().time_since_epoch().count();
    auto latency_ms = (now - update.receivedTimestamp) / 1000000.0;
    
    // Print basic information about the update
    std::cout << "Normalized Order Book Update:" << std::endl;
    std::cout << "Exchange: " << update.exchangeId << std::endl;
    std::cout << "Trading Pair: " << update.tradingPair << std::endl;
    std::cout << "Timestamp: " << update.exchange_timestamp << std::endl;
    std::cout << "Latency: " << std::fixed << std::setprecision(3) << latency_ms << "ms" << std::endl;
    
    // Print the top of the book (best bid/ask)
    if (!update.bids.empty() && !update.asks.empty()) {
        std::cout << "Best Bid: " << update.bids[0].getPrice() << " (" << update.bids[0].getQuantity() << ")" << std::endl;
        std::cout << "Best Ask: " << update.asks[0].getPrice() << " (" << update.asks[0].getQuantity() << ")" << std::endl;
        
        // Calculate spread
        double bid_price = update.bids[0].getPrice();
        double ask_price = update.asks[0].getPrice();
        double spread = ask_price - bid_price;
        double spread_pct = (spread / ask_price) * 100;
        
        std::cout << "Spread: " << spread << " (" << std::fixed << std::setprecision(4) 
                  << spread_pct << "%)" << std::endl;
    }
    
    std::cout << "Bid Levels: " << update.bids.size() << ", Ask Levels: " << update.asks.size() << std::endl;
    std::cout << "-------------------------" << std::endl;
}

int main() {
    // Set up signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    try {
        std::cout << "Starting Exchange Manager Example..." << std::endl;
        
        // Create the Exchange Manager
        ExchangeManager manager(onOrderBookUpdate);
        
        // Register exchanges
        manager.registerExchange(
            "binance",
            [](MarketDataCallback callback) {
                return BinanceClientFactory::createConnectionPool(
                    std::move(callback),
                    2,  // min_connections
                    5   // max_connections
                );
            },
            2,  // min_connections
            5   // max_connections
        );
        
        // Connect to all exchanges
        manager.connectAll();
        
        // Define trading pairs and data types
        std::vector<std::string> tradingPairs = {"BTC/USDT", "ETH/USDT", "SOL/USDT"};
        std::vector<DataType> data_types = {DataType::ORDER_BOOK, DataType::TRADES};
        
        // Subscribe to market data
        manager.subscribeAll(tradingPairs, data_types, 20);
        
        std::cout << "Subscribed to market data, press Ctrl+C to exit" << std::endl;
        
        // Main loop
        int count = 0;
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            // Periodically check connected exchanges
            if (++count % 10 == 0) {
                auto connected_exchanges = manager.getConnectedExchanges();
                
                std::cout << "Connected exchanges: ";
                for (const auto& exchange : connected_exchanges) {
                    std::cout << exchange << " ";
                }
                std::cout << std::endl;
            }
        }
        
        // Clean up
        std::cout << "Disconnecting from exchanges..." << std::endl;
        manager.disconnectAll();
        
        std::cout << "Example completed successfully" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
