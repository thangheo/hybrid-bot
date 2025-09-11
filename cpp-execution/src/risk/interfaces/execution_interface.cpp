/**
 * Implementation of the interface for communication with the execution engine
 *
 * File: execution_interface.cpp
 * Created: 2025-04-29
 */

#include "interfaces/execution_interface.hpp"
#include <common/zeromq_client.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <chrono>
#include <mutex>
#include <iostream>
#include <iomanip>

namespace trading {
namespace risk {

// Helper to log with timestamp (temporary until we integrate with logging system)
void logWithTimestamp(const std::string& level, const std::string& message) {
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
        
    std::stringstream ss;
    ss << "[" << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    ss << "[" << level << "] " << message;
    std::cout << ss.str() << std::endl;
}

class ExecutionInterfaceImpl : public ExecutionInterface {

private:
    // Configuration
    std::string requestEndpoint_;
    std::string publishEndpoint_;
    std::string executionReportsTopic_;
    
    // ZeroMQ clients
    std::unique_ptr<common::ZeroMQClient> requestClient_;
    std::unique_ptr<common::ZeroMQClient> subscriptionClient_;
    
    // Callback for execution reports
    std::function<void(const ExecutionReport&)> executionReportCallback_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Flag to indicate if initialization was successful
    bool initialized_ = false;

    // Convert OrderRequest to JSON
    nlohmann::json orderRequestToJson(const OrderRequest& request) {
        nlohmann::json json;
        json["order_id"] = request.orderId;
        json["client_order_id"] = request.clientOrderId;
        json["exchangeId"] = request.exchangeId.toString();
        json["tradingPair"] = request.tradingPair.toString();
        json["side"] = request.side == OrderSide::BUY ? "buy" : "sell";
        json["type"] = request.type;
        json["quantity"] = request.quantity;
        json["price"] = request.price;
        json["time_in_force"] = request.timeInForce;
        json["strategy_id"] = request.strategyId;
        json["signal_id"] = request.signalId;
        json["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            request.timestamp.time_since_epoch()).count();
        
        // Add optional parameters if provided
        if (!request.params.empty()) {
            json["params"] = nlohmann::json::object();
            for (const auto& param : request.params) {
                json["params"][param.first] = param.second;
            }
        }
        
        return json;
    }

    // Parse OrderRequest from JSON
    OrderRequest jsonToOrderRequest(const nlohmann::json& json) {
        OrderRequest request;
        request.orderId = json["order_id"];
        request.clientOrderId = json["client_order_id"];
        request.exchangeId =  core::ExchangeId::fromString(json["exchangeId"]);
        request.tradingPair = core::Symbol::fromString(json["tradingPair"]);
        request.side = json["side"] == "buy" ? OrderSide::BUY : OrderSide::SELL;
        request.type = json["type"];
        request.quantity = json["quantity"];
        request.price = json["price"];
        request.timeInForce = json["time_in_force"];
        request.strategyId = json["strategy_id"];
        request.signalId = json["signal_id"];
        
        // Parse timestamp
        int64_t timestampMs = json["timestamp"];
        request.timestamp = std::chrono::system_clock::time_point(
            std::chrono::milliseconds(timestampMs));
        
        // Parse optional parameters if provided
        if (json.contains("params") && json["params"].is_object()) {
            for (auto it = json["params"].begin(); it != json["params"].end(); ++it) {
                request.params[it.key()] = it.value();
            }
        }
        
        return request;
    }

    // Parse ExecutionReport from JSON
    ExecutionReport jsonToExecutionReport(const nlohmann::json& json) {
        ExecutionReport report;
        report.setOrderId(json["order_id"]);
        report.setClientOrderId(json["client_order_id"]);
        report.setExchangeOrderId(json["exchange_order_id"]);
        report.setExchangeId(core::ExchangeId::fromString(json["exchangeId"]));
        report.setSymbol(core::Symbol::fromString(json["tradingPair"]));
        
        report.setSide(json["side"] == "buy" ? OrderSide::BUY : OrderSide::SELL);
        report.setType(json["type"]);
        report.setStatus(json["status"]);
        report.setQuantity(json["quantity"]);
        report.setFilledQuantity(json["filled_quantity"]);
        report.setPrice(json["price"]);
        report.setAverageFillPrice(json["average_fill_price"]);
        
        // Parse timestamp
        int64_t timestampMs = json["timestamp"];
        report.setCreatedAt(std::chrono::system_clock::time_point(
            std::chrono::milliseconds(timestampMs)));
        
        
        // Parse optional fields
        // if (json.contains("fee")) report.fee = json["fee"];
        // if (json.contains("fee_currency")) report.feeCurrency = json["fee_currency"];
        if (json.contains("reject_reason")) report.setRejectReason(json["reject_reason"]);
        // if (json.contains("fills") && json["fills"].is_array()) {
        //     for (const auto& fillJson : json["fills"]) {
        //         Fill fill;
        //         fill.price = fillJson["price"];
        //         fill.quantity = fillJson["quantity"];
        //         fill.timestamp = std::chrono::system_clock::time_point(
        //             std::chrono::milliseconds(fillJson["timestamp"]));
        //         report.addFill(fill);
        //     }
        // }
        
        return report;
    }

    // Handle received execution report messages
    void handleExecutionReportMessage(const std::string& topic, const std::string& message) {
        if (topic == executionReportsTopic_) {
            try {
                auto json = nlohmann::json::parse(message);
                auto report = jsonToExecutionReport(json);
                
                // Call the callback if registered
                if (executionReportCallback_) {
                    executionReportCallback_(report);
                }
            } catch (const std::exception& e) {
                logWithTimestamp("ERROR", "Error processing execution report: " + std::string(e.what()));
            }
        }
    }

    // Handle error messages
    void handleErrorMessage(const std::string& context, const std::string& error) {
        logWithTimestamp("ERROR", "ZeroMQ error in " + context + ": " + error);
    }

public:
    ExecutionInterfaceImpl() : ExecutionInterface() {
        // Load configuration
        // In a real implementation, this would be loaded from a config file
        requestEndpoint_ = "tcp://localhost:5556";
        publishEndpoint_ = "tcp://localhost:5559";
        executionReportsTopic_ = "execution_reports";
    }
    // with config
    ExecutionInterfaceImpl(const RiskConfig& config) : ExecutionInterfaceImpl() {
        // Use config to override defaults if needed
        // For example:
        if (!config.executionRequestEndpoint.empty()) {
            requestEndpoint_ = config.executionRequestEndpoint;
        }
        if (!config.executionPublishEndpoint.empty()) {
            publishEndpoint_ = config.executionPublishEndpoint;
        }
        if (!config.executionReportsTopic.empty()) {
            executionReportsTopic_ = config.executionReportsTopic;
        }
    }

    ~ExecutionInterfaceImpl() override {
        // Clean shutdown of ZeroMQ clients
        if (subscriptionClient_) {
            subscriptionClient_->stop();
        }
        if (requestClient_) {
            requestClient_->stop();
        }
    }
    std::map<std::string, double> getAccountBalances(const core::ExchangeId& exchangeId) override {
        if (!initialized_) {
            logWithTimestamp("ERROR", "Execution interface not initialized");
            return {};
        }
        
        try {
            // Create message
            // Create message - use exchangeId.id to get the string
            nlohmann::json request = {
                {"action", "get_account_balances"},
                {"exchangeId", exchangeId.toString()}
            };
            
            // Send request
            if (!requestClient_->publish("account", request.dump())) {
                logWithTimestamp("ERROR", "Failed to send get account balances request");
                return {};
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return an empty map
            return {};
        } catch (const std::exception& e) {
            logWithTimestamp("ERROR", "Failed to get account balances: " + std::string(e.what()));
            return {};
        }
    }
    bool initialize() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (initialized_) {
            return true;
        }
        
        try {
            // Configure ZeroMQ clients
            common::ZeroMQConfig requestConfig;
            requestConfig.publisherEndpoint = requestEndpoint_;
            
            common::ZeroMQConfig subscriptionConfig;
            subscriptionConfig.subscriberEndpoint = publishEndpoint_;
            subscriptionConfig.reconnectInterval = std::chrono::milliseconds(1000);
            subscriptionConfig.maxReconnectAttempts = 5;
            
            // Initialize request client
            requestClient_ = std::make_unique<common::ZeroMQClient>(requestConfig);
            
            // Initialize subscription client
            subscriptionClient_ = std::make_unique<common::ZeroMQClient>(subscriptionConfig);
            
            // Set message handler for subscription client
            subscriptionClient_->setMessageHandler(
                std::bind(&ExecutionInterfaceImpl::handleExecutionReportMessage, this, 
                          std::placeholders::_1, std::placeholders::_2));
            
            // Set error handler for both clients
            subscriptionClient_->setErrorHandler(
                std::bind(&ExecutionInterfaceImpl::handleErrorMessage, this,
                          std::placeholders::_1, std::placeholders::_2));
            requestClient_->setErrorHandler(
                std::bind(&ExecutionInterfaceImpl::handleErrorMessage, this,
                          std::placeholders::_1, std::placeholders::_2));
            
            // Start the clients
            if (!requestClient_->start()) {
                logWithTimestamp("ERROR", "Failed to start request client");
                return false;
            }
            
            if (!subscriptionClient_->start()) {
                logWithTimestamp("ERROR", "Failed to start subscription client");
                return false;
            }
            
            // Subscribe to the execution reports topic
            if (!subscriptionClient_->subscribe(executionReportsTopic_)) {
                logWithTimestamp("ERROR", "Failed to subscribe to execution reports topic");
                return false;
            }
            
            initialized_ = true;
            logWithTimestamp("INFO", "Execution interface initialized successfully");
            return true;
        } catch (const std::exception& e) {
            logWithTimestamp("ERROR", "Failed to initialize execution interface: " + std::string(e.what()));
            return false;
        }
    }
    
    std::string submitOrder(const OrderRequest& orderRequest) override {
        if (!initialized_) {
            logWithTimestamp("ERROR", "Execution interface not initialized");
            return "";
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "submit_order"},
                {"order", orderRequestToJson(orderRequest)}
            };
            
            // Send request
            if (!requestClient_->publish("orders", request.dump())) {
                logWithTimestamp("ERROR", "Failed to send submit order request");
                return "";
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return the client order ID
            return orderRequest.clientOrderId;
        } catch (const std::exception& e) {
            logWithTimestamp("ERROR", "Failed to submit order: " + std::string(e.what()));
            return "";
        }
    }
    
    bool cancelOrder(const std::string& orderId) override {
        if (!initialized_) {
            logWithTimestamp("ERROR", "Execution interface not initialized");
            return false;
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "cancel_order"},
                {"order_id", orderId}
            };
            
            // Send request
            if (!requestClient_->publish("orders", request.dump())) {
                logWithTimestamp("ERROR", "Failed to send cancel order request");
                return false;
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return true
            return true;
        } catch (const std::exception& e) {
            logWithTimestamp("ERROR", "Failed to cancel order: " + std::string(e.what()));
            return false;
        }
    }
    
    int cancelAllOrders(const core::ExchangeId& exchangeId) override {
        if (!initialized_) {
            logWithTimestamp("ERROR", "Execution interface not initialized");
            return 0;
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "cancel_all_orders"},
                {"exchangeId", exchangeId.toString()}
            };
            
            // Send request
            if (!requestClient_->publish("orders", request.dump())) {
                logWithTimestamp("ERROR", "Failed to send cancel all orders request");
                return 0;
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return 0
            return 0;
        } catch (const std::exception& e) {
            logWithTimestamp("ERROR", "Failed to cancel all orders: " + std::string(e.what()));
            return 0;
        }
    }
    // isConnected
    bool isConnected() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && requestClient_ && requestClient_->isConnected() &&
               subscriptionClient_ && subscriptionClient_->isConnected();
    }
    // // cancelAllOrders with reason
    // int cancelAllOrders(const core::ExchangeId& exchangeId, const std::string& reason) override {
    //     if (!initialized_) {
    //         logWithTimestamp("ERROR", "Execution interface not initialized");
    //         return 0;
    //     }

    //     try {
    //         // Create message
    //         nlohmann::json request = {
    //             {"action", "cancel_all_orders"},
    //             {"exchangeId", exchangeId.toString()},
    //             {"reason", reason}
    //         };

    //         // Send request
    //         if (!requestClient_->publish("orders", request.dump())) {
    //             logWithTimestamp("ERROR", "Failed to send cancel all orders request");
    //             return 0;
    //         }

    //         // In a real implementation, we would wait for the response with a timeout
    //         // For now, we'll just return 0
    //         return 0;
    //     } catch (const std::exception& e) {
    //         logWithTimestamp("ERROR", "Failed to cancel all orders: " + std::string(e.what()));
    //         return 0;
    //     }
    // }

    int cancelAllOrders(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair) override {
        if (!initialized_) {
            logWithTimestamp("ERROR", "Execution interface not initialized");
            return 0;
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "cancel_all_orders"},
                {"exchangeId", exchangeId.toString()},
                {"tradingPair", tradingPair.toString()}
            };
            
            // Send request
            if (!requestClient_->publish("orders", request.dump())) {
                logWithTimestamp("ERROR", "Failed to send cancel all orders request");
                return 0;
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return 0
            return 0;
        } catch (const std::exception& e) {
            logWithTimestamp("ERROR", "Failed to cancel all orders: " + std::string(e.what()));
            return 0;
        }
    }
    
    std::string getOrderStatus(const std::string& orderId) override {
        if (!initialized_) {
            logWithTimestamp("ERROR", "Execution interface not initialized");
            return "{}";
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "get_order_status"},
                {"order_id", orderId}
            };
            
            // Send request
            if (!requestClient_->publish("orders", request.dump())) {
                logWithTimestamp("ERROR", "Failed to send get order status request");
                return "{}";
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return an empty JSON object
            return "{}";
        } catch (const std::exception& e) {
            logWithTimestamp("ERROR", "Failed to get order status: " + std::string(e.what()));
            return "{}";
        }
    }
    
    void registerExecutionReportCallback(const std::function<void(const ExecutionReport&)>& callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        executionReportCallback_ = callback;
    }
    
    std::vector<OrderRequest> getOpenOrders(const std::optional<std::string>& exchangeId) override {
        if (!initialized_) {
            logWithTimestamp("ERROR", "Execution interface not initialized");
            return {};
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "get_open_orders"}
            };
            
            // Add exchange ID if provided
            if (exchangeId) {
                request["exchangeId"] = *exchangeId;
            }
            
            // Send request
            if (!requestClient_->publish("orders", request.dump())) {
                logWithTimestamp("ERROR", "Failed to send get open orders request");
                return {};
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return an empty vector
            return {};
        } catch (const std::exception& e) {
            logWithTimestamp("ERROR", "Failed to get open orders: " + std::string(e.what()));
            return {};
        }
    }
    
    bool isExchangeAvailable(const core::ExchangeId& exchangeId) override {
        if (!initialized_) {
            logWithTimestamp("ERROR", "Execution interface not initialized");
            return false;
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "is_exchange_available"},
                {"exchangeId", exchangeId.toString()}
            };
            
            // Send request
            if (!requestClient_->publish("exchange_status", request.dump())) {
                logWithTimestamp("ERROR", "Failed to send exchange availability request");
                return false;
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return false
            return false;
        } catch (const std::exception& e) {
            logWithTimestamp("ERROR", "Failed to check if exchange is available: " + std::string(e.what()));
            return false;
        }
    }
    
    std::string getExchangeStatus(const core::ExchangeId& exchangeId) override {
        if (!initialized_) {
            logWithTimestamp("ERROR", "Execution interface not initialized");
            return "{}";
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "get_exchange_status"},
                {"exchangeId", exchangeId.toString()}
            };
            
            // Send request
            if (!requestClient_->publish("exchange_status", request.dump())) {
                logWithTimestamp("ERROR", "Failed to send get exchange status request");
                return "{}";
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return an empty JSON object
            return "{}";
        } catch (const std::exception& e) {
            logWithTimestamp("ERROR", "Failed to get exchange status: " + std::string(e.what()));
            return "{}";
        }
    }
};

// Factory function to create ExecutionInterface
ExecutionInterface::ExecutionInterface() {
    // Base constructor, nothing to do here
}

// Global factory function to create the concrete implementation
std::unique_ptr<ExecutionInterface> createExecutionInterface(const RiskConfig& config) {
    return std::make_unique<ExecutionInterfaceImpl>(config);
}

} // namespace risk
} // namespace trading