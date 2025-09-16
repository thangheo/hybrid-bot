/**
 * @file data_ingestion_publisher.cpp
 * @brief Standalone data ingestion publisher service using ZeroMQ
 * 
 * This service runs independently and publishes market data via ZeroMQ,
 * decoupling data ingestion from the main trading system.
 */

#include "data_ingestion/data_ingestion_service.hpp"
#include "data_ingestion/publisher/zeromq_publisher.hpp"
#include "market_intelligence/adaptive_intelligence.hpp"
#include "common/event_bus.hpp"
#include "common/logger.hpp"
#include "../../generated/cpp/proto/trading_messages.pb.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <atomic>
#include <thread>
#include <signal.h>
#include <chrono>
#include <unordered_map>
#include <mutex>
#include <iomanip>

using namespace trading::data_ingestion;
using namespace trading::common;
using namespace trading::core;
using json = nlohmann::json;

// Global control
std::atomic<bool> g_running{true};
std::unique_ptr<DataIngestionService> g_dataService;
std::unique_ptr<ZeroMQPublisher> g_zmqPublisher;


class DataIngestionPublisher {
private:
    // Core services
    std::unique_ptr<DataIngestionService> dataService_;
    std::unique_ptr<ZeroMQPublisher> zmqPublisher_;
    std::unique_ptr<trading::intelligence::AdaptiveMarketIntelligence> adaptiveIntel_;
    
    // Market intelligence tracking
    std::unordered_map<std::string, trading::MarketIntelligence> latestIntelligence_;
    std::mutex intelligenceMutex_;
    
    // Statistics
    std::atomic<uint64_t> messagesProcessed_{0};
    std::atomic<uint64_t> intelligenceGenerated_{0};
    std::atomic<uint64_t> orderBooksPublished_{0};
    std::chrono::steady_clock::time_point startTime_;

public:
    DataIngestionPublisher() : startTime_(std::chrono::steady_clock::now()) {
        LOG_INFO("Initializing Data Ingestion Publisher");
        
        // Initialize adaptive intelligence
        adaptiveIntel_ = std::make_unique<trading::intelligence::AdaptiveMarketIntelligence>();
        
        // Initialize data ingestion service
        dataService_ = std::make_unique<DataIngestionService>("config/data_ingestion.json");
        
        // Initialize ZeroMQ publisher
        zmqPublisher_ = std::make_unique<ZeroMQPublisher>(PublisherConfig{"tcp://*:5555"});
        
        setupCallbacks();
    }

    ~DataIngestionPublisher() {
        try {
            stop();
        } catch (const std::exception& e) {
            LOG_ERROR("Error in destructor: " + std::string(e.what()));
        }
    }

    bool start(const std::vector<Symbol>& symbols) {
        LOG_INFO("Starting Data Ingestion Publisher...");
        
        // 1. Start event bus
        trading::common::EventBus::getInstance().start();
        
        // 2. Initialize ZeroMQ publisher
        if (!zmqPublisher_->start()) {
            // If the default bind address is already in use (common in dev), try an ephemeral port
            LOG_WARN("ZeroMQ bind failed on tcp://*:5555, attempting fallback to tcp://127.0.0.1:0");
            zmqPublisher_ = std::make_unique<ZeroMQPublisher>(PublisherConfig{"tcp://127.0.0.1:0"});
            if (!zmqPublisher_->start()) {
                LOG_ERROR("Failed to start ZeroMQ publisher on fallback address");
                return false;
            }
            LOG_INFO("ZeroMQ publisher started on fallback address tcp://127.0.0.1:0");
        } else {
            LOG_INFO("ZeroMQ publisher started on tcp://*:5555");
        }
        
        // 3. Initialize data ingestion service
        if (!dataService_->initialize()) {
            LOG_ERROR("Failed to initialize data ingestion service");
            return false;
        }
        
        // 4. Start data ingestion with symbols
        if (!dataService_->start(symbols)) {
            LOG_ERROR("Failed to start data ingestion service");
            return false;
        }
        
        LOG_INFO("Data Ingestion Publisher started successfully with " + 
                std::to_string(symbols.size()) + " symbols");
        
        // Print subscribed symbols
        std::cout << "📊 Subscribed symbols:" << std::endl;
        for (const auto& symbol : symbols) {
            std::cout << "  - " << symbol.toString() << std::endl;
        }
        
        return true;
    }

    void stop() {
        if (!g_running.exchange(false)) {
            return; // Already stopped
        }
        
        LOG_INFO("Stopping Data Ingestion Publisher...");
        
        // Stop services in reverse order
        if (dataService_) {
            dataService_->stop();
        }
        
        if (zmqPublisher_) {
            zmqPublisher_->stop();
        }
        
        // Stop event bus last
        trading::common::EventBus::getInstance().stop();
        
        LOG_INFO("Data Ingestion Publisher stopped");
    }

    void runMainLoop() {
        auto lastStatus = std::chrono::steady_clock::now();
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Print status every 30 seconds
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatus).count() >= 30) {
                printStatus();
                lastStatus = now;
            }
        }
    }

private:
    void setupCallbacks() {
        // Set up data ingestion callback for order book updates
        dataService_->setOrderBookCallback([this](const OrderBookUpdate& update) {
            this->onOrderBookUpdate(update);
        });
        
        // Set up status callback for monitoring
        dataService_->setStatusCallback([this](const std::string& component, const std::string& status) {
            this->onStatusUpdate(component, status);
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
                std::cout << "Symbol: " << update.tradingPair.toString() << std::endl;
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
            
            // 📤 Publish order book data via ZeroMQ
            publishOrderBook(update);
            
            // 🧠 Generate and publish market intelligence
            generateMarketIntelligence(update);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error processing order book update: " + std::string(e.what()));
        }
    }

    void publishOrderBook(const OrderBookUpdate& update) {
        try {
            // Create JSON message for order book update
            json orderBookMsg;
            orderBookMsg["type"] = "order_book_update";
            orderBookMsg["exchange"] = update.exchangeId.toString();
            orderBookMsg["symbol"] = update.tradingPair.toString();
            orderBookMsg["sequence"] = update.sequenceNumber;
            orderBookMsg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                update.exchangeTimestamp.time_since_epoch()).count();
            
            // Add bids
            orderBookMsg["bids"] = json::array();
            for (const auto& bid : update.bids) {
                orderBookMsg["bids"].push_back({bid.price, bid.quantity});
            }
            
            // Add asks
            orderBookMsg["asks"] = json::array();
            for (const auto& ask : update.asks) {
                orderBookMsg["asks"].push_back({ask.price, ask.quantity});
            }
            
            // Publish via ZeroMQ
            std::string topic = "orderbook." + update.exchangeId.toString() + "." + update.tradingPair.toString();
            std::string jsonStr = orderBookMsg.dump();
            std::vector<uint8_t> data(jsonStr.begin(), jsonStr.end());
            zmqPublisher_->publish(topic, data);
            
            orderBooksPublished_.fetch_add(1);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error publishing order book: " + std::string(e.what()));
        }
    }

    void generateMarketIntelligence(const OrderBookUpdate& update) {
        try {
            // Get the order book from data service
            auto orderBook = dataService_->getOrderBook(update.exchangeId, update.tradingPair);
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
            std::string tradingPair = update.tradingPair.toString();
            
            // Throttle intelligence generation
            static std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastIntelTime;
            auto now = std::chrono::steady_clock::now();
            
            bool shouldGenerate = false;
            if (lastIntelTime.find(tradingPair) == lastIntelTime.end()) {
                shouldGenerate = true; // First time
            } else {
                auto timeSinceLastIntel = now - lastIntelTime[tradingPair];
                if (timeSinceLastIntel >= std::chrono::seconds(10)) { // Generate every 10 seconds
                    shouldGenerate = true;
                }
            }
            
            if (!shouldGenerate) {
                return;
            }
            
            lastIntelTime[tradingPair] = now;
            
            trading::MarketIntelligence intel = adaptiveIntel_->GenerateAdaptiveMarketIntelligence(tradingPair, orderBook);

            if (intel.symbol().empty()) {
                return; // No valid intelligence generated
            }

            // Debug output for intelligence generations
            static std::unordered_map<std::string, int> intelDebugCounts;
            intelDebugCounts[tradingPair]++;
            
            if (intelDebugCounts[tradingPair] <= 3 || intelDebugCounts[tradingPair] % 50 == 0) {
                std::cout << "🧠 Generated intelligence #" << intelDebugCounts[tradingPair] 
                          << " for " << tradingPair << " (spread: " << intel.bid_ask_spread() 
                          << ", imbalance: " << intel.order_book_imbalance() << ")" << std::endl;
            }
            
            // Store latest intelligence
            {
                std::lock_guard<std::mutex> lock(intelligenceMutex_);
                latestIntelligence_[tradingPair] = intel;
            }
            
            // 📤 Publish intelligence via ZeroMQ
            publishMarketIntelligence(intel);
            
            intelligenceGenerated_.fetch_add(1);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error generating market intelligence: " + std::string(e.what()));
        }
    }

    void publishMarketIntelligence(const trading::MarketIntelligence& intel) {
        try {
            // Create JSON message for market intelligence
            json intelMsg;
            intelMsg["type"] = "market_intelligence";
            intelMsg["symbol"] = intel.symbol();
            intelMsg["bid_ask_spread"] = intel.bid_ask_spread();
            intelMsg["liquidity_ratio"] = intel.liquidity_ratio();
            intelMsg["order_book_imbalance"] = intel.order_book_imbalance();
            intelMsg["volatility_estimate"] = intel.realized_volatility_1m();
            intelMsg["current_regime"] = intel.current_regime();
            intelMsg["regime_confidence"] = intel.regime_confidence();
            intelMsg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            // Publish via ZeroMQ
            std::string topic = "intelligence." + intel.symbol();
            std::string jsonStr = intelMsg.dump();
            std::vector<uint8_t> data(jsonStr.begin(), jsonStr.end());
            zmqPublisher_->publish(topic, data);
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error publishing market intelligence: " + std::string(e.what()));
        }
    }

    void onStatusUpdate(const std::string& component, const std::string& status) {
        static std::atomic<int> statusCount{0};
        if (statusCount.fetch_add(1) % 20 == 0) { // Print every 20th status
            LOG_DEBUG("[" + component + "] " + status);
        }
    }

    void printStatus() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
        
        std::cout << "\n=== DATA INGESTION PUBLISHER STATUS ===" << std::endl;
        std::cout << "Runtime: " << elapsed << "s" << std::endl;
        std::cout << "Service Health: " << (dataService_->isHealthy() ? "HEALTHY" : "UNHEALTHY") << std::endl;
        std::cout << "Order book updates processed: " << messagesProcessed_.load() << std::endl;
        std::cout << "Order books published: " << orderBooksPublished_.load() << std::endl;
        std::cout << "Intelligence generated: " << intelligenceGenerated_.load() << std::endl;
        
        if (elapsed > 0) {
            std::cout << "Update rate: " << (messagesProcessed_.load() / elapsed) << " updates/s" << std::endl;
            std::cout << "Publish rate: " << (orderBooksPublished_.load() / elapsed) << " publishes/s" << std::endl;
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

