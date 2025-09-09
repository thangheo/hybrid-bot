/**
 * Interface for communication with the strategy engine
 *
 * File: strategy_interface.hpp
 * Created: 2025-04-29
 */
#pragma once

#include "risk_types.hpp"
#include "risk_events.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace trading {
namespace risk {


/**
 * StrategyInterface provides communication with the Strategy Engine.
 * It allows risk feedback to the strategies and retrieves strategy-specific parameters.
 */
class StrategyInterface {
public:
    /**
     * Constructor
     */
    StrategyInterface();
    
    /**
     * Destructor
     */
    virtual ~StrategyInterface() = default;
    // isConnected
    virtual bool isConnected() const = 0;
    /**
     * Initialize the interface
     * @return true if initialization was successful, false otherwise
     */
    virtual bool initialize() = 0;
    
    /**
     * Send risk feedback to the strategy engine
     * @param strategyId The strategy identifier
     * @param feedback The risk feedback data
     * @return true if sending was successful, false otherwise
     */
    virtual bool sendRiskFeedback(const StrategyId& strategyId, const std::string& feedback) = 0;
    
    /**
     * Get strategy parameters
     * @param strategyId The strategy identifier
     * @return JSON string containing strategy parameters
     */
    virtual std::string getStrategyParameters(const StrategyId& strategyId) = 0;
    
    /**
     * Update strategy parameters
     * @param strategyId The strategy identifier
     * @param parameters JSON string containing strategy parameters
     * @return true if update was successful, false otherwise
     */
    virtual bool updateStrategyParameters(const StrategyId& strategyId, const std::string& parameters) = 0;
    
    /**
     * Send risk event notification to strategy
     * @param strategyId The strategy identifier
     * @param event The risk event
     * @return true if sending was successful, false otherwise
     */
    virtual bool notifyRiskEvent(const StrategyId& strategyId, const RiskEvent& event) = 0;
    
    /**
     * Get active strategies
     * @return Vector of active strategy identifiers
     */
    virtual std::vector<StrategyId> getActiveStrategies() = 0;
    
    /**
     * Check if a strategy is active
     * @param strategyId The strategy identifier
     * @return true if active, false otherwise
     */
    virtual bool isStrategyActive(const StrategyId& strategyId) = 0;
    
    /**
     * Register a callback for strategy events
     * @param callback The function to call when strategy events occur
     */
    virtual void registerStrategyEventCallback(const std::function<void(const StrategyId&, const std::string&)>& callback) = 0;
};

/**
 * Factory function to create StrategyInterface
 * 
 * @return Unique pointer to StrategyInterface implementation
 */
std::unique_ptr<StrategyInterface> createStrategyInterface();

} // namespace risk
} // namespace trading