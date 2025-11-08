/**
 * Implementation of the position limit checker
 *
 * File: position_limit_checker.cpp
 * Created: 2025-04-29
 * Updated: 2025-05-28
 */

#include "risk/pretrade/position_limit_checker.hpp"
#include "common/logger.hpp"

#include <algorithm>
#include <cmath>

namespace trading {
namespace risk {

PositionLimitChecker::PositionLimitChecker(const RiskConfig& config, 
                                          std::shared_ptr<PositionTracker> positionTracker)
    : m_config(config)
    , m_positionTracker(positionTracker)
    , m_defaultLimit(1000.0) // Default limit if not specified
{
    // Initialize empty maps for limits
}
// isHealthy
bool PositionLimitChecker::isHealthy() const {
    return m_positionTracker != nullptr;
}
bool PositionLimitChecker::initialize() {
    try {
        loadLimitsFromConfig();
        LOG_INFO("Position limit checker initialized successfully");
        return true;
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to initialize position limit checker: {}", ex.what());
        return false;
    }
}

void PositionLimitChecker::loadLimitsFromConfig() {
    std::lock_guard<std::mutex> lock(m_limitsLock);
    
    // Load position limits from config using the correct method name
    m_positionLimits = m_config.getPositionLimitConfig();
    m_defaultLimit = m_positionLimits.defaultLimit;
    
    // Load asset-specific limits
    for (const auto& asset : m_positionLimits.assetLimits) {
        m_assetLimits[asset.first] = asset.second;
    }
    
    // Load strategy-specific limits
    for (const auto& strategy : m_positionLimits.strategyLimits) {
        m_strategyLimits[strategy.first] = strategy.second;
    }
    
    // Load exchange-specific limits - convert ExchangeId to string
    for (const auto& exchange : m_positionLimits.exchangeLimits) {
        m_exchangeLimits[exchange.first] = exchange.second;
    }
    
    LOG_INFO("Loaded position limits: {} assets, {} strategies, {} exchanges",
             m_assetLimits.size(), m_strategyLimits.size(), m_exchangeLimits.size());
}

bool PositionLimitChecker::checkPositionLimit(const TradingSignal& signal, 
                                             std::vector<RiskViolation>& violations) {
    // Extract trading pair and asset information
    core::Symbol tradingPairStr = signal.getTradingPair();
    TradingPair tradingPair(tradingPairStr);
    std::string asset = getBaseAsset(tradingPair.toString());
    StrategyId strategyId = signal.getStrategyId();
    core::ExchangeId exchangeId = signal.exchangeId;

    LOG_WARN("POSITION_CHECK_START: signal={} asset={} pair={} qty={} target={}", 
              signal.getSignalId(), asset, tradingPair.toString(), signal.getQuantity(), signal.getTargetSize());

    // We treat configured limits as USD notional limits. Convert current
    // position and proposed order into USD and compare against limits.

    // Current position notional (USD)
    double currentPrice = 0.0;
    try {
        if (m_positionTracker) {
            currentPrice = m_positionTracker->getCachedPrice(tradingPair);
        }
    } catch (...) { currentPrice = 0.0; }

    LOG_WARN("PRICE_DEBUG: pair={} currentPrice={} positionTracker={}", 
              tradingPair.toString(), currentPrice, (m_positionTracker ? "YES" : "NO"));

    double currentNotional = 0.0;
    if (currentPrice > 0.0) {
        // find current position for tradingPair
        auto posOpt = m_positionTracker->getPosition(core::ExchangeId::unknown(), tradingPair);
        if (posOpt.has_value()) {
            currentNotional = std::abs(posOpt->getQuantity()) * currentPrice;
        }
    }

    // Proposed order: if targetSize <= 1.0 treat as fraction of the asset limit (USD);
    // otherwise treat as absolute asset quantity and convert to USD using currentPrice.
    double orderNotional = 0.0;
    double targetSize = signal.getTargetSize();
    double assetLimitUsd = getPositionLimit(asset, strategyId.toString());
    double orderQuantity = std::abs(signal.getQuantity());

    LOG_WARN("DEBUG_LIMITS: signal={} assetLimitUsd={} orderQuantity={} currentPrice={}", 
              signal.getSignalId(), assetLimitUsd, orderQuantity, currentPrice);

    if (orderQuantity > 0.0) {
        // Explicit absolute quantity provided on the signal -> convert to USD using current price
        if (currentPrice > 0.0) {
            orderNotional = orderQuantity * currentPrice;
        } else {
            LOG_WARN("PRICE_UNAVAILABLE_1: signal={} asset={}", 
                     signal.getSignalId(), asset);
            RiskViolation violation(
                ViolationType::PRICE_DEVIATION,
                "Price unavailable to compute order notional for position limit check",
                0.0,
                0.0
            );
            violations.push_back(violation);
            return false;
        }
    } else if (targetSize <= 1.0) {
        // Interpret as fraction of the configured asset limit (USD)
        if (assetLimitUsd <= 0.0) {
            LOG_WARN("ASSET_LIMIT_TOO_LOW: signal={} asset={} assetLimitUsd={}", 
                     signal.getSignalId(), asset, assetLimitUsd);
            RiskViolation violation(
                ViolationType::POSITION_LIMIT,
                "Asset position limit too low to interpret targetSize fractionally for " + asset,
                assetLimitUsd,
                0.0
            );
            violations.push_back(violation);
            return false;
        }
        orderNotional = targetSize * assetLimitUsd;
        // For fractional orders, we don't need current price to validate
        // Just check if the order notional (fraction of limit) is reasonable
        LOG_WARN("FRACTIONAL_ORDER: signal={} asset={} targetSize={} orderNotional={} limit={} currentPrice={}", 
                 signal.getSignalId(), asset, targetSize, orderNotional, assetLimitUsd, currentPrice);
        
        // For fractional orders, if no current price, assume currentNotional = 0
        // This allows the order to proceed with just the fractional limit check
        if (currentPrice <= 0.0) {
            currentNotional = 0.0;
            LOG_WARN("FRACTIONAL_NO_PRICE: signal={} asset={} - proceeding with currentNotional=0", 
                     signal.getSignalId(), asset);
        }
    } else {
        // targetSize > 1.0: treat as absolute asset quantity
        if (currentPrice > 0.0) {
            orderNotional = std::abs(targetSize) * currentPrice;
        } else {
            LOG_WARN("PRICE_UNAVAILABLE_2: signal={} asset={}", 
                     signal.getSignalId(), asset);
            RiskViolation violation(
                ViolationType::PRICE_DEVIATION,
                "Price unavailable to compute order notional for position limit check",
                0.0,
                0.0
            );
            violations.push_back(violation);
            return false;
        }
    }

    LOG_DEBUG("PositionLimitChecker", 
              "Calculated notionals for signal {}: orderQuantity={}, targetSize={}, orderNotional={}", 
              signal.getSignalId(), orderQuantity, targetSize, orderNotional);

    double proposedNotional = currentNotional + orderNotional;

    // Get applicable limits (USD)
    double assetLimit = getPositionLimit(asset, strategyId.toString());
    double strategyLimit = 0.0;
    double exchangeLimit = 0.0;
    
    {
        std::lock_guard<std::mutex> lock(m_limitsLock);
        
        // Get strategy limit
        auto stratIt = m_strategyLimits.find(strategyId.toString());
        strategyLimit = (stratIt != m_strategyLimits.end()) ? 
                       stratIt->second : m_defaultLimit;
        
        // Get exchange limit - convert ExchangeId to string for lookup
        std::string exchangeStr = exchangeId.toString();
        auto exchIt = m_exchangeLimits.find(exchangeStr);
        exchangeLimit = (exchIt != m_exchangeLimits.end()) ? 
                       exchIt->second : m_defaultLimit;
    }
    
    bool withinLimits = true;

    // If the configured assetLimit is interpreted as USD notional, validate proposedNotional
    if (proposedNotional > assetLimit) {
        // print for debug all related values
        LOG_WARN("ASSET_LIMIT_EXCEEDED: signal={} asset={} current={} order={} proposed={} limit={}", 
                  signal.getSignalId(), asset, currentNotional, orderNotional, proposedNotional, assetLimit);
        RiskViolation violation(
            ViolationType::POSITION_LIMIT,
            "Asset position notional limit exceeded for " + asset,
            assetLimit,
            proposedNotional
        );
        violations.push_back(violation);
        withinLimits = false;
    }
    
    // Check strategy limit (if applicable) - compute current strategy-level notional
    double strategyCurrentNotional = 0.0;
    if (m_positionTracker) {
        try {
            auto stratPositions = m_positionTracker->getStrategyPositions(strategyId);
            for (const auto& p : stratPositions) strategyCurrentNotional += p.getNotionalValue();
        } catch (...) { strategyCurrentNotional = 0.0; }
    }
    double proposedStrategyNotional = strategyCurrentNotional + orderNotional;

    if (strategyLimit > 0.0 && proposedStrategyNotional > strategyLimit) {
        LOG_WARN("STRATEGY_LIMIT_EXCEEDED: signal={} strategy={} current={} order={} proposed={} limit={}", 
                  signal.getSignalId(), strategyId.toString(), strategyCurrentNotional, orderNotional, proposedStrategyNotional, strategyLimit);
        RiskViolation violation(
            ViolationType::POSITION_LIMIT,
            "Strategy-level position notional limit exceeded for " + strategyId.toString(),
            strategyLimit,
            proposedStrategyNotional
        );
        violations.push_back(violation);
        withinLimits = false;
    }

    // Check exchange limit - compute current exchange-level notional
    double exchangeCurrentNotional = 0.0;
    if (m_positionTracker) {
        try {
            auto exchPositions = m_positionTracker->getExchangePositions(exchangeId);
            for (const auto& p : exchPositions) exchangeCurrentNotional += p.getNotionalValue();
        } catch (...) { exchangeCurrentNotional = 0.0; }
    }
    double proposedExchangeNotional = exchangeCurrentNotional + orderNotional;

    if (exchangeLimit > 0.0 && proposedExchangeNotional > exchangeLimit) {
        LOG_WARN("EXCHANGE_LIMIT_EXCEEDED: signal={} exchange={} current={} order={} proposed={} limit={}", 
                  signal.getSignalId(), exchangeId.toString(), exchangeCurrentNotional, orderNotional, proposedExchangeNotional, exchangeLimit);
        RiskViolation violation(
            ViolationType::POSITION_LIMIT,
            "Exchange-level position notional limit exceeded for exchange " + exchangeId.toString(),
            exchangeLimit,
            proposedExchangeNotional
        );
        violations.push_back(violation);
        withinLimits = false;
    }
    
    LOG_DEBUG("PositionLimitChecker", 
              "Position limit check completed for signal {}: withinLimits={}, violations={}",
              signal.getSignalId(), withinLimits, violations.size());
    
    return withinLimits;
}

bool PositionLimitChecker::checkPositionLimit(const OrderRequest& orderRequest, 
                                             std::vector<RiskViolation>& violations) {
    // Convert OrderRequest to equivalent TradingSignal for checking
    TradingSignal equivalentSignal;
    equivalentSignal.setTradingPair(orderRequest.tradingPair.toString());
    equivalentSignal.setDirection((orderRequest.side == core::Direction::BUY) ? 
                                core::Direction::BUY : core::Direction::SELL);
    equivalentSignal.setTargetSize(orderRequest.quantity);
    equivalentSignal.setStrategyId(orderRequest.strategyId);
    equivalentSignal.setExchangeId(orderRequest.exchangeId);
    
    return checkPositionLimit(equivalentSignal, violations);
}

double PositionLimitChecker::calculateNewPosition(const TradingPair& tradingPair, 
                                                 core::Direction direction, 
                                                 double quantity) const {
    std::string baseAsset = getBaseAsset(tradingPair.toString());
    
    // Get current position - this assumes PositionTracker has a method to get position by asset
    // We'll need to implement this or use a different approach
    double currentPosition = 0.0; // Placeholder
    
    // TODO: Implement getting current position from position tracker
    // This might require changes to PositionTracker interface
    
    // Calculate new position based on direction
    double change = (direction == core::Direction::BUY) ? quantity : -quantity;
    double newPosition = currentPosition + change;
    
    return newPosition;
}

bool PositionLimitChecker::updatePositionLimit(const std::string& assetId, 
                                              const StrategyId& strategyId, 
                                              double newLimit) {
    if (newLimit <= 0) {
        LOG_ERROR("Invalid position limit value: {}", newLimit);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_limitsLock);

    if (!strategyId.id.empty()) {
        // Update strategy limit
        m_strategyLimits[strategyId.id] = newLimit;
        LOG_INFO("Updated position limit for strategy {}: {}", strategyId.id, newLimit);
    } else if (!assetId.empty()) {
        // Update asset limit
        m_assetLimits[assetId] = newLimit;
        LOG_INFO("Updated position limit for asset {}: {}", assetId, newLimit);
    } else {
        // Update default limit
        m_defaultLimit = newLimit;
        LOG_INFO("Updated default position limit: {}", newLimit);
    }
    
    return true;
}

double PositionLimitChecker::getPositionLimit(const std::string& assetId, 
                                             const std::string& strategyId) const {
    std::lock_guard<std::mutex> lock(m_limitsLock);
    
    // First check for asset+strategy specific limit
    std::string combinedKey = assetId + ":" + strategyId;
    auto it = m_assetLimits.find(combinedKey);
    if (it != m_assetLimits.end()) {
        return it->second;
    }
    
    // Then check for asset-specific limit
    it = m_assetLimits.find(assetId);
    if (it != m_assetLimits.end()) {
        return it->second;
    }
    
    // Then check for strategy-specific limit
    auto stratIt = m_strategyLimits.find(strategyId);
    if (stratIt != m_strategyLimits.end()) {
        return stratIt->second;
    }
    
    // Fall back to default limit
    return m_defaultLimit;
}

std::string PositionLimitChecker::getBaseAsset(const core::Symbol& tradingPair) const {
    // Extract base asset from trading pair (e.g., "BTC/USDT" -> "BTC")
    if(tradingPair.base.empty()) {
        return "UNKNOWN";
    }
    return tradingPair.base;
}

std::string PositionLimitChecker::getQuoteAsset(const core::Symbol& tradingPair) const {
    // Extract quote asset from trading pair (e.g., "BTC/USDT" -> "USDT")
    if(tradingPair.quote.empty()) {
        return "UNKNOWN";
    }
    return tradingPair.quote;
}

} // namespace risk
} // namespace trading