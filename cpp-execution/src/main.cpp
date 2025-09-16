/**
 * @file full_system_integration_demo.cpp
 * @brief Complete trading system integration demonstration
 * 
 * This demo integrates all major components:
 * - Data Ingestion Service (order book data)
 * - Strategy Engine (signal generation)
 * - Risk Manager (validation and monitoring)
 * - Execution Engine (order placement)
 * - Monitoring (performance tracking)
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <fstream>
#include <signal.h>

// Common includes
#include "common/event_bus.hpp"
#include "common/trading_events.hpp"
#include "common/logger.hpp"
#include "common/zeromq_client.hpp"
#include "common/exchange_types.hpp"
// Data ingestion
#include "data_ingestion/data_ingestion_service.hpp"
//zmq server for market intellgence
#include "communication/zmq_server.hpp"
#include "market_intelligence/adaptive_intelligence.hpp"
#include "../../generated/cpp/proto/trading_messages.pb.h"
#include <nlohmann/json.hpp>

// Strategy engine
#include "strategy/engine/strategy_engine.hpp"
#include "strategy/config/strategy_config_loader.hpp"
//ptca
#include "learning/ptca_integration_manager.hpp"
#include "learning/pavlovian_credit_assignment.hpp"
#include "risk/interfaces/market_data_interface.hpp"

// Risk management
#include "risk/risk_manager.hpp"
#include "risk/risk_config.hpp"
#include "risk/risk_types.hpp"

// Execution engine
#include "execution/execution_service.hpp"
#include "execution/trading_signal.hpp"

// Core types
#include "core/market_types.hpp"
#include "core/direction.hpp"
#include "core/types.hpp"

using namespace trading;
using namespace std::chrono_literals;
using namespace trading::common;
using namespace trading::core;
using namespace trading::data_ingestion;
using namespace trading::strategy;
using namespace trading::risk;
using namespace trading::execution;

class FullSystemDemo {
public:
    FullSystemDemo() : running_(false), systemHealthy_(true) {
        setupConfigurations();
        setupStatistics();
                // Initialize adaptive intelligence
        adaptiveIntel_ = std::make_unique<trading::intelligence::AdaptiveMarketIntelligence>();
                
        // Initialize ZMQ communication
        zmqServer_ = std::make_unique<TradingCommunicationServer>();
        setupCallbacks();
    }
    
    ~FullSystemDemo() {
        shutdown();
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

     void setupCallbacks() {
       
        
        // Set up ZMQ execution callback
        zmqServer_->SetExecutionCallback([this](const trading::TradingIntent& intent) {
            return this->handleTradingIntent(intent);
        });
    }

    bool initialize() {
        // First, initialize the execution service
        setupExecutionEngine();
        // Debug: Verify execution service and router
        if (!executionService_) {
            std::cerr << "Execution Service failed to initialize!" << std::endl;
            return false;
        }
        std::cout << "=== INITIALIZING FULL TRADING SYSTEM ===" << std::endl;
        
        // 1. Initialize event bus first
        common::EventBus::getInstance().start();
        
        // 2. Initialize data ingestion service
        dataIngestionService_ = std::make_unique<data_ingestion::DataIngestionService>(
            "config/data_ingestion.json");

        // sleep for 2s
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!dataIngestionService_->initialize()) {
            std::cerr << "Failed to initialize data ingestion service" << std::endl;
            return false;
        }

               
        // ✅ NEW - Use the factory method
        riskManager_ = RiskManager::create(executionService_, riskConfig_);
        if (!riskManager_) {
            LOG_ERROR("Application", "Failed to initialize risk manager");
            return false;
        }
        if (!riskManager_->initialize()) {
            std::cerr << "Failed to initialize risk manager" << std::endl;
            return false;
        }
        
        // 4. Initialize strategy engine
        std::cout << "Initializing strategy engine..." << std::endl;
        strategyEngine_ = std::make_unique<strategy::StrategyEngine>(StrategyId("integrated_strategy_test_v1"));
        
        if (!strategyEngine_->initialize(strategyConfig_)) {
            std::cerr << "Failed to initialize strategy engine" << std::endl;
            return false;
        }
        //5. Initialize PTCA manager
        ptcaManager_->start();        
        lastPriceUpdate_ = std::chrono::steady_clock::now();
        // 6. Setup component connections
        setupComponentConnections();
        
        std::cout << "System initialization complete!" << std::endl;
        return true;
    }
    
    bool start() {
        if (!running_) {
            std::cout << "\n=== STARTING FULL TRADING SYSTEM ===" << std::endl;
            
            running_ = true;
            systemHealthy_ = true;
            startTime_ = std::chrono::steady_clock::now();
            zmqServer_->Start();
            
            //load symbols from strategy config
            std::vector<Symbol> symbols;
            for (const auto& [name, pair] : strategyConfig_.tradingPairs) {
                symbols.push_back(Symbol::fromString(name));
            } 
            // 1. Start data ingestion
            std::cout << "Starting data ingestion with symbols: ";
            for (const auto& symbol : symbols) {
                std::cout << symbol << " ";
            }
            std::cout << std::endl;
            
            if (!dataIngestionService_->start(symbols)) {
                std::cerr << "Failed to start data ingestion service" << std::endl;
                return false;
            }
            
            // 2. Start strategy engine
            if (!strategyEngine_->start()) {
                std::cerr << "Failed to start strategy engine" << std::endl;
                return false;
            }
            
            // 3. Start PTCA manager
            ptcaManager_->start();        
            lastPriceUpdate_ = std::chrono::steady_clock::now();
            
            std::cout << "🚀 FULL TRADING SYSTEM IS NOW LIVE!" << std::endl;
            std::cout << "Monitoring system health and processing..." << std::endl;
            
            return true;
        }
        return false;
    }
     
    // Or run without time limit:
    void runIndefinitely() {
        auto lastStatusTime = std::chrono::steady_clock::now();
        
        while (running_ && systemHealthy_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            auto now = std::chrono::steady_clock::now();
            
            // Print status every 30 seconds - simplified like full_bridge.cpp
            if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatusTime).count() >= 30) {
                printStatus();
                lastStatusTime = now;
            }
            
            if (!checkSystemHealth()) {
                std::cout << "⚠️ System health issues detected!" << std::endl;
                systemHealthy_ = false;
            }
        }
    } 
    void shutdown() {
        if (running_) {
            std::cout << "\n=== SHUTTING DOWN TRADING SYSTEM ===" << std::endl;
            
            running_ = false;
            
            if (ptcaManager_) {
                std::cout << "Saving final PTCA model..." << std::endl;
                ptcaManager_->saveModel("final_model.json");
                
                reportPTCAStats();
                
                ptcaManager_->stop();
                ptcaManager_.reset();
            }
            
            // Stop data ingestion FIRST and wait for cleanup
            if (dataIngestionService_) {
                std::cout << "Stopping data ingestion service..." << std::endl;
                dataIngestionService_->stop();
                
                // Give WebSocket connections time to close gracefully
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            }
            
            // Stop other services
            if (strategyEngine_) {
                std::cout << "Stopping strategy engine..." << std::endl;
                strategyEngine_->stop();
            }
            
            // Stop services in reverse order
            if (zmqServer_) {
                zmqServer_->Stop();
            }
             
            // // Clear data ingestion service pointer after shutdown
            // std::cout << "Clearing data ingestion service..." << std::endl;
            // dataIngestionService_.reset();
            
            // Stop event bus last
            std::cout << "Stopping event bus..." << std::endl;
            common::EventBus::getInstance().stop();
            
            std::cout << "System shutdown complete" << std::endl;
        }
    } 
    void evaluatePTCADecision(const core::Symbol& symbol) {
        // Get current signals for the symbol
        std::vector<strategy::TradingSignal> currentSignals = getCurrentSignalsForSymbol(symbol);
        
        if (!currentSignals.empty()) {
            auto decision = ptcaManager_->generateTradingDecision(symbol, currentSignals);
            
            std::cout << "PTCA Decision for " << symbol.toString() << ":" << std::endl;
            std::cout << "  Direction: " << core::directionToString(decision.recommendedDirection) << std::endl;
            std::cout << "  Confidence: " << decision.confidence << std::endl;
            std::cout << "  Expected Outcome: " << decision.expectedOutcome << std::endl;
            std::cout << "  Reasoning: " << decision.reasoning << std::endl;
            
        }
    }
        
    // Helper method to get current signals for a symbol (you'll need to implement this)
    std::vector<strategy::TradingSignal> getCurrentSignalsForSymbol(const core::Symbol& symbol) {
        // This should return current active signals for the given symbol
        // You'll need to implement this based on your signal storage
        std::vector<strategy::TradingSignal> signals;
        // Your implementation here...
        return signals;
    }
    void reportPTCAStats() {
        auto stats = ptcaManager_->getStats();
        
        std::cout << "=== PTCA Learning Statistics ===" << std::endl;
        std::cout << "Total signals tracked: " << stats.totalSignalsTracked << std::endl;
        std::cout << "Signals with outcomes: " << stats.signalsWithOutcomes << std::endl;
        std::cout << "Executed signals: " << stats.executedSignals << std::endl;
        std::cout << "Average signal P&L: " << (stats.averageSignalPnL * 100) << "%" << std::endl;
        std::cout << "Learning accuracy: " << (stats.learningAccuracy * 100) << "%" << std::endl;
        std::cout << "Currently tracked: " << stats.currentlyTrackedSignals << std::endl;
        std::cout << "PTCA events learned: " << stats.ptcaStats.totalEvents << std::endl;
        std::cout << "PTCA timesteps: " << stats.ptcaStats.totalTimesteps << std::endl;
        std::cout << "=================================" << std::endl;
    }

    void printStatus() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();

        std::cout << "\n=== FULL TRADING SYSTEM STATUS ===" << std::endl;
        std::cout << "Runtime: " << elapsed << "s" << std::endl;  //this will be always 0s
        std::cout << "Service Health: " << (dataIngestionService_->isHealthy() ? "HEALTHY" : "UNHEALTHY") << std::endl;
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
        
        std::cout << "=========================================" << std::endl;
    }
    

    void generateMarketIntelligence(const OrderBookUpdate& update, std::shared_ptr<OrderBook> orderBook) {
        try {
            if (!orderBook) {
                return; // No order book available
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
            
            // Throttle intelligence generation like full_bridge.cpp
            static std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastIntelTime;
            static std::unordered_map<std::string, int> intelDebugCounts;
            
            auto now = std::chrono::steady_clock::now();
            bool shouldGenerate = false;
            
            if (lastIntelTime.find(tradingPair) == lastIntelTime.end()) {
                shouldGenerate = true; // First time
            } else {
                auto timeSinceLastIntel = now - lastIntelTime[tradingPair];
                if (timeSinceLastIntel >= std::chrono::milliseconds(10)) { // Generate every 10 milliseconds
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
            
            // ✅ Direct ZMQ publishing like full_bridge.cpp
            if (zmqServer_ and !intel.symbol().empty()) {
                // Debug output for publishing
                static std::atomic<int> publishCount{0};
                int count = publishCount.fetch_add(1);
                if (count < 5 || count % 100 == 0) {
                    std::cout << "📢 Publishing intelligence #" << count
                              << " for " << tradingPair << std::endl;
                }   
                zmqServer_->PublishMarketIntelligence(intel);
                intelligenceGenerated_.fetch_add(1);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error generating market intelligence: " << e.what() << std::endl;
        }
    }

    // ✅ NEW: Publish orderbook data for risk module
    void publishOrderBookForRisk(const OrderBookUpdate& update) {
        try {
            // Check if zmqServer has orderbook publishing capability and update is valid
            if (update.bids.empty() && update.asks.empty()  ) {
                std::cerr << "Warning: Orderbook update is empty for "
                          << update.exchangeId.toString() << ":" << update.tradingPair.toString() << std::endl;
                return;
            }
            if (!zmqServer_) {
                std::cerr << "Warning: ZMQ server is not available for "
                          << update.exchangeId.toString() << ":" << update.tradingPair.toString() << std::endl;
                return;
            }

            // Create JSON message for orderbook (compatible with risk module)
            nlohmann::json orderBookMsg;
            orderBookMsg["exchange"] = update.exchangeId.toString();
            orderBookMsg["symbol"] = update.tradingPair.toString();
            orderBookMsg["sequence"] = update.sequenceNumber;
            orderBookMsg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                update.exchangeTimestamp.time_since_epoch()).count();
            
            // Add bids
            orderBookMsg["bids"] = nlohmann::json::array();
            for (const auto& bid : update.bids) {
                orderBookMsg["bids"].push_back({bid.price, bid.quantity});
            }
            
            // Add asks
            orderBookMsg["asks"] = nlohmann::json::array();
            for (const auto& ask : update.asks) {
                orderBookMsg["asks"].push_back({ask.price, ask.quantity});
            }
            
            std::string jsonStr = orderBookMsg.dump();
            
            // Publish with risk-compatible topic format
            static std::atomic<int> publishCount{0};
            int count = publishCount.fetch_add(1);
            
            if (count < 5 || count % 100 == 0) {
                std::cout << "📊 Publishing orderbook #" << count 
                          << " for " << update.exchangeId.toString() 
                          << ":" << update.tradingPair.toString() << std::endl;
            }
            
            if (jsonStr.size() > 10000) {
                std::cerr << "Warning: Orderbook message size is large (" 
                          << jsonStr.size() << " bytes)" << std::endl;
            }
            //check if orderbook is empty
            if (update.bids.empty() || update.asks.empty()) {
                std::cerr << "Warning: Orderbook is empty for "
                          << update.exchangeId.toString() << ":" << update.tradingPair.toString() << std::endl;
                return;
            }
            zmqServer_->PublishOrderBookData(
                update.exchangeId.toString(), 
                update.tradingPair.toString(), 
                jsonStr
            );
            
        } catch (const std::exception& e) {
            std::cerr << "Error publishing orderbook for risk: " << e.what() << std::endl;
        }
    }

    // ✅ Simple health check like full_bridge.cpp
    bool checkSystemHealth() {
        try {
            // Basic health checks
            if (!running_ || !systemHealthy_) {
                return false;
            }
            
            // Check if services are responsive
            bool dataIngestionHealthy = dataIngestionService_ ? dataIngestionService_->isHealthy() : false;
            bool strategyEngineHealthy = strategyEngine_ ? true : false;
            bool riskManagerHealthy = riskManager_ ? true : false;
            bool zmqHealthy = zmqServer_ ? true : false;
            
            bool overallHealthy = dataIngestionHealthy && strategyEngineHealthy && 
                                riskManagerHealthy && zmqHealthy;
            
            if (!overallHealthy) {
                std::cout << "⚠️ System health issues detected!" << std::endl;
                std::cout << "  Data Ingestion: " << (dataIngestionHealthy ? "✅" : "❌") << std::endl;
                std::cout << "  Strategy Engine: " << (strategyEngineHealthy ? "✅" : "❌") << std::endl;
                std::cout << "  Risk Manager: " << (riskManagerHealthy ? "✅" : "❌") << std::endl;
                std::cout << "  ZMQ Server: " << (zmqHealthy ? "✅" : "❌") << std::endl;
                
                systemHealthy_ = false;
                return false;
            }
            
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Error in system health check: " << e.what() << std::endl;
            return false;
        }
    }
private:
    //market intel
    std::atomic<uint64_t> messagesProcessed_{0};
    std::atomic<uint64_t> intelligenceGenerated_{0};

    // PTCA: Add PTCA-related members
    std::unique_ptr<trading::learning::PTCAIntegrationManager> ptcaManager_;
    std::chrono::time_point<std::chrono::steady_clock> lastPriceUpdate_;
    // std::unique_ptr<DataInterface> m_dataInterface;
    std::unique_ptr<TradingCommunicationServer> zmqServer_;
    std::unique_ptr<trading::intelligence::AdaptiveMarketIntelligence> adaptiveIntel_;
    std::mutex intelligenceMutex_;

    // Market intelligence tracking
    std::unordered_map<std::string, trading::MarketIntelligence> latestIntelligence_;

    // Core components
    std::unique_ptr<data_ingestion::DataIngestionService> dataIngestionService_;
    std::unique_ptr<strategy::StrategyEngine> strategyEngine_;
    std::shared_ptr<risk::RiskManager> riskManager_;
    std::shared_ptr<execution::ExecutionService> executionService_;
    // gateway
    std::shared_ptr<common::ExchangeGateway> exchangeGateway_;

    // Configurations
    risk::RiskConfig riskConfig_;
    strategy::StrategyConfig strategyConfig_;
    // System state
    std::atomic<bool> running_;
    std::atomic<bool> systemHealthy_;
    std::chrono::steady_clock::time_point startTime_;
    
    // Monitoring threads
    std::vector<std::thread> monitoringThreads_;
    
    // Statistics
    std::atomic<size_t> orderBookUpdatesReceived_{0};
    std::atomic<size_t> signalsGenerated_{0};
    std::atomic<size_t> signalsValidated_{0};
    std::atomic<size_t> signalsApproved_{0};
    std::atomic<size_t> signalsRejected_{0};
    std::atomic<size_t> ordersExecuted_{0};
    std::atomic<size_t> riskEventsTriggered_{0};
    
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

    void setupConfigurations() {
        // Create configuration files
        createDataIngestionConfig();
        createStrategyConfig();
        createRiskConfig();                
        createPTCAConfig();
    }
    
    void createDataIngestionConfig() {
        // what is the point of this function?
        //no this file was never used
        const char* configContent = R"({
    "exchanges": {
        "BINANCE": {
            "id": "BINANCE",
            "name": "Binance",
            "ws_url": "wss://stream.binance.com:9443/ws",
            "rest_url": "https://api.binance.com",
            "symbols": ["ENJ/USDT", "ETH/USDT", "BNB/USDT", "ADA/USDT"],
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
        
        std::ofstream configFile("system_config.json");
        configFile << configContent;
        configFile.close();
    }
    
    void createStrategyConfig() {
      
        std::string configPath = "config/strategy_config.json";
        strategyConfig_ = loadStrategyConfigFromFile(configPath);
            //  std::map<std::string, core::ExchangeId> tradingPairs;
        for (const auto& [name, pair] : strategyConfig_.tradingPairs) {
            std::cout << "Configured trading pair: " << name 
                      << " on exchange " << pair.toString() << std::endl;            
        }
    }

    void onMarketPriceUpdate(const core::Symbol& symbol, double price) {
        ptcaManager_->updateMarketPrice(symbol, price);
    }
        // Add this to your FullSystemDemo class:
    void setupPTCAConnections() {
      
        
        // 2. Execution callback - use executionService_ directly
        if (executionService_) {
            executionService_->registerExecutionCallback([this](const ExecutionResponse& response) {
                onExecutionResponse(response);
            });
        }
    }
    
    void onExecutionResponse(const execution::ExecutionResponse& response) {
        std::cout << "📈 Execution: " << response.client_order_id
                << " Status: " << response.order_status  << std::endl;

        ordersExecuted_++;
        
        // Register with PTCA if needed
        if (ptcaManager_) {
            // Convert ExecutionResponse to ExecutionReport format if needed
            // ptcaManager_->registerExecution(response.order_id, response);
        }
    }

    
    //create ptca config
    void createPTCAConfig() {
        trading::learning::PTCAIntegrationConfig ptcaConfig;        
        // Configure PTCA for your trading environment
        ptcaConfig.outcomeEvaluationDelay = std::chrono::minutes(2);  // Evaluate outcomes after 2 minutes
        ptcaConfig.signalTrackingWindow = std::chrono::minutes(15);   // Track signals for 15 minutes
        ptcaConfig.minimumPnLThreshold = 0.0005;                     // 0.05% minimum P&L to learn from
        ptcaConfig.priceMovementThreshold = 0.003;                   // 0.3% price movement significance
        ptcaConfig.pnlSuccessThreshold = 0.008;                      // 0.8% P&L success threshold
        ptcaConfig.strongOutcomeThreshold = 0.02;                    // 2% strong outcome threshold
        
        // Set up model persistence
        ptcaConfig.modelSaveDirectory = "models/ptca/";
        ptcaConfig.modelSaveInterval = std::chrono::minutes(20);
        ptcaConfig.enableContinuousLearning = true;
        ptcaConfig.enableDetailedLogging = true;
        
        // Performance limits
        ptcaConfig.maxSignalsToTrack = 500;  // Limit memory usage
        
        ptcaManager_ = std::make_unique<trading::learning::PTCAIntegrationManager>(ptcaConfig);

    }
    void createRiskConfig() {
        // Setup risk configuration (similar to risk demo)
        // load config from file
        std::string configPath = "config/risk_config.json";
        riskConfig_.loadFromFile(configPath);
        if (!riskConfig_.validate()) {
            std::cerr << "Invalid risk configuration!" << std::endl;
        }
        auto& positionConfig = const_cast<PositionLimitConfig&>(riskConfig_.getPositionLimitConfig());
        positionConfig.assetLimits["BTC"] = 0.0001;
        positionConfig.assetLimits["ETH"] = 0.1;
        positionConfig.assetLimits["BNB"] = 0.1;
        positionConfig.assetLimits["ADA"] = 10.0;
        positionConfig.defaultLimit = 0.1;
        positionConfig.enforceStrictly = true;
        
        auto& exposureConfig = const_cast<ExposureLimitConfig&>(riskConfig_.getExposureLimitConfig());
        exposureConfig.maxTotalExposure = 100.0;
        exposureConfig.maxAssetExposure = 50.0;
        exposureConfig.maxStrategyExposure = 750.0;
        exposureConfig.maxExchangeExposure = 800.0;
    }
    
    void setupStatistics() {
        // Reset all counters
        orderBookUpdatesReceived_ = 0;
        signalsGenerated_ = 0;
        signalsValidated_ = 0;
        signalsApproved_ = 0;
        signalsRejected_ = 0;
        ordersExecuted_ = 0;
        riskEventsTriggered_ = 0;
    }
    
    void setupExecutionEngine() {
        std::cout << "=== SETTING UP EXECUTION ENGINE ===" << std::endl;
        // Create the authentication service first
        auto authService = std::make_shared<AuthenticationService>();
        
        // Create API client manager with auth service
        auto api_client_manager = std::make_shared<ApiClientManager>(authService);
        
        // Set up credentials and clients (your existing code)
        //get from environment .env file
        const char* apiKey = std::getenv("BINANCE_API_KEY");
        const char* apiSecret = std::getenv("BINANCE_API_SECRET");
        
        if(!apiKey || !apiSecret) {
            std::cerr << "Error: Missing Binance API credentials in environment variables." << std::endl;
            systemHealthy_ = false;
            return;
        }
        
        auto credentials = ExchangeCredentials{
            apiKey,
            apiSecret,
            "", 
            true
        };
        
        authService->setCredentials(core::ExchangeId::binance(), credentials);
        api_client_manager->addClient(core::ExchangeId::binance(),
            std::make_shared<BinanceApiClient>(credentials, 
            std::make_shared<RateLimitController>(core::ExchangeId::binance())));
                std::cout << "=== DEBUG API CLIENT MANAGER ===" << std::endl;
        std::cout << "ApiClientManager created, checking status..." << std::endl;
        if (!api_client_manager->initialize()) {
            std::cerr << "Failed to initialize ApiClientManager" << std::endl;
            systemHealthy_ = false;
            return;
        }
        // Create other components
        auto order_generator = std::make_shared<OrderGenerator>();
        auto smart_order_router = std::make_shared<SmartOrderRouter>();
        auto order_type_selector = std::make_shared<OrderTypeSelector>();
        auto execution_algorithm_factory = std::make_shared<ExecutionAlgorithmFactory>();
        auto order_lifecycle_tracker = std::make_shared<OrderLifecycleTracker>();
        auto fill_reconciliation = std::make_shared<FillReconciliation>();

        // **CRITICAL: Initialize the smart order router with exchange information**
        std::vector<ExchangeInfo> exchanges;
        ExchangeInfo binanceInfo;
        binanceInfo.exchangeId = core::ExchangeId::binance();
        binanceInfo.trading_fee = 0.001;
        binanceInfo.liquidity_score = 0.9;
        binanceInfo.latency_ms = 50;
        binanceInfo.success_rate = 0.99;
        exchanges.push_back(binanceInfo);
        
        // After initializing the router, add this debug check:
        smart_order_router->initialize(exchanges);

        exchangeGateway_ = std::make_shared<common::ExchangeGateway>(api_client_manager);
        if (!exchangeGateway_->isHealthy()) {
            std::cout << "Exchange Gateway is not healthy!" << std::endl;
            std::cout << "start using fallback ExecutionService" << std::endl;
            executionService_ = std::make_shared<ExecutionService>(
            order_generator,
            smart_order_router,
            order_type_selector,
            execution_algorithm_factory,
            order_lifecycle_tracker,
            fill_reconciliation,
            api_client_manager
            );
        } else {
            std::cout << "Exchange Gateway initialized and healthy." << std::endl;
            executionService_ = std::make_shared<ExecutionService>(
            exchangeGateway_,
            order_generator,
            smart_order_router,
            order_type_selector,
            execution_algorithm_factory,
            order_lifecycle_tracker,
            fill_reconciliation
            );
        }       
       
    }
    void setupComponentConnections() {        
        
            // ✅ UNCOMMENT AND USE THIS:
        dataIngestionService_->setOrderBookCallback([this](const auto& update) {
            static std::atomic<int> callbackCount{0};
            int count = callbackCount.fetch_add(1);
            
            if (count < 5 || count % 500 == 0) {
                std::cout << "📊 Order book callback #" << count 
                          << " for " << update.tradingPair.toString() 
                          << " (seq: " << update.sequenceNumber << ")" << std::endl;
            }
            
            // DEFENSIVE FILTER: Only process configured symbols
            std::string symbolStr = update.tradingPair.toString();
            auto it = strategyConfig_.tradingPairs.find(symbolStr);
            
            if (it == strategyConfig_.tradingPairs.end()) {
                if (count % 100 == 0) {
                    LOG_WARN(std::format("FullSystemDemo", "Received update for unconfigured symbol: " + symbolStr + " - IGNORING"));
                }
                return;  // Skip this update
            }
            
            // Extract price for PTCA
            if (!update.bids.empty() && !update.asks.empty()) {
                double midPrice = (update.bids[0].price + update.asks[0].price) / 2.0;
                // Rate-limited PTCA update
                static std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastPtcaUpdate;
                auto now = std::chrono::steady_clock::now();
                std::string symbolStr = update.tradingPair.toString();
                if (now - lastPtcaUpdate[symbolStr] >= std::chrono::seconds(1)) {
                    onMarketPriceUpdate(update.tradingPair, midPrice);
                    lastPtcaUpdate[symbolStr] = now;
                }
            }
           
            // Get the existing order book and send to strategy
            auto orderBook = dataIngestionService_->getOrderBook(update.exchangeId, update.tradingPair);            

            if(orderBook) {
                // Throttle generateMarketIntelligence and publishOrderBookForRisk per symbol
                static std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastIntelGen;
                static std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastOrderbookPub;
                auto now = std::chrono::steady_clock::now();
                std::string symbolStr = update.tradingPair.toString();

                // Generate market intelligence at most every 53ms per symbol
                if (adaptiveIntel_ && (now - lastIntelGen[symbolStr] >= std::chrono::milliseconds(53))) {
                    generateMarketIntelligence(update, orderBook);
                    lastIntelGen[symbolStr] = now;
                }
                // Publish orderbook for risk at most every 107ms per symbol (adjust as needed)
                else if (zmqServer_ && (now - lastOrderbookPub[symbolStr] >= std::chrono::milliseconds(107))) {
                    publishOrderBookForRisk(update);
                    lastOrderbookPub[symbolStr] = now;
                }
              
                // Update the order book state
                orderBook->applySnapshot(update.bids, update.asks, 
                                    update.sequenceNumber, update.exchangeTimestamp);
                

                if (strategyEngine_) {
                    strategyEngine_->processOrderBook(*orderBook);
                }
            }
            
        });
            
       
            // 2. Connect strategy engine to risk manager
        strategyEngine_->registerSignalCallback([this](const strategy::TradingSignal& signal) {
            signalsGenerated_++;
            ptcaManager_->registerSignal(signal);
            //print signal details
            // std::cout << "PTCA tracking signals: " << ptcaManager_->getStats().currentlyTrackedSignals << std::endl;
            double ptcaRecommendation = ptcaManager_->getSignalRecommendation(signal);
             // PTCA: Optionally, adjust signal strength based on PTCA confidence
            double adjustedStrength = signal.strength;
            if (ptcaRecommendation > 0.2) {
                // Boost signals that PTCA likes
                adjustedStrength = std::min(1.0, signal.strength * (1.0 + ptcaRecommendation * 0.3));
                std::cout << "PTCA Boost: Signal " << signal.id 
                        << " strength boosted from " << signal.strength 
                        << " to " << adjustedStrength << std::endl;
            } else if (ptcaRecommendation < -0.1) {
                // Reduce strength for signals PTCA is uncertain about
                adjustedStrength = signal.strength * (1.0 + ptcaRecommendation * 0.2);
                std::cout << "PTCA Caution: Signal " << signal.id 
                        << " strength reduced from " << signal.strength 
                        << " to " << adjustedStrength << std::endl;
            }
            // Convert strategy signal to execution signal for risk validation
            execution::TradingSignal execSignal;
            execSignal.id = signal.id;
            execSignal.symbol = signal.symbol;
            execSignal.direction = signal.direction;
            execSignal.strength = signal.strength;
            execSignal.targetSize = signal.targetSize;
            execSignal.strategyId = signal.strategyId;
            //get mid price from order book
          
            execSignal.confidence = signal.confidence;
            execSignal.quality = signal.quality;
            execSignal.expirationTime = signal.expirationTime;
            execSignal.targets = signal.targets;
            execSignal.exchangeId = signal.exchangeId;
            
            // Basic filtering
                    //create a asymmetric filter for buy and sell signals
            if (execSignal.direction == core::Direction::BUY) {
                if (execSignal.strength < 0.5) return;     // 30% threshold
                if (execSignal.confidence < 0.6) return;  // 30% threshold  
                // if (execSignal.quality < 0.44) return;     // 30% threshold
            } else if (execSignal.direction == core::Direction::SELL) {
                if (execSignal.strength < 0.3) return;    // 20% threshold
                if (execSignal.confidence < 0.6) return;  // 20% threshold
                // if (execSignal.quality < 0.3) return;     // 20% threshold
            }
            
            double percentage_change_threshold = 0.002; // 0.2%
            int targetPass = 0;
            //     bool allTargetsTooClose = true;
            for (const auto& target : execSignal.targets) {
                // std::cout << "  - Target Price: " << target.price 
                //           << " | Percentage Change: " << target.percentage * 100 << "%"
                //           << std::endl;
                //check for percentage of long and short targets
                if (target.percentage > percentage_change_threshold) {
                    targetPass++;
                }                
            }
            if (targetPass == 0) return; // Ignore signals with all targets too close
            else{
                std::cout << "Generated Signal: " << execSignal.id 
                          << " Symbol: " << execSignal.symbol.toString()
                          << " Direction: " << (execSignal.direction == core::Direction::BUY ? "BUY" : 
                                              execSignal.direction == core::Direction::SELL ? "SELL" : "NEUTRAL")
                          << " Strength: " << execSignal.strength
                          << " Confidence: " << execSignal.confidence
                          << " Quality: " << execSignal.quality
                          << " Target Size: " << execSignal.targetSize
                          << " Expiration: " << execSignal.expirationTime
                          << " Valid Targets: " << targetPass
                          << std::endl;
            }
            // Validate with risk manager
            if (riskManager_) {
                auto validationResult = riskManager_->validateSignal(execSignal);
                signalsValidated_++;
                
                if (validationResult.approved) {
                    signalsApproved_++;
                    std::cout << "✅ Signal " << execSignal.id << " approved for execution" << std::endl;

                    // Here we would normally send to execution engine
                    realExecution(execSignal);
                } else {
                    signalsRejected_++;
                    std::cout << "❌ Signal " << execSignal.id << " rejected by risk management" << std::endl;
                }
            }
        });
        
        // 3. Connect risk manager events
        riskManager_->registerRiskEventCallback([this](const risk::RiskEvent& event) {
            riskEventsTriggered_++;
            std::cout << "🚨 Risk Event: " << event.description 
                      << " (Severity: " << event.severity << ")" << std::endl;
        });
        setupPTCAConnections();
    }
    
    void realExecution(const execution::TradingSignal& signal) {
        ordersExecuted_++;
        
        // Simulate execution delay
        std::thread([this, signal]() {
            std::this_thread::sleep_for(100ms);
            ExecutionResponse response = executionService_->executeSignal(signal);
            if (!response.success) {
                std::cout << "❌ Execution failed for signal " << signal.id 
                          << ": " << response.error_message << std::endl;
                return;
            }
            // Create mock execution report
            execution::models::ExecutionReport report;

            report.setOrderId("ORDER_" + signal.id);
            report.setSymbol(signal.symbol);
            report.setSide(signal.direction == core::Direction::BUY ? 
                          execution::models::OrderSide::BUY : 
                          execution::models::OrderSide::SELL);
            report.setQuantity(signal.targetSize);
            report.setFilledQuantity(signal.targetSize);
            report.setStatus(execution::models::OrderState::FILLED);
            
            // Send back to risk manager for position tracking
            if (riskManager_) {
                riskManager_->processExecutionReport(report);
            }

            std::cout << "📈 Executed: " << signal.symbol.toString()
                      << " " << (signal.direction == core::Direction::BUY ? "BUY" : "SELL")
                      << " " << signal.targetSize << std::endl;
        }).detach();
    }
    
          
};

// Global demo instance for signal handling
std::unique_ptr<FullSystemDemo> demoInstance;

void signalHandler(int sig) {
    std::cout << "\nShutdown signal received..." << std::endl;
    if (demoInstance) {
        demoInstance->shutdown();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    try {
        std::cout << "🚀 STARTING FULL TRADING SYSTEM INTEGRATION DEMO 🚀" << std::endl;
        std::cout << "=====================================================" << std::endl;
        
        // Create demo instance
        demoInstance = std::make_unique<FullSystemDemo>();
        
        // Initialize system
        if (!demoInstance->initialize()) {
            std::cerr << "Failed to initialize trading system" << std::endl;
            return 1;
        }
        // PTCA: Set up periodic statistics reporting
        std::thread statsThread([demoInstance = demoInstance.get()]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::minutes(10));
                if (demoInstance) {
                    demoInstance->reportPTCAStats();
                }
            }
        });
        // Start system
        if (!demoInstance->start()) {
            std::cerr << "Failed to start trading system" << std::endl;
            return 1;
        }
        
        // Run demo
        demoInstance->runIndefinitely();

        // Cleanup
        demoInstance->shutdown();
        demoInstance.reset();
        
        if (statsThread.joinable()) {
            statsThread.detach(); // Let it finish naturally
        }
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        if (demoInstance) {
            demoInstance->shutdown();
        }
        return 1;
    }
    
    return 0;
}