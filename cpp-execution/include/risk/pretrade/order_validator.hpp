/**
 * Validates incoming orders against configuration limits
 *
 * File: order_validator.hpp (Fixed version)
 * Created: 2025-04-29
 */
#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"
#include "../interfaces/data_interface.hpp"
#include <execution/trading_signal.hpp>  // Use existing execution trading signal
#include "risk/pretrade/trading_signal_extensions.hpp"  // Use the extensions

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <mutex>
#include <memory>

namespace trading {
namespace risk {

/**
 * OrderValidator validates order parameters against configuration limits.
 * It ensures compliance with exchange-specific rules and verifies order prices against market data.
 */
class OrderValidator {
public:
    /**
     * Constructor
     * @param config Risk configuration containing order validation rules
     * @param dataInterface Interface to access market data
     */
    OrderValidator(const RiskConfig& config, std::shared_ptr<DataInterface> dataInterface);
    
    /**
     * Initialize the validator
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    // isHealthy
    bool isHealthy() const;
    /**
     * Validate order parameters from a trading signal
     * @param signal The trading signal to validate
     * @param violations Vector to fill with any violations
     * @return true if valid, false otherwise
     */
    bool validateOrder(const execution::TradingSignal& signal, std::vector<RiskViolation>& violations);
    
    /**
     * Validate order parameters
     * @param orderRequest The order request to validate
     * @param violations Vector to fill with any violations
     * @return true if valid, false otherwise
     */
    bool validateOrder(const OrderRequest& orderRequest, std::vector<RiskViolation>& violations);
    
    /**
     * Check if the given trading pair is on the symbol whitelist
     * @param tradingPair The trading pair to check (as string)
     * @return true if on whitelist or whitelist is disabled, false otherwise
     */
    bool isOnSymbolWhitelist(const core::Symbol& tradingPair) const;
    
    /**
     * Check if the given trading pair is on the symbol whitelist
     * @param tradingPair The trading pair to check (as TradingPair object)
     * @return true if on whitelist or whitelist is disabled, false otherwise
     */
    // bool isOnSymbolWhitelist(const TradingPair& tradingPair) const;
    
    /**
     * Check if an order price is within acceptable range compared to current market price
     * @param tradingPair The trading pair for the order
     * @param price The order price to check
     * @param violations Vector to fill with any violations
     * @return true if within range, false otherwise
     */
    bool isPriceWithinRange(const core::Symbol& tradingPair, double price, std::vector<RiskViolation>& violations);
    
    /**
     * Check if an order size meets minimum requirements for the exchange
     * @param exchangeId The exchange identifier
     * @param tradingPair The trading pair for the order (as string)
     * @param quantity The order quantity to check
     * @param violations Vector to fill with any violations
     * @return true if size is valid, false otherwise
     */
    bool isOrderSizeValid(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair, double quantity, std::vector<RiskViolation>& violations);
    
    /**
     * Update the symbol whitelist
     * @param newWhitelist New set of allowed trading pairs
     * @param enforceWhitelist Whether to enforce the whitelist
     * @return true if update was successful, false otherwise
     */
    bool updateSymbolWhitelist(const std::vector<std::string>& newWhitelist, bool enforceWhitelist);
    
    /**
     * Update minimum order size requirements
     * @param exchangeId The exchange identifier
     * @param tradingPair The trading pair
     * @param minSize The minimum order size
     * @return true if update was successful, false otherwise
     */
    bool updateMinOrderSize(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair, double minSize);
    
    /**
     * Update price validation settings
     * @param maxDeviationPercent Maximum allowed deviation from market price
     * @return true if update was successful, false otherwise
     */
    bool updatePriceValidationSettings(double maxDeviationPercent);

private:
    std::shared_ptr<DataInterface> m_dataInterface;
    RiskConfig m_config;
    OrderValidationConfig m_validationConfig;
    
    // Thread safety
    mutable std::mutex m_configMutex;
    
    // Cached whitelist for quick lookup
    mutable std::mutex m_whitelistMutex;
    std::unordered_set<std::string> m_symbolWhitelist;
    bool m_enforceWhitelist;
    
    // Cached minimum order sizes for quick lookup
    mutable std::mutex m_minSizeMutex;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> m_minOrderSizes;
    
    // Global minimum trade value (notional) in base currency units (fallback/default)
    double m_minTradeValue = 0.0;
    
    // Helper methods
    bool validateOrderType(OrderType orderType, std::vector<RiskViolation>& violations);
    bool validateTimeInForce(const std::string& timeInForce, std::vector<RiskViolation>& violations);
    double getCurrentMarketPrice(const core::Symbol& tradingPair) const;
};

} // namespace risk
} // namespace trading