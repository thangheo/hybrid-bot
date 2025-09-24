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
#include <unordered_map>
#include <deque>
#include <condition_variable>
#include <mutex>

// Common includes
#include "common/event_bus.hpp"
#include "common/trading_events.hpp"
#include "common/logger.hpp"
#include "communication/zmq_server.hpp"
#include "common/exchange_types.hpp"
// Data ingestion
#include "data_ingestion/data_ingestion_service.hpp"
//zmq server for market intellgence
// #include "communication/zmq_server.hpp"
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
// Position sizer
#include "risk/position_sizer.hpp"
#include <algorithm>

// Execution engine
#include "execution/execution_service.hpp"
#include "execution/trading_signal.hpp"
#include "execution/filter/signal_dedup.hpp"
// Core types
#include "core/market_types.hpp"
#include "core/direction.hpp"
#include "core/types.hpp"

// zmq clients
#include "communication/zmq_config.hpp"

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
        ZmqServerConfig serverConfig;
        //use default config for now
        zmq_server_ = std::make_shared<GenericZmqServer>(serverConfig);
        zmq_server_->registerRequestHandler("ping", [](const std::string& request, 
                                                const std::unordered_map<std::string, std::string>& metadata) -> std::string {
            nlohmann::json response;
            response["status"] = "pong";
            response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            return response.dump();
        });
       
        ZmqClientConfig clientConfig;
        // clientConfig.server_pub_endpoint = "tcp://localhost:5557"; // Address of the ZMQ server
        // clientConfig.auto_reconnect = true;
        // clientConfig.request_timeout = std::chrono::milliseconds(30000); // 30 seconds
        zmq_client_ = std::make_unique<GenericZmqClient>(clientConfig);
       
        // Initialize MarketDataInterface
        riskConfig_.zmqSubscriberEndpoint = SERVER_INTERNAL_ENDPOINT;
        riskConfig_.zmqPublisherEndpoint = SERVER_REQUEST_ENDPOINT;
        marketDataInterface_ = std::make_shared<trading::risk::MarketDataInterface>(riskConfig_);
        // set zmq client to marketDataInterface_
        marketDataInterface_->setZmqClient(std::move(zmq_client_));
    // Initialize position sizer with default config for now
    positionSizer_ = std::make_unique<trading::risk::PositionSizer>();
       
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

    // }

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
        //set comm server
        dataIngestionService_->setCommServer(zmq_server_);
        // sleep for 2s
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!dataIngestionService_->initialize()) {
            std::cerr << "Failed to initialize data ingestion service" << std::endl;
            return false;
        }
        // 3. Initialize market data interface
        if (!marketDataInterface_->initialize()) {
            std::cerr << "Failed to initialize MarketDataInterface" << std::endl;
            return false;
        }
        // 3. Initialize risk manager
               
        // ✅ NEW - Use the factory method
        riskManager_ = RiskManager::create(executionService_, riskConfig_, marketDataInterface_);
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

            // if (!zmq_server_->start()) {
            //     std::cerr << "Failed to start internal ZMQ server: " << zmq_server_->getLastError() << std::endl;
            //     return -1;
            // }
            
            if (!zmq_server_->start()) {
                std::cerr << "Failed to start internal ZMQ server: " << zmq_server_->getLastError() << std::endl;
                return -1;
            }

            // CRITICAL DEBUG - Check if threads are actually running
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

            // Try to trigger the diagnostic logs by checking queue state
            std::cout << "=== THREAD DEBUG ===" << std::endl;
            std::cout << "Intel queue size: " << zmq_server_->getQueueSize("intel") << std::endl;
            std::cout << "Internal queue size: " << zmq_server_->getQueueSize("internal") << std::endl;
            std::cout << "Health check: " << (zmq_server_->healthCheck() ? "HEALTHY" : "UNHEALTHY") << std::endl;

            //load symbols from strategy config
            std::vector<Symbol> symbols;
            for (const auto& [name, pair] : strategyConfig_.tradingPairs) {
                symbols.push_back(name);
            } 
            // 1. Start data ingestion
            std::cout << "Starting data ingestion with symbols: ";
            for (const auto& symbol : symbols) {
                std::cout << symbol << " ";
            }
            std::cout << std::endl;
            //order book depth from config
            int depth = 20;//strategyConfig_.orderBookDepth;
           
            if (!dataIngestionService_->start(symbols, depth)) {
                std::cerr << "Failed to start data ingestion service" << std::endl;
                return false;
            }
            // Start orderbook worker pool after data ingestion starts
            startOrderbookWorkerPool();
            
            // 2. Start strategy engine
            if (!strategyEngine_->start()) {
                std::cerr << "Failed to start strategy engine" << std::endl;
                return false;
            }

            // Start signal worker pool
            startSignalWorkerPool();
            
            // 3. Start PTCA manager
            ptcaManager_->start();        
            lastPriceUpdate_ = std::chrono::steady_clock::now();
            // Start exec worker pool
            startExecWorkerPool();
            
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

            // Stop worker pool
            stopOrderbookWorkerPool();
            stopSignalWorkerPool();
            stopExecWorkerPool();
            
            // Stop other services
            if (strategyEngine_) {
                std::cout << "Stopping strategy engine..." << std::endl;
                strategyEngine_->stop();
            }
            
            // Stop services in reverse order
            if (zmq_server_) {
                zmq_server_->stop();
            }
            // if (riskManager_) {
            //     std::cout << "Stopping risk manager..." << std::endl;
            //     // riskManager_->
            //     riskManager_.reset();
            // }
            // if (executionService_) {
            //     std::cout << "Stopping execution service..." << std::endl;
            //     executionService_->stop();
            //     executionService_.reset();
            // }
             
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
    std::cout << "Order book updates received: " << messagesProcessed_.load() << std::endl;
    std::cout << "Order book updates processed: " << orderBookUpdatesReceived_.load() << std::endl;
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
        
        // Print publish stats if available
        uint64_t calls = publishCalls_.load();
        if (calls > 0) {
            double avg_ns = static_cast<double>(publishTimeNs_.load()) / static_cast<double>(calls);
            double avg_ms = avg_ns / 1e6;
            std::cout << "Avg publish latency: " << std::fixed << std::setprecision(3) << avg_ms << " ms over " << calls << " calls" << std::endl;
        }
        // Print process RSS to detect memory growth
        try {
            FILE* f = fopen("/proc/self/statm", "r");
            if (f) {
                long size = 0, resident = 0;
                if (fscanf(f, "%ld %ld", &size, &resident) >= 2) {
                    long pageSizeKb = sysconf(_SC_PAGESIZE) / 1024; // in KB
                    long rssKb = resident * pageSizeKb;
                    std::cout << "RSS: " << (rssKb / 1024) << " MB" << std::endl;
                }
                fclose(f);
            }
        } catch (...) {
            // ignore
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
            
            // Throttle intelligence generation (use instance maps to avoid data races)
            auto now = std::chrono::steady_clock::now();
            bool shouldGenerate = false;
            {
                std::lock_guard<std::mutex> lm(intelMapsMutex_);
                if (lastIntelTime_.find(tradingPair) == lastIntelTime_.end()) {
                    shouldGenerate = true; // First time
                } else {
                    auto timeSinceLastIntel = now - lastIntelTime_[tradingPair];
                    if (timeSinceLastIntel >= std::chrono::milliseconds(500)) { // Generate every 500 ms
                        shouldGenerate = true;
                    }
                }
                if (!shouldGenerate) return;
                lastIntelTime_[tradingPair] = now;
            }

            trading::MarketIntelligence intel = adaptiveIntel_->GenerateAdaptiveMarketIntelligence(tradingPair, orderBook);

            if (intel.symbol().empty()) {
                return; // No valid intelligence generated
            }

            // Debug output for intelligence generations
            {
                std::lock_guard<std::mutex> lm(intelMapsMutex_);
                intelDebugCounts_[tradingPair]++;
                if (intelDebugCounts_[tradingPair] <= 1 || intelDebugCounts_[tradingPair] % 5000 == 0) {
                    std::cout << "🧠 Generated intelligence #" << intelDebugCounts_[tradingPair]
                              << " for " << tradingPair << " (spread: " << intel.bid_ask_spread()
                              << ", imbalance: " << intel.order_book_imbalance() << ")" << std::endl;
                }
            }
            
            // Store latest intelligence
            {
                std::lock_guard<std::mutex> lock(intelligenceMutex_);
                latestIntelligence_[tradingPair] = intel;
            }
            
            // ✅ Direct ZMQ publishing like full_bridge.cpp
            if (zmq_server_ && !intel.symbol().empty()) {
                // Debug output for publishing
                static std::atomic<int> publishCount{0};
                int count = publishCount.fetch_add(1);
                if (count < 5 || count % 100 == 0) {
                    std::cout << "📢 Publishing intelligence #" << count
                              << " for " << tradingPair << std::endl;
                }   
                if (intel.DebugString().size() > 1000) {
                    std::cerr << "Warning: Intelligence message size is large ("
                              << intel.DebugString().size() << " bytes)" << std::endl;
                }
                if (zmq_server_) {
                    // Serialize protobuf safely into a std::string (SerializeToString returns bool)
                    std::string serialized_intel;
                    if (!intel.SerializeToString(&serialized_intel)) {
                        std::cerr << "Failed to serialize MarketIntelligence protobuf" << std::endl;
                    } else {
                        // Backpressure: check queue size for topic and skip if too large
                        std::string topic = "MARKET_INTEL." + tradingPair;
                        bool publishAllowed = true;
                        try {
                            size_t qsize = zmq_server_->getQueueSize(topic);
                            if (qsize > 1000) { // arbitrary high watermark
                                publishAllowed = false;
                                std::cerr << "Backpressure: ZMQ queue for " << topic << " size=" << qsize
                                          << ", skipping publish to avoid memory growth" << std::endl;
                            }
                        } catch (...) {
                            // If getQueueSize not implemented or fails, allow publish
                        }

                        if (publishAllowed) {
                            // Use publishAsync to avoid blocking the intelligence generation path.
                            // Move the serialized string to avoid an extra copy.
                            zmq_server_->publishAsync("MARKET_INTEL", std::move(serialized_intel), MessagePriority::NORMAL);
                            // zmq_server_->publishProtobuf("MARKET_INTEL", intel, MessagePriority::NORMAL);
                            intelligenceGenerated_.fetch_add(1);
                        }
                    }
                }
                else {
                    std::cerr << "ZMQ server not available for publishing intelligence" << std::endl;
                    // Handle case where ZMQ server is not available
                }
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
            
            // Create JSON message for orderbook (compatible with risk module)
            nlohmann::json orderBookMsg;
            orderBookMsg["exchange"] = update.exchangeId.toString();
            orderBookMsg["symbol"] = update.tradingPair.toString();
            orderBookMsg["sequence"] = update.sequenceNumber;
            orderBookMsg["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                update.exchangeTimestamp.time_since_epoch()).count();
                        
            // Best bid/ask
            orderBookMsg["bid"] = !update.bids.empty() ? update.bids[0].price : 0.0;
            orderBookMsg["ask"] = !update.asks.empty() ? update.asks[0].price : 0.0;
                        
            // Mid price
            orderBookMsg["price"] = (!update.bids.empty() && !update.asks.empty())
                ? (update.bids[0].price + update.asks[0].price) / 2.0 : 0.0;

            // Volume at top 5 levels
            double bidVolume = 0.0, askVolume = 0.0;
            for (int i = 0; i < std::min(5, (int)update.bids.size()); ++i) bidVolume += update.bids[i].quantity;
            for (int i = 0; i < std::min(5, (int)update.asks.size()); ++i) askVolume += update.asks[i].quantity;
            orderBookMsg["volume"] = bidVolume + askVolume;

            // Add bids/asks arrays
            orderBookMsg["bids"] = nlohmann::json::array();
            for (const auto& bid : update.bids) orderBookMsg["bids"].push_back({bid.price, bid.quantity});
            orderBookMsg["asks"] = nlohmann::json::array();
            for (const auto& ask : update.asks) orderBookMsg["asks"].push_back({ask.price, ask.quantity});

            // Placeholders for 24h stats and last trade
            orderBookMsg["lastTradePrice"] = orderBookMsg["price"];
            orderBookMsg["lastTradeQuantity"] = 0.0;
            orderBookMsg["open24h"] = orderBookMsg["price"];
            orderBookMsg["high24h"] = orderBookMsg["price"];
            orderBookMsg["low24h"] = orderBookMsg["price"];
            orderBookMsg["volumeQuote24h"] = 0.0;
            orderBookMsg["priceChange24h"] = 0.0;
            orderBookMsg["priceChangePercent24h"] = 0.0;
            // Optionally: orderBookMsg["extra"] = nlohmann::json::object();

            std::string jsonStr = orderBookMsg.dump();
            std::string topic = "orderbook." + update.exchangeId.toString() + "." + update.tradingPair.toString();
            // Move the JSON string into publishAsync to avoid an extra copy.
            zmq_server_->publishAsync(topic, std::move(jsonStr), MessagePriority::HIGH);
            orderBookUpdatesReceived_.fetch_add(1);
            //print debug info for every 5000th
            static std::atomic<int> publishCount{0};
            int count = publishCount.fetch_add(1);
            if (count < 5 || count % 5000 == 0) {
                std::cout << "📢 Published orderbook #" << count
                          << " for " << update.exchangeId.toString()
                          << ":" << update.tradingPair.toString() << std::endl;
            }
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
            bool zmqHealthy = zmq_server_ ? zmq_server_->isRunning() : false;

            bool overallHealthy = dataIngestionHealthy && strategyEngineHealthy &&
                                riskManagerHealthy && zmqHealthy ;

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
    // Orderbook processing queue and worker pool
    std::deque<OrderBookUpdate> orderbookQueue_;
    std::mutex orderbookQueueMutex_;
    std::condition_variable orderbookQueueCv_;
    std::vector<std::thread> orderbookWorkers_;
    std::atomic<bool> orderbookWorkersRunning_{false};
    size_t orderbookQueueLimit_ = 2000; // hard cap
    size_t orderbookBatchSize_ = 20; // number of updates per batch
    size_t orderbookWorkerCount_ = 2; // start with 2 workers (tune for VPS)

    // Signal processing queue and worker pool
    std::deque<strategy::TradingSignal> signalQueue_;
    std::mutex signalQueueMutex_;
    std::condition_variable signalQueueCv_;
    std::vector<std::thread> signalWorkers_;
    std::atomic<bool> signalWorkersRunning_{false};
    size_t signalQueueLimit_ = 10000; // cap signals
    size_t signalBatchSize_ = 16; // number of signals per batch
    size_t signalWorkerCount_ = 2; // number of workers for signals

    // Execution task queue and worker pool (avoid spawning detached threads)
    std::deque<execution::TradingSignal> execQueue_;
    std::mutex execQueueMutex_;
    std::condition_variable execQueueCv_;
    std::vector<std::thread> execWorkers_;
    std::atomic<bool> execWorkersRunning_{false};
    size_t execQueueLimit_ = 5000;
    size_t execWorkerCount_ = 2; // tune for VPS

    //market intel
    std::atomic<uint64_t> messagesProcessed_{0};
    std::atomic<uint64_t> intelligenceGenerated_{0};
    // marketDataInterface_
    std::shared_ptr<trading::risk::MarketDataInterface> marketDataInterface_;
    // PTCA: Add PTCA-related members
    std::unique_ptr<trading::learning::PTCAIntegrationManager> ptcaManager_;
    std::chrono::time_point<std::chrono::steady_clock> lastPriceUpdate_;
    // std::unique_ptr<DataInterface> m_dataInterface;
    
    std::shared_ptr<GenericZmqServer> zmq_server_;
    std::unique_ptr<GenericZmqClient> zmq_client_;

    std::unique_ptr<trading::intelligence::AdaptiveMarketIntelligence> adaptiveIntel_;
    std::mutex intelligenceMutex_;

    // Market intelligence tracking
    std::unordered_map<std::string, trading::MarketIntelligence> latestIntelligence_;
    // Thread-safe helper maps for intelligence throttling and debug counts
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastIntelTime_;
    std::unordered_map<std::string, int> intelDebugCounts_;
    std::mutex intelMapsMutex_;

    // Core components
    std::unique_ptr<data_ingestion::DataIngestionService> dataIngestionService_;
    std::unique_ptr<strategy::StrategyEngine> strategyEngine_;
    std::shared_ptr<risk::RiskManager> riskManager_;
    std::shared_ptr<execution::ExecutionService> executionService_;
    // Position sizing helper (Kelly / volatility adjusted)
    std::unique_ptr<trading::risk::PositionSizer> positionSizer_;
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
    // Instrumentation for publish latency
    std::atomic<uint64_t> publishTimeNs_{0};
    std::atomic<uint64_t> publishCalls_{0};
    
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
        // createDataIngestionConfig();
        createStrategyConfig();
        createRiskConfig();                
        createPTCAConfig();
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
            // ptcaManager_->registerExecution(response.client_order_id, response.order_status);
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
        positionConfig.assetLimits["DOGE"] = 100.0;
        positionConfig.assetLimits["USDT"] = 50.0;
        positionConfig.assetLimits["USDC"] = 50.0;
        positionConfig.assetLimits["XRP"] = 10.0;
        positionConfig.assetLimits["SOL"] = 1.0;
        positionConfig.assetLimits["LTC"] = 0.1;
        positionConfig.assetLimits["DOT"] = 1.0;
        positionConfig.assetLimits["MATIC"] = 10.0;
        positionConfig.assetLimits["AVAX"] = 1.0;
        positionConfig.assetLimits["SHIB"] = 1000.0;
        positionConfig.assetLimits["TRX"] = 100.0;
        positionConfig.assetLimits["UNI"] = 1.0;
        positionConfig.assetLimits["LINK"] = 1.0;
        positionConfig.assetLimits["BCH"] = 0.1;
        positionConfig.assetLimits["XLM"] = 10.0;
        positionConfig.assetLimits["ATOM"] = 1.0;
        positionConfig.assetLimits["VET"] = 100.0;
        positionConfig.assetLimits["FIL"] = 1.0;
        positionConfig.assetLimits["ICP"] = 0.5;
        positionConfig.assetLimits["ALGO"] = 10.0;
        positionConfig.assetLimits["XTZ"] = 1.0;
        positionConfig.assetLimits["AAVE"] = 0.5;
        positionConfig.assetLimits["EGLD"] = 0.1;
        positionConfig.assetLimits["SAND"] = 5.0;
        positionConfig.assetLimits["MANA"] = 5.0;
        positionConfig.assetLimits["GALA"] = 10.0;
        positionConfig.assetLimits["AXS"] = 1.0;
        positionConfig.assetLimits["SUI"] = 5.0;
        positionConfig.assetLimits["APT"] = 5.0;
        positionConfig.assetLimits["KAS"] = 50.0;
        positionConfig.assetLimits["XMR"] = 0.1;
        positionConfig.assetLimits["ZEC"] = 0.1;
        positionConfig.assetLimits["PEPE"] = 1000.0;
        positionConfig.assetLimits["FTM"] = 5.0;
        positionConfig.assetLimits["HBAR"] = 50.0;
        positionConfig.assetLimits["PAXG"] = 0.01;
        positionConfig.assetLimits["ONDO"] = 50.0;
        positionConfig.assetLimits["BONK"] = 1000.0;
        positionConfig.assetLimits["FLOKI"] = 1000.0;
        positionConfig.assetLimits["AGIX"] = 5.0;
        positionConfig.assetLimits["FET"] = 10.0;
        positionConfig.assetLimits["INJ"] = 1.0;
        positionConfig.assetLimits["OP"] = 5.0;
        positionConfig.assetLimits["ARB"] = 5.0;
        positionConfig.assetLimits["STARK"] = 1.0;
        positionConfig.assetLimits["TIA"] = 5.0;
        positionConfig.assetLimits["XRP"] = 10.0;
        positionConfig.assetLimits["SOL"] = 1.0;
        positionConfig.assetLimits["ADA"] = 10.0;
        positionConfig.defaultLimit = 100; // dont care because btc =110k white doge or other maybe 0.001,
        positionConfig.enforceStrictly = true;
        
        auto& exposureConfig = const_cast<ExposureLimitConfig&>(riskConfig_.getExposureLimitConfig());
        exposureConfig.maxTotalExposure = 1000.0;
        exposureConfig.maxAssetExposure = 300.0;
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
    // Orderbook worker pool control
    void startOrderbookWorkerPool() {
        if (orderbookWorkersRunning_.exchange(true)) return; // already running
        orderbookWorkers_.clear();
        for (size_t i = 0; i < orderbookWorkerCount_; ++i) {
            orderbookWorkers_.emplace_back([this, i]() {
                this->orderbookWorkerLoop(i);
            });
        }
        std::cout << "Started " << orderbookWorkers_.size() << " orderbook worker(s)" << std::endl;
    }
 
    void stopOrderbookWorkerPool() {
        if (!orderbookWorkersRunning_.exchange(false)) return;
        
        orderbookQueueCv_.notify_all();
        
        // Join all threads first
        for (auto &t : orderbookWorkers_) {
            if (t.joinable()) t.join();
        }
        
        // Clear containers and force deallocation
        orderbookWorkers_.clear();
        orderbookWorkers_.shrink_to_fit();
        
        {
            std::lock_guard<std::mutex> lk(orderbookQueueMutex_);
            orderbookQueue_.clear();
            orderbookQueue_.shrink_to_fit(); // Force deallocation
        }
        
        std::cout << "Orderbook worker pool stopped and memory cleared" << std::endl;
    }

    void orderbookWorkerLoop(size_t workerId) {
        std::vector<OrderBookUpdate> batch;
        batch.reserve(orderbookBatchSize_);
        std::cout << "Orderbook worker " << workerId << " started" << std::endl;
        while (orderbookWorkersRunning_.load()) {
            // Wait for data
            std::unique_lock<std::mutex> lk(orderbookQueueMutex_);
            orderbookQueueCv_.wait_for(lk, std::chrono::milliseconds(200), [this]() {
                return !orderbookQueue_.empty() || !orderbookWorkersRunning_.load();
            });

            if (!orderbookWorkersRunning_.load() && orderbookQueue_.empty()) break;

            // Pop up to batchSize items
            batch.clear();
            if (batch.capacity() > orderbookBatchSize_ * 2) {
                batch.shrink_to_fit();
                batch.reserve(orderbookBatchSize_);
            }
            for (size_t i = 0; i < orderbookBatchSize_ && !orderbookQueue_.empty(); ++i) {
                batch.push_back(std::move(orderbookQueue_.front()));
                orderbookQueue_.pop_front();
            }
            lk.unlock();

            if (batch.empty()) continue;

            // Process batch
            for (const auto &update : batch) {
                try {
                    // PTCA quick update
                    if (!update.bids.empty() && !update.asks.empty()) {
                        double midPrice = (update.bids[0].price + update.asks[0].price) / 2.0;
                        onMarketPriceUpdate(update.tradingPair, midPrice);
                    }

                    auto orderBook = dataIngestionService_->getOrderBook(update.exchangeId, update.tradingPair);
                   
                    if (orderBook) {
                        orderBookUpdatesReceived_++;                      
                        auto [bidDepth, askDepth] = orderBook->getDepthLevels();
                        // std::cout << "OrderBook worker " << workerId << " processing update for "
                        //           << update.exchangeId.toString() << ":" << update.tradingPair.toString()
                        //           << " | Bids: " << bidDepth << " levels, Asks: " << askDepth << " levels"
                        //           << std::endl;
                          // Generate intelligence (throttling inside function)
                        // generateMarketIntelligence(update, orderBook); //no need at this time
                        // Publish orderbook for risk asynchronously
                        publishOrderBookForRisk(update);                 
                        // Send to strategy engine
                        //thing got stuck here
                        if (strategyEngine_) {
                            strategyEngine_->processOrderBook(*orderBook);
                        }
                        
                    }
                } catch (const std::exception &e) {
                    std::cerr << "Orderbook worker " << workerId << " error: " << e.what() << std::endl;
                }
            }
        }
    }
    // Signal worker pool control
    void startSignalWorkerPool() {
        if (signalWorkersRunning_.exchange(true)) return;
        signalWorkers_.clear();
        for (size_t i = 0; i < signalWorkerCount_; ++i) {
            signalWorkers_.emplace_back([this, i]() {
                this->signalWorkerLoop(i);
            });
        }
        std::cout << "Started " << signalWorkers_.size() << " signal worker(s)" << std::endl;
    }

    void stopSignalWorkerPool() {
        if (!signalWorkersRunning_.exchange(false)) return;
        signalQueueCv_.notify_all();
        for (auto &t : signalWorkers_) if (t.joinable()) t.join();
        signalWorkers_.clear();
        {
            std::lock_guard<std::mutex> lk(signalQueueMutex_);
            signalQueue_.clear();
        }
        std::cout << "Signal worker pool stopped" << std::endl;
    }

    void signalWorkerLoop(size_t workerId) {
        std::vector<strategy::TradingSignal> batch;
        // signal_dedup set; // optional deduplication
        trading::execution::SignalDeduplicator::Config cfg;
        cfg.includeExchangeInKey = true;
        trading::execution::SignalDeduplicator dedup(cfg);

        batch.reserve(signalBatchSize_);
        while (signalWorkersRunning_.load()) {
            // Wait for data
            std::unique_lock<std::mutex> lk(signalQueueMutex_);

            signalQueueCv_.wait_for(lk, std::chrono::milliseconds(100), [this]() {
                return !signalQueue_.empty() || !signalWorkersRunning_.load();
            });
            if (!signalWorkersRunning_.load() && signalQueue_.empty()) break;
            batch.clear();
            for (size_t i = 0; i < signalBatchSize_ && !signalQueue_.empty(); ++i) {
                batch.push_back(std::move(signalQueue_.front()));
                signalQueue_.pop_front();
            }
            lk.unlock();
            if (batch.empty()) continue;

            for (auto &signal : batch) {
                try {
                    //if signal is neutral, skip
                    if (signal.direction == core::Direction::NEUTRAL) continue;
                    signalsGenerated_++;
                    // Get PTCA recommendation first (needs signal alive)
                    double ptcaRecommendation = ptcaManager_->getSignalRecommendation(signal);
                    auto decision = ptcaManager_->generateTradingDecision(signal.symbol, std::vector<strategy::TradingSignal>{signal});
                    // if (decision.recommendedDirection == core::Direction::NEUTRAL) {
                    //         std::cout << "PTCA suggests HOLD for signal " << signal.id 
                    //                   << " on " << signal.symbol.toString() << std::endl;
                    //     continue; // Skip further processing
                    // }
                    // std::cout << "PTCA recommendation for signal " << signal.id 
                    //           << ": " << ptcaRecommendation << std::endl;
                    double adjustedStrength = signal.strength;
                    if (ptcaRecommendation > 0.2) {
                        // std::cout << "Boosting strength for signal " << signal.id 
                        //           << " from " << signal.strength 
                        //           << " to " << std::min(1.0, signal.strength * (1.0 + ptcaRecommendation * 0.3))
                        //           << " based on PTCA recommendation of " << ptcaRecommendation << std::endl;
                        adjustedStrength = std::min(1.0, signal.strength * (1.0 + ptcaRecommendation * 0.3));
                    } else if (ptcaRecommendation < -0.1) {
                        adjustedStrength = signal.strength * (1.0 + ptcaRecommendation * 0.2);
                    }

                    // convert and validate
                    execution::TradingSignal execSignal;
                    // Copy needed fields from the strategy signal BEFORE we move it
                    execSignal.id = signal.id;
                    execSignal.symbol = signal.symbol;
                    execSignal.direction = signal.direction;
                    execSignal.strength = adjustedStrength;
                    execSignal.targetSize = signal.targetSize;
                    execSignal.strategyId = signal.strategyId;
                    execSignal.confidence = signal.confidence;
                    execSignal.quality = signal.quality;
                    execSignal.expirationTime = signal.expirationTime;
                    execSignal.targets = signal.targets;
                    execSignal.exchangeId = signal.exchangeId;

                    if (!dedup.shouldProcessSignal(execSignal)) {
                        // Duplicate — skip processing, do NOT register the original with PTCA
                        continue;
                    }
                    // Now register the original signal with PTCA by moving it (no further use of `signal`)
                    ptcaManager_->registerSignal(std::move(signal));

                    // Basic filtering
                    if (execSignal.direction == core::Direction::BUY) {
                        if (execSignal.strength < 0.5) continue;
                        if (execSignal.confidence < 0.6) continue;
                    } else if (execSignal.direction == core::Direction::SELL) {
                        if (execSignal.strength < 0.3) continue;
                        if (execSignal.confidence < 0.6) continue;
                    }
                    //recalculate targetSize based on limit money
                    double allowedSize = 0.0;
                    if (riskManager_) {
                        allowedSize = riskManager_->getAllowedPositionSize(execSignal.symbol);
                    }

                    double sizerFraction = 1.0;
                    if (positionSizer_) {
                        std::map<std::string, double> metrics;
                        // Minimal metrics: use confidence as a rough proxy for win_rate
                        metrics["win_rate"] = execSignal.confidence;
                        // placeholder volatility - can be replaced with real vol from market data
                        metrics["volatility"] = 0.05;
                        std::map<std::string, double> portfolio;
                        sizerFraction = positionSizer_->calculateSize(execSignal.symbol.toString(), metrics, portfolio);
                        sizerFraction = std::clamp(sizerFraction, 0.0, 1.0);
                    }
                    double positionSize = allowedSize * sizerFraction;
                    // clamp this to
                    double finalQty = positionSize * execSignal.targetSize;
                    // clamp to limit exposure or it will be rejected by risk
                    finalQty = std::clamp(finalQty, 0.0, allowedSize);

                    execSignal.targetSize = finalQty; // now this is absolute size
                    
                    std::cout << "🛎️ Signal passed filtering: " << execSignal.id 
                              << " for " << execSignal.symbol.toString() 
                              << " Direction: " << (execSignal.direction == core::Direction::BUY ? "BUY" : "SELL")
                              << " Strength: " << execSignal.strength 
                              << " Confidence: " << execSignal.confidence 
                              << " Quality: " << execSignal.quality 
                              << " TargetSize: " << execSignal.targetSize 
                              << " PTCA Recommendation: " << ptcaRecommendation
                              << std::endl;
                    //check if price target percentage > 0.2%
                    for (const auto& target : execSignal.targets) {
                        if (target.percentage > 0.002) {
                            std::cout << "  Target size: " << target.size
                                      << " at " << target.price 
                                      << " (" << target.percentage * 100 << "%)" 
                                      << std::endl;
                        }
                    }                   

                    // Validate with risk manager
                    if (riskManager_) {
                        auto validationResult = riskManager_->validateSignal(execSignal);
                        signalsValidated_++;
                        if (validationResult.approved) {
                            signalsApproved_++;
                            realExecution(execSignal);
                        } else {
                            signalsRejected_++;
                        }
                    }
                } catch (const std::exception &e) {
                    std::cerr << "Signal worker " << workerId << " error: " << e.what() << std::endl;
                }
            }
        }
    }
    // Execution worker pool control
    void startExecWorkerPool() {
        if (execWorkersRunning_.exchange(true)) return;
        execWorkers_.clear();
        for (size_t i = 0; i < execWorkerCount_; ++i) {
            execWorkers_.emplace_back([this, i]() { this->execWorkerLoop(i); });
        }
        std::cout << "Started " << execWorkers_.size() << " exec worker(s)" << std::endl;
    }

    void stopExecWorkerPool() {
        if (!execWorkersRunning_.exchange(false)) return;
        execQueueCv_.notify_all();
        for (auto &t : execWorkers_) if (t.joinable()) t.join();
        execWorkers_.clear();
        {
            std::lock_guard<std::mutex> lk(execQueueMutex_);
            execQueue_.clear();
        }
        std::cout << "Exec worker pool stopped" << std::endl;
    }

    void execWorkerLoop(size_t workerId) {
        while (execWorkersRunning_.load()) {
            execution::TradingSignal task;
            {
                std::unique_lock<std::mutex> lk(execQueueMutex_);
                execQueueCv_.wait_for(lk, std::chrono::milliseconds(100), [this]() {
                    return !execQueue_.empty() || !execWorkersRunning_.load();
                });
                if (!execWorkersRunning_.load() && execQueue_.empty()) break;
                if (execQueue_.empty()) continue;
                task = std::move(execQueue_.front());
                execQueue_.pop_front();
            }
            try {
                std::this_thread::sleep_for(1ms); // simulate delay .why?
                if (!executionService_) {
                    std::cerr << "Execution service not initialized!" << std::endl;
                    continue;
                }
                ExecutionResponse response = executionService_->executeSignal(task);
                if (!response.success) {
                    std::cout << "❌ Execution failed for signal " << task.id 
                              << ": " << response.error_message << std::endl;
                    // print all of data in this failed order/signal
                    std::cout << "Signal details - Symbol: " << task.symbol.toString()
                              << ", Direction: " << (task.direction == core::Direction::BUY ? "BUY" : (task.direction == core::Direction::SELL ? "SELL" : "NEUTRAL"))
                              << ", Strength: " << task.strength
                              << ", Confidence: " << task.confidence
                              << ", Quality: " << task.quality
                              << ", TargetSize: " << task.targetSize
                              << ", StrategyId: " << task.strategyId
                              << ", ExchangeId: " << task.exchangeId.toString()
                              << std::endl;
                    continue;
                }
                execution::models::ExecutionReport report;
                report.setOrderId("ORDER_" + task.id);
                report.setSymbol(task.symbol);
                report.setSide(task.direction == core::Direction::BUY ? execution::models::OrderSide::BUY : execution::models::OrderSide::SELL);
                // targetSize is a scale from 0 to 1 (1 == full position as allowed by risk)
                // We compute an allowed size from the risk manager then modulate it by the PositionSizer
                double allowedSize = 0.0;
                if (riskManager_) {
                    allowedSize = riskManager_->getAllowedPositionSize(task.symbol);
                }

                double sizerFraction = 1.0;
                if (positionSizer_) {
                    std::map<std::string, double> metrics;
                    // Minimal metrics: use confidence as a rough proxy for win_rate
                    metrics["win_rate"] = task.confidence;
                    // placeholder volatility - can be replaced with real vol from market data
                    metrics["volatility"] = 0.05;
                    std::map<std::string, double> portfolio;
                    sizerFraction = positionSizer_->calculateSize(task.symbol.toString(), metrics, portfolio);
                    sizerFraction = std::clamp(sizerFraction, 0.0, 1.0);
                }

                double positionSize = allowedSize * sizerFraction;
                // clamp this to
                double finalQty = positionSize * task.targetSize;

                // clamp to limit exposure or it will be rejected by risk
                finalQty = std::clamp(finalQty, 0.0, allowedSize);
                report.setQuantity(finalQty);
                report.setFilledQuantity(finalQty);
                report.setStatus(execution::models::OrderState::FILLED);
                if (riskManager_) {
                    riskManager_->processExecutionReport(report);
                }
                std::cout << "📈 Executed: " << task.symbol.toString() << " " 
                          << (task.direction == core::Direction::BUY ? "BUY" : "SELL") 
                          << " " << task.targetSize << std::endl;
            } catch (const std::exception &e) {
                std::cerr << "Exec worker " << workerId << " error: " << e.what() << std::endl;
            }
        }
    }
    void setupComponentConnections() {        
        // Minimal callback: validate & enqueue update to worker queue
        dataIngestionService_->setOrderBookCallback([this](auto update) {
            // Quick checks only - avoid heavy work here
            std::string symbolStr = update.tradingPair.toString();
            if (strategyConfig_.tradingPairs.find(symbolStr) == strategyConfig_.tradingPairs.end()) {
                return; // ignore unconfigured symbols early
            }

            // Enqueue (bounded)
            std::unique_lock<std::mutex> lk(orderbookQueueMutex_);
            // Count incoming messages
            messagesProcessed_.fetch_add(1);

            if (orderbookQueue_.size() >= orderbookQueueLimit_) {
                // Drop oldest to make space (or simply drop this update)
                orderbookQueue_.pop_front();
            }
            // Move the update into the queue to avoid copying large OrderBookUpdate objects.
            orderbookQueue_.push_back(std::move(update));
            lk.unlock();
            orderbookQueueCv_.notify_one();
        });
            
       
            // 2. Connect strategy engine to risk manager
        // Enqueue signals into bounded queue; processing done by worker pool
        strategyEngine_->registerSignalCallback([this](const strategy::TradingSignal& signal) {
            // quick enqueue
            signalsGenerated_++;
            std::unique_lock<std::mutex> lk(signalQueueMutex_);
            // std::cout << "Enqueuing signal " << signal.id << " from strategy " << signal.strategyId << std::endl;
            //print debug info for every 20th signal
            static std::atomic<int> signalCount{0};
            int count = signalCount.fetch_add(1);
            // if (count < 20 || count % 500 == 0) {
            //     std::cout << "🛎️ Signal #" << count << ": "
            //               << signal.symbol.toString() 
            //               << " Dir: " << (signal.direction == core::Direction::BUY ? "BUY" : "SELL")
            //               << " Strength: " << signal.strength 
            //               << " Confidence: " << signal.confidence 
            //               << " Quality: " << signal.quality 
            //               << " Targets: " << signal.targets.size() << std::endl;
            // }
            if (signalQueue_.size() >= signalQueueLimit_) {
                // drop oldest to make room
                signalQueue_.pop_front();
            }
            signalQueue_.push_back(signal);
            lk.unlock();
            signalQueueCv_.notify_one();
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
        // Enqueue execution task to worker pool to avoid thread explosion
        ordersExecuted_++;
        std::unique_lock<std::mutex> lk(execQueueMutex_);
        if (execQueue_.size() >= execQueueLimit_) {
            // drop oldest
            execQueue_.pop_front();
        }
        execQueue_.push_back(signal);
        lk.unlock();
        execQueueCv_.notify_one();
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
    // Initialize logger early and allow overriding via environment variable
    // Use HYBRID_LOG_LEVEL (TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL, OFF)
    auto parseLogLevel = [](const std::string& s) {
        if (s == "TRACE" || s == "trace") return trading::common::LogLevel::TRACE;
        if (s == "DEBUG" || s == "debug") return trading::common::LogLevel::DEBUG;
        if (s == "INFO" || s == "info") return trading::common::LogLevel::INFO;
        if (s == "WARN" || s == "warn" || s == "WARNING" || s == "warning") return trading::common::LogLevel::WARN;
        if (s == "ERROR" || s == "error") return trading::common::LogLevel::ERROR;
        if (s == "CRITICAL" || s == "critical") return trading::common::LogLevel::CRITICAL;
        if (s == "OFF" || s == "off") return trading::common::LogLevel::OFF;
        return trading::common::LogLevel::INFO;
    };

    const char* envLevel = std::getenv("HYBRID_LOG_LEVEL");
    if (envLevel) {
        trading::common::Logger::initialize("TradingApp", parseLogLevel(envLevel));
    } else {
        // Default logger level - set to INFO to reduce noisy DEBUG logs
        trading::common::Logger::initialize("TradingApp", trading::common::LogLevel::INFO);
    }
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