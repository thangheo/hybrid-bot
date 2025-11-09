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
#include "risk/portfolio/portfolio_manager.hpp"
#include <memory>
#include <vector>
#include <string>

namespace trading {
namespace risk {

// Use the execution TradingSignal as the primary type
using TradingSignal = execution::TradingSignal;

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
    // new constructor with DataInterface
    explicit PreTradeRiskFilter(const RiskConfig& config, std::shared_ptr<DataInterface> dataInterface);
    /**
     * Constructor with shared DataInterface
     * @param config Risk configuration for this filter
     * @param dataInterface Shared data interface for market data access
     */
    
    // explicit PreTradeRiskFilter(const RiskConfig& config, const PortfolioManager& portfolioManager);
    /**
     * Virtual destructor
     */
    virtual ~PreTradeRiskFilter();
    // getAllowedPositionSize
    virtual double getAllowedPositionSize(const TradingPair& tradingPair) const;
    /**
     * Initialize the filter and its components
     * @return true if initialization was successful, false otherwise
     */
    virtual bool initialize();
    virtual bool shutdown();
    //isHealthy
    virtual bool isHealthy() const;
    /**
     * Validate a trading signal
     * @param signal The trading signal to validate
     * @return ValidationResult containing approval status and any violations
     */
    virtual ValidationResult validateSignal(const TradingSignal& signal);
    
    /**
     * Validate an order request
     * @param orderRequest The order request to validate
     * @return ValidationResult containing approval status and any violations
     */
    virtual ValidationResult validateOrder(const OrderRequest& orderRequest);
    
    /**
     * Update risk parameters
     * @param params The new risk parameters
     * @return true if update was successful, false otherwise
     */
    virtual bool updateRiskParameters(const RiskParameters& params);
    
    /**
     * Get the current risk parameters
     * @param exchangeId The exchange ID
     * @param tradingPair The trading pair
     * @param strategyId The strategy ID
     * @return RiskParameters containing current risk limits
     */
    virtual RiskParameters getRiskParameters(const core::ExchangeId& exchangeId, 
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
    
    /**
     * Get the position tracker for cached position lookups
     * @return shared_ptr to PositionTracker, may be nullptr if not initialized
     */
    std::shared_ptr<PositionTracker> getPositionTracker() const;

    /**
     * Set the position tracker (for shared instance injection)
     * @param positionTracker shared PositionTracker instance
     */
    void setPositionTracker(std::shared_ptr<PositionTracker> positionTracker);

    /**
     * Expose PositionLimitChecker pointer for optional injection of dependencies
     * @return raw pointer to PositionLimitChecker or nullptr if not initialized
     */
    PositionLimitChecker* getPositionLimitChecker() const;

    // Helper methods for signal property extraction
    std::string getBaseAsset(const core::Symbol& tradingPair) const;
    // createPreTradeFilter factory function

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
    
};

    static std::unique_ptr<PreTradeRiskFilter> createPreTradeFilter(const RiskConfig& config);
    
} // namespace risk
} // namespace trading