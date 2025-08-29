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
#include "trading_messages.pb.h"

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
    
    void IntentHandlerLoop() {
        while (running_) {
            try {
                // Set timeout for non-blocking receive
                int timeout_ms = 100;
                intelligence_publisher_->set(zmq::sockopt::rcvtimeo, timeout_ms);
                
                zmq::message_t request;
                auto result = intent_receiver_->recv(request, zmq::recv_flags::dontwait);
                
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
        ack.set_estimated_completion_time(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count() + intent.max_execution_time_ms()
        );
        
        // Set default execution parameters
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

// Market Intelligence Generator for simulation/testing
class MarketIntelligenceGenerator {
private:
    TradingCommunicationServer* server_;
    std::vector<std::string> symbols_;
    std::atomic<bool> running_;
    std::thread generation_thread_;

public:
    MarketIntelligenceGenerator(TradingCommunicationServer* server, 
                               const std::vector<std::string>& symbols)
        : server_(server), symbols_(symbols), running_(false) {}
    
    void Start() {
        running_ = true;
        generation_thread_ = std::thread([this]() {
            this->GenerationLoop();
        });
    }
    
    void Stop() {
        running_ = false;
        if (generation_thread_.joinable()) {
            generation_thread_.join();
        }
    }

private:
    void GenerationLoop() {
        while (running_) {
            for (const auto& symbol : symbols_) {
                trading::MarketIntelligence intel = GenerateSimulatedIntelligence(symbol);
                server_->PublishMarketIntelligence(intel);
            }
            
            // Publish every 100ms
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    trading::MarketIntelligence GenerateSimulatedIntelligence(const std::string& symbol) {
        trading::MarketIntelligence intel;
        
        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        intel.set_symbol(symbol);
        intel.set_timestamp(now);
        
        // Simulate realistic market data
        double base_spread = 0.01 + (rand() % 100) / 10000.0;  // 0.01-0.02
        intel.set_bid_ask_spread(base_spread);
        
        double imbalance = (rand() % 200 - 100) / 100.0;  // -1.0 to 1.0
        intel.set_order_book_imbalance(imbalance);
        
        double liquidity = 0.3 + (rand() % 70) / 100.0;  // 0.3-1.0
        intel.set_liquidity_ratio(liquidity);
        
        double volatility = 0.005 + (rand() % 50) / 1000.0;  // 0.005-0.055
        intel.set_realized_volatility_1m(volatility);
        
        // Flow metrics
        double net_flow_5s = (rand() % 200 - 100) / 50.0;  // -2.0 to 2.0
        intel.set_net_flow_5s(net_flow_5s);
        intel.set_net_flow_30s(net_flow_5s * 0.8);  // Smoother over longer period
        
        double buy_ratio = 0.3 + (rand() % 40) / 100.0;  // 0.3-0.7
        intel.set_aggressive_buy_ratio(buy_ratio);
        intel.set_aggressive_sell_ratio(1.0 - buy_ratio);
        
        // Regime classification
        if (volatility > 0.03) {
            intel.set_current_regime(trading::OB_VOLATILE);
        } else if (liquidity > 0.8) {
            intel.set_current_regime(trading::OB_LIQUID);
        } else if (liquidity < 0.4) {
            intel.set_current_regime(trading::OB_THIN);
        } else {
            intel.set_current_regime(trading::OB_NORMAL);
        }
        
        intel.set_regime_confidence(0.6 + (rand() % 40) / 100.0);
        
        return intel;
    }
};

// Custom execution handler example
class SimpleExecutionHandler {
public:
    trading::ExecutionAcknowledgment HandleTradingIntent(const trading::TradingIntent& intent) {
        std::cout << "Processing trading intent:" << std::endl;
        std::cout << "  Symbol: " << intent.symbol() << std::endl;
        std::cout << "  Side: " << (intent.side() == trading::SIDE_BUY ? "BUY" : "SELL") << std::endl;
        std::cout << "  Quantity: " << intent.target_quantity() << std::endl;
        std::cout << "  Market Regime: " << intent.market_regime() << std::endl;
        std::cout << "  Signal Strength: " << intent.signal_strength() << std::endl;
        
        trading::ExecutionAcknowledgment ack;
        ack.set_intent_id(intent.intent_id());
        ack.set_accepted(true);
        ack.set_execution_id("exec_" + intent.intent_id() + "_" + std::to_string(time(nullptr)));
        ack.set_reason("Intent accepted and queued for execution");
        
        // Estimate completion time based on execution style and market conditions
        int64_t estimated_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        
        if (intent.execution_style() == trading::EXEC_AGGRESSIVE) {
            estimated_time += 5000;  // 5 seconds for aggressive
        } else if (intent.execution_style() == trading::EXEC_STEALTH) {
            estimated_time += std::min(60000, intent.max_execution_time_ms());  // Up to 1 minute
        } else {
            estimated_time += 15000;  // 15 seconds default
        }
        
        ack.set_estimated_completion_time(estimated_time);
        
        // Set execution parameters based on intent
        trading::ExecutionParameters* params = ack.mutable_execution_params();
        SetExecutionParameters(*params, intent);
        
        return ack;
    }

private:
    void SetExecutionParameters(trading::ExecutionParameters& params, const trading::TradingIntent& intent) {
        // Base parameters by execution style
        switch (intent.execution_style()) {
            case trading::EXEC_AGGRESSIVE:
                params.set_chunk_size_ratio(0.4);
                params.set_aggression_level(0.9);
                params.set_max_chunk_delay_ms(500);
                params.set_preferred_order_type(trading::ORDER_MARKET);
                params.set_post_only(false);
                break;
                
            case trading::EXEC_BALANCED:
                params.set_chunk_size_ratio(0.25);
                params.set_aggression_level(0.6);
                params.set_max_chunk_delay_ms(2000);
                params.set_preferred_order_type(trading::ORDER_LIMIT);
                params.set_post_only(false);
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
                params.set_post_only(true);
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
                
            default:
                // No adjustment for balanced/uncertain regimes
                break;
        }
        
        // Adjust based on urgency and confidence
        double urgency_factor = intent.urgency_factor();
        double confidence = intent.confidence();
        
        params.set_aggression_level(params.aggression_level() * (0.5 + confidence));
        params.set_max_chunk_delay_ms(params.max_chunk_delay_ms() * (1.5 - urgency_factor));
        
        // Set other parameters
        params.set_price_improvement_threshold(0.001 / confidence);
        params.set_max_participation_rate(0.15 * confidence);
        params.set_time_in_force_ms(std::min(30000, intent.max_execution_time_ms() / 2));
        
        // Ensure values stay within reasonable bounds
        params.set_aggression_level(std::min(1.0, std::max(0.1, params.aggression_level())));
        params.set_chunk_size_ratio(std::min(0.5, std::max(0.05, params.chunk_size_ratio())));
        params.set_max_participation_rate(std::min(0.3, std::max(0.05, params.max_participation_rate())));
    }
};

// Main server application
int main() {
    try {
        std::cout << "Starting Trading Communication Server..." << std::endl;
        auto version = zmq::version();
        std::cout << "ZMQ Version: " << std::get<0>(version) << "." << std::get<1>(version) << "." << std::get<2>(version) << std::endl;
        
        // Initialize server
        TradingCommunicationServer server;
        
        // Set up execution handler
        SimpleExecutionHandler execution_handler;
        server.SetExecutionCallback([&execution_handler](const trading::TradingIntent& intent) {
            return execution_handler.HandleTradingIntent(intent);
        });
        
        // Start server
        server.Start();
        
        // Initialize market intelligence generator for testing
        std::vector<std::string> symbols = {"AAPL", "GOOGL", "MSFT", "NVDA", "TSLA"};
        MarketIntelligenceGenerator intel_generator(&server, symbols);
        intel_generator.Start();
        
        std::cout << "Server running. Press Enter to stop..." << std::endl;
        std::cin.get();
        
        // Graceful shutdown
        intel_generator.Stop();
        server.Stop();
        
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Server shutdown complete." << std::endl;
    return 0;
}   
