#include "interfaces/data_interface.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace trading::risk;

int main() {
    // Create data interface
    auto dataInterface = createDataInterface();
    
    // Initialize
    if (!dataInterface->initialize()) {
        std::cerr << "Failed to initialize data interface" << std::endl;
        return 1;
    }
    
    // Register callback for market data updates
    dataInterface->registerMarketDataCallback([](const MarketData& marketData) {
        std::cout << "Received market data for " << marketData.tradingPair
                  << " from " << marketData.exchangeId.toString() << std::endl;
        std::cout << "  Price: " << marketData.price << std::endl;
        std::cout << "  Bid: " << marketData.bid << std::endl;
        std::cout << "  Ask: " << marketData.ask << std::endl;
    });
    
    // Subscribe to market data for BTC/USDT on Binance
    if (!dataInterface->subscribeMarketData("BTC/USDT", "binance")) {
        std::cerr << "Failed to subscribe to market data" << std::endl;
        return 1;
    }
    
    // Run for a while to receive updates
    std::this_thread::sleep_for(std::chrono::seconds(60));
    
    // Unsubscribe
    dataInterface->unsubscribeMarketData("BTC/USDT", "binance");
    
    return 0;
}