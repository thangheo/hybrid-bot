/**
 * @file simplified_trading_server.cpp
 * @brief Simplified trading server using the new DataIngestionService
 */

#include "data_ingestion/data_ingestion_service.hpp"
#include "communication/zmq_server_client.hpp"
#include "market_intelligence/adaptive_intelligence.hpp"
#include "strategy/core_analysis/order_book_analyzer.hpp"
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
// #include yaml-cpp for future config parsing if needed
// #include <yaml-cpp/yaml.h>

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
    std::unique_ptr<GenericZmqServer> zmqServer_;
    std::unique_ptr<trading::intelligence::AdaptiveMarketIntelligence> adaptiveIntel_;

    // Strategy components - for comprehensive metrics calculation
    std::shared_ptr<trading::strategy::OrderBookAnalyzerManager> analyzerManager_;

    // Market intelligence tracking
    std::unordered_map<std::string, trading::MarketIntelligence> latestIntelligence_;
    std::mutex intelligenceMutex_;

    // Statistics
    std::atomic<uint64_t> messagesProcessed_{0};
    std::atomic<uint64_t> intelligenceGenerated_{0};
    std::chrono::steady_clock::time_point startTime_;

public:
    SimplifiedTradingServer() : startTime_(std::chrono::steady_clock::now()) {
        std::cout << "Initializing Simplified Trading Server with enhanced metrics..." << std::endl;

        // Initialize OrderBookAnalyzer manager with comprehensive configuration
        trading::strategy::ParameterSet analyzerParams;
        analyzerParams.setParameter("depthLevels", 20.0);           // Use 20 levels for better accuracy
        analyzerParams.setParameter("priceGroupingFactor", 0.0001); // Group prices for cleaner analysis
        analyzerParams.setParameter("emaAlpha", 0.2);               // EMA smoothing factor
        analyzerParams.setParameter("minSamplesForStability", 5);   // Minimum samples for stable metrics
        analyzerParams.setParameter("warmupMinLevels", 5);          // Warmup threshold
        analyzerParams.setParameter("historySize", 100.0);          // Historical data size

        analyzerManager_ = std::make_shared<trading::strategy::OrderBookAnalyzerManager>(analyzerParams);
        std::cout << "  ✓ OrderBookAnalyzer manager initialized (20 levels, comprehensive metrics)" << std::endl;

        // Initialize adaptive intelligence
        adaptiveIntel_ = std::make_unique<trading::intelligence::AdaptiveMarketIntelligence>();
        std::cout << "  ✓ Adaptive intelligence initialized (with adaptive thresholds)" << std::endl;

        // Initialize data ingestion service
        dataService_ = std::make_unique<DataIngestionService>("config/data_ingestion.json");
        std::cout << "  ✓ Data ingestion service created" << std::endl;

        // Initialize ZMQ communication
        ZmqServerConfig zmqConfig;
        zmqServer_ = std::make_unique<GenericZmqServer>(zmqConfig);
        std::cout << "  ✓ ZMQ server created" << std::endl;

        setupCallbacks();
        std::cout << "  ✓ Callbacks configured" << std::endl;
    }

    ~SimplifiedTradingServer() {
        try {
            stop();
        } catch (const std::exception& e) {
            std::cerr << "Error in destructor: " << e.what() << std::endl;
        }
    }

    bool start(const std::vector<Symbol>& symbols) {
        std::cout << "Starting Simplified Trading Server..." << std::endl;
        // adaptiveIntel_->ApplyEmergencyThresholds();
        // 1. Initialize data ingestion service
        if (!dataService_->initialize()) {
            std::cerr << "Failed to initialize data ingestion service" << std::endl;
            return false;
        }
        
        // 2. Start ZMQ communication
        zmqServer_->start();
        
        // 3. Start data ingestion with symbols
        if (!dataService_->start(symbols, 20)) {
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
            zmqServer_->stop();
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
        // zmqServer_->SetExecutionCallback([this](const trading::TradingIntent& intent) {
        //     return this->handleTradingIntent(intent);
        // });
        zmqServer_->registerRequestHandler("TradingIntent", 
            [this](const std::string& request_data, const std::unordered_map<std::string, std::string>& /*metadata*/) -> std::string {
                // Reuse protobuf objects to reduce heap churn from repeated allocations
                static thread_local trading::TradingIntent tl_intent;
                static thread_local trading::ExecutionAcknowledgment tl_ack;

                tl_intent.Clear();
                if (!tl_intent.ParseFromString(request_data)) {
                    std::cerr << "Failed to parse TradingIntent from request data." << std::endl;
                    return "";
                }

                // Use the reused ack object for the response
                tl_ack = this->handleTradingIntent(tl_intent);
                std::string response_data;
                if (!tl_ack.SerializeToString(&response_data)) {
                    std::cerr << "Failed to serialize ExecutionAcknowledgment." << std::endl;
                    return "";
                }
                return response_data;
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
            
            // Generate market intelligence
            generateMarketIntelligence(update);
            
        } catch (const std::exception& e) {
            std::cerr << "Error processing order book update: " << e.what() << std::endl;
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
            auto bids = orderBook->getBids(20);
            auto asks = orderBook->getAsks(20);

            if (bids.empty() || asks.empty()) {
                return;
            }

            // Verify prices are valid
            if (bids[0].price <= 0 || asks[0].price <= 0 || asks[0].price <= bids[0].price) {
                return;
            }

            std::string tradingPair = update.tradingPair.toString();

            // ═══════════════════════════════════════════════════════════════
            // ENHANCED APPROACH: Use OrderBookAnalyzer for comprehensive metrics
            // ═══════════════════════════════════════════════════════════════

            // 1. Get or create analyzer for this symbol
            auto analyzer = analyzerManager_->getAnalyzerForSymbol(
                update.tradingPair,
                update.exchangeId
            );

            // 2. Process orderbook snapshot to calculate ALL metrics ONCE
            //    This includes: volume, imbalance, volatility, regime, flow, etc.
            analyzer->processOrderBook(*orderBook);

            // 3. Get pre-calculated comprehensive metrics from analyzer
            auto obMetrics = analyzer->getMetrics();

            // 4. Get flow metrics from liquidity monitor if available
            std::shared_ptr<trading::strategy::LiquidityMetrics> flowMetrics;
            auto flowMonitor = orderBook->getLiquidityFlowMonitor();
            if (flowMonitor) {
                auto metrics = flowMonitor->getLiquidityMetrics();
                flowMetrics = std::make_shared<trading::strategy::LiquidityMetrics>(
                    std::move(metrics)
                );
            }

            // 5. Generate intelligence using comprehensive pre-calculated metrics
            //    This is MUCH MORE ACCURATE than the old basic calculation
            trading::MarketIntelligence intel =
                adaptiveIntel_->generateIntelligenceFromMetrics(
                    update.tradingPair,
                    obMetrics,      // Comprehensive metrics from OrderBookAnalyzer
                    flowMetrics     // Flow metrics from liquidity monitor
                );

            if (intel.symbol().empty()) {
                return; // No valid intelligence generated
            }

            // ═══════════════════════════════════════════════════════════════
            // Enhanced debug output showing comprehensive metrics
            // ═══════════════════════════════════════════════════════════════
            static std::unordered_map<std::string, int> intelDebugCounts;
            intelDebugCounts[tradingPair]++;

            if (intelDebugCounts[tradingPair] <= 3) {
                std::cout << "\n╔═══════════════════════════════════════════════════════════╗" << std::endl;
                std::cout << "║ ENHANCED ADAPTIVE INTELLIGENCE: " << std::left << std::setw(22) << tradingPair << "║" << std::endl;
                std::cout << "╠═══════════════════════════════════════════════════════════╣" << std::endl;
                std::cout << "║ Price & Spread Metrics:                                  ║" << std::endl;
                std::cout << "║   Spread: " << std::setw(48) << intel.bid_ask_spread() << "║" << std::endl;
                std::cout << "║   Spread Stability: " << std::setw(38) << intel.spread_stability() << "║" << std::endl;
                std::cout << "║                                                           ║" << std::endl;
                std::cout << "║ Volume & Liquidity Metrics:                              ║" << std::endl;
                std::cout << "║   Liquidity Ratio: " << std::setw(39) << intel.liquidity_ratio() << "║" << std::endl;
                std::cout << "║   Volume Ratio to Avg: " << std::setw(34) << intel.volume_ratio_to_avg() << "║" << std::endl;
                std::cout << "║                                                           ║" << std::endl;
                std::cout << "║ Imbalance Metrics:                                       ║" << std::endl;
                std::cout << "║   Order Book Imbalance: " << std::setw(33) << intel.order_book_imbalance() << "║" << std::endl;
                std::cout << "║   Depth Imbalance: " << std::setw(39) << intel.depth_imbalance() << "║" << std::endl;
                std::cout << "║                                                           ║" << std::endl;
                std::cout << "║ Volatility Metrics:                                      ║" << std::endl;
                std::cout << "║   Realized Vol 1m: " << std::setw(39) << intel.realized_volatility_1m() << "║" << std::endl;
                std::cout << "║   Expected Vol 5m: " << std::setw(39) << intel.expected_volatility_5m() << "║" << std::endl;
                std::cout << "║                                                           ║" << std::endl;
                std::cout << "║ Flow Metrics:                                            ║" << std::endl;
                std::cout << "║   Aggressive Buy Ratio: " << std::setw(34) << intel.aggressive_buy_ratio() << "║" << std::endl;
                std::cout << "║   Aggressive Sell Ratio: " << std::setw(33) << intel.aggressive_sell_ratio() << "║" << std::endl;
                std::cout << "║   Flow Momentum: " << std::setw(41) << intel.flow_momentum() << "║" << std::endl;
                std::cout << "║                                                           ║" << std::endl;
                std::cout << "║ Regime Classification:                                   ║" << std::endl;
                std::cout << "║   Regime: " << std::setw(48) << intel.current_regime() << "║" << std::endl;
                std::cout << "║   Confidence: " << std::setw(44) << intel.regime_confidence() << "║" << std::endl;
                std::cout << "║   Regime Stability: " << std::setw(38) << intel.regime_stability() << "║" << std::endl;
                std::cout << "║                                                           ║" << std::endl;
                std::cout << "║ Price Impact:                                            ║" << std::endl;
                std::cout << "║   Estimate: " << std::setw(46) << intel.price_impact_estimate() << "║" << std::endl;
                std::cout << "╚═══════════════════════════════════════════════════════════╝" << std::endl;
            }

            // Store latest intelligence
            {
                std::lock_guard<std::mutex> lock(intelligenceMutex_);
                latestIntelligence_[tradingPair] = intel;
            }

            // Publish to ZMQ for Python consumption
            if (zmqServer_ && intel.symbol() == tradingPair) {
                zmqServer_->publishAsync("market_intelligence", intel.SerializeAsString());
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

        // Define symbols to monitor// just hardcode list above for now
        std::vector<std::string> pairs = {
            "BTCUSDT", "ETHUSDT", "BNBUSDT", "XRPUSDT", "SOLUSDT", "ADAUSDT", "AVAXUSDT", "DOGEUSDT", "DOTUSDT", "MATICUSDT",
            "LINKUSDT", "UNIUSDT", "AAVEUSDT", "COMPUSDT", "SUSHIUSDT", "ARBUSDT", "OPUSDT",
            "RENDERUSDT", "FETUSDT", "OCEANUSDT", "AGIXUSDT", "INJUSDT", "NEARUSDT",
            "ONDOUSDT", "HBARUSDT", "PAXGUSDT",
            "MANAUSDT", "SANDUSDT", "GALAUSDT", "IMXUSDT", "AXSUSDT", "BEAMUSDT",
            "SUIUSDT", "APTUSDT", "SEIUSDT", "TIAUSDT", "STRKUSDT",
            "SHIBUSDT", "PEPEUSDT", "FLOKIUSDT", "BONKUSDT",
            "ATOMUSDT", "ICPUSDT", "FTMUSDT", "ALGOUSDT", "HNTUSDT", "FILUSDT",
            "XMRUSDT", "ZECUSDT"
        };

        std::cout << "Monitoring " << pairs.size() << " symbols:" << std::endl;
        for (const auto& sym : pairs) {
                    std::cout << "  " << sym << std::endl;
                }
        std::vector<Symbol> symbols;
        for (const auto& s : pairs) {
            symbols.push_back(Symbol::fromString(s));
        }

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