// src/data_ingestion/websocket/examples_websocket.cpp
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "core/market_types.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <csignal>
#include <atomic>

using namespace trading::data_ingestion;
using namespace trading::core;

// Signal handler to gracefully exit the program
std::atomic<bool> running{true};

void signalHandler(int signal) {
    std::cout << "Received signal " << signal << ", shutting down..." << std::endl;
    running = false;
}

// Market data callback function - Updated signature to match your BinanceWebSocketClient
void onMarketData(const ExchangeId& exchange, const std::string& symbol, const std::vector<uint8_t>& data) {
    std::cout << "Received data from " << exchange.toString() << " for " << symbol << std::endl;
    
    // Print the first few bytes of the data for debugging
    size_t printSize = std::min(size_t(100), data.size());
    std::string dataPreview(data.begin(), data.begin() + printSize);
    if (data.size() > printSize) {
        dataPreview += "...";
    }
    
    std::cout << "Data preview: " << dataPreview << std::endl;
    std::cout << "Data size: " << data.size() << " bytes" << std::endl;
    std::cout << "-------------------------" << std::endl;
}

int main() {
    // Set up signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    try {
        std::cout << "Starting WebSocket example..." << std::endl;
        
        // Create a connection pool for Binance
        auto pool = std::make_unique<ConnectionPool>(
            "binance_example_pool",
            []() -> ClientInterfacePtr {
                // Factory function to create Binance clients
                return std::make_shared<BinanceWebSocketClient>(
                    "stream.binance.com", 
                    "9443",
                    onMarketData  // Pass the callback function
                );
            },
            1,  // Min connections (start with 1 for example)
            3   // Max connections
        );
        
        std::cout << "Connection pool created" << std::endl;
        
        // Initialize and connect the pool
        pool->initialize();
        if (!pool->connectAll()) {
            std::cerr << "Failed to connect all connections in the pool" << std::endl;
            return 1;
        }
        
        std::cout << "Connection pool initialized and connected" << std::endl;
        
        // Acquire a connection from the pool
        auto connection = pool->acquireConnection(5000); // 5 second timeout
        
        if (!connection) {
            std::cerr << "Failed to acquire a connection from the pool" << std::endl;
            return 1;
        }
        
        std::cout << "Acquired connection from pool" << std::endl;
        
        // Create Symbol objects properly - base and quote currencies
        std::vector<Symbol> symbols = {
            Symbol("BTC", "USDT"),  // Bitcoin/USDT
            Symbol("ETH", "USDT"),  // Ethereum/USDT
            Symbol("BNB", "USDT")   // Binance Coin/USDT
        };
        
        // Subscribe to market data for these symbols
        if (connection->subscribe(symbols)) {
            std::cout << "Successfully subscribed to " << symbols.size() << " symbols:" << std::endl;
            for (const auto& symbol : symbols) {
                std::cout << "  - " << symbol.toString() << std::endl;
            }
        } else {
            std::cerr << "Failed to subscribe to symbols" << std::endl;
            pool->releaseConnection(connection);
            return 1;
        }
        
        // Keep the program running until a signal is received
        std::cout << "\nReceiving data... Press Ctrl+C to exit\n" << std::endl;
        
        int counter = 0;
        while (running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // Print pool status every 5 seconds
            if (pool->isHealthy()) {
                std::cout << "Connection pool status: Healthy ("
                          << pool->getActiveConnectionCount() << " active, "
                          << pool->getAvailableConnectionCount() << " available, "
                          << pool->getTotalConnectionCount() << " total)" << std::endl;
                
                // Print connection stats
                std::cout << "Connection stats: " 
                          << connection->getMessageCount() << " messages received, "
                          << "avg latency: " << connection->getAverageLatency() << "ms" << std::endl;
            } else {
                std::cerr << "Connection pool status: Unhealthy" << std::endl;
            }
            
            counter++;
            if (counter >= 12) { // Exit after 1 minute for demo
                std::cout << "Demo time completed, exiting..." << std::endl;
                break;
            }
        }
        
        // Clean shutdown
        std::cout << "\nShutting down..." << std::endl;
        
        // Unsubscribe from symbols
        connection->unsubscribe(symbols);
        
        // Release the connection back to the pool
        pool->releaseConnection(connection);
        std::cout << "Released connection back to pool" << std::endl;
        
        // Shutdown the pool
        pool->shutdown();
        std::cout << "Connection pool shut down" << std::endl;
        
        std::cout << "Example completed successfully" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}