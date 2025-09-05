// data_ingestion_main.cpp
#include "data_ingestion/data_ingestion_service.hpp"
#include "common/event_bus.hpp"  // Add this
#include <signal.h>
#include <common/logger.hpp>
#include <iostream>
#include <atomic>

std::unique_ptr<trading::data_ingestion::DataIngestionService> service;
std::atomic<bool> shutdownRequested{false};

void signalHandler(int signal) {
    std::cout << "\nShutdown signal received..." << std::endl;
    shutdownRequested.store(true);
    if (service) {
        service->stop();
    }
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    // Start the event bus first
    trading::common::EventBus::getInstance().start();
    
    service = std::make_unique<trading::data_ingestion::DataIngestionService>("config/test_config.json");
    
    // Set callbacks
    service->setStatusCallback([](const std::string& component, const std::string& status) {
        std::cout << "[" << component << "] " << status << std::endl;
    });
    
    // service->setOrderBookCallback([](const auto& update) {
    //     std::cout << "Order book update: " << update.symbol.toString() 
    //               << " bids=" << update.bids.size() 
    //               << " asks=" << update.asks.size() << std::endl;
    // });
    // In your main function, replace the orderBookCallback with this:
    service->setOrderBookCallback([](const auto& update) {
        std::cout << "\n=== ORDER BOOK UPDATE ===" << std::endl;
        std::cout << "Exchange: " << update.exchangeId.toString() << std::endl;
        std::cout << "Symbol: " << update.symbol.toString() << std::endl;
        std::cout << "Sequence: " << update.sequenceNumber << std::endl;
        std::cout << "Is Snapshot: " << (update.isSnapshot ? "YES" : "NO") << std::endl;
        
        // Print top 5 bids and asks
        std::cout << "TOP BIDS:" << std::endl;
        for (size_t i = 0; i < std::min(size_t(5), update.bids.size()); ++i) {
            std::cout << "  " << update.bids[i].price << " @ " << update.bids[i].quantity << std::endl;
        }
        
        std::cout << "TOP ASKS:" << std::endl;
        for (size_t i = 0; i < std::min(size_t(5), update.asks.size()); ++i) {
            std::cout << "  " << update.asks[i].price << " @ " << update.asks[i].quantity << std::endl;
        }
        
        if (!update.bids.empty() && !update.asks.empty()) {
            double spread = update.asks[0].price - update.bids[0].price;
            double midPrice = (update.asks[0].price + update.bids[0].price) / 2.0;
            std::cout << "Spread: " << spread << " Mid: " << midPrice << std::endl;
        }
        std::cout << "========================\n" << std::endl;
    });
    // Initialize and start
    if (!service->initialize()) {
        std::cerr << "Failed to initialize service" << std::endl;
        trading::common::EventBus::getInstance().stop();
        return 1;
    }
    
    std::vector<std::string> symbols = {"BTCUSDT", "ETHUSDT", "BNBUSDT"};
    if (!service->start(symbols)) {
        std::cerr << "Failed to start service" << std::endl;
        trading::common::EventBus::getInstance().stop();
        return 1;
    }
    
    std::cout << "Service running... Press Ctrl+C to stop" << std::endl;
    
    // Keep running with better shutdown handling
    while (service->isRunning() && !shutdownRequested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Shorter sleep for faster response
        
        // Optional: Print periodic status
        static int counter = 0;
        if (++counter % 100 == 0) {  // Every 10 seconds
            std::cout << "." << std::flush;  // Just a simple heartbeat
        }
    }
    
    std::cout << "\nShutting down..." << std::endl;
    
    // Cleanup
    service->stop();
    service.reset();
    
    // Stop event bus last
    trading::common::EventBus::getInstance().stop();
    
    std::cout << "Shutdown complete" << std::endl;
    return 0;
}