#include "execution/order_generator.hpp"
#include "execution/models/order.hpp"
#include "execution/trading_signal.hpp" 
#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>

namespace trading {
namespace execution {

OrderGenerator::OrderGenerator() {
    // Initialize with a random starting sequence to avoid predictable order IDs
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(1000, 9999);
    order_id_sequence_ = distrib(gen);
}

models::OrderPtr OrderGenerator::createOrder(const OrderGenerationParams& params) {
    OrderGenerationParams processed_params = params;
    
    // Apply custom transformations if provided
    if (parameter_transformer_) {
        parameter_transformer_(processed_params);
    }
    
    // Validate parameters
    validateParams(processed_params);
    
    // Create the order
    models::OrderPtr order = std::make_shared<models::Order>();
    
    // Set client order ID if provided or generate one
    if (!processed_params.client_order_id.empty()) {
        order->setClientOrderId(processed_params.client_order_id);
    } else {
        order->setClientOrderId(generateClientOrderId());
    }
    
    // Set basic order properties
    order->setTradingPair(processed_params.tradingPair);
    order->setSide(processed_params.side);
    order->setType(processed_params.type);
    order->setQuantity(processed_params.quantity);
    
    // Set price if applicable (for limit orders)
    if (processed_params.price > 0 && 
        (processed_params.type == models::OrderType::LIMIT || 
         processed_params.type == models::OrderType::STOP_LIMIT)) {
        order->setPrice(processed_params.price);
    }
    
    // Set time in force
    order->setTimeInForce(processed_params.time_in_force);
    
    // Set strategy information
    order->setStrategyId(processed_params.strategy_id);
    order->setSignalId(processed_params.signal_id);
    
    // Enrich the order with additional properties
    enrichOrder(order, processed_params);
    
    // Notify via callback if registered
    if (order_callback_) {
        order_callback_(order);
    }
    
    return order;
}
models::OrderPtr OrderGenerator::createOrderFromSignal(const std::shared_ptr<TradingSignal>& signal) {
    // Extract order parameters from the trading signal
    OrderGenerationParams params;
    
    // Basic signal to order conversion
    params.tradingPair = signal->tradingPair;
    params.side = signal->direction;  // Already a models::OrderSide, no conversion needed
    params.quantity = signal->target_size;
    params.strategy_id = signal->strategy_id;
    params.signal_id = signal->signal_id;
    params.signal_strength = signal->strength;
    params.target_size = signal->target_size;
    params.signal_ttl = signal->signal_ttl;
    
    // Default to market order, will be refined by OrderTypeSelector
    params.type = models::OrderType::MARKET;
    
    // Set a unique client order ID
    params.client_order_id = generateClientOrderId();
    
    // Create the order using the standard method
    return createOrder(params);
}

std::string OrderGenerator::generateClientOrderId() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Format: OID-TIMESTAMP-SEQUENCE
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    std::stringstream ss;
    ss << order_id_prefix_ << "-"
       << std::hex << std::setw(12) << std::setfill('0') << now_ms << "-"
       << std::setw(4) << std::setfill('0') << (order_id_sequence_++);
    
    // Wrap around if sequence gets too large
    if (order_id_sequence_ > 9999) {
        order_id_sequence_ = 0;
    }
    
    return ss.str();
}

void OrderGenerator::setOrderCallback(const OrderCallback& callback) {
    order_callback_ = callback;
}

void OrderGenerator::setParameterTransformer(
    const std::function<void(OrderGenerationParams&)>& transformer) {
    parameter_transformer_ = transformer;
}

void OrderGenerator::validateParams(const OrderGenerationParams& params) const {
    // Check for required fields
    if (params.tradingPair.empty()) {
        throw std::invalid_argument("Trading pair cannot be empty");
    }
    
    if (params.quantity <= 0) {
        throw std::invalid_argument("Order quantity must be positive");
    }
    
    // Validate price for limit orders
    if ((params.type == models::OrderType::LIMIT || params.type == models::OrderType::STOP_LIMIT) 
        && params.price <= 0) {
        throw std::invalid_argument("Limit orders require a valid price");
    }
}

void OrderGenerator::enrichOrder(models::OrderPtr& order, const OrderGenerationParams& params) {
    // Add exchange-specific parameters if needed
    // These could be derived from the trading pair or other parameters
    
    // Set default expiry time for GTD orders
    if (params.time_in_force == models::TimeInForce::GTD) {
        // Default to 24 hours from now
        auto expiry_time = std::chrono::system_clock::now() + std::chrono::hours(24);
        order->setExpiryTime(expiry_time);
    }
    
    // Add order creation timestamp
    // (This is already handled by the Order constructor)
}

} // namespace execution
} // namespace trading