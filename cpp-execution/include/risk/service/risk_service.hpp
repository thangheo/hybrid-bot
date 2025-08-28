/**
 * Interface defining the risk service according to specifications
 *
 * File: risk_service.hpp
 * Created: 2025-04-29
 */
#pragma once

#include "../risk_types.hpp"
#include "../risk_events.hpp"
#include "../risk_config.hpp"
#include "risk_validation_service.hpp"

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

namespace trading {
namespace risk {

/**
 * RiskService provides an external interface for the Risk Management System.
 * It implements service contracts and handles communication protocol details.
 */
class RiskService {
public:
    /**
     * Constructor
     * @param config Risk configuration
     */
    explicit RiskService(const RiskConfig& config);
    
    /**
     * Destructor
     */
    ~RiskService();
    
    /**
     * Initialize the service
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    
    /**
     * Start the service
     * @return true if service was started successfully, false otherwise
     */
    bool start();
    
    /**
     * Stop the service
     * @return true if service was stopped successfully, false otherwise
     */
    bool stop();
    
    /**
     * Check if the service is running
     * @return true if running, false otherwise
     */
    bool isRunning() const;
    
    /**
     * Register a callback for risk events
     * @param callback The function to call when risk events occur
     */
    void registerRiskEventCallback(const RiskEventCallback& callback);
    
    /**
     * Set the risk validation service
     * @param validationService Pointer to the validation service
     */
    void setRiskValidationService(std::shared_ptr<RiskValidationService> validationService);
    
    /**
     * Get the risk validation service
     * @return Reference to the validation service
     */
    std::shared_ptr<RiskValidationService> getRiskValidationService() const;
    
    /**
     * Validate a trading signal
     * @param signal The trading signal to validate
     * @return ValidationResult containing approval status and any violations
     */
    ValidationResult validateSignal(const TradingSignal& signal);
    
    /**
     * Get risk parameters
     * @param exchangeId The exchange identifier
     * @param tradingPair The trading pair
     * @param strategyId The strategy identifier
     * @return RiskParameters containing risk limits
     */
    RiskParameters getRiskParameters(const ExchangeId& exchangeId, 
                                   const TradingPair& tradingPair,
                                   const StrategyId& strategyId) const;
    
    /**
     * Update risk parameters
     * @param params The new risk parameters
     * @return true if update was successful, false otherwise
     */
    bool updateRiskParameters(const RiskParameters& params);
    
    /**
     * Get the current risk manager mode
     * @return The current mode
     */
    RiskManagerMode getMode() const;
    
    /**
     * Set the risk manager mode
     * @param mode The new mode
     * @return true if update was successful, false otherwise
     */
    bool setMode(RiskManagerMode mode);
    
    /**
     * Get the health status of the service
     * @return JSON string containing health status
     */
    std::string getHealthStatus() const;
    
    /**
     * Get service metrics
     * @return JSON string containing metrics
     */
    std::string getMetrics() const;

private:
    RiskConfig m_config;
    std::shared_ptr<RiskValidationService> m_validationService;
    
    // Service state
    std::atomic<bool> m_running;
    std::thread m_serviceThread;
    
    // Event handling
    mutable std::mutex m_callbackMutex;
    std::vector<RiskEventCallback> m_eventCallbacks;
    
    // Helper methods
    void serviceLoop();
    void notifyRiskEvent(const RiskEvent& event);
    void handleRpcRequest(const std::string& request, std::string& response);
    std::string processCommand(const std::string& command, const std::string& payload);
};

} // namespace risk
} // namespace trading
