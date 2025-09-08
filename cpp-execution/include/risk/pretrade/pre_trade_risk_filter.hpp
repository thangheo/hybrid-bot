/**
 * Unified pre-trade risk filter that combines all pre-trade checks
 *
 * File: pre_trade_risk_filter.hpp
 * Created: 2025-04-29
 * Updated: 2025-05-28
 */

#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"
#include "risk/pretrade/position_limit_checker.hpp"
#include "risk/pretrade/exposure_calculator.hpp"
#include "risk/pretrade/order_validator.hpp"
#include "risk/pretrade/fat_finger_protection.hpp"
#include "execution/trading_signal.hpp"
#include "core/market_types.hpp"

#include <memory>
#include <vector>
#include <string>

namespace trading {
namespace risk {

// Use the execution TradingSignal as the primary type
using TradingSignal = execution::TradingSignal;

// Forward declarations for missing types
struct ValidationResult {
    std::string signalId;
    bool approved = false;
    double adjustedSize = 0.0;
    uint64_t timestamp = 0;
    std::vector<RiskViolation> violations;
    
    ValidationResult() = default;
};

struct RiskParameters {
    core::ExchangeId exchangeId;
    TradingPair tradingPair;
    std::string strategyId;
    double maxPositionSize = 0.0;
    double maxExposure = 0.0;
    double maxOrderSize = 0.0;
    double minOrderSize = 0.0;
    
    RiskParameters() = default;
};

/**
 * PreTradeRiskFilter orchestrates all pre-trade risk checks.
 * It validates trading signals before they become orders.
 */
class PreTradeRiskFilter {
public:
    /**
     * Constructor
     * @param config Risk configuration for this filter
     */
    explicit PreTradeRiskFilter(const RiskConfig& config);
    
    /**
     * Initialize the filter and its components
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
     * Validate an order request
     * @param orderRequest The order request to validate
     * @return ValidationResult containing approval status and any violations
     */
    ValidationResult validateOrder(const OrderRequest& orderRequest);
    
    /**
     * Update risk parameters
     * @param params The new risk parameters
     * @return true if update was successful, false otherwise
     */
    bool updateRiskParameters(const RiskParameters& params);
    
    /**
     * Get the current risk parameters
     * @param exchangeId The exchange ID
     * @param tradingPair The trading pair
     * @param strategyId The strategy ID
     * @return RiskParameters containing current risk limits
     */
    RiskParameters getRiskParameters(const core::ExchangeId& exchangeId, 
                                   const TradingPair& tradingPair,
                                   const std::string& strategyId) const;
    
    /**
     * Set the active mode for the risk filter
     * @param mode The new risk manager mode
     */
    void setMode(RiskManagerMode mode);
    
    /**
     * Get the active mode for the risk filter
     * @return The current risk manager mode
     */
    RiskManagerMode getMode() const;

private:
    // Sub-components
    std::unique_ptr<PositionLimitChecker> m_positionLimitChecker;
    std::unique_ptr<ExposureCalculator> m_exposureCalculator;
    std::unique_ptr<OrderValidator> m_orderValidator;
    std::unique_ptr<FatFingerProtection> m_fatFingerProtection;
    
    // Dependencies
    std::shared_ptr<PositionTracker> m_positionTracker;
    std::shared_ptr<DataInterface> m_dataInterface;
    
    // Configuration and state
    RiskConfig m_config;
    RiskManagerMode m_activeMode;
    
    // Helper methods
    ValidationResult performAllChecks(const TradingSignal& signal);
    ValidationResult performAllChecks(const OrderRequest& orderRequest);
    bool passesBasicValidation(const TradingSignal& signal, std::vector<RiskViolation>& violations);
    bool passesBasicValidation(const OrderRequest& orderRequest, std::vector<RiskViolation>& violations);
    
    // Helper methods for signal property extraction
    std::string getBaseAsset(const std::string& tradingPair) const;
};

} // namespace risk
} // namespace trading