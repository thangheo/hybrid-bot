#include "execution/smart_order_router.hpp"
#include <algorithm>
#include <numeric>
#include <vector>

namespace trading {
namespace execution {
 
SmartOrderRouter::SmartOrderRouter() {
    // Set default routing strategy
    routing_strategy_ = [this](const models::OrderPtr& order, 
                              const std::vector<ExchangeInfo>& exchanges, 
                              const RoutingFactors& factors) {
        return this->defaultRoutingStrategy(order, exchanges, factors);
    };
}

void SmartOrderRouter::initialize(const std::vector<ExchangeInfo>& exchanges) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    exchanges_.clear();
    for (const auto& exchange : exchanges) {
        exchanges_[exchange.exchangeId] = exchange;
    }
}

void SmartOrderRouter::updateExchangeInfo(const ExchangeInfo& exchange_info) {
    std::lock_guard<std::mutex> lock(mutex_);
    exchanges_[exchange_info.exchangeId] = exchange_info;
}
RoutingResult SmartOrderRouter::selectExchange(const models::OrderPtr& order) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Get all available exchanges for this trading pair
    std::vector<ExchangeInfo> available_exchanges;
    for (const auto& [id, info] : exchanges_) {
        if (info.supports_trading_pair(order->getTradingPair()) && 
            info.supports_order_type(order->getType())) {
            available_exchanges.push_back(info);
        }
    }
    
    // If no available exchanges, return empty result
    if (available_exchanges.empty()) {
        return RoutingResult{
            ExchangeId(),0.0, "No available exchanges for this trading pair and order type"
        };
    }
    
    // Use the current routing strategy
    return routing_strategy_(order, available_exchanges, routing_factors_);
}

RoutingResult SmartOrderRouter::routeOrder(const models::OrderPtr& order) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Get all available exchanges for this trading pair
    std::vector<ExchangeInfo> available_exchanges;
    for (const auto& [id, info] : exchanges_) {
        if (info.supports_trading_pair(order->getTradingPair().toString()) && 
            info.supports_order_type(order->getType())) {
            available_exchanges.push_back(info);
        }
    }
    
    // If no available exchanges, return empty result
    if (available_exchanges.empty()) {
        return RoutingResult{
            ExchangeId(), 0.0, "No available exchanges for this trading pair and order type"
        };
    }
    
    // Use the current routing strategy
    return routing_strategy_(order, available_exchanges, routing_factors_);
}

bool SmartOrderRouter::routeToExchange(models::OrderPtr& order, const core::ExchangeId& exchangeId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if exchange exists and supports this trading pair and order type
    auto it = exchanges_.find(exchangeId);
    if (it == exchanges_.end()) {
        return false;
    }
    
    const auto& exchange = it->second;
    if (!exchange.supports_trading_pair(order->getTradingPair()) || 
        !exchange.supports_order_type(order->getType())) {
        return false;
    }
    
    // Set the exchange ID on the order
    order->setExchangeId(exchangeId);
    return true;
}

std::vector<std::string> SmartOrderRouter::getAvailableExchanges(const core::Symbol& tradingPair) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::string> available;
    for (const auto& [id, info] : exchanges_) {
        if (info.supports_trading_pair(tradingPair)) {
            available.push_back(id.toString());
        }
    }
    
    return available;
}

void SmartOrderRouter::setRoutingStrategy(
    const std::function<RoutingResult(const models::OrderPtr&, 
                                     const std::vector<ExchangeInfo>&,
                                     const RoutingFactors&)>& strategy) {
    routing_strategy_ = strategy;
}

void SmartOrderRouter::setRoutingFactors(const RoutingFactors& factors) {
    routing_factors_ = factors;
}

RoutingFactors SmartOrderRouter::getRoutingFactors() const {
    return routing_factors_;
}

RoutingResult SmartOrderRouter::defaultRoutingStrategy(
    const models::OrderPtr& order,
    const std::vector<ExchangeInfo>& exchanges,
    const RoutingFactors& factors) const {
    
    // Calculate scores for each exchange
    std::vector<std::pair<ExchangeId, double>> exchange_scores; // Changed to store ExchangeId directly
    for (const auto& exchange : exchanges) {
        double score = calculateExchangeScore(exchange, order);
        exchange_scores.emplace_back(exchange.exchangeId, score); // Use exchangeId directly
    }
    
    // Find exchange with highest score
    auto best_exchange = std::max_element(
        exchange_scores.begin(), 
        exchange_scores.end(),
        [](const auto& a, const auto& b) { return a.second < b.second; }
    );
    
    // Build routing result
    RoutingResult result;
    if (best_exchange != exchange_scores.end()) {
        result.exchangeId = best_exchange->first; // Directly assign the ExchangeId
        result.score = best_exchange->second;
        result.reason = "Selected based on optimal routing score";
    } else {
        result.exchangeId = core::ExchangeId(); // Default (invalid) exchange
        result.score = 0.0;
        result.reason = "No suitable exchange found";
    }
    
    return result;
}

double SmartOrderRouter::calculateExchangeScore(
    const ExchangeInfo& exchange,
    const models::OrderPtr& order) const {
    
    // Normalize scores to 0-1 range
    double fee_score = 1.0 - std::min(exchange.trading_fee / 0.01, 1.0); // Assume 1% is max fee
    double liquidityProfileScore = exchange.liquidityProfileScore; // Already 0-1
    double latency_score = 1.0 - std::min(exchange.latency_ms / 500.0, 1.0); // Assume 500ms is worst
    double reliability_score = exchange.success_rate; // Already 0-1
    
    // Weight each factor
    double weighted_score = 
        fee_score * routing_factors_.fee_weight +
        liquidityProfileScore * routing_factors_.liquidity_weight +
        latency_score * routing_factors_.latency_weight +
        reliability_score * routing_factors_.success_rate_weight;
    
    return weighted_score;
}

} // namespace execution
} // namespace trading