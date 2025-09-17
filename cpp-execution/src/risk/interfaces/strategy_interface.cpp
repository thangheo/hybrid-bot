/**
 * Implementation of the interface for communication with the strategy engine
 *
 * File: strategy_interface.cpp
 * Created: 2025-04-29
 */

#include "interfaces/strategy_interface.hpp"
#include <communication/zmq_client.hpp>
#include <nlohmann/json.hpp>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <iostream>
#include <iomanip>
#include "common/logger.hpp"
namespace trading {
namespace risk {

// Base StrategyInterface constructor implementations
StrategyInterface::StrategyInterface() {
    // Default constructor
}

StrategyInterface::StrategyInterface(const RiskConfig& config) {
    // Constructor with config
}

// Implementation of the StrategyInterface that uses ZeroMQ for communication
class StrategyInterfaceImpl : public StrategyInterface {
private:
    // Configuration
    std::string requestEndpoint_;
    std::string publishEndpoint_;
    std::string strategyEventsTopic_;
    
    // ZeroMQ clients
    std::unique_ptr<common::ZeroMQClient> requestClient_;
    std::unique_ptr<common::ZeroMQClient> subscriptionClient_;
    
    // Callback for strategy events
    std::function<void(const StrategyId&, const std::string&)> strategyEventCallback_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Flag to indicate if initialization was successful
    bool initialized_ = false;

    // Convert StrategyId to JSON
    nlohmann::json strategyIdToJson(const StrategyId& strategyId) {
        nlohmann::json json;
        json["id"] = strategyId.id;
        json["version"] = strategyId.version;
        return json;
    }

    // Parse StrategyId from JSON
    StrategyId jsonToStrategyId(const nlohmann::json& json) {
        StrategyId strategyId;
        strategyId.id = json["id"];
        strategyId.version = json["version"];
        return strategyId;
    }

    // Convert RiskEvent to JSON
    nlohmann::json riskEventToJson(const RiskEvent& event) {
        nlohmann::json json;
        json["type"] = static_cast<int>(event.type);  // Convert enum to int
        json["type_string"] = riskEventTypeToString(event.type);  // Human-readable type
        json["severity"] = event.severity;
        json["message"] = event.message;
        json["description"] = event.description;  // Use description instead of data
        json["event_id"] = event.eventId;
        json["event_source"] = event.eventSource;
        json["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            event.timestamp.time_since_epoch()).count();
        
        // Include metadata if present
        if (!event.metadata.empty()) {
            json["metadata"] = event.metadata;
        }
        
        return json;
    }

    // Handle received strategy event messages
    void handleStrategyEventMessage(const std::string& topic, const std::string& message) {
        if (topic == strategyEventsTopic_) {
            try {
                auto json = nlohmann::json::parse(message);
                
                StrategyId strategyId = jsonToStrategyId(json["strategy_id"]);
                std::string eventData = json["event_data"].dump();
                
                // Call the callback if registered
                if (strategyEventCallback_) {
                    strategyEventCallback_(strategyId, eventData);
                }
            } catch (const std::exception& e) {
                LOG_ERROR( "Error processing strategy event: " + std::string(e.what()));
            }
        }
    }

    // Handle error messages
    void handleErrorMessage(const std::string& context, const std::string& error) {
        LOG_ERROR( "ZeroMQ error in " + context + ": " + error);
    }

public:
    StrategyInterfaceImpl() : StrategyInterface() {
        // Load configuration
        // In a real implementation, this would be loaded from a config file
        requestEndpoint_ = "tcp://localhost:5562";
        publishEndpoint_ = "tcp://localhost:5563";
        strategyEventsTopic_ = "strategy_events";
    }
    StrategyInterfaceImpl(const RiskConfig& config) : StrategyInterface(config) {
        // Load configuration from RiskConfig
        requestEndpoint_ = "tcp://localhost:5562";
        publishEndpoint_ = "tcp://localhost:5563";
        strategyEventsTopic_ = "strategy_events";

        // requestEndpoint_ = config.strategyRequestEndpoint;
        // publishEndpoint_ = config.strategyPublishEndpoint;
        // strategyEventsTopic_ = config.strategyEventsTopic;
        }
    ~StrategyInterfaceImpl() override {
        // Clean shutdown of ZeroMQ clients
        if (subscriptionClient_) {
            subscriptionClient_->stop();
        }
        if (requestClient_) {
            requestClient_->stop();
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
                std::bind(&StrategyInterfaceImpl::handleStrategyEventMessage, this, 
                          std::placeholders::_1, std::placeholders::_2));
            
            // Set error handler for both clients
            subscriptionClient_->setErrorHandler(
                std::bind(&StrategyInterfaceImpl::handleErrorMessage, this,
                          std::placeholders::_1, std::placeholders::_2));
            requestClient_->setErrorHandler(
                std::bind(&StrategyInterfaceImpl::handleErrorMessage, this,
                          std::placeholders::_1, std::placeholders::_2));
            
            // Start the clients
            if (!requestClient_->start()) {
                LOG_ERROR( "Failed to start request client");
                return false;
            }
            
            if (!subscriptionClient_->start()) {
                LOG_ERROR( "Failed to start subscription client");
                return false;
            }
            
            // Subscribe to the strategy events topic
            if (!subscriptionClient_->subscribe(strategyEventsTopic_)) {
                LOG_ERROR( "Failed to subscribe to strategy events topic");
                return false;
            }
            
            initialized_ = true;
            LOG_INFO("Strategy interface initialized successfully");
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR( "Failed to initialize strategy interface: " + std::string(e.what()));
            return false;
        }
    }
    // isConnected
    bool isConnected() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && requestClient_ && requestClient_->isConnected() &&
               subscriptionClient_ && subscriptionClient_->isConnected();
    }
    bool sendRiskFeedback(const StrategyId& strategyId, const std::string& feedback) override {
        if (!initialized_) {
            LOG_ERROR( "Strategy interface not initialized");
            return false;
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "risk_feedback"},
                {"strategy_id", strategyIdToJson(strategyId)},
                {"feedback", nlohmann::json::parse(feedback)}
            };
            
            // Send request
            if (!requestClient_->publish("strategy_risk", request.dump())) {
                LOG_ERROR( "Failed to send risk feedback request");
                return false;
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return true
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR( "Failed to send risk feedback: " + std::string(e.what()));
            return false;
        }
    }
    
    std::string getStrategyParameters(const StrategyId& strategyId) override {
        if (!initialized_) {
            LOG_ERROR( "Strategy interface not initialized");
            return "{}";
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "get_parameters"},
                {"strategy_id", strategyIdToJson(strategyId)}
            };
            
            // Send request
            if (!requestClient_->publish("strategy_request", request.dump())) {
                LOG_ERROR( "Failed to send get parameters request");
                return "{}";
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return an empty JSON object
            return "{}";
        } catch (const std::exception& e) {
            LOG_ERROR( "Failed to get strategy parameters: " + std::string(e.what()));
            return "{}";
        }
    }
    
    bool updateStrategyParameters(const StrategyId& strategyId, const std::string& parameters) override {
        if (!initialized_) {
            LOG_ERROR( "Strategy interface not initialized");
            return false;
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "update_parameters"},
                {"strategy_id", strategyIdToJson(strategyId)},
                {"parameters", nlohmann::json::parse(parameters)}
            };
            
            // Send request
            if (!requestClient_->publish("strategy_request", request.dump())) {
                LOG_ERROR( "Failed to send update parameters request");
                return false;
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return true
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR( "Failed to update strategy parameters: " + std::string(e.what()));
            return false;
        }
    }
    
    bool notifyRiskEvent(const StrategyId& strategyId, const RiskEvent& event) override {
        if (!initialized_) {
            LOG_ERROR( "Strategy interface not initialized");
            return false;
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "risk_event"},
                {"strategy_id", strategyIdToJson(strategyId)},
                {"event", riskEventToJson(event)}
            };
            
            // Send request
            if (!requestClient_->publish("strategy_risk", request.dump())) {
                LOG_ERROR( "Failed to send risk event notification");
                return false;
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return true
            return true;
        } catch (const std::exception& e) {
            LOG_ERROR( "Failed to notify risk event: " + std::string(e.what()));
            return false;
        }
    }
    
    std::vector<StrategyId> getActiveStrategies() override {
        if (!initialized_) {
            LOG_ERROR( "Strategy interface not initialized");
            return {};
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "get_active_strategies"}
            };
            
            // Send request
            if (!requestClient_->publish("strategy_request", request.dump())) {
                LOG_ERROR( "Failed to send get active strategies request");
                return {};
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return an empty vector
            return {};
        } catch (const std::exception& e) {
            LOG_ERROR( "Failed to get active strategies: " + std::string(e.what()));
            return {};
        }
    }
    
    bool isStrategyActive(const StrategyId& strategyId) override {
        if (!initialized_) {
            LOG_ERROR( "Strategy interface not initialized");
            return false;
        }
        
        try {
            // Create message
            nlohmann::json request = {
                {"action", "is_strategy_active"},
                {"strategy_id", strategyIdToJson(strategyId)}
            };
            
            // Send request
            if (!requestClient_->publish("strategy_request", request.dump())) {
                LOG_ERROR( "Failed to send strategy active check request");
                return false;
            }
            
            // In a real implementation, we would wait for the response with a timeout
            // For now, we'll just return false
            return false;
        } catch (const std::exception& e) {
            LOG_ERROR( "Failed to check if strategy is active: " + std::string(e.what()));
            return false;
        }
    }
    
    void registerStrategyEventCallback(const std::function<void(const StrategyId&, const std::string&)>& callback) override {
        std::lock_guard<std::mutex> lock(mutex_);
        strategyEventCallback_ = callback;
    }
    bool notifyEmergencyState(const std::string& reason) {
        if (!initialized_) {
            LOG_ERROR( "Strategy interface not initialized");
            return false;
        }
    }
};
 
std::unique_ptr<StrategyInterface> createStrategyInterface(const RiskConfig& config) {
    return std::make_unique<StrategyInterfaceImpl>(config);
}

} // namespace risk
} // namespace trading