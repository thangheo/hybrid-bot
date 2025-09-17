/**
 * @file zmq_server_example.cpp
 * @brief Example usage of the Generic ZeroMQ Server
 * 
 * This file demonstrates how to use the GenericZmqServer for various
 * trading system components and message types.
 * 
 * @author Trading System Team
 * @date September 17, 2025
 */

#include "communication/zmq_server.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>
#include <random>

using namespace trading::communication;

/**
 * Example: Risk Management Request Handler
 */
std::string riskValidationHandler(const std::string& request_data, 
                                 const std::unordered_map<std::string, std::string>& metadata) {
    try {
        // Parse the risk validation request
        auto request = nlohmann::json::parse(request_data);
        
        std::string order_id = request.value("order_id", "");
        std::string symbol = request.value("symbol", "");
        double quantity = request.value("quantity", 0.0);
        double price = request.value("price", 0.0);
        
        // Simulate risk validation logic
        bool approved = true;
        std::string reason = "Order approved";
        
        // Simple validation rules
        if (quantity > 10000.0) {
            approved = false;
            reason = "Order size exceeds limit";
        }
        
        if (price <= 0.0) {
            approved = false;
            reason = "Invalid price";
        }
        
        // Create response
        nlohmann::json response;
        response["order_id"] = order_id;
        response["approved"] = approved;
        response["reason"] = reason;
        response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        
        if (approved) {
            response["validated_quantity"] = quantity;
            response["validated_price"] = price;
        }
        
        return response.dump();
        
    } catch (const std::exception& e) {
        nlohmann::json error_response;
        error_response["error"] = true;
        error_response["message"] = "Failed to process risk validation: " + std::string(e.what());
        return error_response.dump();
    }
}

/**
 * Example: Strategy Parameter Request Handler
 */
std::string strategyParameterHandler(const std::string& request_data,
                                   const std::unordered_map<std::string, std::string>& metadata) {
    try {
        auto request = nlohmann::json::parse(request_data);
        
        std::string strategy_id = request.value("strategy_id", "");
        std::string action = request.value("action", "get");
        
        nlohmann::json response;
        response["strategy_id"] = strategy_id;
        
        if (action == "get") {
            // Return current strategy parameters
            response["parameters"] = {
                {"imbalance_threshold", 0.15},
                {"signal_decay_time", 5000},
                {"max_position_size", 1000.0},
                {"risk_multiplier", 1.2}
            };
        } else if (action == "update") {
            // Update parameters (in real system, persist to database)
            response["status"] = "updated";
            response["message"] = "Parameters updated successfully";
        }
        
        return response.dump();
        
    } catch (const std::exception& e) {
        nlohmann::json error_response;
        error_response["error"] = true;
        error_response["message"] = "Failed to process strategy parameter request: " + std::string(e.what());
        return error_response.dump();
    }
}

/**
 * Example: Market Data Publishing
 */
void publishMarketData(GenericZmqServer& server) {
    // Simulate publishing different types of market data
    std::vector<std::string> symbols = {"BTC/USDT", "ETH/USDT", "ADA/USDT", "SOL/USDT"};
    std::vector<std::string> exchanges = {"binance", "coinbase", "ftx", "kraken"};
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> price_dist(100.0, 70000.0);
    std::uniform_real_distribution<> volume_dist(0.1, 1000.0);
    std::uniform_int_distribution<> symbol_dist(0, symbols.size() - 1);
    std::uniform_int_distribution<> exchange_dist(0, exchanges.size() - 1);
    
    while (server.isRunning()) {
        try {
            std::string symbol = symbols[symbol_dist(gen)];
            std::string exchange = exchanges[exchange_dist(gen)];
            
            // Create order book update
            nlohmann::json orderbook;
            orderbook["symbol"] = symbol;
            orderbook["exchange"] = exchange;
            orderbook["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            // Add bid levels
            orderbook["bids"] = nlohmann::json::array();
            double base_price = price_dist(gen);
            for (int i = 0; i < 5; ++i) {
                orderbook["bids"].push_back({
                    {"price", base_price - i * 0.01},
                    {"quantity", volume_dist(gen)}
                });
            }
            
            // Add ask levels
            orderbook["asks"] = nlohmann::json::array();
            for (int i = 0; i < 5; ++i) {
                orderbook["asks"].push_back({
                    {"price", base_price + (i + 1) * 0.01},
                    {"quantity", volume_dist(gen)}
                });
            }
            
            // Publish order book update
            std::string topic = "market.orderbook." + exchange + "." + symbol;
            server.publishAsync(topic, orderbook.dump(), MessagePriority::HIGH);
            
            // Create and publish trade data
            nlohmann::json trade;
            trade["symbol"] = symbol;
            trade["exchange"] = exchange;
            trade["price"] = base_price + (gen() % 2 == 0 ? 0.005 : -0.005);
            trade["quantity"] = volume_dist(gen);
            trade["side"] = gen() % 2 == 0 ? "buy" : "sell";
            trade["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            
            topic = "market.trades." + exchange + "." + symbol;
            server.publishAsync(topic, trade.dump(), MessagePriority::NORMAL);
            
            // Occasionally publish strategy signals
            if (gen() % 10 == 0) {
                nlohmann::json signal;
                signal["signal_id"] = "sig_" + std::to_string(gen());
                signal["symbol"] = symbol;
                signal["direction"] = gen() % 2 == 0 ? "buy" : "sell";
                signal["strength"] = std::uniform_real_distribution<>(0.1, 1.0)(gen);
                signal["strategy_id"] = "imbalance_strategy_v1";
                signal["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                
                topic = "strategy.signals." + symbol;
                server.publishAsync(topic, signal.dump(), MessagePriority::HIGH);
            }
            
            // Occasionally publish risk events
            if (gen() % 20 == 0) {
                nlohmann::json risk_event;
                risk_event["event_type"] = "position_limit_warning";
                risk_event["symbol"] = symbol;
                risk_event["current_position"] = std::uniform_real_distribution<>(500.0, 1000.0)(gen);
                risk_event["position_limit"] = 1000.0;
                risk_event["utilization"] = 0.85;
                risk_event["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                
                topic = "risk.events." + symbol;
                server.publishAsync(topic, risk_event.dump(), MessagePriority::CRITICAL);
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error publishing market data: " << e.what() << std::endl;
        }
        
        // Publish at different frequencies based on message type
        std::this_thread::sleep_for(std::chrono::milliseconds(100 + gen() % 400));  // 100-500ms
    }
}

/**
 * Example: System Monitoring
 */
void publishSystemMetrics(GenericZmqServer& server) {
    while (server.isRunning()) {
        try {
            // Get server statistics
            const auto& stats = server.getStatistics();
            
            nlohmann::json metrics;
            metrics["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            metrics["component"] = "zmq_server";
            metrics["metrics"] = {
                {"messages_published", stats.messages_published.load()},
                {"messages_queued", stats.messages_queued.load()},
                {"messages_dropped", stats.messages_dropped.load()},
                {"requests_processed", stats.requests_processed.load()},
                {"requests_failed", stats.requests_failed.load()},
                {"bytes_sent", stats.bytes_sent.load()},
                {"bytes_received", stats.bytes_received.load()},
                {"queue_size", server.getQueueSize()},
                {"queue_utilization", server.getQueueUtilization()},
                {"health_status", server.healthCheck() ? "healthy" : "unhealthy"}
            };
            
            server.publishAsync("system.metrics.zmq_server", metrics.dump(), MessagePriority::LOW);
            
        } catch (const std::exception& e) {
            std::cerr << "Error publishing system metrics: " << e.what() << std::endl;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(30));  // Every 30 seconds
    }
}

int main() {
    try {
        std::cout << "Starting Generic ZMQ Server Example..." << std::endl;
        
        // Create server configuration
        ZmqServerConfig config;
        config.publisher_endpoint = "tcp://*:5555";
        config.request_endpoint = "tcp://*:5556";
        config.publisher_threads = 3;
        config.request_handler_threads = 2;
        config.max_queue_size = 50000;
        config.enable_monitoring = true;
        config.enable_heartbeat = true;
        config.heartbeat_interval = std::chrono::seconds(15);
        
        // Create and start server
        GenericZmqServer server(config);
        
        // Register request handlers
        server.registerRequestHandler("risk_validation", riskValidationHandler);
        server.registerRequestHandler("strategy_parameters", strategyParameterHandler);
        
        // Add topic filters (optional - for demonstration)
        server.addTopicFilter("market\\..*");      // Allow all market data
        server.addTopicFilter("strategy\\..*");    // Allow all strategy data  
        server.addTopicFilter("risk\\..*");        // Allow all risk data
        server.addTopicFilter("system\\..*");      // Allow all system data
        
        // Start the server
        if (!server.start()) {
            std::cerr << "Failed to start ZMQ server" << std::endl;
            return 1;
        }
        
        std::cout << "ZMQ Server started successfully!" << std::endl;
        std::cout << "Publisher endpoint: " << config.publisher_endpoint << std::endl;
        std::cout << "Request endpoint: " << config.request_endpoint << std::endl;
        
        // Start data publishing threads
        std::thread market_data_thread(publishMarketData, std::ref(server));
        std::thread system_metrics_thread(publishSystemMetrics, std::ref(server));
        
        // Run for demonstration period
        std::cout << "Publishing data for 60 seconds..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(60));
        
        std::cout << "Stopping server..." << std::endl;
        
        // Stop the server (this will signal all threads to stop)
        server.stop();
        
        // Wait for publishing threads to finish
        if (market_data_thread.joinable()) {
            market_data_thread.join();
        }
        if (system_metrics_thread.joinable()) {
            system_metrics_thread.join();
        }
        
        // Print final statistics
        const auto& final_stats = server.getStatistics();
        std::cout << "\n=== Final Statistics ===" << std::endl;
        std::cout << "Messages Published: " << final_stats.messages_published.load() << std::endl;
        std::cout << "Messages Queued: " << final_stats.messages_queued.load() << std::endl;
        std::cout << "Messages Dropped: " << final_stats.messages_dropped.load() << std::endl;
        std::cout << "Requests Processed: " << final_stats.requests_processed.load() << std::endl;
        std::cout << "Requests Failed: " << final_stats.requests_failed.load() << std::endl;
        std::cout << "Bytes Sent: " << final_stats.bytes_sent.load() << std::endl;
        std::cout << "Bytes Received: " << final_stats.bytes_received.load() << std::endl;
        std::cout << "Connection Errors: " << final_stats.connection_errors.load() << std::endl;
        std::cout << "Queue Overflows: " << final_stats.queue_overflows.load() << std::endl;
        
        std::cout << "Example completed successfully!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error in main: " << e.what() << std::endl;
        return 1;
    }
}
