/**
 * Implementation of the order validator (Fixed version)
 *
 * File: order_validator.cpp
 * Created: 2025-04-29
 */

#include "risk/pretrade/order_validator.hpp"
#include "common/logger.hpp"

#include <algorithm>
#include <stdexcept>

namespace trading {
namespace risk {

OrderValidator::OrderValidator(const RiskConfig& config,
                              std::shared_ptr<DataInterface> dataInterface)
    : m_config(config)
    , m_dataInterface(dataInterface)
    , m_enforceWhitelist(false)
{
    // Initialize with empty whitelist and validation rules
}
// isHealthy
bool OrderValidator::isHealthy() const {
    return m_dataInterface && m_dataInterface->isConnected();
}
bool OrderValidator::initialize() {
    try {
        // Load validation configuration from config
        m_validationConfig = m_config.getOrderValidationConfig();
        
        std::lock_guard<std::mutex> whitelistLock(m_whitelistMutex);
        
        // Initialize symbol whitelist
        m_symbolWhitelist.clear();
        for (const auto& symbol : m_validationConfig.symbolWhitelist) {
            m_symbolWhitelist.insert(symbol);
        }
        m_enforceWhitelist = m_validationConfig.enforceSymbolWhitelist;
        
        // Initialize minimum order sizes
        std::lock_guard<std::mutex> sizeLock(m_minSizeMutex);
        m_minOrderSizes.clear();
        for (const auto& entry : m_validationConfig.minOrderSizes) {
            const auto& exchangeId = entry.first;
            for (const auto& symbolSize : entry.second) {
                m_minOrderSizes[exchangeId.toString()][symbolSize.first.toString()] = symbolSize.second;
            }
        }
        // Global minimum trade value (notional)
        m_minTradeValue = m_validationConfig.minTradeValue;
        
        LOG_INFO("Order validator initialized successfully with {} whitelisted symbols, {} exchanges with min sizes, defaultMinOrderSize={}",
            m_symbolWhitelist.size(), m_minOrderSizes.size(), m_validationConfig.defaultMinOrderSize);
        
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to initialize order validator: {}", ex.what());
        return false;
    }
}

bool OrderValidator::validateOrder(const execution::TradingSignal& signal,
                                  std::vector<RiskViolation>& violations) {
    // Use the extension functions to access signal properties
    using namespace trading::risk; // Bring in extension functions
    
    // Validate trading pair using the extension function
    if (!isOnSymbolWhitelist(getSymbol(signal))) {
        RiskViolation violation;
        violation.type = ViolationType::OTHER;  // Use enum instead of string
        violation.description = "Trading pair is not on the whitelist: " + getSymbol(signal).toString();
        violation.limitValue = 0;
        violation.actualValue = 0;
        violations.push_back(violation);
        return false;
    }
    
    // Validate order size using the extension functions
    // Note: trading signals may express targetSize as a fraction (<=1.0) or
    // an absolute asset quantity (>1.0). The low-level isOrderSizeValid expects
    // an asset quantity. If the signal provided a fractional target we cannot
    // reliably convert it to an absolute quantity here (missing allowed USD
    // budget). In that case, perform lightweight validation and defer exact
    // notional/min-trade checks until order creation when the notional is
    // resolved.
    double target = signal.getTargetSize();
    if (target > 1.0) {
        // Interpret as absolute asset quantity
        double qty = target;
        // Try to retrieve market price once for notional checks
        std::optional<double> marketPrice;
        try {
            marketPrice = getCurrentMarketPrice(getSymbol(signal));
        } catch (...) {
            marketPrice = std::nullopt;
        }

        if (!isOrderSizeValid(getExchangeId(signal), getSymbol(signal), qty, violations, marketPrice)) {
            return false;
        }
    } else {
        // Fractional target: basic sanity checks only
        if (target <= 0.0) {
            RiskViolation violation;
            violation.type = ViolationType::ORDER_SIZE_LIMIT;
            violation.description = "Target size must be positive";
            violation.limitValue = 0.0;
            violation.actualValue = target;
            violations.push_back(violation);
            return false;
        }
        // Defer notional/min trade value checks until we have a concrete order
        LOG_DEBUG("Signal target is fractional ({}). Deferring notional checks until order creation", target);
    }
    
    // For signals, we don't have a price to check yet
    // The actual price check will happen when an order is created
    
    return true;
}

bool OrderValidator::validateOrder(const OrderRequest& orderRequest,
                                  std::vector<RiskViolation>& violations) {
    // Validate trading pair using string version
    if (!isOnSymbolWhitelist(orderRequest.tradingPair)) {
        RiskViolation violation;
        violation.type = ViolationType::OTHER;  // Use enum instead of string
        violation.description = "Trading pair is not on the whitelist: " + orderRequest.tradingPair.toString();
        violation.limitValue = 0;
        violation.actualValue = 0;
        violations.push_back(violation);
        return false;
    }
    
    // Validate order type
    if (!validateOrderType(orderRequest.type, violations)) {
        return false;
    }
    
    // Validate time in force
    if (!validateTimeInForce(orderRequest.timeInForce, violations)) {
        return false;
    }
    
    // Validate order size using string version
    // Compute market price for both notional and price checks in a single call
    std::optional<double> marketPrice;
    try {
        marketPrice = getCurrentMarketPrice(orderRequest.tradingPair);
    } catch (...) {
        marketPrice = std::nullopt;
    }

    if (!isOrderSizeValid(orderRequest.exchangeId, orderRequest.tradingPair, orderRequest.quantity, violations, marketPrice)) {
        if (!violations.empty()) {
            LOG_INFO("Order size validation failed: {} (limit={}, actual={})", violations[0].description, violations[0].limitValue, violations[0].actualValue);
        }
        return false;
    }
    
    // Validate price (for limit orders)
    if (orderRequest.type == OrderType::LIMIT && 
        !isPriceWithinRange(orderRequest.tradingPair, orderRequest.price, violations, marketPrice)) {
        if (!violations.empty()) {
            LOG_INFO("Price validation failed: {} (limit={}, actual={})", violations[0].description, violations[0].limitValue, violations[0].actualValue);
        }
        return false;
    }
    
    return true;
}

bool OrderValidator::isOnSymbolWhitelist(const core::Symbol& tradingPair) const {
    // If whitelist is not enforced, always return true
    std::lock_guard<std::mutex> lock(m_whitelistMutex);
    if (!m_enforceWhitelist) {
        return true;
    }
    
    // Check if the symbol is in the whitelist
    return m_symbolWhitelist.find(tradingPair.toString()) != m_symbolWhitelist.end();
}

// bool OrderValidator::isOnSymbolWhitelist(const TradingPair& tradingPair) const {
//     return isOnSymbolWhitelist(tradingPair);
// }

bool OrderValidator::isPriceWithinRange(const core::Symbol& tradingPair,
                                       double price,
                                       std::vector<RiskViolation>& violations,
                                       std::optional<double> marketPrice) {
    // Get current market price
    double marketPriceVal = marketPrice.has_value() ? marketPrice.value() : getCurrentMarketPrice(tradingPair);
    
    // Calculate price deviation as a percentage
    if (marketPriceVal <= 0.0) {
        LOG_WARN("Could not determine market price for {} - skipping price deviation check", tradingPair.toString());
        return true; // skip
    }
    double deviation = std::abs((price - marketPriceVal) / marketPriceVal);
    
    // Check if deviation exceeds the maximum allowed
    double maxDeviation = m_validationConfig.maxPriceDeviationPercent / 100.0; // Convert from percent to ratio
    
    if (deviation > maxDeviation) {
        RiskViolation violation;
        violation.type = ViolationType::PRICE_DEVIATION;  // Use appropriate enum
        violation.description = "Price deviates too much from market price";
        violation.limitValue = maxDeviation * 100; // Convert back to percent for reporting
        violation.actualValue = deviation * 100;
        violations.push_back(violation);
        return false;
    }
    
    return true;
}

bool OrderValidator::isOrderSizeValid(const core::ExchangeId& exchangeId,
                                     const core::Symbol& tradingPair,
                                     double quantity,
                                     std::vector<RiskViolation>& violations,
                                     std::optional<double> marketPrice) {
    // Check for negative or zero quantity
    if (quantity <= 0) {
        RiskViolation violation;
        violation.type = ViolationType::ORDER_SIZE_LIMIT;  // Use appropriate enum
        violation.description = "Order size must be positive";
        violation.limitValue = 0;
        violation.actualValue = quantity;
        violations.push_back(violation);
        return false;
    }
    
    // Get minimum order size for this exchange and trading pair
    double minSize = 0.0;
    {
        std::lock_guard<std::mutex> lock(m_minSizeMutex);
        
        // Check if we have exchange-specific minimums
        auto exchangeIt = m_minOrderSizes.find(exchangeId.toString());
        if (exchangeIt != m_minOrderSizes.end()) {
            // Check if we have symbol-specific minimums for this exchange
            auto symbolIt = exchangeIt->second.find(tradingPair.toString());
            if (symbolIt != exchangeIt->second.end()) {
                minSize = symbolIt->second;
            } else {
                // Use default minimum for this exchange if available
                auto defaultIt = exchangeIt->second.find("default");
                if (defaultIt != exchangeIt->second.end()) {
                    minSize = defaultIt->second;
                }
            }
        }
        
        // If no exchange-specific minimum found, use global default
        if (minSize <= 0) {
            minSize = m_validationConfig.defaultMinOrderSize;
        }
    LOG_INFO("Computed minSize for {}@{} = {} (defaultMinOrderSize={})", tradingPair.toString(), exchangeId.toString(), minSize, m_validationConfig.defaultMinOrderSize);
    }
    
    // Check if quantity meets minimum requirement
    LOG_DEBUG("isOrderSizeValid: checking quantity {} vs minSize {} for {}/{}", quantity, minSize, exchangeId.toString(), tradingPair.toString());
    if (quantity < minSize) {
        RiskViolation violation;
        violation.type = ViolationType::ORDER_SIZE_LIMIT;  // Use appropriate enum
        violation.description = "Order size below minimum for exchange";
        violation.limitValue = minSize;
        violation.actualValue = quantity;
        LOG_INFO("Order size violation: minSize={}, qty={}, exchange={}, pair={}", minSize, quantity, exchangeId.toString(), tradingPair.toString());
        violations.push_back(violation);
        return false;
    }

    // Enforce minimum trade value (notional) if configured
    if (m_minTradeValue > 0.0) {
        try {
            double marketPriceVal;
            if (marketPrice.has_value()) {
                marketPriceVal = marketPrice.value();
            } else {
                marketPriceVal = getCurrentMarketPrice(tradingPair);
            }
            // If market price is not available (0 or negative), skip notional validation
            if (marketPriceVal <= 0.0) {
                LOG_WARN("Could not determine market price for {} - skipping notional (min trade value) check", tradingPair.toString());
            } else {
                double notional = quantity * marketPriceVal;
                if (notional < m_minTradeValue) {
                    RiskViolation violation;
                    violation.type = ViolationType::ORDER_SIZE_LIMIT;
                    violation.description = "Order notional value below configured minimum";
                    violation.limitValue = m_minTradeValue;
                    violation.actualValue = notional;
                    LOG_INFO("Order notional violation: minNotional={}, notional={}, exchange={}, pair={}", m_minTradeValue, notional, exchangeId.toString(), tradingPair.toString());
                    violations.push_back(violation);
                    return false;
                }
            }
        } catch (const std::exception& ex) {
            // If we cannot fetch market price, fall back to size-based validation only
            LOG_WARN("Could not fetch market price for notional check: {}. Skipping notional validation.", ex.what());
        }
    }
    
    // Check against maximum order size if configured
    double maxSize = m_validationConfig.maxOrderSize;
    if (maxSize > 0 && quantity > maxSize) {
        RiskViolation violation;
        violation.type = ViolationType::ORDER_SIZE_LIMIT;  // Use appropriate enum
        violation.description = "Order size exceeds maximum allowed";
        violation.limitValue = maxSize;
        violation.actualValue = quantity;
        violations.push_back(violation);
        return false;
    }
    
    return true;
}

bool OrderValidator::updateSymbolWhitelist(const std::vector<std::string>& newWhitelist,
                                          bool enforceWhitelist) {
    std::lock_guard<std::mutex> lock(m_whitelistMutex);
    
    // Update whitelist
    m_symbolWhitelist.clear();
    for (const auto& symbol : newWhitelist) {
        m_symbolWhitelist.insert(symbol);
    }
    
    // Update enforcement flag
    m_enforceWhitelist = enforceWhitelist;
    
    LOG_INFO("Updated symbol whitelist: {} symbols, enforce: {}", 
             m_symbolWhitelist.size(), m_enforceWhitelist);
    
    return true;
}

bool OrderValidator::updateMinOrderSize(const core::ExchangeId& exchangeId,
                                       const core::Symbol& tradingPair,
                                       double minSize) {
    if (minSize < 0) {
        LOG_ERROR("Invalid minimum order size: {}", minSize);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_minSizeMutex);
    
    // Update minimum order size
    m_minOrderSizes[exchangeId.toString()][tradingPair.toString()] = minSize;

    LOG_INFO("Updated minimum order size for {}/{}: {}", 
             exchangeId.toString(), tradingPair.toString(), minSize);

    return true;
}

bool OrderValidator::updatePriceValidationSettings(double maxDeviationPercent) {
    if (maxDeviationPercent <= 0) {
        LOG_ERROR("Invalid maximum price deviation percentage: {}", maxDeviationPercent);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_configMutex);
    
    // Update price validation settings
    m_validationConfig.maxPriceDeviationPercent = maxDeviationPercent;
    
    LOG_INFO("Updated maximum price deviation percentage: {}", maxDeviationPercent);
    
    return true;
}

bool OrderValidator::validateOrderType(OrderType orderType, std::vector<RiskViolation>& violations) {
    // Check if this order type is allowed
    const auto& allowedTypes = m_validationConfig.allowedOrderTypes;
    
    if (std::find(allowedTypes.begin(), allowedTypes.end(), orderType) == allowedTypes.end()) {
        RiskViolation violation;
        violation.type = ViolationType::OTHER;  // Use appropriate enum
        violation.description = "Order type is not allowed: " + toString(orderType);
        violation.limitValue = 0;
        violation.actualValue = 0;
        violations.push_back(violation);
        return false;
    }
    
    return true;
}

bool OrderValidator::validateTimeInForce(const std::string& timeInForce, std::vector<RiskViolation>& violations) {
    // Check if this time in force is allowed
    const auto& allowedTifs = m_validationConfig.allowedTimeInForce;
    
    if (std::find(allowedTifs.begin(), allowedTifs.end(), timeInForce) == allowedTifs.end()) {
        RiskViolation violation;
        violation.type = ViolationType::OTHER;  // Use appropriate enum
        violation.description = "Time in force is not allowed: " + timeInForce;
        violation.limitValue = 0;
        violation.actualValue = 0;
        violations.push_back(violation);
        return false;
    }
    
    return true;
}

double OrderValidator::getCurrentMarketPrice(const core::Symbol& tradingPair) const {
    // Backwards-compatible behavior: prefer the string-based const getPrice
    // used by older mocks/tests. If that returns 0.0 or throws, fall back to
    // the modern getCurrentPrice API.
    // Try string-based getPrice (const overload) first for backwards-compatibility
    // with legacy mocks used by the unit tests. Use a const-ref call to ensure
    // gmock-compatible dispatch to the const mocked method.
    LOG_INFO("OrderValidator::getCurrentMarketPrice calling legacy getPrice for {}", tradingPair.toString());
    std::string pairStr = tradingPair.toString();
    const trading::risk::DataInterface& constIface = static_cast<const trading::risk::DataInterface&>(*m_dataInterface);
    double legacy = constIface.getPrice(pairStr);
    if (legacy != 0.0) {
        return legacy;
    }

    try {
        LOG_INFO("OrderValidator::getCurrentMarketPrice falling back to getCurrentPrice for {}", tradingPair.toString());
            double fetched = m_dataInterface->getCurrentPrice(tradingPair, core::ExchangeId::binance().toString());
            LOG_INFO("OrderValidator::getCurrentMarketPrice fetched price {} for {}", fetched, tradingPair.toString());
            return fetched;
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to get market price for {}: {}", tradingPair.toString(), ex.what());
        throw std::runtime_error("Failed to get market price");
    }
}

} // namespace risk
} // namespace trading