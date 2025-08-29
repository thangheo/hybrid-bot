// cpp/src/trading_server.cpp
// Complete C++ ZMQ Trading Server Implementation

#include <zmq.hpp>
#include <thread>
#include <chrono>
#include <iostream>
#include <string>
#include <memory>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <random>
#include <cstring>
#include <unordered_map>
#include <functional>
#include <csignal>

// Include generated protobuf headers
#include "proto/generated/cpp/trading_messages.pb.h"

class TradingCommunicationServer {
private:
    zmq::context_t context_;
    std::unique_ptr<zmq::socket_t> intelligence_publisher_;
    std::unique_ptr<zmq::socket_t> intent_receiver_;
    
    std::atomic<bool> running_;
    std::thread intelligence_thread_;
    std::thread intent_handler_thread_;
    
    // Market intelligence generation
    std::mutex intel_queue_mutex_;
    std::queue<trading::MarketIntelligence> intel_queue_;
    std::condition_variable intel_cv_;
    
    // Execution acknowledgment callback
    std::function<trading::ExecutionAcknowledgment(const trading::TradingIntent&)> execution_callback_;

public:
    TradingCommunicationServer() : context_(1), running_(false) {
        InitializeSockets();
    }
    
    ~TradingCommunicationServer() {
        Stop();
    }
    
    void InitializeSockets() {
        try {
            // Market Intelligence Publisher (PUB socket)
            intelligence_publisher_ = std::make_unique<zmq::socket_t>(context_, ZMQ_PUB);
            intelligence_publisher_->bind("tcp://*:5555");
            
            // Trading Intent Receiver (REP socket) 
            intent_receiver_ = std::make_unique<zmq::socket_t>(context_, ZMQ_REP);
            intent_receiver_->bind("tcp://*:5556");
            
            std::cout << "ZMQ Server initialized successfully" << std::endl;
            std::cout << "Market Intelligence Publisher: tcp://*:5555" << std::endl;
            std::cout << "Trading Intent Receiver: tcp://*:5556" << std::endl;
            
        } catch (const zmq::error_t& e) {
            std::cerr << "Failed to initialize ZMQ sockets: " << e.what() << std::endl;
            throw;
        }
    }
    
    void SetExecutionCallback(std::function<trading::ExecutionAcknowledgment(const trading::TradingIntent&)> callback) {
        execution_callback_ = callback;
    }
    
    void Start() {
        running_ = true;
        
        // Start market intelligence publishing thread
        intelligence_thread_ = std::thread([this]() {
            this->IntelligencePublisherLoop();
        });
        
        // Start trading intent handler thread
        intent_handler_thread_ = std::thread([this]() {
            this->IntentHandlerLoop();
        });
        
        std::cout << "Trading communication server started" << std::endl;
    }
    
    void Stop() {
        running_ = false;
        intel_cv_.notify_all();
        
        if (intelligence_thread_.joinable()) {
            intelligence_thread_.join();
        }
        if (intent_handler_thread_.joinable()) {
            intent_handler_thread_.join();
        }
        
        std::cout << "Trading communication server stopped" << std::endl;
    }
    
    void PublishMarketIntelligence(const trading::MarketIntelligence& intel) {
        {
            std::lock_guard<std::mutex> lock(intel_queue_mutex_);
            intel_queue_.push(intel);
        }
        intel_cv_.notify_one();
    }

private:
    void IntelligencePublisherLoop() {
        while (running_) {
            std::unique_lock<std::mutex> lock(intel_queue_mutex_);
            
            // Wait for market intelligence to publish
            intel_cv_.wait(lock, [this]() { 
                return !intel_queue_.empty() || !running_; 
            });
            
            if (!running_) break;
            
            // Process all pending market intelligence
            while (!intel_queue_.empty()) {
                trading::MarketIntelligence intel = intel_queue_.front();
                intel_queue_.pop();
                lock.unlock();
                
                PublishMarketIntelligenceSync(intel);
                
                lock.lock();
            }
        }
    }
    
    void PublishMarketIntelligenceSync(const trading::MarketIntelligence& intel) {
        try {
            std::string serialized = intel.SerializeAsString();
            
            // Send topic first, then data
            zmq::message_t topic_msg(strlen("MARKET_INTEL"));
            memcpy(topic_msg.data(), "MARKET_INTEL", strlen("MARKET_INTEL"));
            
            zmq::message_t data_msg(serialized.size());
            memcpy(data_msg.data(), serialized.c_str(), serialized.size());
            
            intelligence_publisher_->send(topic_msg, zmq::send_flags::sndmore);
            intelligence_publisher_->send(data_msg, zmq::send_flags::none);
            
        } catch (const zmq::error_t& e) {
            std::cerr << "Failed to publish market intelligence: " << e.what() << std::endl;
        }
    }
    
    void IntentHandlerLoop() {
        while (running_) {
            try {
                zmq::message_t request;
                zmq::recv_result_t result = intent_receiver_->recv(request, zmq::recv_flags::dontwait);
                
                if (result) {
                    // Parse trading intent
                    trading::TradingIntent intent;
                    std::string data(static_cast<char*>(request.data()), request.size());
                    
                    if (intent.ParseFromString(data)) {
                        std::cout << "Received trading intent for " << intent.symbol() 
                                 << " (ID: " << intent.intent_id() << ")" << std::endl;
                        
                        // Process intent and generate acknowledgment
                        trading::ExecutionAcknowledgment ack;
                        if (execution_callback_) {
                            ack = execution_callback_(intent);
                        } else {
                            // Default acknowledgment
                            ack = CreateDefaultAcknowledgment(intent);
                        }
                        
                        // Send acknowledgment back
                        std::string ack_data = ack.SerializeAsString();
                        zmq::message_t reply(ack_data.size());
                        memcpy(reply.data(), ack_data.c_str(), ack_data.size());
                        intent_receiver_->send(reply, zmq::send_flags::none);
                        
                    } else {
                        std::cerr << "Failed to parse trading intent" << std::endl;
                        SendErrorAcknowledgment("Failed to parse message");
                    }
                }
                
                // Small sleep to prevent busy waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                
            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN) {  // EAGAIN is expected for timeout
                    std::cerr << "ZMQ error in intent handler: " << e.what() << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
    }
    
    trading::ExecutionAcknowledgment CreateDefaultAcknowledgment(const trading::TradingIntent& intent) {
        trading::ExecutionAcknowledgment ack;
        ack.set_intent_id(intent.intent_id());
        ack.set_accepted(true);
        ack.set_execution_id("exec_" + intent.intent_id());
        ack.set_reason("Accepted for execution");
        
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        ack.set_estimated_completion_time(now + intent.max_execution_time_ms());
        
        // Set execution parameters
        trading::ExecutionParameters* params = ack.mutable_execution_params();
        params->set_chunk_size_ratio(0.2);
        params->set_aggression_level(0.5);
        params->set_max_chunk_delay_ms(2000);
        params->set_price_improvement_threshold(0.001);
        params->set_use_hidden_orders(false);
        params->set_max_participation_rate(0.15);
        params->set_preferred_order_type(trading::ORDER_LIMIT);
        params->set_post_only(true);
        params->set_time_in_force_ms(30000);
        
        return ack;
    }
    
    void SendErrorAcknowledgment(const std::string& error_reason) {
        trading::ExecutionAcknowledgment ack;
        ack.set_intent_id("unknown");
        ack.set_accepted(false);
        ack.set_reason(error_reason);
        
        std::string ack_data = ack.SerializeAsString();
        zmq::message_t reply(ack_data.size());
        memcpy(reply.data(), ack_data.c_str(), ack_data.size());
        intent_receiver_->send(reply, zmq::send_flags::none);
    }
};

// Market Intelligence Generator for testing/simulation
class MarketIntelligenceGenerator {
private:
    TradingCommunicationServer* server_;
    std::vector<std::string> symbols_;
    std::atomic<bool> running_;
    std::thread generation_thread_;
    std::mt19937 rng_;

public:
    MarketIntelligenceGenerator(TradingCommunicationServer* server, 
                               const std::vector<std::string>& symbols)
        : server_(server), symbols_(symbols), running_(false) {
        rng_.seed(std::chrono::steady_clock::now().time_since_epoch().count());
    }
    
    void Start() {
        running_ = true;
        generation_thread_ = std::thread([this]() {
            this->GenerationLoop();
        });
        std::cout << "Market intelligence generator started" << std::endl;
    }
    
    void Stop() {
        running_ = false;
        if (generation_thread_.joinable()) {
            generation_thread_.join();
        }
        std::cout << "Market intelligence generator stopped" << std::endl;
    }

private:
    void GenerationLoop() {
        int message_count = 0;
        
        while (running_) {
            for (const auto& symbol : symbols_) {
                trading::MarketIntelligence intel = GenerateRealisticIntelligence(symbol);
                server_->PublishMarketIntelligence(intel);
                message_count++;
                
                if (message_count % 50 == 0) {
                    std::cout << "Published " << message_count << " market intelligence messages" << std::endl;
                }
            }
            
            // Publish every 100ms (10Hz per symbol)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    trading::MarketIntelligence GenerateRealisticIntelligence(const std::string& symbol) {
        trading::MarketIntelligence intel;
        
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        intel.set_symbol(symbol);
        intel.set_timestamp(now);
        
        // Generate realistic market microstructure data
        std::uniform_real_distribution<double> spread_dist(0.005, 0.025);
        intel.set_bid_ask_spread(spread_dist(rng_));
        
        std::uniform_real_distribution<double> imbalance_dist(-0.8, 0.8);
        intel.set_order_book_imbalance(imbalance_dist(rng_));
        
        std::uniform_real_distribution<double> depth_imbalance_dist(-0.5, 0.5);
        intel.set_depth_imbalance(depth_imbalance_dist(rng_));
        
        std::uniform_real_distribution<double> liquidity_dist(0.2, 1.0);
        intel.set_liquidity_ratio(liquidity_dist(rng_));
        
        // Flow metrics
        std::normal_distribution<double> flow_dist(0.0, 1.5);
        intel.set_net_flow_5s(flow_dist(rng_));
        intel.set_net_flow_30s(flow_dist(rng_) * 0.8);
        intel.set_net_flow_5m(flow_dist(rng_) * 0.6);
        
        // Buy/sell ratios
        std::uniform_real_distribution<double> buy_ratio_dist(0.2, 0.8);
        double aggressive_buy_ratio = buy_ratio_dist(rng_);
        intel.set_aggressive_buy_ratio(aggressive_buy_ratio);
        intel.set_aggressive_sell_ratio(1.0 - aggressive_buy_ratio);
        
        double passive_buy_ratio = buy_ratio_dist(rng_);
        intel.set_passive_buy_ratio(passive_buy_ratio);
        intel.set_passive_sell_ratio(1.0 - passive_buy_ratio);
        
        // Volatility indicators
        std::uniform_real_distribution<double> vol_dist(0.005, 0.08);
        intel.set_realized_volatility_1m(vol_dist(rng_));
        intel.set_realized_volatility_5m(vol_dist(rng_) * 1.2);
        
        std::uniform_real_distribution<double> impact_dist(0.001, 0.01);
        intel.set_price_impact_estimate(impact_dist(rng_));
        
        // Volume analysis
        std::uniform_real_distribution<double> volume_5s_dist(100, 5000);
        intel.set_volume_5s(volume_5s_dist(rng_));
        intel.set_volume_30s(intel.volume_5s() * 6);
        
        std::uniform_real_distribution<double> volume_ratio_dist(0.5, 2.0);
        intel.set_volume_ratio_to_avg(volume_ratio_dist(rng_));
        
        // Regime classification
        intel.set_current_regime(ClassifyOrderBookRegime(intel));
        
        std::uniform_real_distribution<double> confidence_dist(0.6, 0.95);
        intel.set_regime_confidence(confidence_dist(rng_));
        
        return intel;
    }
    
    trading::OrderBookRegime ClassifyOrderBookRegime(const trading::MarketIntelligence& intel) {
        if (intel.liquidity_ratio() > 0.8 && intel.bid_ask_spread() < 0.01) {
            return trading::OB_LIQUID;
        } else if (intel.realized_volatility_1m() > 0.04 || std::abs(intel.order_book_imbalance()) > 0.5) {
            return trading::OB_VOLATILE;
        } else if (intel.liquidity_ratio() < 0.3 || intel.bid_ask_spread() > 0.02) {
            return trading::OB_THIN;
        } else {
            return trading::OB_NORMAL;
        }
    }
};

// Enhanced execution handler
class ExecutionHandler {
public:
    trading::ExecutionAcknowledgment HandleTradingIntent(const trading::TradingIntent& intent) {
        std::cout << "\n=== Processing Trading Intent ===" << std::endl;
        std::cout << "Symbol: " << intent.symbol() << std::endl;
        std::cout << "Side: " << (intent.side() == trading::SIDE_BUY ? "BUY" : "SELL") << std::endl;
        std::cout << "Quantity: " << intent.target_quantity() << std::endl;
        std::cout << "Signal Strength: " << intent.signal_strength() << std::endl;
        std::cout << "Market Regime: " << MarketRegimeToString(intent.market_regime()) << std::endl;
        
        // Validate intent
        if (!ValidateIntent(intent)) {
            return CreateRejectionAcknowledgment(intent, "Intent validation failed");
        }
        
        trading::ExecutionAcknowledgment ack;
        ack.set_intent_id(intent.intent_id());
        ack.set_accepted(true);
        ack.set_execution_id("exec_" + intent.intent_id() + "_" + 
                           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ack.set_reason("Intent accepted for execution");
        
        // Calculate completion time
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        int64_t completion_delay = CalculateCompletionDelay(intent);
        ack.set_estimated_completion_time(now + completion_delay);
        
        // Set execution parameters
        trading::ExecutionParameters* params = ack.mutable_execution_params();
        SetExecutionParameters(*params, intent);
        
        std::cout << "Execution accepted with " << params->aggression_level() 
                 << " aggression level" << std::endl;
        std::cout << "================================\n" << std::endl;
        
        return ack;
    }

private:
    bool ValidateIntent(const trading::TradingIntent& intent) {
        return !intent.symbol().empty() && intent.target_quantity() > 0 &&
               intent.signal_strength() >= 0.0 && intent.signal_strength() <= 1.0 &&
               intent.confidence() >= 0.0 && intent.confidence() <= 1.0;
    }
    
    trading::ExecutionAcknowledgment CreateRejectionAcknowledgment(
        const trading::TradingIntent& intent, const std::string& reason) {
        trading::ExecutionAcknowledgment ack;
        ack.set_intent_id(intent.intent_id());
        ack.set_accepted(false);
        ack.set_reason(reason);
        return ack;
    }
    
    int64_t CalculateCompletionDelay(const trading::TradingIntent& intent) {
        int64_t base_delay = 15000; // 15 seconds default
        
        switch (intent.execution_style()) {
            case trading::EXEC_AGGRESSIVE:
                base_delay = 5000;
                break;
            case trading::EXEC_STEALTH:
                base_delay = std::min(60000LL, (int64_t)intent.max_execution_time_ms());
                break;
            case trading::EXEC_PASSIVE:
                base_delay = 30000;
                break;
        }
        
        // Adjust for urgency
        base_delay *= (1.5 - intent.urgency_factor());
        
        return base_delay;
    }
    
    void SetExecutionParameters(trading::ExecutionParameters& params, 
                              const trading::TradingIntent& intent) {
        // Base parameters by style
        SetBaseParametersByStyle(params, intent.execution_style());
        
        // Apply regime adjustments
        ApplyMarketRegimeAdjustments(params, intent.market_regime());
        
        // Apply confidence/urgency adjustments
        double confidence = intent.confidence();
        double urgency = intent.urgency_factor();
        
        params.set_aggression_level(params.aggression_level() * (0.5 + confidence * 0.5));
        params.set_max_chunk_delay_ms(params.max_chunk_delay_ms() * (1.5 - urgency));
        
        // Apply risk constraints
        if (intent.max_market_impact() > 0 && intent.max_market_impact() < 0.02) {
            params.set_chunk_size_ratio(params.chunk_size_ratio() * 0.7);
        }
        
        // Ensure bounds
        params.set_aggression_level(std::min(1.0, std::max(0.1, params.aggression_level())));
        params.set_chunk_size_ratio(std::min(0.5, std::max(0.05, params.chunk_size_ratio())));
    }
    
    void SetBaseParametersByStyle(trading::ExecutionParameters& params, trading::ExecutionStyle style) {
        switch (style) {
            case trading::EXEC_AGGRESSIVE:
                params.set_chunk_size_ratio(0.35);
                params.set_aggression_level(0.9);
                params.set_max_chunk_delay_ms(500);
                params.set_preferred_order_type(trading::ORDER_MARKET);
                params.set_post_only(false);
                params.set_max_participation_rate(0.3);
                break;
                
            case trading::EXEC_BALANCED:
                params.set_chunk_size_ratio(0.2);
                params.set_aggression_level(0.6);
                params.set_max_chunk_delay_ms(2000);
                params.set_preferred_order_type(trading::ORDER_LIMIT);
                params.set_post_only(false);
                params.set_max_participation_rate(0.2);
                break;
                
            case trading::EXEC_PASSIVE:
                params.set_chunk_size_ratio(0.12);
                params.set_aggression_level(0.3);
                params.set_max_chunk_delay_ms(5000);
                params.set_preferred_order_type(trading::ORDER_LIMIT);
                params.set_post_only(true);
                params.set_max_participation_rate(0.1);
                break;
                
            case trading::EXEC_STEALTH:
                params.set_chunk_size_ratio(0.06);
                params.set_aggression_level(0.15);
                params.set_max_chunk_delay_ms(8000);
                params.set_preferred_order_type(trading::ORDER_HIDDEN);
                params.set_use_hidden_orders(true);
                params.set_max_participation_rate(0.05);
                break;
        }
        
        params.set_price_improvement_threshold(0.001);
        params.set_time_in_force_ms(30000);
    }
    
    void ApplyMarketRegimeAdjustments(trading::ExecutionParameters& params, trading::MarketRegime regime) {
        switch (regime) {
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
                
            default:
                break;
        }
    }
    
    std::string MarketRegimeToString(trading::MarketRegime regime) {
        switch (regime) {
            case trading::REGIME_ACCUMULATION: return "ACCUMULATION";
            case trading::REGIME_INSTITUTIONAL_FOMO: return "INSTITUTIONAL_FOMO";
            case trading::REGIME_EUPHORIA: return "EUPHORIA";
            case trading::REGIME_SIDEWAYS: return "SIDEWAYS";
            case trading::REGIME_BALANCED: return "BALANCED";
            case trading::REGIME_UNCERTAIN: return "UNCERTAIN";
            case trading::REGIME_DISTRIBUTION: return "DISTRIBUTION";
            case trading::REGIME_PROFIT_TAKING: return "PROFIT_TAKING";
            case trading::REGIME_PANIC: return "PANIC";
            default: return "UNKNOWN";
        }
    }
};

// Signal handler for graceful shutdown
std::atomic<bool> shutdown_requested(false);

void SignalHandler(int signal) {
    std::cout << "\nShutdown signal received..." << std::endl;
    shutdown_requested = true;
}

// Main server application
int main(int argc, char* argv[]) {
    // Install signal handlers
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    
    try {
        std::cout << "========================================" << std::endl;
        std::cout << "Hybrid Trading Communication Server" << std::endl;
        std::cout << "========================================" << std::endl;
        
        auto version = zmq::version();
        std::cout << "ZMQ Version: " << std::get<0>(version) << "." 
                 << std::get<1>(version) << "." << std::get<2>(version) << std::endl;
        
        // Parse symbols from command line
        std::vector<std::string> symbols = {"AAPL", "GOOGL", "MSFT", "NVDA", "TSLA"};
        if (argc > 1) {
            symbols.clear();
            for (int i = 1; i < argc; ++i) {
                symbols.emplace_back(argv[i]);
            }
        }
        
        std::cout << "Monitoring symbols: ";
        for (const auto& symbol : symbols) {
            std::cout << symbol << " ";
        }
        std::cout << std::endl;
        
        // Initialize server
        TradingCommunicationServer server;
        
        // Set up execution handler
        ExecutionHandler execution_handler;
        server.SetExecutionCallback([&execution_handler](const trading::TradingIntent& intent) {
            return execution_handler.HandleTradingIntent(intent);
        });
        
        // Start server
        server.Start();
        
        // Initialize market intelligence generator
        MarketIntelligenceGenerator intel_generator(&server, symbols);
        intel_generator.Start();
        
        std::cout << "\nServer Status: RUNNING" << std::endl;
        std::cout << "Press Ctrl+C to stop..." << std::endl;
        
        // Main loop
        while (!shutdown_requested) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // Graceful shutdown
        std::cout << "\nShutting down..." << std::endl;
        intel_generator.Stop();
        server.Stop();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Server shutdown complete." << std::endl;
    return 0;
}