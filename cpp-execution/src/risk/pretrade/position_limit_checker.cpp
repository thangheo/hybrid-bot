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
    std::string tradingPairStr = signal.getTradingPair();
    TradingPair tradingPair(tradingPairStr);
    std::string asset = getBaseAsset(tradingPairStr);
    std::string strategyId = signal.getStrategyId();
    core::ExchangeId exchangeId = signal.exchange;
    
    // Calculate new position after this trade
    double newPosition = calculateNewPosition(
        tradingPair, 
        signal.getDirection(), 
        signal.getTargetSize()
    );
    
    // Get applicable limits
    double assetLimit = getPositionLimit(asset, strategyId);
    double strategyLimit = 0.0;
    double exchangeLimit = 0.0;
    
    {
        std::lock_guard<std::mutex> lock(m_limitsLock);
        
        // Get strategy limit
        auto stratIt = m_strategyLimits.find(strategyId);
        strategyLimit = (stratIt != m_strategyLimits.end()) ? 
                       stratIt->second : m_defaultLimit;
        
        // Get exchange limit - convert ExchangeId to string for lookup
        std::string exchangeStr = exchangeId.toString();
        auto exchIt = m_exchangeLimits.find(exchangeStr);
        exchangeLimit = (exchIt != m_exchangeLimits.end()) ? 
                       exchIt->second : m_defaultLimit;
    }
    
    bool withinLimits = true;
    
    // Check asset-specific limit
    if (std::abs(newPosition) > assetLimit) {
        RiskViolation violation(
            ViolationType::POSITION_LIMIT,
            "Asset position limit exceeded for " + asset,
            assetLimit,
            std::abs(newPosition)
        );
        violations.push_back(violation);
        withinLimits = false;
    }
    
    // Check strategy limit (if applicable)
    // Note: This requires implementing getTotalPositionForStrategy in PositionTracker
    // For now, we'll skip this check as the method signature isn't defined
    
    // Check exchange limit (if applicable)
    // Note: This requires implementing getTotalPositionForExchange in PositionTracker
    // For now, we'll skip this check as the method signature isn't defined
    
    return withinLimits;
}

bool PositionLimitChecker::checkPositionLimit(const OrderRequest& orderRequest, 
                                             std::vector<RiskViolation>& violations) {
    // Convert OrderRequest to equivalent TradingSignal for checking
    TradingSignal equivalentSignal;
    equivalentSignal.tradingPair = orderRequest.tradingPair;
    equivalentSignal.direction = (orderRequest.side == core::Direction::BUY) ? 
                                core::Direction::BUY : core::Direction::SELL;
    equivalentSignal.target_size = orderRequest.quantity;
    equivalentSignal.strategy_id = orderRequest.strategyId;
    equivalentSignal.exchange = orderRequest.exchangeId;
    
    return checkPositionLimit(equivalentSignal, violations);
}

double PositionLimitChecker::calculateNewPosition(const TradingPair& tradingPair, 
                                                 core::Direction direction, 
                                                 double quantity) const {
    std::string baseAsset = getBaseAsset(tradingPair.getSymbol());
    
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
                                              const std::string& strategyId, 
                                              double newLimit) {
    if (newLimit <= 0) {
        LOG_ERROR("Invalid position limit value: {}", newLimit);
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_limitsLock);
    
    if (!strategyId.empty()) {
        // Update strategy limit
        m_strategyLimits[strategyId] = newLimit;
        LOG_INFO("Updated position limit for strategy {}: {}", strategyId, newLimit);
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

std::string PositionLimitChecker::getBaseAsset(const std::string& tradingPair) const {
    // Extract base asset from trading pair (e.g., "BTC/USDT" -> "BTC")
    const auto delimiter = tradingPair.find('/');
    if (delimiter != std::string::npos) {
        return tradingPair.substr(0, delimiter);
    }
    
    // Handle pairs without delimiter (e.g., "BTCUSDT" -> "BTC")
    // This is a simple heuristic - for production, you might want more sophisticated parsing
    if (tradingPair.length() >= 6) {
        return tradingPair.substr(0, 3); // Assume first 3 characters are base
    }
    
    return tradingPair; // Fallback
}

std::string PositionLimitChecker::getQuoteAsset(const std::string& tradingPair) const {
    // Extract quote asset from trading pair (e.g., "BTC/USDT" -> "USDT")
    const auto delimiter = tradingPair.find('/');
    if (delimiter != std::string::npos) {
        return tradingPair.substr(delimiter + 1);
    }
    
    // Handle pairs without delimiter (e.g., "BTCUSDT" -> "USDT")
    if (tradingPair.length() >= 6) {
        return tradingPair.substr(3); // Assume remaining characters are quote
    }
    
    return ""; // Fallback
}

} // namespace risk
} // namespace trading