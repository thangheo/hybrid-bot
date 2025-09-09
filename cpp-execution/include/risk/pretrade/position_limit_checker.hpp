/**
 * Interface for enforcing position size limits per asset
 *
 * File: position_limit_checker.hpp
 * Created: 2025-04-29
 * Updated: 2025-05-28
 */
#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"
#include "../portfolio/position_tracker.hpp"
#include "execution/trading_signal.hpp"

#include <map>
#include <string>
#include <mutex>
#include <memory>

namespace trading {
namespace risk {

// Use the execution TradingSignal as the primary type
using TradingSignal = execution::TradingSignal;

/**
 * PositionLimitChecker enforces maximum position sizes per asset, strategy, and exchange.
 * It prevents excessive concentration in any single position.
 */
class PositionLimitChecker {
public:
    /**
     * Constructor
     * @param config Risk configuration containing position limits
     * @param positionTracker Reference to the position tracker to get current positions
     */
    PositionLimitChecker(const RiskConfig& config, std::shared_ptr<PositionTracker> positionTracker);
    
    /**
     * Initialize the checker
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    // isHealthy
    bool isHealthy() const;
    /**
     * Check if adding a new position would exceed position limits
     * @param signal The trading signal to check
     * @param violations Vector to fill with any violations
     * @return true if within limits, false otherwise
     */
    bool checkPositionLimit(const TradingSignal& signal, std::vector<RiskViolation>& violations);
    
    /**
     * Check if adding a new position would exceed position limits
     * @param orderRequest The order request to check
     * @param violations Vector to fill with any violations
     * @return true if within limits, false otherwise
     */
    bool checkPositionLimit(const OrderRequest& orderRequest, std::vector<RiskViolation>& violations);
    
    /**
     * Update position limits
     * @param assetId The asset identifier
     * @param strategyId The strategy identifier
     * @param newLimit The new position limit
     * @return true if update was successful, false otherwise
     */
    bool updatePositionLimit(const std::string& assetId, const core::StrategyId& strategyId, double newLimit);
    
    /**
     * Get the current position limit for an asset and strategy
     * @param assetId The asset identifier
     * @param strategyId The strategy identifier
     * @return The current position limit
     */
    double getPositionLimit(const std::string& assetId, const std::string& strategyId) const;
    
    /**
     * Calculate the new position size if the signal/order was executed
     * @param tradingPair The trading pair
     * @param direction The trading direction
     * @param quantity The order quantity
     * @return The new theoretical position size
     */
    double calculateNewPosition(const TradingPair& tradingPair, core::Direction direction, double quantity) const;

private:
    std::shared_ptr<PositionTracker> m_positionTracker;
    RiskConfig m_config;
    PositionLimitConfig m_positionLimits;
    mutable std::mutex m_limitsLock;
    
    // Internal limits cache for faster access
    std::map<std::string, double> m_assetLimits;
    std::map<std::string, double> m_strategyLimits;
    std::map<std::string, double> m_exchangeLimits;
    double m_defaultLimit;
    
    void loadLimitsFromConfig();
    
    // Helper methods
    std::string getBaseAsset(const std::string& tradingPair) const;
    std::string getQuoteAsset(const std::string& tradingPair) const;
};

} // namespace risk
} // namespace trading