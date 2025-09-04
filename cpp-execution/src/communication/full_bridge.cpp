/**
 * @file integrated_trading_server.cpp
 * @brief Integrated server combining pipeline processing with ZMQ communication
 */

#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/publisher/zeromq_publisher.hpp"
#include "data_ingestion/protocol/binance_adapter.hpp"
#include "data_ingestion/config.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "data_ingestion/orderbook/reconstructor.hpp"
#include "common/exchange_types.hpp"
#include "core/market_types.hpp"
#include "communication/zmq_server.hpp"
#include "../../generated/cpp/proto/trading_messages.pb.h"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <signal.h>
#include <unordered_map>
#include <mutex>
#include "market_intelligence/adaptive_intelligence.hpp"

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

class IntegratedTradingServer {
private:
    // Pipeline components
    // std::shared_ptr<NormalizationPipeline> pipeline_;
    std::unique_ptr<ConnectionPool> connectionPool_;
    std::vector<ClientInterfacePtr> activeConnections_;
    //market intel
    std::unique_ptr<trading::intelligence::AdaptiveMarketIntelligence> adaptive_intel_;

    // Communication components
    std::unique_ptr<TradingCommunicationServer> zmq_server_;
    
    // Market intelligence tracking
    std::unordered_map<std::string, trading::MarketIntelligence> latest_intelligence_;
    std::mutex intelligence_mutex_;
    
    // Order book tracking
    std::unordered_map<std::string, std::shared_ptr<OrderBook>> order_books_;
    // std::unique_ptr<OrderBookReconstructor> order_book_reconstructor_;
    OrderBookReconstructor* order_book_reconstructor_;
    // std::mutex orderbook_mutex_;
    
    // Statistics
    std::atomic<uint64_t> messages_processed_{0};
    std::atomic<uint64_t> intelligence_generated_{0};
    std::chrono::steady_clock::time_point start_time_;

public:
    IntegratedTradingServer() : start_time_(std::chrono::steady_clock::now()) {
        order_book_reconstructor_ = &OrderBookReconstructor::getInstance();
        //Initialize adaptive intelligence
        adaptive_intel_ = std::make_unique<trading::intelligence::AdaptiveMarketIntelligence>();
        InitializeConnectionPool();
        InitializeCommunication();
    }

    ~IntegratedTradingServer() {
        try {
            Stop();
            
            // Don't delete singleton - just nullify pointer
            order_book_reconstructor_ = nullptr;
            
        } catch (const std::exception& e) {
            std::cerr << "Error in destructor: " << e.what() << std::endl;
        }
    }
    void InitializeConnectionPool() {
        connectionPool_ = std::make_unique<ConnectionPool>(
            "integrated_pool",
            [this]() -> ClientInterfacePtr {
                return std::make_shared<BinanceWebSocketClient>(
                    "stream.binance.com",
                    "9443",
                    [this](const ExchangeId& exchange, const std::string& symbol, const std::vector<uint8_t>& data) {
                        this->OnMarketData(exchange, symbol, data);
                    }
                );
            },
            5, 10
        );
    }
    // Add this method to IntegratedTradingServer class
    void InitializeOrderBooks(const std::vector<std::string>& symbols) {
        std::cout << "Initializing order books for " << symbols.size() << " symbols..." << std::endl;
        PublisherConfig pub_config;
        pub_config.zmq_endpoint = "tcp://*:5560"; // Or your desired endpoint
        // Set other config fields as needed
        
        auto publisher = std::make_shared<ZeroMQPublisher>(pub_config);
        // Initialize publisher for reconstructor if not done
        // auto publisher = std::make_shared<ZeroMQPublisher>();
        if (!order_book_reconstructor_->initialize(publisher)) {
            std::cerr << "Failed to initialize order book reconstructor" << std::endl;
            return;
        }
        
        for (const auto& symbol : symbols) {
            // Create empty order book with initial snapshot
            std::vector<trading::data_ingestion::PriceLevel> empty_bids;
            std::vector<trading::data_ingestion::PriceLevel> empty_asks;
            
            bool success = order_book_reconstructor_->processSnapshot(
                ExchangeId::binance(), 
                Symbol::fromString(symbol),
                empty_bids,
                empty_asks,
                0, // sequence number
                std::chrono::system_clock::now()
            );
            
            if (success) {
                // Now get the created order book
                auto orderbook = order_book_reconstructor_->getOrderBook(
                    ExchangeId::binance(), 
                    Symbol::fromString(symbol)
                );
                
                if (orderbook) {
                    order_books_[symbol] = orderbook;
                    std::cout << "Created order book for " << symbol << std::endl;
                } else {
                    std::cerr << "Failed to retrieve order book for " << symbol << std::endl;
                }
            } else {
                std::cerr << "Failed to create initial snapshot for " << symbol << std::endl;
            }
        }
    }

    // Modified Start method
    bool Start(const std::vector<std::string>& symbols) {
        std::cout << "Starting Integrated Trading Server..." << std::endl;
        
        // Initialize order books FIRST
        InitializeOrderBooks(symbols);
        
        // Setup WebSocket connections
        connectionPool_->initialize();
        if (!connectionPool_->connectAll()) {
            std::cerr << "Failed to establish WebSocket connections" << std::endl;
            return false;
        }
        
        // Wait for connections to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // Start ZMQ communication
        zmq_server_->Start();
        
        // Acquire and subscribe connections
        int connectionsNeeded = std::min(static_cast<int>(symbols.size() / 5 + 1), 5);
        
        for (int i = 0; i < connectionsNeeded; ++i) {
            auto conn = connectionPool_->acquireConnection(10000);
            if (conn && conn->isConnected()) {
                activeConnections_.push_back(conn);
            }
        }
        
        if (activeConnections_.empty()) {
            std::cerr << "No connections available" << std::endl;
            return false;
        }
        
        // Subscribe to symbols
        for (size_t i = 0; i < activeConnections_.size(); ++i) {
            std::vector<Symbol> connectionSymbols;
            
            for (size_t j = i; j < symbols.size(); j += activeConnections_.size()) {
                connectionSymbols.emplace_back(Symbol::fromString(symbols[j]));
            }
            
            if (!connectionSymbols.empty()) {
                activeConnections_[i]->subscribe(connectionSymbols);
            }
        }
        
        std::cout << "Integrated Trading Server started successfully" << std::endl;
        return true;
    }
    void GenerateAndPublishIntelligence__obsolet(const std::string& symbol, std::shared_ptr<OrderBook> orderbook) {
        try {
            /*obsolete intelligence*/
            auto bids = orderbook->getBids(5);
            auto asks = orderbook->getAsks(5);
            
            // DEBUG: Check if we have order book data
            static std::unordered_map<std::string, int> debug_counters;
            debug_counters[symbol]++;
            
            if (debug_counters[symbol] <= 3) {
                std::cout << "\n=== DEBUG: OrderBook state for " << symbol << " ===" << std::endl;
                std::cout << "Bids count: " << bids.size() << std::endl;
                std::cout << "Asks count: " << asks.size() << std::endl;
                
                if (!bids.empty()) {
                    std::cout << "Best bid: " << bids[0].getPrice() << " @ " << bids[0].getQuantity() << std::endl;
                }
                if (!asks.empty()) {
                    std::cout << "Best ask: " << asks[0].getPrice() << " @ " << asks[0].getQuantity() << std::endl;
                }
                std::cout << "===============================================" << std::endl;
            }
            
            if (bids.empty() || asks.empty()) {
                return; // No valid order book data
            }
            
            double best_bid = bids[0].getPrice();
            double best_ask = asks[0].getPrice();
            
            if (best_bid <= 0 || best_ask <= 0 || best_ask <= best_bid) {
                return; // Invalid prices
            }
            
            // Generate intelligence
            trading::MarketIntelligence intel;
            
            auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            intel.set_symbol(symbol);
            intel.set_timestamp(now);
            
            // Calculate spread
            double spread = best_ask - best_bid;
            double mid_price = (best_bid + best_ask) / 2.0;
            intel.set_bid_ask_spread(spread / mid_price);
            
            // Calculate volumes
            double bid_volume = 0, ask_volume = 0;
            for (size_t i = 0; i < std::min(size_t(5), bids.size()); ++i) {
                bid_volume += bids[i].getQuantity();
            }
            for (size_t i = 0; i < std::min(size_t(5), asks.size()); ++i) {
                ask_volume += asks[i].getQuantity();
            }
            
            if (bid_volume + ask_volume > 0) {
                intel.set_order_book_imbalance((bid_volume - ask_volume) / (bid_volume + ask_volume));
                intel.set_liquidity_ratio(std::min(1.0, (bid_volume + ask_volume) / 10000.0));
            }
            
            // Set other metrics (simplified for now)
            intel.set_realized_volatility_1m(spread / mid_price * 10);
            intel.set_net_flow_5s(intel.order_book_imbalance() * 0.5);
            intel.set_net_flow_30s(intel.order_book_imbalance() * 0.3);
            intel.set_aggressive_buy_ratio(0.5 + intel.order_book_imbalance() * 0.2);
            intel.set_aggressive_sell_ratio(0.5 - intel.order_book_imbalance() * 0.2);
            
            // Classify regime
            double total_volume = bid_volume + ask_volume;
            if (total_volume > 15000 && intel.bid_ask_spread() < 0.001) {
                intel.set_current_regime(trading::OB_LIQUID);
            } else if (total_volume < 2000 || intel.bid_ask_spread() > 0.005) {
                intel.set_current_regime(trading::OB_THIN);
            } else if (std::abs(intel.order_book_imbalance()) > 0.3) {
                intel.set_current_regime(trading::OB_VOLATILE);
            } else {
                intel.set_current_regime(trading::OB_NORMAL);
            }
            
            intel.set_regime_confidence(0.8);
            
            // DEBUG: Print generated intelligence
            if (debug_counters[symbol] <= 3) {
                std::cout << "Generated intelligence for " << symbol << ":" << std::endl;
                std::cout << "  Spread: " << intel.bid_ask_spread() << std::endl;
                std::cout << "  Liquidity: " << intel.liquidity_ratio() << std::endl;
                std::cout << "  Imbalance: " << intel.order_book_imbalance() << std::endl;
                std::cout << "  Regime: " << intel.current_regime() << std::endl;
            }
            
            // Store and publish
            {
                std::lock_guard<std::mutex> intel_lock(intelligence_mutex_);
                latest_intelligence_[symbol] = intel;
            }
            
            if (zmq_server_) {
                zmq_server_->PublishMarketIntelligence(intel);
                intelligence_generated_.fetch_add(1);
            }
            
        } catch (const std::exception& e) {
            std::cout << "Error generating intelligence for " << symbol << ": " << e.what() << std::endl;
        }
    }

    void GenerateAndPublishIntelligence(const std::string& symbol, std::shared_ptr<OrderBook> orderbook) {
        try {
                         
            // NEW CODE: Use adaptive intelligence
            trading::MarketIntelligence intel = adaptive_intel_->GenerateAdaptiveMarketIntelligence(symbol, orderbook);
            
            if (intel.symbol().empty()) {
                return; // No valid intelligence generated
            }
            
            // DEBUG: Print generated intelligence (first few times)
            static std::unordered_map<std::string, int> debug_counters;
            debug_counters[symbol]++;
            
            if (debug_counters[symbol] <= 3) {
                std::cout << "\n=== ADAPTIVE INTELLIGENCE for " << symbol << " ===" << std::endl;
                std::cout << "  Spread: " << intel.bid_ask_spread() << std::endl;
                std::cout << "  Liquidity: " << intel.liquidity_ratio() << std::endl;
                std::cout << "  Imbalance: " << intel.order_book_imbalance() << std::endl;
                std::cout << "  Regime: " << intel.current_regime() << std::endl;
                std::cout << "  Confidence: " << intel.regime_confidence() << std::endl;
                std::cout << "=============================================" << std::endl;
            }
            
            // Store and publish (unchanged)
            {
                std::lock_guard<std::mutex> intel_lock(intelligence_mutex_);
                latest_intelligence_[symbol] = intel;
            }
            
            if (zmq_server_) {
                zmq_server_->PublishMarketIntelligence(intel);
                intelligence_generated_.fetch_add(1);
            }
            
        } catch (const std::exception& e) {
            std::cout << "Error generating adaptive intelligence for " << symbol << ": " << e.what() << std::endl;
        }
    }
    void InitializeCommunication() {
        zmq_server_ = std::make_unique<TradingCommunicationServer>();
        
        // Set execution callback
        zmq_server_->SetExecutionCallback([this](const trading::TradingIntent& intent) {
            return this->HandleTradingIntent(intent);
        });
        
        std::cout << "Communication server initialized" << std::endl;
    }
      
    void Stop() {
        g_running = false;
        
        // 1. Stop ZMQ server first
        if (zmq_server_) {
            zmq_server_->Stop();
            zmq_server_.reset();  // Explicitly reset
        }
        
        // 2. Release connections before destroying pool
        for (auto& conn : activeConnections_) {
            if (connectionPool_) {
                connectionPool_->releaseConnection(conn);
            }
        }
        activeConnections_.clear();
        
        // 3. Shutdown pool last
        if (connectionPool_) {
            connectionPool_->shutdown();
            connectionPool_.reset();
        }
        
        std::cout << "Integrated Trading Server stopped" << std::endl;
    }
    void RunMainLoop() {
        auto last_status = std::chrono::steady_clock::now();
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // Print status every 5 seconds
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_status).count() >= 5) {
                PrintStatus();
                last_status = now;
            }
             
        }
    }

private:
    void OnMarketData(const ExchangeId& exchange, const std::string& symbol, const std::vector<uint8_t>& data) {
        static std::atomic<int> total_messages{0};
        static std::atomic<int> depth_messages{0};
        static std::unordered_map<std::string, int> symbol_messages;
        
        int msg_count = total_messages.fetch_add(1);
        
        // Convert to string for JSON parsing
        std::string dataStr(data.begin(), data.end());
        
        // Debug first few messages
        if (msg_count < 10) {
            std::cout << "\n=== MESSAGE #" << msg_count << " ===" << std::endl;
            std::cout << "Exchange: " << exchange.toString() << std::endl;
            std::cout << "Symbol: " << symbol << std::endl;
            std::cout << "Data size: " << data.size() << " bytes" << std::endl;
            std::cout << "Data preview: " << dataStr.substr(0, 200) << "..." << std::endl;
        }
        
        // Create ExchangeData for pipeline
        ExchangeData exchangeData;
        exchangeData.exchangeId = exchange;
        exchangeData.symbol = Symbol::fromString(symbol);
        exchangeData.data = dataStr;
        exchangeData.receivedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        exchangeData.dataType = "orderbook";
        exchangeData.isSnapshot = false;
        exchangeData.sourceType = "websocket";
        
        // CRITICAL: Pre-filter messages
        try {
            json parsed = json::parse(dataStr);
            
            // Debug message type
            if (msg_count < 10) {
                if (parsed.contains("e")) {
                    std::cout << "Message type: " << parsed["e"] << std::endl;
                } else {
                    std::cout << "Message has no 'e' field" << std::endl;
                }
            }
            
            // Only process depthUpdate messages
            if (!parsed.contains("e") || parsed["e"] != "depthUpdate") {
                if (msg_count < 10) {
                    std::cout << "SKIPPING: Not a depthUpdate message" << std::endl;
                }
                return; // SKIP non-depth messages
            }
            
            // Count depth messages
            int depth_count = depth_messages.fetch_add(1);
            symbol_messages[symbol]++;
            
            if (depth_count < 5) {
                std::cout << "\n=== DEPTH UPDATE #" << depth_count << " ===" << std::endl;
                std::cout << "Symbol: " << symbol << std::endl;
                if (parsed.contains("u")) {
                    std::cout << "Sequence: " << parsed["u"] << std::endl;
                }
                if (parsed.contains("b") && parsed["b"].is_array()) {
                    std::cout << "Bid updates: " << parsed["b"].size() << std::endl;
                }
                if (parsed.contains("a") && parsed["a"].is_array()) {
                    std::cout << "Ask updates: " << parsed["a"].size() << std::endl;
                }
            }
            
            // Validate required fields
            if (!parsed.contains("b") || !parsed.contains("a") || !parsed.contains("u")) {
                if (msg_count < 10) {
                    std::cout << "SKIPPING: Missing required fields (b, a, u)" << std::endl;
                }
                return; // SKIP invalid depth messages
            }
            
        } catch (const std::exception& e) {
            if (msg_count < 10) {
                std::cout << "SKIPPING: JSON parse error: " << e.what() << std::endl;
            }
            return; // SKIP invalid JSON
        }
    
        messages_processed_.fetch_add(1);

        // Also process for real-time intelligence generation
        ProcessMarketDataForIntelligence(symbol, dataStr);
        
        // Print periodic stats
        if (msg_count % 100 == 0) {
            std::cout << "\n=== MESSAGE STATS ===" << std::endl;
            std::cout << "Total messages: " << total_messages.load() << std::endl;
            std::cout << "Depth messages: " << depth_messages.load() << std::endl;
            std::cout << "Processed: " << messages_processed_.load() << std::endl;
            
            std::cout << "Per symbol depth messages:" << std::endl;
            for (const auto& [sym, count] : symbol_messages) {
                std::cout << "  " << sym << ": " << count << std::endl;
            }
            std::cout << "=====================" << std::endl;
        }
    }
    void OnMarketData__(const ExchangeId& exchange, const std::string& symbol, const std::vector<uint8_t>& data) {
        
        // Convert to string for JSON parsing
        std::string dataStr(data.begin(), data.end());
        
        // Create ExchangeData for pipeline
        ExchangeData exchangeData;
        exchangeData.exchangeId = exchange;
        exchangeData.symbol = Symbol::fromString(symbol);
        exchangeData.data = dataStr;
        exchangeData.receivedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        exchangeData.dataType = "orderbook";
        exchangeData.isSnapshot = false;
        exchangeData.sourceType = "websocket";
        // CRITICAL: Pre-filter messages
        try {
            json parsed = json::parse(dataStr);
            
            // Only process depthUpdate messages
            if (!parsed.contains("e") || parsed["e"] != "depthUpdate") {
                return; // SKIP non-depth messages
            }
            
            // Validate required fields
            if (!parsed.contains("b") || !parsed.contains("a") || !parsed.contains("u")) {
                return; // SKIP invalid depth messages
            }
            
        } catch (...) {
            return; // SKIP invalid JSON
        }
       
        messages_processed_.fetch_add(1);

        // Also process for real-time intelligence generation
        ProcessMarketDataForIntelligence(symbol, dataStr);
    }
    void ProcessMarketDataForIntelligence(const std::string& symbol, const std::string& data) {
        try {
            json parsed = json::parse(data);
            
            // Check if it's order book update
            if (!parsed.contains("e") || !parsed["e"].is_string() || 
                parsed["e"] != "depthUpdate") {
                return;
            }
            
            // Update order book through reconstructor
            order_book_reconstructor_->UpdateOrderBookFromJson(
                ExchangeId::binance(), 
                parsed, 
                Symbol::fromString(symbol)
            );
            
            // Get the updated order book
            auto orderbook = order_books_[symbol];
            if (!orderbook) {
                // Try to get it from reconstructor
                orderbook = order_book_reconstructor_->getOrderBook(
                    ExchangeId::binance(), 
                    Symbol::fromString(symbol)
                );
                
                if (orderbook) {
                    order_books_[symbol] = orderbook;
                } else {
                    std::cout << "Warning: Could not find order book for " << symbol << std::endl;
                    return;
                }
            }
            
            // Check if order book has valid data
            auto bids = orderbook->getBids(5);
            auto asks = orderbook->getAsks(5);
            
            if (bids.empty() || asks.empty()) {
                static std::unordered_map<std::string, int> empty_warnings;
                empty_warnings[symbol]++;
                
                if (empty_warnings[symbol] <= 5) {
                    std::cout << "Warning: Order book for " << symbol 
                            << " is empty (bids:" << bids.size() 
                            << ", asks:" << asks.size() << ")" << std::endl;
                }
                return;
            }
            
            // Verify prices are valid
            if (bids[0].getPrice() <= 0 || asks[0].getPrice() <= 0 || 
                asks[0].getPrice() <= bids[0].getPrice()) {
                std::cout << "Warning: Invalid prices in order book for " << symbol 
                        << " (bid:" << bids[0].getPrice() 
                        << ", ask:" << asks[0].getPrice() << ")" << std::endl;
                return;
            }
            
            // Generate and publish intelligence
            GenerateAndPublishIntelligence(symbol, orderbook);
            
        } catch (const std::exception& e) {
            static std::unordered_map<std::string, int> error_counts;
            error_counts[symbol]++;
            
            if (error_counts[symbol] <= 3) {
                std::cout << "Error processing market data for " << symbol 
                        << ": " << e.what() << std::endl;
            }
        }
    }
            
        
    trading::ExecutionAcknowledgment HandleTradingIntent(const trading::TradingIntent& intent) {
        std::cout << "\n=== PROCESSING TRADING INTENT ===" << std::endl;
        std::cout << "Intent ID: " << intent.intent_id() << std::endl;
        std::cout << "Symbol: " << intent.symbol() << std::endl;
        std::cout << "Side: " << (intent.side() == trading::SIDE_BUY ? "BUY" : "SELL") << std::endl;
        std::cout << "Quantity: " << intent.target_quantity() << std::endl;
        std::cout << "Market Regime: " << intent.market_regime() << std::endl;
        std::cout << "Execution Style: " << intent.execution_style() << std::endl;
        std::cout << "Signal Strength: " << intent.signal_strength() << std::endl;
        std::cout << "Confidence: " << intent.confidence() << std::endl;
        std::cout << "Urgency: " << intent.urgency_factor() << std::endl;
        
        // Get current market intelligence for this symbol
        trading::MarketIntelligence current_intel;
        {
            std::lock_guard<std::mutex> lock(intelligence_mutex_);
            auto it = latest_intelligence_.find(intent.symbol());
            if (it != latest_intelligence_.end()) {
                current_intel = it->second;
                std::cout << "Current Market Intelligence:" << std::endl;
                std::cout << "  Spread: " << current_intel.bid_ask_spread() << std::endl;
                std::cout << "  Imbalance: " << current_intel.order_book_imbalance() << std::endl;
                std::cout << "  Liquidity: " << current_intel.liquidity_ratio() << std::endl;
                std::cout << "  Regime: " << current_intel.current_regime() << std::endl;
            }
        }
        
        // Create acknowledgment
        // trading::ExecutionAcknowledgment ack;
        // ack.set_intent_id(intent.intent_id());
        // ack.set_accepted(true);
        // ack.set_execution_id("exec_" + intent.intent_id() + "_" + std::to_string(std::time(nullptr)));
        // ack.set_reason("Intent accepted - Market intelligence integrated");
        // 
        trading::ExecutionAcknowledgment ack;
        ack.set_intent_id(intent.intent_id());
        ack.set_accepted(true);  // Always accept for now
        ack.set_execution_id("cpp_ack_" + intent.intent_id());
        ack.set_reason("Acknowledged - execution handled by Python");
        
        // Estimate completion time based on market conditions
        int64_t base_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        // int completion_delay = CalculateCompletionDelay(intent, current_intel);
        // ack.set_estimated_completion_time(base_time + completion_delay);
        
        // Set execution parameters based on market intelligence
        // trading::ExecutionParameters* params = ack.mutable_execution_params();
        // SetIntelligentExecutionParameters(*params, intent, current_intel);
        
        // std::cout << "Execution Parameters:" << std::endl;
        // std::cout << "  Aggression Level: " << params->aggression_level() << std::endl;
        // std::cout << "  Chunk Size Ratio: " << params->chunk_size_ratio() << std::endl;
        // std::cout << "  Max Participation: " << params->max_participation_rate() << std::endl;
        // std::cout << "  Order Type: " << params->preferred_order_type() << std::endl;
        // std::cout << "=================================" << std::endl;
        
        return ack;
    }
    
    int CalculateCompletionDelay(const trading::TradingIntent& intent, 
                                const trading::MarketIntelligence& intel) {
        int base_delay = 15000; // 15 seconds base
        
        // Adjust for execution style
        switch (intent.execution_style()) {
            case trading::EXEC_AGGRESSIVE:
                base_delay = 5000;
                break;
            case trading::EXEC_STEALTH:
                base_delay = std::min(60000, intent.max_execution_time_ms());
                break;
            case trading::EXEC_PASSIVE:
                base_delay = 30000;
                break;
        }
        
        // Adjust for market regime
        switch (intent.market_regime()) {
            case trading::REGIME_EUPHORIA:
            case trading::REGIME_PANIC:
                base_delay *= 0.7; // Faster in high urgency regimes
                break;
            case trading::REGIME_ACCUMULATION:
            case trading::REGIME_SIDEWAYS:
                base_delay *= 1.5; // Slower in patient regimes
                break;
        }
        
        // Adjust for market conditions
        if (intel.current_regime() == trading::OB_THIN) {
            base_delay *= 2.0; // Much slower in thin markets
        } else if (intel.current_regime() == trading::OB_LIQUID) {
            base_delay *= 0.8; // Faster in liquid markets
        }
        
        return base_delay;
    }
    
    void SetIntelligentExecutionParameters(trading::ExecutionParameters& params,
                                          const trading::TradingIntent& intent,
                                          const trading::MarketIntelligence& intel) {
        // Base parameters by execution style
        switch (intent.execution_style()) {
            case trading::EXEC_AGGRESSIVE:
                params.set_chunk_size_ratio(0.4);
                params.set_aggression_level(0.9);
                params.set_max_chunk_delay_ms(500);
                params.set_preferred_order_type(trading::ORDER_MARKET);
                break;
                
            case trading::EXEC_BALANCED:
                params.set_chunk_size_ratio(0.25);
                params.set_aggression_level(0.6);
                params.set_max_chunk_delay_ms(2000);
                params.set_preferred_order_type(trading::ORDER_LIMIT);
                break;
                
            case trading::EXEC_PASSIVE:
                params.set_chunk_size_ratio(0.15);
                params.set_aggression_level(0.3);
                params.set_max_chunk_delay_ms(5000);
                params.set_preferred_order_type(trading::ORDER_LIMIT);
                params.set_post_only(true);
                break;
                
            case trading::EXEC_STEALTH:
                params.set_chunk_size_ratio(0.08);
                params.set_aggression_level(0.15);
                params.set_max_chunk_delay_ms(8000);
                params.set_preferred_order_type(trading::ORDER_HIDDEN);
                params.set_use_hidden_orders(true);
                break;
        }
        
        // Adjust based on market regime
        switch (intent.market_regime()) {
            case trading::REGIME_EUPHORIA:
            case trading::REGIME_PANIC:
                params.set_aggression_level(params.aggression_level() * 1.3);
                params.set_max_chunk_delay_ms(params.max_chunk_delay_ms() * 0.7);
                break;
                
            case trading::REGIME_ACCUMULATION:
            case trading::REGIME_SIDEWAYS:
                params.set_aggression_level(params.aggression_level() * 0.7);
                params.set_max_chunk_delay_ms(params.max_chunk_delay_ms() * 1.5);
                break;
        }
        
        // Adjust based on real-time market intelligence
        if (intel.symbol() == intent.symbol()) {
            // Liquidity adjustments
            if (intel.liquidity_ratio() < 0.3) {
                params.set_chunk_size_ratio(params.chunk_size_ratio() * 0.5);
                params.set_aggression_level(params.aggression_level() * 0.7);
            } else if (intel.liquidity_ratio() > 0.8) {
                params.set_chunk_size_ratio(params.chunk_size_ratio() * 1.2);
            }
            
            // Order book regime adjustments
            switch (intel.current_regime()) {
                case trading::OB_THIN:
                    params.set_max_participation_rate(0.05);
                    params.set_use_hidden_orders(true);
                    break;
                case trading::OB_LIQUID:
                    params.set_max_participation_rate(0.25);
                    break;
                case trading::OB_VOLATILE:
                    params.set_max_chunk_delay_ms(params.max_chunk_delay_ms() * 0.5);
                    break;
            }
            
            // Spread-based adjustments
            if (intel.bid_ask_spread() > 0.005) {
                params.set_aggression_level(params.aggression_level() * 0.8);
            }
        }
        
        // Apply intent-specific parameters
        params.set_aggression_level(params.aggression_level() * (0.5 + intent.confidence()));
        params.set_max_chunk_delay_ms(static_cast<int32_t>(
            params.max_chunk_delay_ms() * (1.5 - intent.urgency_factor())));
        
        // Set remaining parameters
        params.set_price_improvement_threshold(0.001 / std::max(0.1, intent.confidence()));
        params.set_time_in_force_ms(std::min(30000, intent.max_execution_time_ms() / 2));
        
        // Ensure bounds
        params.set_aggression_level(std::min(1.0, std::max(0.1, params.aggression_level())));
        params.set_chunk_size_ratio(std::min(0.5, std::max(0.05, params.chunk_size_ratio())));
        
        if (params.max_participation_rate() == 0) {
            params.set_max_participation_rate(0.15);
        }
        params.set_max_participation_rate(std::min(0.3, std::max(0.05, params.max_participation_rate())));
    }

    void PrintStatus() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_).count();
        
        std::cout << "\n=== INTEGRATED SERVER STATUS ===" << std::endl;
        std::cout << "Runtime: " << elapsed << "s" << std::endl;
        std::cout << "Messages processed: " << messages_processed_.load() << std::endl;
        std::cout << "Intelligence generated: " << intelligence_generated_.load() << std::endl;
        std::cout << "Rate: " << (elapsed > 0 ? messages_processed_.load() / elapsed : 0) << " msg/s" << std::endl;
        
        // Active symbols
        {
            std::lock_guard<std::mutex> lock(intelligence_mutex_);
            std::cout << "Active symbols: " << latest_intelligence_.size() << std::endl;
            
            for (const auto& [symbol, intel] : latest_intelligence_) {
                std::cout << "  " << symbol << ": " 
                          << "spread=" << intel.bid_ask_spread() 
                          << ", liq=" << intel.liquidity_ratio()
                          << ", regime=" << intel.current_regime() << std::endl;
            }
        }
        std::cout << "================================" << std::endl;
    }
};

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "=== INTEGRATED TRADING SERVER ===" << std::endl;
    
    try {
        IntegratedTradingServer server;
        /*                
        symbols:
            # TIER 1: Major Caps (High liquidity, ETF correlation)
            - "BTCUSDT"    # Bitcoin - Direct ETF correlation
            - "ETHUSDT"    # Ethereum - Major DeFi
            # TIER 2: Large Caps (Strong institutional interest)
            - "SOLUSDT"    # Solana - High performance blockchain
            - "XRPUSDT"    # Ripple - Payment network
            - "ADAUSDT"    # Cardano - Academic blockchain
            - "AVAXUSDT"   # Avalanche - DeFi ecosystem
            - "DOTUSDT"    # Polkadot - Interoperability
            - "LINKUSDT"   # Chainlink - Oracle network
            - "UNIUSDT"    # Uniswap - DEX protocol
            # TIER 3: Mid Caps (Growing ecosystems)
            - "ATOMUSDT"   # Cosmos - Internet of blockchains
            - "ALGOUSDT"   # Algorand - Pure proof of stake
            - "AAVEUSDT"   # Aave - DeFi lending
            # TIER 4: Layer 2 & Scaling (High growth potential)
            - "ARBUSDT"    # Arbitrum - Ethereum L2
            - "OPUSDT"     # Optimism - Ethereum L2  
            - "INJUSDT"    # Injective - DeFi derivatives
            - "SUIUSDT"    # Sui - High performance blockchain
            # TIER 5: Specialized/Oracle/Infrastructure
            - "TRBUSDT"    # Tellor - Oracle network
            - "BANDUSDT"   # Band Protocol - Oracle
            - "CHZUSDT"    # Chiliz - Sports/entertainment
            - "ENJUSDT"    # Enjin - Gaming/NFT
            # TIER 6: DeFi Protocols (High volatility, regime-sensitive)
            - "YFIUSDT"    # Yearn Finance - Yield farming
            - "1INCHUSDT"  # 1inch - DEX aggregator
            # TIER 7: Emerging/Trending (High beta to market regimes)
            - "DOGEUSDT"   # Dogecoin - Payment/meme
            - "WIFUSDT"    # Dogwifhat - Solana meme
            */
        // Start with a subset of symbols for testing
        std::vector<std::string> symbols = {
            "BTCUSDT", "ETHUSDT", "SOLUSDT", "XRPUSDT", "ADAUSDT",
            "AVAXUSDT", "DOTUSDT", "LINKUSDT", "UNIUSDT", "ATOMUSDT",
            "ALGOUSDT", "AAVEUSDT", "ARBUSDT", "OPUSDT", "INJUSDT",
            "SUIUSDT", "TRBUSDT", "BANDUSDT", "CHZUSDT", "ENJUSDT",
            "YFIUSDT", "1INCHUSDT", "DOGEUSDT", "WIFUSDT"
        };
        
        if (!server.Start(symbols)) {
            std::cerr << "Failed to start server" << std::endl;
            return 1;
        }
        
        std::cout << "\nServer running. Processing " << symbols.size() << " symbols..." << std::endl;
        // std::cout << "Market data pipeline: WebSocket -> OrderBook -> Intelligence -> ZMQ" << std::endl;
        std::cout << "Press Ctrl+C to stop gracefully..." << std::endl;
        
        // Main loop
        server.RunMainLoop();
        
        std::cout << "\nShutting down..." << std::endl;
        server.Stop();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Server shutdown complete." << std::endl;
    return 0;
}