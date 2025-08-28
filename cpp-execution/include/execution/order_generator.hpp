#pragma once

#include "models/order.hpp"
#include <core/market_types.hpp>
#include "core/direction.hpp"
#include <memory>
#include <string>
#include <functional>

namespace trading {
namespace execution {

using namespace core;
/**
 * Parameters for order generation
 */
struct OrderGenerationParams {
    std::string tradingPair;
    core::Direction side;
    models::OrderType type = models::OrderType::MARKET;
    double quantity = 0.0;
    double price = 0.0;
    models::TimeInForce time_in_force = models::TimeInForce::GTC;
    std::string strategy_id;
    std::string signal_id;
    std::string client_order_id;
    // signal_strength 
    double signal_strength = 0.0;
    double target_size = 0.0;
    std::chrono::milliseconds signal_ttl = std::chrono::milliseconds(0);
    ExchangeId exchangeId;

};

/**
 * OrderGenerator creates orders based on trading signals or algorithm requirements
 */
class OrderGenerator {
public:
    using OrderCallback = std::function<void(const models::OrderPtr&)>;
    
    // Constructor
    OrderGenerator();
    
    // Create an order from parameters
    models::OrderPtr createOrder(const OrderGenerationParams& params);
    
    // Create an order from a trading signal
    models::OrderPtr createOrderFromSignal(const std::shared_ptr<class TradingSignal>& signal);
    
    // Generate a unique client order ID
    std::string generateClientOrderId();
    
    // Set callback for new orders
    void setOrderCallback(const OrderCallback& callback);
    
    // Apply custom parameter transformations
    void setParameterTransformer(
        const std::function<void(OrderGenerationParams&)>& transformer);
    
private:
    // Configuration
    std::string order_id_prefix_ = "OID";
    int order_id_sequence_ = 0;
    
    // Callbacks
    OrderCallback order_callback_;
    std::function<void(OrderGenerationParams&)> parameter_transformer_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Internal helper methods
    void validateParams(const OrderGenerationParams& params) const;
    void enrichOrder(models::OrderPtr& order, const OrderGenerationParams& params);
};

} // namespace execution
} // namespace trading