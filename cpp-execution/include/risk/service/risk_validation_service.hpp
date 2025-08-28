/**
 * Interface for the risk validation service that validates trading signals
 *
 * File: risk_validation_service.hpp
 * Created: 2025-04-29
 */
#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"

#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <functional>

namespace trading {
namespace risk {

// Forward declarations
class RiskManager;

/**
 * RiskValidationService validates trading signals against risk parameters.
 * It provides an abstraction for the risk validation process.
 */
class RiskValidationService {
public:
    /**
     * Constructor
     * @param config Risk configuration
     * @param riskManager Pointer to the risk manager
     */
    RiskValidationService(const RiskConfig& config, std::shared_ptr<RiskManager> riskManager);
    
    /**
     * Initialize the service
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    
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
     * Register a validation callback
     * @param callback The function to call when validation occurs
     */
    void registerValidationCallback(const std::function<void(const TradingSignal&, const ValidationResult&)>& callback);
    
    /**
     * Set a validation timeout
     * @param timeoutMs Timeout in milliseconds
     */
    void setValidationTimeout(int timeoutMs);
    
    /**
     * Get the validation timeout
     * @return Timeout in milliseconds
     */
    int getValidationTimeout() const;

private:
    std::shared_ptr<RiskManager> m_riskManager;
    RiskConfig m_config;
    
    // Validation settings
    int m_validationTimeoutMs;
    
    // Callbacks
    mutable std::mutex m_callbackMutex;
    std::vector<std::function<void(const TradingSignal&, const ValidationResult&)>> m_validationCallbacks;
    
    // Helper methods
    void notifyValidation(const TradingSignal& signal, const ValidationResult& result);
    ValidationResult createTimeoutResult(const TradingSignal& signal);
    bool validateSignalInternal(const TradingSignal& signal, ValidationResult& result, int timeoutMs);
};

} // namespace risk
} // namespace trading
// End of file