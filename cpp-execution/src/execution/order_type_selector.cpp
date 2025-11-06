#include "execution/order_type_selector.hpp"
#include <algorithm>

namespace trading {
namespace execution {

OrderTypeSelector::OrderTypeSelector() {
    // Set default selection strategy
    selection_strategy_ = [this](const models::OrderPtr& order, const MarketCondition& condition) {
        return this->defaultSelectionStrategy(order, condition);
    };
}

OrderTypeSelection OrderTypeSelector::selectOrderType(
    const models::OrderPtr& order,
    const MarketCondition& market_condition) const {
    
    // Use the current selection strategy
    return selection_strategy_(order, market_condition);
}

void OrderTypeSelector::updateMarketCondition(
    const core::Symbol& tradingPair,
    const MarketCondition& condition) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    market_conditions_[tradingPair.toString()] = condition;
}

MarketCondition OrderTypeSelector::getMarketCondition(const core::Symbol& tradingPair) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = market_conditions_.find(tradingPair.toString());
    if (it != market_conditions_.end()) {
        return it->second;
    }
    
    // Return default market condition if not found
    return MarketCondition{};
}

void OrderTypeSelector::setSelectionStrategy(
    const std::function<OrderTypeSelection(
        const models::OrderPtr&,
        const MarketCondition&)>& strategy) {
    
    selection_strategy_ = strategy;
}

void OrderTypeSelector::setVolatilityThreshold(double threshold) {
    volatility_threshold_ = threshold;
}

void OrderTypeSelector::setLiquidityThreshold(double threshold) {
    liquidity_threshold_ = threshold;
}

void OrderTypeSelector::setSpreadThreshold(double threshold) {
    spread_threshold_ = threshold;
}

OrderTypeSelection OrderTypeSelector::defaultSelectionStrategy(
    const models::OrderPtr& order,
    const MarketCondition& condition) const {
    
    OrderTypeSelection selection;
    
    // If already a market order and market conditions are favorable, keep it
    if (order->getType() == models::OrderType::MARKET) {
        // Check if we should convert to a limit order
        if (condition.spread > spread_threshold_ && condition.liquidity_score > liquidity_threshold_) {
            // Convert to limit order for better price
            selection.type = models::OrderType::LIMIT;
            
            // For buy orders: use price slightly above best bid to ensure quick fill
            // For sell orders: use price slightly below best ask to ensure quick fill
            if (order->getSide() == models::OrderSide::BUY) {
                if (condition.bidPrice > 0.0) {
                    selection.price = condition.bidPrice * (1.001); // 0.1% above bid
                } else {
                    // Fallback: use a safe default or revert to market
                    selection.type = models::OrderType::MARKET;
                    selection.price = 0.0;
                    selection.reason = "Converted to market - no valid bid price";
                }
            } else { // SELL order
                if (condition.askPrice > 0.0) {
                    selection.price = condition.askPrice * (0.999); // 0.1% below ask
                } else {
                    // CRITICAL FIX: Never set SELL price to 0!
                    selection.type = models::OrderType::MARKET;
                    selection.price = 0.0;
                    selection.reason = "Converted to market - no valid ask price (prevents 0 price SELL)";
                }
            }
            
            selection.time_in_force = models::TimeInForce::GTC;
            selection.reason = "Converted to limit order due to wide spread";
        } else {
            // Keep as market order
            selection.type = models::OrderType::MARKET;
            selection.price = 0.0; // Not applicable for market orders
            selection.time_in_force = models::TimeInForce::IOC; // Immediate execution
            selection.reason = "Using market order for immediate execution";
        }
        
        return selection;
    }
    
    // If already a limit order, check if we should adjust or convert
    if (order->getType() == models::OrderType::LIMIT) {
        // If market is highly volatile, consider switching to market
        if (condition.volatility > volatility_threshold_) {
            selection.type = models::OrderType::MARKET;
            selection.price = 0.0;
            selection.time_in_force = models::TimeInForce::IOC;
            selection.reason = "Converted to market order due to high volatility";
        } else {
            // Keep as limit order but potentially adjust price
            selection.type = models::OrderType::LIMIT;
            double current_price = order->getPrice();
            
            // Adjust price if needed based on current market conditions
            if (order->getSide() == models::OrderSide::BUY && condition.bidPrice > 0.0 && current_price < condition.bidPrice) {
                selection.price = condition.bidPrice * (1.001); // 0.1% above bid
                selection.reason = "Adjusted limit buy price to increase fill probability";
            } else if (order->getSide() == models::OrderSide::SELL && condition.askPrice > 0.0 && current_price > condition.askPrice) {
                selection.price = condition.askPrice * (0.999); // 0.1% below ask
                selection.reason = "Adjusted limit sell price to increase fill probability";
            } else if (current_price > 0.0) {
                selection.price = current_price;
                selection.reason = "Maintained current limit price";
            } else {
                // CRITICAL FIX: If no valid prices available, convert to market
                selection.type = models::OrderType::MARKET;
                selection.price = 0.0;
                selection.time_in_force = models::TimeInForce::IOC;
                selection.reason = "Converted to market - no valid price data available";
            }
            
            selection.time_in_force = models::TimeInForce::GTC;
        }
        
        return selection;
    }
    
    // For other order types (STOP, STOP_LIMIT, etc.), keep the original type
    selection.type = order->getType();
    selection.price = order->getPrice();
    selection.time_in_force = order->getTimeInForce();
    selection.reason = "Maintained original order type";
    
    return selection;
}

} // namespace execution
} // namespace trading