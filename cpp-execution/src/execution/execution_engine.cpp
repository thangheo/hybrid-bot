// ================================================================
// C++ Execution Engine for Hybrid Trading System
// File: execution_engine.cpp
// ================================================================

#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include <thread>
#include <atomic>
#include <unordered_map>
#include <queue>
#include <mutex>
#include <chrono>
#include <string>
#include <vector>
#include <iostream>

using json = nlohmann::json;
using namespace std::chrono;

// ================================================================
// MESSAGE STRUCTURES
// ================================================================

enum class TradeSide {
    BUY,
    SELL
};

enum class ExecutionStyle {
    AGGRESSIVE,
    BALANCED, 
    PASSIVE,
    STEALTH
};

enum class MarketRegime {
    ACCUMULATION,
    INSTITUTIONAL_FOMO,
    EUPHORIA,
    SIDEWAYS,
    BALANCED,
    UNCERTAIN,
    DISTRIBUTION,
    PROFIT_TAKING,
    PANIC
};

enum class VolatilityRegime {
    LOW_VOL,
    MEDIUM_VOL,
    HIGH_VOL
};

enum class ETFFlowRegime {
    POSITIVE_FLOW,
    NEUTRAL_FLOW,
    NEGATIVE_FLOW
};

enum class ExecutionState {
    PENDING,
    IN_PROGRESS,
    COMPLETED,
    PARTIALLY_FILLED,
    CANCELLED,
    FAILED
};

enum class OrderBookRegime {
    LIQUID,
    NORMAL,
    THIN,
    VOLATILE
};

struct TradingIntent {
    std::string intent_id;
    std::string symbol;
    TradeSide side;
    double target_quantity;
    double max_price = 0.0;
    double min_price = 0.0;
    MarketRegime market_regime = MarketRegime::BALANCED;
    VolatilityRegime volatility_regime = VolatilityRegime::MEDIUM_VOL;
    ETFFlowRegime etf_flow_regime = ETFFlowRegime::NEUTRAL_FLOW;
    double signal_strength = 0.5;
    double confidence = 0.5;
    int64_t signal_timestamp = 0;
    std::string strategy_id = "default";
    ExecutionStyle execution_style = ExecutionStyle::BALANCED;
    int max_execution_time_ms = 60000;
    double max_slippage_tolerance = 0.01;
    bool allow_partial_fills = true;
    double max_market_impact = 0.005;
    double urgency_factor = 0.5;
};

struct ExecutionStatus {
    std::string intent_id;
    ExecutionState state;
    double filled_quantity = 0.0;
    double remaining_quantity = 0.0;
    double average_fill_price = 0.0;
    double total_fees = 0.0;
    double current_bid = 0.0;
    double current_ask = 0.0;
    double order_book_imbalance = 0.0;
    double estimated_slippage = 0.0;
    double realized_slippage = 0.0;
    double market_impact = 0.0;
    int64_t execution_start_time = 0;
    int64_t last_update_time = 0;
    OrderBookRegime ob_regime = OrderBookRegime::NORMAL;
    double liquidity_score = 0.5;
};

struct MarketIntelligence {
    std::string symbol;
    int64_t timestamp;
    double bid_ask_spread;
    double order_book_imbalance;
    double depthImbalance;
    double liquidity_ratio;
    double net_flow_5s;
    double net_flow_30s;
    double aggressive_buy_ratio;
    double aggressive_sell_ratio;
    double realized_volatility_1m;
    double price_impact_estimate;
    OrderBookRegime current_regime = OrderBookRegime::NORMAL;
    double regime_confidence = 0.5;
};

struct OrderBookLevel {
    double price;
    double quantity;
    int order_count;
};

struct OrderBook {
    std::string symbol;
    int64_t timestamp;
    std::vector<OrderBookLevel> bids;
    std::vector<OrderBookLevel> asks;
    
    double get_bid() const { return bids.empty() ? 0.0 : bids[0].price; }
    double get_ask() const { return asks.empty() ? 0.0 : asks[0].price; }
    double get_spread() const { 
        double bid = get_bid();
        double ask = get_ask();
        return (ask > 0 && bid > 0) ? ask - bid : 0.0;
    }
    double get_mid_price() const {
        double bid = get_bid();
        double ask = get_ask();
        return (ask > 0 && bid > 0) ? (bid + ask) / 2.0 : 0.0;
    }
};

// ================================================================
// ORDER BOOK ANALYZER
// ================================================================

class OrderBookAnalyzer {
private:
    std::unordered_map<std::string, std::queue<OrderBook>> history_;
    std::mutex history_mutex_;
    
    static constexpr size_t MAX_HISTORY_SIZE = 100;
    
public:
    MarketIntelligence analyze_order_book(const OrderBook& ob) {
        MarketIntelligence intel{};
        intel.symbol = ob.symbol;
        intel.timestamp = ob.timestamp;
        
        // Basic spread analysis
        intel.bid_ask_spread = ob.get_spread();
        
        // Calculate order book imbalance
        intel.order_book_imbalance = calculate_imbalance(ob);
        
        // Calculate depth imbalance
        intel.depthImbalance = calculate_depth_imbalance(ob);
        
        // Calculate liquidity ratio
        intel.liquidity_ratio = calculate_liquidity_ratio(ob);
        
        // Calculate flow metrics (requires historical data)
        std::lock_guard<std::mutex> lock(history_mutex_);
        calculate_flow_metrics(ob, intel);
        
        // Update history
        auto& hist = history_[ob.symbol];
        hist.push(ob);
        if (hist.size() > MAX_HISTORY_SIZE) {
            hist.pop();
        }
        
        // Classify regime
        intel.current_regime = classify_order_book_regime(ob, intel);
        
        return intel;
    }
    
private:
    double calculate_imbalance(const OrderBook& ob) {
        if (ob.bids.empty() || ob.asks.empty()) return 0.0;
        
        double bid_volume = 0.0;
        double ask_volume = 0.0;
        
        // Sum top 5 levels
        for (size_t i = 0; i < std::min(size_t(5), ob.bids.size()); ++i) {
            bid_volume += ob.bids[i].quantity;
        }
        for (size_t i = 0; i < std::min(size_t(5), ob.asks.size()); ++i) {
            ask_volume += ob.asks[i].quantity;
        }
        
        double total_volume = bid_volume + ask_volume;
        if (total_volume == 0.0) return 0.0;
        
        return (bid_volume - ask_volume) / total_volume;
    }
    
    double calculate_depth_imbalance(const OrderBook& ob) {
        if (ob.bids.empty() || ob.asks.empty()) return 0.0;
        
        // Calculate weighted imbalance considering price distance
        double weighted_bid = 0.0;
        double weighted_ask = 0.0;
        double mid_price = ob.get_mid_price();
        
        if (mid_price == 0.0) return 0.0;
        
        for (const auto& bid : ob.bids) {
            double distance_weight = 1.0 - std::abs(bid.price - mid_price) / mid_price;
            weighted_bid += bid.quantity * distance_weight;
        }
        
        for (const auto& ask : ob.asks) {
            double distance_weight = 1.0 - std::abs(ask.price - mid_price) / mid_price;
            weighted_ask += ask.quantity * distance_weight;
        }
        
        double total_weighted = weighted_bid + weighted_ask;
        return total_weighted > 0.0 ? (weighted_bid - weighted_ask) / total_weighted : 0.0;
    }
    
    double calculate_liquidity_ratio(const OrderBook& ob) {
        if (ob.bids.empty() || ob.asks.empty()) return 0.0;
        
        double total_volume = 0.0;
        for (const auto& bid : ob.bids) total_volume += bid.quantity;
        for (const auto& ask : ob.asks) total_volume += ask.quantity;
        
        double spread = ob.get_spread();
        double mid_price = ob.get_mid_price();
        
        if (mid_price == 0.0 || spread == 0.0) return 0.0;
        
        // Higher volume and tighter spread = higher liquidity
        double spread_ratio = spread / mid_price;
        return total_volume / (1.0 + spread_ratio * 1000.0);
    }
    
    void calculate_flow_metrics(const OrderBook& current, MarketIntelligence& intel) {
        auto& hist = history_[current.symbol];
        if (hist.size() < 2) {
            intel.net_flow_5s = 0.0;
            intel.net_flow_30s = 0.0;
            intel.aggressive_buy_ratio = 0.5;
            intel.aggressive_sell_ratio = 0.5;
            return;
        }
        
        // Simplified flow calculation
        // In practice, you'd track actual trades and classify as aggressive/passive
        double price_change = current.get_mid_price() - hist.back().get_mid_price();
        double volume_change = calculate_volume_change(current, hist.back());
        
        intel.net_flow_5s = price_change * volume_change;
        intel.net_flow_30s = intel.net_flow_5s; // Simplified
        intel.aggressive_buy_ratio = price_change > 0 ? 0.7 : 0.3;
        intel.aggressive_sell_ratio = 1.0 - intel.aggressive_buy_ratio;
    }
    
    double calculate_volume_change(const OrderBook& current, const OrderBook& previous) {
        double current_vol = 0.0, prev_vol = 0.0;
        
        for (const auto& bid : current.bids) current_vol += bid.quantity;
        for (const auto& ask : current.asks) current_vol += ask.quantity;
        
        for (const auto& bid : previous.bids) prev_vol += bid.quantity;
        for (const auto& ask : previous.asks) prev_vol += ask.quantity;
        
        return current_vol - prev_vol;
    }
    
    OrderBookRegime classify_order_book_regime(const OrderBook& ob, const MarketIntelligence& intel) {
        if (intel.liquidity_ratio < 0.3) return OrderBookRegime::THIN;
        if (intel.realized_volatility_1m > 0.05) return OrderBookRegime::VOLATILE;
        if (intel.liquidity_ratio > 0.8) return OrderBookRegime::LIQUID;
        return OrderBookRegime::NORMAL;
    }
};

// ================================================================
// EXECUTION ENGINE
// ================================================================

class ExecutionEngine {
private:
    std::unique_ptr<zmq::context_t> zmq_context_;
    std::unique_ptr<zmq::socket_t> intent_responder_;
    std::unique_ptr<zmq::socket_t> intelligence_publisher_;
    std::unique_ptr<zmq::socket_t> status_publisher_;
    
    OrderBookAnalyzer analyzer_;
    std::unordered_map<std::string, OrderBook> current_order_books_;
    std::unordered_map<std::string, ExecutionStatus> active_executions_;
    
    std::atomic<bool> running_{false};
    std::vector<std::thread> worker_threads_;
    
public:
    ExecutionEngine() {
        zmq_context_ = std::make_unique<zmq::context_t>(1);
    }
    
    ~ExecutionEngine() {
        stop();
    }
    
    bool initialize(const json& config) {
        try {
            // Initialize ZeroMQ sockets
            intent_responder_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_REP);
            intelligence_publisher_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
            status_publisher_ = std::make_unique<zmq::socket_t>(*zmq_context_, ZMQ_PUB);
            
            // Bind sockets
            std::string intent_endpoint = config.value("intent_endpoint", "tcp://*:5556");
            std::string intel_endpoint = config.value("intelligence_endpoint", "tcp://*:5555");
            std::string status_endpoint = config.value("status_endpoint", "tcp://*:5557");
            
            intent_responder_->bind(intent_endpoint);
            intelligence_publisher_->bind(intel_endpoint);
            status_publisher_->bind(status_endpoint);
            
            printf("C++ Execution Engine initialized successfully\n");
            return true;
            
        } catch (const std::exception& e) {
            printf("Failed to initialize execution engine: %s\n", e.what());
            return false;
        }
    }
    
    void start() {
        running_ = true;
        
        // Start worker threads
        worker_threads_.emplace_back(&ExecutionEngine::intent_handler_loop, this);
        worker_threads_.emplace_back(&ExecutionEngine::market_data_loop, this);
        worker_threads_.emplace_back(&ExecutionEngine::execution_loop, this);
        
        printf("C++ Execution Engine started\n");
    }
    
    void stop() {
        running_ = false;
        
        for (auto& thread : worker_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        worker_threads_.clear();
        printf("C++ Execution Engine stopped\n");
    }
    
private:
    void intent_handler_loop() {
        while (running_) {
            try {
                zmq::message_t request;
                auto result = intent_responder_->recv(request, zmq::recv_flags::dontwait);
                
                if (result) {
                    std::string message_str(static_cast<char*>(request.data()), request.size());
                    json intent_json = json::parse(message_str);
                    
                    // Process trading intent
                    json response = process_trading_intent(intent_json);
                    
                    std::string response_str = response.dump();
                    zmq::message_t reply(response_str.size());
                    memcpy(reply.data(), response_str.c_str(), response_str.size());
                    intent_responder_->send(reply, zmq::send_flags::dontwait);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                
            } catch (const std::exception& e) {
                printf("Error in intent handler: %s\n", e.what());
            }
        }
    }
    
    void market_data_loop() {
        while (running_) {
            try {
                // Simulate order book updates (replace with real exchange data)
                for (const auto& [symbol, ob] : current_order_books_) {
                    MarketIntelligence intel = analyzer_.analyze_order_book(ob);
                    publish_market_intelligence(intel);
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                
            } catch (const std::exception& e) {
                printf("Error in market data loop: %s\n", e.what());
            }
        }
    }
    
    void execution_loop() {
        while (running_) {
            try {
                // Process active executions
                for (auto& [intent_id, status] : active_executions_) {
                    if (status.state == ExecutionState::IN_PROGRESS) {
                        update_execution_status(status);
                        publish_execution_status(status);
                    }
                }
                
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                
            } catch (const std::exception& e) {
                printf("Error in execution loop: %s\n", e.what());
            }
        }
    }
    
    json process_trading_intent(const json& intent_json) {
        try {
            TradingIntent intent;
            intent.intent_id = intent_json.value("intent_id", "");
            intent.symbol = intent_json.value("symbol", "");
            intent.target_quantity = intent_json.value("target_quantity", 0.0);
            intent.signal_strength = intent_json.value("signal_strength", 0.5);
            intent.confidence = intent_json.value("confidence", 0.5);
            
            // Map execution style
            std::string style_str = intent_json.value("execution_style", "BALANCED");
            intent.execution_style = map_execution_style(style_str);
            
            // Create execution status
            ExecutionStatus status;
            status.intent_id = intent.intent_id;
            status.state = ExecutionState::IN_PROGRESS;
            status.remaining_quantity = intent.target_quantity;
            status.execution_start_time = duration_cast<milliseconds>(
                system_clock::now().time_since_epoch()
            ).count();
            
            active_executions_[intent.intent_id] = status;
            
            printf("Processing trading intent: %s for %s\n", 
                   intent.intent_id.c_str(), intent.symbol.c_str());
            
            return json{{"status", "accepted"}, {"intent_id", intent.intent_id}};
            
        } catch (const std::exception& e) {
            return json{{"status", "rejected"}, {"reason", e.what()}};
        }
    }
    
    ExecutionStyle map_execution_style(const std::string& style_str) {
        if (style_str == "AGGRESSIVE") return ExecutionStyle::AGGRESSIVE;
        if (style_str == "PASSIVE") return ExecutionStyle::PASSIVE;
        if (style_str == "STEALTH") return ExecutionStyle::STEALTH;
        return ExecutionStyle::BALANCED;
    }
    
    void update_execution_status(ExecutionStatus& status) {
        // Simulate execution progress
        if (status.remaining_quantity > 0) {
            double fill_amount = std::min(status.remaining_quantity, 
                                        status.remaining_quantity * 0.1);
            
            status.filled_quantity += fill_amount;
            status.remaining_quantity -= fill_amount;
            status.average_fill_price = 50000.0; // Simulated price
            
            if (status.remaining_quantity <= 0.001) {
                status.state = ExecutionState::COMPLETED;
                printf("Execution completed: %s\n", status.intent_id.c_str());
            }
        }
        
        status.last_update_time = duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count();
    }
    
    void publish_market_intelligence(const MarketIntelligence& intel) {
        try {
            json intel_json{
                {"symbol", intel.symbol},
                {"timestamp", intel.timestamp},
                {"bid_ask_spread", intel.bid_ask_spread},
                {"order_book_imbalance", intel.order_book_imbalance},
                {"depthImbalance", intel.depthImbalance},
                {"liquidity_ratio", intel.liquidity_ratio},
                {"net_flow_5s", intel.net_flow_5s},
                {"net_flow_30s", intel.net_flow_30s}
            };
            
            std::string topic = "MARKET_INTEL";
            std::string data = intel_json.dump();
            
            zmq::message_t topic_msg(topic.size());
            zmq::message_t data_msg(data.size());
            
            memcpy(topic_msg.data(), topic.c_str(), topic.size());
            memcpy(data_msg.data(), data.c_str(), data.size());
            
            intelligence_publisher_->send(topic_msg, zmq::send_flags::sndmore);
            intelligence_publisher_->send(data_msg, zmq::send_flags::dontwait);
            
        } catch (const std::exception& e) {
            printf("Error publishing market intelligence: %s\n", e.what());
        }
    }
    
    void publish_execution_status(const ExecutionStatus& status) {
        try {
            json status_json{
                {"intent_id", status.intent_id},
                {"state", static_cast<int>(status.state)},
                {"filled_quantity", status.filled_quantity},
                {"remaining_quantity", status.remaining_quantity},
                {"average_fill_price", status.average_fill_price},
                {"realized_slippage", status.realized_slippage}
            };
            
            std::string topic = "EXEC_STATUS";
            std::string data = status_json.dump();
            
            zmq::message_t topic_msg(topic.size());
            zmq::message_t data_msg(data.size());
            
            memcpy(topic_msg.data(), topic.c_str(), topic.size());
            memcpy(data_msg.data(), data.c_str(), data.size());
            
            status_publisher_->send(topic_msg, zmq::send_flags::sndmore);
            status_publisher_->send(data_msg, zmq::send_flags::dontwait);
            
        } catch (const std::exception& e) {
            printf("Error publishing execution status: %s\n", e.what());
        }
    }
};

// ================================================================
// MAIN FUNCTION
// ================================================================
//avoid conflict with main
int main_(int argc, char* argv[]) {
    printf("Starting C++ Execution Engine...\n");
    
    // Load configuration
    json config{
        {"intent_endpoint", "tcp://*:5556"},
        {"intelligence_endpoint", "tcp://*:5555"},
        {"status_endpoint", "tcp://*:5557"}
    };
    
    // Create and initialize engine
    ExecutionEngine engine;
    if (!engine.initialize(config)) {
        printf("Failed to initialize execution engine\n");
        return 1;
    }
    
    // Start engine
    engine.start();
    
    // Wait for shutdown signal
    printf("Press Enter to stop...\n");
    std::cin.get();

    // Stop engine
    engine.stop();
    
    printf("C++ Execution Engine stopped\n");
    return 0;
}