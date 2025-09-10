#pragma once

#include "models/order.hpp"
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <core/market_types.hpp>

namespace trading { 
namespace execution {

using namespace core;
/**
 * Exchange information used for routing decisions
 */
// Update smart_order_router.hpp - add missing member variables to ExchangeInfo
 
// Trong smart_order_router.hpp - implement inline:
struct ExchangeInfo {
    core::ExchangeId exchangeId;
    double trading_fee;
    double liquidity_score;
    double latency_ms; 
    double success_rate;
    
    // Inline implementations (no .cpp needed)
    bool supports_order_type(models::OrderType type) const {
        // Support basic types for all exchanges
        return type == models::OrderType::MARKET || 
               type == models::OrderType::LIMIT ||
               type == models::OrderType::STOP ||
               type == models::OrderType::STOP_LIMIT;
    }
    
    bool supports_trading_pair(const core::Symbol& trading_pair) const {
        return true;
        // Accept all major trading pairs
        // return trading_pair.find("USDT") != std::string::npos ||
        //        trading_pair.find("USD") != std::string::npos ||
        //        trading_pair.find("BTC") != std::string::npos ||
        //        trading_pair.find("ETH") != std::string::npos ||
        //        trading_pair.find("LTC") != std::string::npos;
    }
};
/**
 * Routing factors to consider when selecting an exchange
 */
struct RoutingFactors {
    double fee_weight = 0.3;
    double liquidity_weight = 0.3;
    double latency_weight = 0.2;
    double success_rate_weight = 0.2;
};

/**
 * Result of the routing decision
 */
struct RoutingResult {
    ExchangeId exchangeId;
    double score;
    std::string reason;
};

/**
 * Smart Order Router selects the optimal exchange for order execution
 */
class SmartOrderRouter {
public:
    // Constructor
    SmartOrderRouter();
    
    // Initialize with exchange information
    void initialize(const std::vector<ExchangeInfo>& exchanges);
    // selectExchange
    // Select the best exchange for a given order
    RoutingResult selectExchange(const models::OrderPtr& order) const;
    // Update exchange information
    void updateExchangeInfo(const ExchangeInfo& exchange_info);
    
    // Route an order to the best exchange
    RoutingResult routeOrder(const models::OrderPtr& order) const;
    
    // Route an order to a specific exchange
    bool routeToExchange(models::OrderPtr& order, const core::ExchangeId& exchangeId) const;
    
    // Get available exchanges for a trading pair
    std::vector<std::string> getAvailableExchanges(const core::Symbol& tradingPair) const;
    
    // Set custom routing strategy
    void setRoutingStrategy(
        const std::function<RoutingResult(const models::OrderPtr&, 
                                          const std::vector<ExchangeInfo>&,
                                          const RoutingFactors&)>& strategy);
    
    // Configure routing factors
    void setRoutingFactors(const RoutingFactors& factors);
    
    // Get current routing factors
    RoutingFactors getRoutingFactors() const;
    
private:
    // Exchange information
    std::unordered_map<core::ExchangeId, ExchangeInfo> exchanges_;
    
    // Routing configuration
    RoutingFactors routing_factors_;
    
    // Custom routing strategy
    std::function<RoutingResult(const models::OrderPtr&, 
                               const std::vector<ExchangeInfo>&,
                               const RoutingFactors&)> routing_strategy_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Internal helper methods
    double calculateExchangeScore(const ExchangeInfo& exchange, 
                                 const models::OrderPtr& order) const;
    
    RoutingResult defaultRoutingStrategy(const models::OrderPtr& order,
                                        const std::vector<ExchangeInfo>& exchanges,
                                        const RoutingFactors& factors) const;
};

} // namespace execution
} // namespace trading