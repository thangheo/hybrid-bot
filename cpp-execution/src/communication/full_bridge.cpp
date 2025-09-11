/**
 * @file simplified_trading_server.cpp
 * @brief Simplified trading server using the new DataIngestionService
 */

#include "data_ingestion/data_ingestion_service.hpp"
#include "communication/zmq_server.hpp"
#include "market_intelligence/adaptive_intelligence.hpp"
#include "../../generated/cpp/proto/trading_messages.pb.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <signal.h>
#include <unordered_map>
#include <mutex>
#include <iomanip>

using namespace trading::data_ingestion;
using namespace trading::common;
using namespace trading::core;
using json = nlohmann::json;

// Global control
std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    g_running = false;
}

class SimplifiedTradingServer {
private:
    // Core services
    std::unique_ptr<DataIngestionService> dataService_;
    std::unique_ptr<TradingCommunicationServer> zmqServer_;
    std::unique_ptr<trading::intelligence::AdaptiveMarketIntelligence> adaptiveIntel_;
    
    // Market intelligence tracking
    std::unordered_map<std::string, trading::MarketIntelligence> latestIntelligence_;
    std::mutex intelligenceMutex_;
    
    // Statistics
    std::atomic<uint64_t> messagesProcessed_{0};
    std::atomic<uint64_t> intelligenceGenerated_{0};
    std::chrono::steady_clock::time_point startTime_;

public:
    SimplifiedTradingServer() : startTime_(std::chrono::steady_clock::now()) {
        // Initialize adaptive intelligence
        adaptiveIntel_ = std::make_unique<trading::intelligence::AdaptiveMarketIntelligence>();
        
        // Initialize data ingestion service
        dataService_ = std::make_unique<DataIngestionService>("data_ingestion.json");
        
        // Initialize ZMQ communication
        zmqServer_ = std::make_unique<TradingCommunicationServer>();
        
        setupCallbacks();
    }

    ~SimplifiedTradingServer() {
        try {
            stop();
        } catch (const std::exception& e) {
            std::cerr << "Error in destructor: " << e.what() << std::endl;
        }
    }

    bool start(const std::vector<std::string>& symbols) {
        std::cout << "Starting Simplified Trading Server..." << std::endl;
        // adaptiveIntel_->ApplyEmergencyThresholds();
        // 1. Initialize data ingestion service
        if (!dataService_->initialize()) {
            std::cerr << "Failed to initialize data ingestion service" << std::endl;
            return false;
        }
        
        // 2. Start ZMQ communication
        zmqServer_->Start();
        
        // 3. Start data ingestion with symbols
        if (!dataService_->start(symbols)) {
            std::cerr << "Failed to start data ingestion service" << std::endl;
            return false;
        }
        
        std::cout << "Simplified Trading Server started successfully with " 
                  << symbols.size() << " symbols" << std::endl;
        return true;
    }

    void stop() {
        if (!g_running.exchange(false)) {
            return; // Already stopped
        }
        
        std::cout << "Stopping Simplified Trading Server..." << std::endl;
        
        // Stop services in reverse order
        if (zmqServer_) {
            zmqServer_->Stop();
        }
        
        if (dataService_) {
            dataService_->stop();
        }
        
        std::cout << "Simplified Trading Server stopped" << std::endl;
    }

    void runMainLoop() {
        auto lastStatus = std::chrono::steady_clock::now();
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Print status every 10 seconds
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatus).count() >= 10) {
                printStatus();
                lastStatus = now;
            }
        }
    }

private:
    void setupCallbacks() {
        // Set up data ingestion callback
        dataService_->setOrderBookCallback([this](const OrderBookUpdate& update) {
            this->onOrderBookUpdate(update);
        });
        
        // Set up status callback
        dataService_->setStatusCallback([this](const std::string& component, const std::string& status) {
            this->onStatusUpdate(component, status);
        });
        
        // Set up ZMQ execution callback
        zmqServer_->SetExecutionCallback([this](const trading::TradingIntent& intent) {
            return this->handleTradingIntent(intent);
        });
    }

    void onOrderBookUpdate(const OrderBookUpdate& update) {
        try {
            messagesProcessed_.fetch_add(1);
            
            // Debug output for first few messages
            static std::atomic<int> debugCount{0};
            int count = debugCount.fetch_add(1);
            
            if (count < 5) {
                std::cout << "\n=== ORDER BOOK UPDATE #" << count << " ===" << std::endl;
                std::cout << "Exchange: " << update.exchangeId.toString() << std::endl;
                std::cout << "Symbol: " << update.symbol.toString() << std::endl;
                std::cout << "Sequence: " << update.sequenceNumber << std::endl;
                std::cout << "Bids: " << update.bids.size() << ", Asks: " << update.asks.size() << std::endl;
                
                if (!update.bids.empty() && !update.asks.empty()) {
                    std::cout << "Best Bid: " << update.bids[0].price << " @ " << update.bids[0].quantity << std::endl;
                    std::cout << "Best Ask: " << update.asks[0].price << " @ " << update.asks[0].quantity << std::endl;
                    double spread = update.asks[0].price - update.bids[0].price;
                    std::cout << "Spread: " << spread << std::endl;
                }
                std::cout << "=================================" << std::endl;
            }
            
            // Generate market intelligence
            generateMarketIntelligence(update);
            
        } catch (const std::exception& e) {
            std::cerr << "Error processing order book update: " << e.what() << std::endl;
        }
    }

    void generateMarketIntelligence(const OrderBookUpdate& update) {
        try {
            // Get the order book from data service
            auto orderBook = dataService_->getOrderBook(update.exchangeId, update.symbol);
            if (!orderBook) {
                return;
            }
            
            // Validate order book has data
            auto bids = orderBook->getBids(5);
            auto asks = orderBook->getAsks(5);
            
            if (bids.empty() || asks.empty()) {
                return;
            }
            
            // Verify prices are valid
            if (bids[0].price <= 0 || asks[0].price <= 0 || asks[0].price <= bids[0].price) {
                return;
            }
            
            // Generate adaptive market intelligence
            std::string symbol = update.symbol.toString();
            trading::MarketIntelligence intel = adaptiveIntel_->GenerateAdaptiveMarketIntelligence(symbol, orderBook);
            
            if (intel.symbol().empty()) {
                return; // No valid intelligence generated
            }
            
            // Debug output for first few intelligence generations per symbol
            static std::unordered_map<std::string, int> intelDebugCounts;
            intelDebugCounts[symbol]++;
            
            if (intelDebugCounts[symbol] <= 3) {
                std::cout << "\n=== ADAPTIVE INTELLIGENCE for " << symbol << " ===" << std::endl;
                std::cout << "  Spread: " << intel.bid_ask_spread() << std::endl;
                std::cout << "  Liquidity: " << intel.liquidity_ratio() << std::endl;
                std::cout << "  Imbalance: " << intel.order_book_imbalance() << std::endl;
                std::cout << "  Regime: " << intel.current_regime() << std::endl;
                std::cout << "  Confidence: " << intel.regime_confidence() << std::endl;
                std::cout << "=============================================" << std::endl;
            }
            
            // Store latest intelligence
            {
                std::lock_guard<std::mutex> lock(intelligenceMutex_);
                latestIntelligence_[symbol] = intel;
            }
            
            // Publish to ZMQ
            if (zmqServer_) {
                zmqServer_->PublishMarketIntelligence(intel);
                intelligenceGenerated_.fetch_add(1);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error generating market intelligence: " << e.what() << std::endl;
        }
    }

    void onStatusUpdate(const std::string& component, const std::string& status) {
        static std::atomic<int> statusCount{0};
        if (statusCount.fetch_add(1) % 10 == 0) { // Print every 10th status
            std::cout << "[" << component << "] " << status << std::endl;
        }
    }

    trading::ExecutionAcknowledgment handleTradingIntent(const trading::TradingIntent& intent) {
        std::cout << "\n=== PROCESSING TRADING INTENT ===" << std::endl;
        std::cout << "Intent ID: " << intent.intent_id() << std::endl;
        std::cout << "Symbol: " << intent.symbol() << std::endl;
        std::cout << "Side: " << (intent.side() == trading::SIDE_BUY ? "BUY" : "SELL") << std::endl;
        std::cout << "Quantity: " << intent.target_quantity() << std::endl;
        std::cout << "Signal Strength: " << intent.signal_strength() << std::endl;
        std::cout << "Confidence: " << intent.confidence() << std::endl;
        
        // Get current market intelligence for this symbol
        trading::MarketIntelligence currentIntel;
        bool hasIntel = false;
        {
            std::lock_guard<std::mutex> lock(intelligenceMutex_);
            auto it = latestIntelligence_.find(intent.symbol());
            if (it != latestIntelligence_.end()) {
                currentIntel = it->second;
                hasIntel = true;
                std::cout << "Current Market Intelligence:" << std::endl;
                std::cout << "  Spread: " << currentIntel.bid_ask_spread() << std::endl;
                std::cout << "  Imbalance: " << currentIntel.order_book_imbalance() << std::endl;
                std::cout << "  Liquidity: " << currentIntel.liquidity_ratio() << std::endl;
                std::cout << "  Regime: " << currentIntel.current_regime() << std::endl;
            }
        }
        
        // Create acknowledgment
        trading::ExecutionAcknowledgment ack;
        ack.set_intent_id(intent.intent_id());
        ack.set_accepted(true);
        ack.set_execution_id("exec_" + intent.intent_id() + "_" + std::to_string(std::time(nullptr)));
        
        if (hasIntel) {
            ack.set_reason("Intent accepted - Market intelligence integrated (" + 
                          std::to_string(currentIntel.liquidity_ratio()) + " liquidity, " +
                          std::to_string(currentIntel.bid_ask_spread()) + " spread)");
        } else {
            ack.set_reason("Intent accepted - No market intelligence available yet");
        }
        
        // Estimate completion time
        int64_t baseTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        int completionDelay = calculateCompletionDelay(intent, currentIntel, hasIntel);
        ack.set_estimated_completion_time(baseTime + completionDelay);
        
        std::cout << "Acknowledged intent with " << completionDelay << "ms estimated completion" << std::endl;
        std::cout << "=================================" << std::endl;
        
        return ack;
    }

    int calculateCompletionDelay(const trading::TradingIntent& intent, 
                                const trading::MarketIntelligence& intel,
                                bool hasIntel) {
        int baseDelay = 15000; // 15 seconds base
        
        // Adjust for execution style
        switch (intent.execution_style()) {
            case trading::EXEC_AGGRESSIVE:
                baseDelay = 5000;
                break;
            case trading::EXEC_STEALTH:
                baseDelay = std::min(60000, intent.max_execution_time_ms());
                break;
            case trading::EXEC_PASSIVE:
                baseDelay = 30000;
                break;
            default:
                baseDelay = 15000;
                break;
        }
        
        // Adjust for market conditions if we have intelligence
        if (hasIntel) {
            if (intel.current_regime() == trading::OB_THIN) {
                baseDelay *= 2.0; // Slower in thin markets
            } else if (intel.current_regime() == trading::OB_LIQUID) {
                baseDelay *= 0.8; // Faster in liquid markets
            }
            
            // Adjust for spread
            if (intel.bid_ask_spread() > 0.005) {
                baseDelay *= 1.2; // Slower for wide spreads
            }
        }
        
        return static_cast<int>(baseDelay);
    }

    void printStatus() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
        
        std::cout << "\n=== SIMPLIFIED TRADING SERVER STATUS ===" << std::endl;
        std::cout << "Runtime: " << elapsed << "s" << std::endl;
        std::cout << "Service Health: " << (dataService_->isHealthy() ? "HEALTHY" : "UNHEALTHY") << std::endl;
        std::cout << "Order book updates processed: " << messagesProcessed_.load() << std::endl;
        std::cout << "Intelligence generated: " << intelligenceGenerated_.load() << std::endl;
        
        if (elapsed > 0) {
            std::cout << "Update rate: " << (messagesProcessed_.load() / elapsed) << " updates/s" << std::endl;
            std::cout << "Intelligence rate: " << (intelligenceGenerated_.load() / elapsed) << " intel/s" << std::endl;
        }
        
        // Active symbols with intelligence
        {
            std::lock_guard<std::mutex> lock(intelligenceMutex_);
            std::cout << "Symbols with intelligence: " << latestIntelligence_.size() << std::endl;
            
            int displayed = 0;
            for (const auto& [symbol, intel] : latestIntelligence_) {
                if (displayed < 5) { // Show first 5 symbols
                    std::cout << "  " << symbol << ": " 
                              << "spread=" << std::fixed << std::setprecision(6) << intel.bid_ask_spread() 
                              << ", liq=" << std::setprecision(3) << intel.liquidity_ratio()
                              << ", regime=" << intel.current_regime() << std::endl;
                    displayed++;
                } else if (displayed == 5) {
                    std::cout << "  ... and " << (latestIntelligence_.size() - 5) << " more" << std::endl;
                    break;
                }
            }
        }
        
        // Data service statistics
        auto stats = dataService_->getStatistics();
        std::cout << "Data service stats:" << std::endl;
        std::cout << "  Messages: " << stats.messagesProcessed << std::endl;
        std::cout << "  Errors: " << stats.errorsEncountered << std::endl;
        std::cout << "  Active order books: " << stats.totalOrderBooks << std::endl;
        
        std::cout << "=========================================" << std::endl;
    }
};

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "=== SIMPLIFIED TRADING SERVER ===" << std::endl;
    try {
        SimplifiedTradingServer server;
        
        // Define symbols to monitor
        std::vector<std::string> symbols = {
            "BTC/USDT", "ETH/USDT", "SOL/USDT", "XRP/USDT", "ADA/USDT",
            "AVAX/USDT", "DOT/USDT", "LINK/USDT", "UNI/USDT", "ATOM/USDT"
            , "ALGO/USDT"  
            , "AAVE/USDT"   
            , "ARB/USDT"   
            , "OP/USDT"    
            , "INJ/USDT"    
            , "SUI/USDT"   
            , "TRB/USDT"  
            , "BAND/USDT"  
            , "CHZ/USDT"    
            , "ENJ/USDT"  
            , "YFI/USDT"    
            , "1INCH/USDT" 
            , "DOGE/USDT"  
            , "WIFI/USDT"   
   };   
        if (!server.start(symbols)) {
            std::cerr << "Failed to start server" << std::endl;
            return 1;
        }
        
        std::cout << "\nServer running with " << symbols.size() << " symbols..." << std::endl;
        std::cout << "Data flow: DataIngestionService -> OrderBooks -> Intelligence -> ZMQ" << std::endl;
        std::cout << "Press Ctrl+C to stop gracefully..." << std::endl;
        
        // Run main loop
        server.runMainLoop();
        
        std::cout << "\nShutting down..." << std::endl;
        server.stop();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Server shutdown complete." << std::endl;
    return 0;
}