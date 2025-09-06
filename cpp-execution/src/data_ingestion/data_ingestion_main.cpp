// Fixed data_ingestion_main.cpp for testing DataIngestionService
#include "data_ingestion/data_ingestion_service.hpp"
#include "common/event_bus.hpp"
#include <signal.h>
#include <common/logger.hpp>
#include <iostream>
#include <atomic>
#include <thread>
// Add this include at the top of data_ingestion_main.cpp
#include <fstream>
std::unique_ptr<trading::data_ingestion::DataIngestionService> service;
std::atomic<bool> shutdownRequested{false};

void signalHandler(int signal) {
    std::cout << "\nShutdown signal received..." << std::endl;
    shutdownRequested.store(true);
    if (service) {
        service->stop();
    }
}
// Create a minimal config file for testing
void createTestConfig() {
    const char* configContent = R"({
    "exchanges": {
        "BINANCE": {
            "id": "BINANCE",
            "name": "Binance",
            "ws_url": "wss://stream.binance.com:9443/ws",
            "rest_url": "https://api.binance.com",
            "symbols": ["BTCUSDT", "ETHUSDT", "BNBUSDT", "LTCUSDT", "XRPUSDT", "BCHUSDT", "ADAUSDT", "DOTUSDT", "LINKUSDT", "XLMUSDT", "DOGEUSDT", "SOLUSDT", "MATICUSDT", "AVAXUSDT", "SHIBUSDT", "TRXUSDT", "UNIUSDT", "ATOMUSDT", "ALGOUSDT", "VETUSDT", "FILUSDT"],
            "connection_timeout_ms": 5000,
            "max_reconnect_attempts": 3
        }
    },
    "order_book": {
        "depth": 20,
        "snapshot_interval_ms": 30000
    },
    "publisher": {
        "zmq_endpoint": "tcp://*:5555",
        "max_queue_size": 1000,
        "publish_interval_ms": 100
    }
})";
    
    std::ofstream configFile("config/test_config.json");
    configFile << configContent;
    configFile.close();
    std::cout << "Created test config file" << std::endl;
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "=== TESTING DATA INGESTION SERVICE ===" << std::endl;
    
    try {
        // Create test config if it doesn't exist
        createTestConfig();
        
        // Start the event bus first
        trading::common::EventBus::getInstance().start();
        
        // Create the service
        service = std::make_unique<trading::data_ingestion::DataIngestionService>("config/test_config.json");
        
        // Set up callbacks for debugging
        service->setStatusCallback([](const std::string& component, const std::string& status) {
            static std::atomic<int> statusCount{0};
            if (statusCount.fetch_add(1) % 5 == 0) { // Print every 5th status
                std::cout << "[" << component << "] " << status << std::endl;
            }
        });
        
        service->setOrderBookCallback([](const auto& update) {
            static std::atomic<int> updateCount{0};
            int count = updateCount.fetch_add(1);
            
            if (count < 10) { // Debug first 10 updates
                std::cout << "\n=== ORDER BOOK UPDATE #" << count << " ===" << std::endl;
                std::cout << "Exchange: " << update.exchangeId.toString() << std::endl;
                std::cout << "Symbol: " << update.symbol.toString() << std::endl;
                std::cout << "Sequence: " << update.sequenceNumber << std::endl;
                std::cout << "Is Snapshot: " << (update.isSnapshot ? "YES" : "NO") << std::endl;
                std::cout << "Bids: " << update.bids.size() << ", Asks: " << update.asks.size() << std::endl;
                
                if (!update.bids.empty() && !update.asks.empty()) {
                    std::cout << "Best Bid: " << update.bids[0].price << " @ " << update.bids[0].quantity << std::endl;
                    std::cout << "Best Ask: " << update.asks[0].price << " @ " << update.asks[0].quantity << std::endl;
                    double spread = update.asks[0].price - update.bids[0].price;
                    double midPrice = (update.asks[0].price + update.bids[0].price) / 2.0;
                    std::cout << "Spread: " << spread << " Mid: " << midPrice << std::endl;
                }
                std::cout << "========================\n" << std::endl;
            } else if (count % 100 == 0) {
                std::cout << "Processed " << count << " order book updates..." << std::endl;
            }
        });
        
        // Initialize the service
        std::cout << "Initializing DataIngestionService..." << std::endl;
        if (!service->initialize()) {
            std::cerr << "Failed to initialize service" << std::endl;
            trading::common::EventBus::getInstance().stop();
            return 1;
        }
        
        // Start with test symbols
        /*{
      "symbol": "BTC/USDT",
      "exchange": "BINANCE"
    },
    {
      "symbol": "ETH/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "LTC/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "XRP/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "BCH/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "ADA/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "DOT/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "LINK/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "XLM/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "DOGE/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "SOL/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "MATIC/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "AVAX/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "SHIB/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "TRX/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "UNI/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "ATOM/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "ALGO/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "VET/USDT", 
      "exchange": "BINANCE"
    },
    {
      "symbol": "FIL/USDT", 
      "exchange": "BINANCE"
    }*/
        std::vector<std::string> symbols = {"BTCUSDT", "ETHUSDT", "BNBUSDT", "LTCUSDT", "XRPUSDT", "BCHUSDT", "ADAUSDT", "DOTUSDT", "LINKUSDT", "XLMUSDT", "DOGEUSDT", "SOLUSDT", "MATICUSDT", "AVAXUSDT", "SHIBUSDT", "TRXUSDT", "UNIUSDT", "ATOMUSDT", "ALGOUSDT", "VETUSDT", "FILUSDT"};
        std::cout << "Starting service with symbols: ";
        for (const auto& symbol : symbols) {
            std::cout << symbol << " ";
        }
        std::cout << std::endl;
        
        if (!service->start(symbols)) {
            std::cerr << "Failed to start service" << std::endl;
            trading::common::EventBus::getInstance().stop();
            return 1;
        }
        
        std::cout << "DataIngestionService running successfully!" << std::endl;
        std::cout << "Monitoring for order book updates..." << std::endl;
        std::cout << "Press Ctrl+C to stop" << std::endl;
        
        // Main loop with status reporting
        auto lastStatusTime = std::chrono::steady_clock::now();
        int loopCount = 0;
        
        while (service->isRunning() && !shutdownRequested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Print periodic status
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatusTime).count() >= 10) {
                // Get service statistics
                auto stats = service->getStatistics();
                
                std::cout << "\n=== SERVICE STATUS ===" << std::endl;
                std::cout << "Running: " << (service->isRunning() ? "YES" : "NO") << std::endl;
                std::cout << "Healthy: " << (service->isHealthy() ? "YES" : "NO") << std::endl;
                std::cout << "Messages processed: " << stats.messagesProcessed << std::endl;
                std::cout << "Errors: " << stats.errorsEncountered << std::endl;
                std::cout << "Updates/sec: " << stats.updatesPerSecond << std::endl;
                std::cout << "Active order books: " << stats.totalOrderBooks << std::endl;
                
                auto activeBooks = service->getActiveOrderBooks();
                std::cout << "Order book symbols: ";
                for (const auto& symbol : activeBooks) {
                    std::cout << symbol << " ";
                }
                std::cout << std::endl;
                std::cout << "======================" << std::endl;
                
                lastStatusTime = now;
            }
            
            loopCount++;
        }
        
        std::cout << "\nShutting down service..." << std::endl;
        service->stop();
        service.reset();
        
        // Stop event bus last
        trading::common::EventBus::getInstance().stop();
        
        std::cout << "DataIngestionService test completed successfully" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Service test failed: " << e.what() << std::endl;
        
        // Cleanup on error
        if (service) {
            service->stop();
            service.reset();
        }
        trading::common::EventBus::getInstance().stop();
        
        return 1;
    }
    
    return 0;
}