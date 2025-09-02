/**
 * @file integrated_trading_server.cpp
 * @brief Integrated server combining pipeline processing with ZMQ communication
 */

// #include "data_ingestion/pipeline/normalization_pipeline.hpp"
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/publisher/zeromq_publisher.hpp"
#include "data_ingestion/protocol/binance_adapter.hpp"
#include "data_ingestion/config.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
// #include "data_ingestion/orderbook/reconstructor.hpp"
#include "common/exchange_types.hpp"
#include "core/market_types.hpp"
#include "communication/zmq_server.hpp"
#include "trading_messages.pb.h"
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
    
    // Communication components
    std::unique_ptr<TradingCommunicationServer> zmq_server_;
    
    // Market intelligence tracking
    std::unordered_map<std::string, trading::MarketIntelligence> latest_intelligence_;
    std::mutex intelligence_mutex_;
    
    // Order book tracking
    std::unordered_map<std::string, std::shared_ptr<OrderBook>> order_books_;
    std::mutex orderbook_mutex_;
    
    // Statistics
    std::atomic<uint64_t> messages_processed_{0};
    std::atomic<uint64_t> intelligence_generated_{0};
    std::chrono::steady_clock::time_point start_time_;

public:
    IntegratedTradingServer() : start_time_(std::chrono::steady_clock::now()) {
        // InitializePipeline();
        InitializeConnectionPool();
        InitializeCommunication();
    }
    
    ~IntegratedTradingServer() {
        Stop();
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
            2, 4
        );
    }
  
    void GenerateAndPublishIntelligence(const std::string& symbol, std::shared_ptr<OrderBook> orderbook) {
        try {
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

    void InitializeCommunication() {
        zmq_server_ = std::make_unique<TradingCommunicationServer>();
        
        // Set execution callback
        zmq_server_->SetExecutionCallback([this](const trading::TradingIntent& intent) {
            return this->HandleTradingIntent(intent);
        });
        
        std::cout << "Communication server initialized" << std::endl;
    }
    
    bool Start(const std::vector<std::string>& symbols) {
        std::cout << "Starting Integrated Trading Server..." << std::endl;
        // Setup WebSocket connections
        connectionPool_->initialize();
        if (!connectionPool_->connectAll()) {
            std::cerr << "Failed to establish WebSocket connections" << std::endl;
            return false;
        }
        // Initialize order books for symbols
        for (const auto& symbol : symbols) {
            order_books_[symbol] = std::make_shared<OrderBook>(
                ExchangeId::binance(),  // Replace with the appropriate ExchangeId
                Symbol::fromString(symbol),
                nullptr               // Pass nullptr for the callback if not needed
            );
        }      
        // Wait for connections to be ready
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        if ((connectionPool_->getActiveConnectionCount() < 2)) {
            std::cerr << "Not all WebSocket connections are active" << std::endl;
            return false;
        }        
        // Start ZMQ communication
        zmq_server_->Start();
        
        // Acquire and subscribe connections
        int connectionsNeeded = std::min(static_cast<int>(symbols.size() / 5 + 1), 4);
        
        for (int i = 0; i < connectionsNeeded; ++i) {
            auto conn = connectionPool_->acquireConnection(10000);
            if (conn && conn->isConnected()) {  // Check connection status
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
    
    void Stop() {
        g_running = false;
        
        // Stop ZMQ server
        if (zmq_server_) {
            zmq_server_->Stop();
        }
        
        // Release connections
        for (auto& conn : activeConnections_) {
            connectionPool_->releaseConnection(conn);
        }
        activeConnections_.clear();
        
        if (connectionPool_) {
            connectionPool_->shutdown();
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
            
            // Update order book
            std::lock_guard<std::mutex> lock(orderbook_mutex_);
            auto it = order_books_.find(symbol);
            if (it != order_books_.end()) {
                UpdateOrderBookFromJson(it->second, parsed,symbol);
                
                // Generate market intelligence from updated order book
                auto intel = GenerateMarketIntelligence(symbol, it->second);
                
                // Store latest intelligence
                {
                    std::lock_guard<std::mutex> intel_lock(intelligence_mutex_);
                    latest_intelligence_[symbol] = intel;
                }
                
                // Publish via ZMQ
                if (zmq_server_) {
                    zmq_server_->PublishMarketIntelligence(intel);
                    intelligence_generated_.fetch_add(1);
                }
            }
            
        } catch (const std::exception& e) {
            // Silently handle JSON parsing errors
        }
    }
        
    
    trading::MarketIntelligence GenerateMarketIntelligence(const std::string& symbol, 
                                                          std::shared_ptr<OrderBook> orderbook) {
        trading::MarketIntelligence intel;
        
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        intel.set_symbol(symbol);
        intel.set_timestamp(now);
        
        // Get order book state
        auto bids = orderbook->getBids(5);
        auto asks = orderbook->getAsks(5);
        
        if (!bids.empty() && !asks.empty()) {
            double best_bid = bids[0].getPrice();
            double best_ask = asks[0].getPrice();
            double spread = best_ask - best_bid;
            double mid_price = (best_bid + best_ask) / 2.0;
            
            intel.set_bid_ask_spread(spread / mid_price);
            
            // Calculate order book imbalance
            double bid_volume = 0, ask_volume = 0;
            for (size_t i = 0; i < std::min(size_t(5), bids.size()); ++i) {
                bid_volume += bids[i].getQuantity();
            }
            for (size_t i = 0; i < std::min(size_t(5), asks.size()); ++i) {
                ask_volume += asks[i].getQuantity();
            }
            
            double total_volume = bid_volume + ask_volume;
            if (total_volume > 0) {
                intel.set_order_book_imbalance((bid_volume - ask_volume) / total_volume);
            }
            
            // Calculate liquidity metrics
            intel.set_liquidity_ratio(std::min(1.0, total_volume / 10000.0));
            
            // Estimate volatility (simplified)
            intel.set_realized_volatility_1m(spread / mid_price * 10); // Rough estimate
            
            // Flow metrics (placeholder - would need trade data)
            intel.set_net_flow_5s(intel.order_book_imbalance() * 0.5);
            intel.set_net_flow_30s(intel.order_book_imbalance() * 0.3);
            intel.set_aggressive_buy_ratio(0.5 + intel.order_book_imbalance() * 0.2);
            intel.set_aggressive_sell_ratio(0.5 - intel.order_book_imbalance() * 0.2);
            
            // Classify order book regime
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
        }
        
        return intel;
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
        trading::ExecutionAcknowledgment ack;
        ack.set_intent_id(intent.intent_id());
        ack.set_accepted(true);
        ack.set_execution_id("exec_" + intent.intent_id() + "_" + std::to_string(std::time(nullptr)));
        ack.set_reason("Intent accepted - Market intelligence integrated");
        
        // Estimate completion time based on market conditions
        int64_t base_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        int completion_delay = CalculateCompletionDelay(intent, current_intel);
        ack.set_estimated_completion_time(base_time + completion_delay);
        
        // Set execution parameters based on market intelligence
        trading::ExecutionParameters* params = ack.mutable_execution_params();
        SetIntelligentExecutionParameters(*params, intent, current_intel);
        
        std::cout << "Execution Parameters:" << std::endl;
        std::cout << "  Aggression Level: " << params->aggression_level() << std::endl;
        std::cout << "  Chunk Size Ratio: " << params->chunk_size_ratio() << std::endl;
        std::cout << "  Max Participation: " << params->max_participation_rate() << std::endl;
        std::cout << "  Order Type: " << params->preferred_order_type() << std::endl;
        std::cout << "=================================" << std::endl;
        
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

    void UpdateOrderBookFromJson(std::shared_ptr<OrderBook> orderbook, const json& data, const std::string& symbol) {
        try {
            static std::atomic<int> debug_updates{0};
            bool is_debug = debug_updates.fetch_add(1) < 3;
            
            if (is_debug) {
                std::cout << "\n=== DEBUG: Processing depthUpdate ===" << std::endl;
                std::cout << "Full message: " << data.dump().substr(0, 500) << std::endl;
            }
            
            // Collect ALL bid and ask updates in vectors
            std::vector<trading::data_ingestion::PriceLevel> bid_updates;
            std::vector<trading::data_ingestion::PriceLevel> ask_updates;
            
            // Process ALL bids into vector
            if (data.contains("b") && data["b"].is_array()) {
                if (is_debug) {
                    std::cout << "Found bids array with " << data["b"].size() << " entries" << std::endl;
                }
                
                for (const auto& bid : data["b"]) {
                    if (bid.is_array() && bid.size() >= 2) {
                        try {
                            std::string priceStr = bid[0].get<std::string>();
                            std::string qtyStr = bid[1].get<std::string>();
                            
                            double price = std::stod(priceStr);
                            double quantity = std::stod(qtyStr);
                            
                            if (is_debug) {
                                std::cout << "Processing bid: " << priceStr << " @ " << qtyStr 
                                        << " (parsed: " << price << " @ " << quantity << ")" << std::endl;
                            }
                            
                            if (price > 0) {
                                // Add to bid updates vector (including zero quantity for removal)
                                bid_updates.emplace_back(price, quantity);
                                
                                if (is_debug) {
                                    if (quantity == 0.0) {
                                        std::cout << "  Will remove bid at " << price << std::endl;
                                    } else {
                                        std::cout << "  Will update bid: " << price << " @ " << quantity << std::endl;
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            if (is_debug) std::cout << "  Error parsing bid: " << e.what() << std::endl;
                        }
                    }
                }
            }
            
            // Process ALL asks into vector
            if (data.contains("a") && data["a"].is_array()) {
                if (is_debug) {
                    std::cout << "Found asks array with " << data["a"].size() << " entries" << std::endl;
                }
                
                for (const auto& ask : data["a"]) {
                    if (ask.is_array() && ask.size() >= 2) {
                        try {
                            std::string priceStr = ask[0].get<std::string>();
                            std::string qtyStr = ask[1].get<std::string>();
                            
                            double price = std::stod(priceStr);
                            double quantity = std::stod(qtyStr);
                            
                            if (is_debug) {
                                std::cout << "Processing ask: " << priceStr << " @ " << qtyStr 
                                        << " (parsed: " << price << " @ " << quantity << ")" << std::endl;
                            }
                            
                            if (price > 0) {
                                // Add to ask updates vector (including zero quantity for removal)  
                                ask_updates.emplace_back(price, quantity);
                                
                                if (is_debug) {
                                    if (quantity == 0.0) {
                                        std::cout << "  Will remove ask at " << price << std::endl;
                                    } else {
                                        std::cout << "  Will update ask: " << price << " @ " << quantity << std::endl;
                                    }
                                }
                            }
                        } catch (const std::exception& e) {
                            if (is_debug) std::cout << "  Error parsing ask: " << e.what() << std::endl;
                        }
                    }
                }
            }
            
            // Apply ALL updates at once using proper OrderBook method
            if (!bid_updates.empty() || !ask_updates.empty()) {
                // Get sequence number from message if available
                uint64_t sequence_num = 0;
                if (data.contains("u")) {
                    sequence_num = data["u"].get<uint64_t>();
                }
                
                // Apply the update
                bool success = orderbook->applyUpdate(
                    bid_updates, 
                    ask_updates, 
                    sequence_num, 
                    std::chrono::system_clock::now()
                );
                
                if (is_debug) {
                    std::cout << "Applied update with " << bid_updates.size() 
                            << " bid changes and " << ask_updates.size() << " ask changes" << std::endl;
                    std::cout << "Update success: " << (success ? "YES" : "NO") << std::endl;
                }
                
                if (success) {
                    // Verify order book state after update
                    auto bids = orderbook->getBids(5);
                    auto asks = orderbook->getAsks(5);
                    
                    if (is_debug) {
                        std::cout << "OrderBook state after update:" << std::endl;
                        std::cout << "  Bids count: " << bids.size() << std::endl;
                        std::cout << "  Asks count: " << asks.size() << std::endl;
                        
                        if (!bids.empty()) {
                            std::cout << "  Best bid: " << bids[0].getPrice() << " @ " << bids[0].getQuantity() << std::endl;
                        }
                        if (!asks.empty()) {
                            std::cout << "  Best ask: " << asks[0].getPrice() << " @ " << asks[0].getQuantity() << std::endl;
                        }
                    }
                    
                    // Generate intelligence only if we have valid data
                    if (!bids.empty() && !asks.empty() && bids[0].getPrice() > 0 && asks[0].getPrice() > 0) {
                        GenerateAndPublishIntelligence(symbol, orderbook);
                        if (is_debug) std::cout << "Intelligence generated and published" << std::endl;
                    } else {
                        if (is_debug) std::cout << "No intelligence generated - invalid order book state" << std::endl;
                    }
                } else {
                    if (is_debug) std::cout << "OrderBook update failed" << std::endl;
                }
            } else {
                if (is_debug) std::cout << "No price level updates to apply" << std::endl;
            }
            
            if (is_debug) {
                std::cout << "==================================" << std::endl;
            }
            
        } catch (const std::exception& e) {
            std::cout << "Error processing order book update: " << e.what() << std::endl;
        }
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
        
        // Start with a subset of symbols for testing
        std::vector<std::string> symbols = {
            "BTCUSDT",
            "ETHUSDT", 
            "BNBUSDT",
            "ADAUSDT",
            "SOLUSDT"
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