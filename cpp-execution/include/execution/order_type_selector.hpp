#pragma once

#include "models/order.hpp"
#include <memory>
#include <string>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace trading {
namespace execution {

/**
 * Market condition information
 */
struct MarketCondition {
    double bidPrice;
    double askPrice;
    double spread;
    double volatility;
    double volume;
    double liquidity_score;
    double market_impact_estimate;
};

/**
 * Order type selection result
 */
struct OrderTypeSelection {
    models::OrderType type;
    double price;
    models::TimeInForce time_in_force;
    std::string reason;
};

/**
 * OrderTypeSelector determines the optimal order type based on market conditions
 */
class OrderTypeSelector {
public:
    // Constructor
    OrderTypeSelector();
    
    // Select optimal order type
    OrderTypeSelection selectOrderType(
        const models::OrderPtr& order,
        const MarketCondition& market_condition) const;
    
    // Update market condition
    void updateMarketCondition(
        const core::Symbol& tradingPair,
        const MarketCondition& condition);
    
    // Get current market condition
    MarketCondition getMarketCondition(const core::Symbol& tradingPair) const;
    
    // Set custom selection strategy
    void setSelectionStrategy(
        const std::function<OrderTypeSelection(
            const models::OrderPtr&,
            const MarketCondition&)>& strategy);
    
    // Configure selection thresholds
    void setVolatilityThreshold(double threshold);
    void setLiquidityThreshold(double threshold);
    void setSpreadThreshold(double threshold);
    
private:
    // Market conditions for different trading pairs
    std::unordered_map<std::string, MarketCondition> market_conditions_;
    
    // Selection thresholds
    double volatility_threshold_ = 0.02;
    double liquidity_threshold_ = 0.5;
    double spread_threshold_ = 0.001;
    
    // Custom selection strategy
    std::function<OrderTypeSelection(
        const models::OrderPtr&,
        const MarketCondition&)> selection_strategy_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Default selection strategy
    OrderTypeSelection defaultSelectionStrategy(
        const models::OrderPtr& order,
        const MarketCondition& condition) const;
};

} // namespace execution
} // namespace trading