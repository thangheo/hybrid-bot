// models/order.cpp
#include "models/order.hpp"
#include <sstream>
#include <iomanip>
#include <uuid/uuid.h>
#include <mutex>
#include <core/market_types.hpp>

namespace trading {
namespace execution {
namespace models {
// Helper function to generate UUID
std::string generateUuid() {
    uuid_t uuid;
    uuid_generate(uuid);
    
    char uuid_str[37];
    uuid_unparse_lower(uuid, uuid_str);
    
    return std::string(uuid_str);
}

// Default constructor
Order::Order() : client_order_id_(generateUuid()) {
    created_at_ = std::chrono::system_clock::now();
    updated_at_ = created_at_;
}

// Constructor with client order ID
Order::Order(const std::string& client_order_id) : client_order_id_(client_order_id) {
    created_at_ = std::chrono::system_clock::now();
    updated_at_ = created_at_;
}
//copy constructor
Order::Order(const Order& other) {
    std::lock_guard<std::mutex> lock(other.mutex_);
    
    order_id_ = other.order_id_;
    client_order_id_ = other.client_order_id_;
    exchangeId_ = other.exchangeId_;
    exchange_order_id_ = other.exchange_order_id_;
    trading_pair_ = other.trading_pair_;
    side_ = other.side_;
    type_ = other.type_;
    quantity_ = other.quantity_;
    price_ = other.price_;
    stop_price_ = other.stop_price_;
    time_in_force_ = other.time_in_force_;
    expiry_time_ = other.expiry_time_;
    state_ = other.state_.load();
    rejection_reason_ = other.rejection_reason_;
    created_at_ = other.created_at_;
    updated_at_ = other.updated_at_;
    strategy_id_ = other.strategy_id_;
    signal_id_ = other.signal_id_;
    fills_ = other.fills_;
    filled_quantity_ = other.filled_quantity_;
    average_fill_price_ = other.average_fill_price_;
    extra_params_ = other.extra_params_;
}
// Factory method for market orders
std::shared_ptr<Order> Order::createMarketOrder(
    const core::Symbol& tradingPair,
    OrderSide side, 
    double quantity) {
    
    auto order = std::make_shared<Order>();
    order->setTradingPair(tradingPair);
    order->setSide(side);
    order->setType(OrderType::MARKET);
    order->setQuantity(quantity);
    
    return order;
}

// Factory method for limit orders
std::shared_ptr<Order> Order::createLimitOrder(
    const core::Symbol& tradingPair,
    OrderSide side, 
    double quantity, 
    double price) {
    
    auto order = std::make_shared<Order>();
    order->setTradingPair(tradingPair);
    order->setSide(side);
    order->setType(OrderType::LIMIT);
    order->setQuantity(quantity);
    order->setPrice(price);
    
    return order;
}

// Getters
const std::string& Order::getOrderId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return order_id_;
}

const std::string& Order::getClientOrderId() const {
    // No lock needed for immutable value
    return client_order_id_;
}

const core::ExchangeId& Order::getExchangeId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return exchangeId_;
}

const std::string& Order::getExchangeOrderId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return exchange_order_id_;
}

const core::Symbol& Order::getTradingPair() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return trading_pair_;
}

OrderSide Order::getSide() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return side_;
}

OrderType Order::getType() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return type_;
}

double Order::getQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return quantity_;
}

double Order::getPrice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return price_;
}
std::string Order::getExtraParam(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = extra_params_.find(key);
    if (it != extra_params_.end()) {
        return it->second;
    }
    return "";  // Return empty string if parameter not found
}
std::optional<double> Order::getStopPrice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stop_price_;
}

TimeInForce Order::getTimeInForce() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return time_in_force_;
}

OrderState Order::getState() const {
    // Using atomic variable, no lock needed
    return state_;
}

const std::string& Order::getStrategyId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return strategy_id_;
}

const std::string& Order::getSignalId() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return signal_id_;
}

std::chrono::system_clock::time_point Order::getCreatedTime() const {
    // No lock needed for immutable value
    return created_at_;
}

std::chrono::system_clock::time_point Order::getUpdatedTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return updated_at_;
}

std::optional<std::chrono::system_clock::time_point> Order::getExpiryTime() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return expiry_time_;
}

const std::vector<Fill>& Order::getFills() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return fills_;
}

double Order::getFilledQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return filled_quantity_;
}

double Order::getRemainingQuantity() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return quantity_ - filled_quantity_;
}

double Order::getAverageFillPrice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return average_fill_price_;
}

const std::unordered_map<std::string, std::string>& Order::getExtraParams() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return extra_params_;
}

// Setters
void Order::setOrderId(const std::string& order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    order_id_ = order_id;
    updated_at_ = std::chrono::system_clock::now();
}
//setClientOrderId
void Order::setClientOrderId(const std::string& client_order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    client_order_id_ = client_order_id;
    updated_at_ = std::chrono::system_clock::now();
}
void Order::setExchangeId(const core::ExchangeId& exchangeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    exchangeId_ = exchangeId;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setExchangeOrderId(const std::string& exchange_order_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    exchange_order_id_ = exchange_order_id;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setTradingPair(const core::Symbol& tradingPair) {
    std::lock_guard<std::mutex> lock(mutex_);
    trading_pair_ = tradingPair;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setSide(OrderSide side) {
    std::lock_guard<std::mutex> lock(mutex_);
    side_ = side;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setType(OrderType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    type_ = type;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setQuantity(double quantity) {
    std::lock_guard<std::mutex> lock(mutex_);
    quantity_ = quantity;
    updated_at_ = std::chrono::system_clock::now();
}
void Order::setState(OrderState state) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = state;
    updated_at_ = std::chrono::system_clock::now();
}
void Order::setPrice(double price) {
    std::lock_guard<std::mutex> lock(mutex_);
    price_ = price;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setStopPrice(std::optional<double> stop_price) {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_price_ = stop_price;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setTimeInForce(TimeInForce time_in_force) {
    std::lock_guard<std::mutex> lock(mutex_);
    time_in_force_ = time_in_force;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setExpiryTime(std::optional<std::chrono::system_clock::time_point> expiry_time) {
    std::lock_guard<std::mutex> lock(mutex_);
    expiry_time_ = expiry_time;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setStrategyId(const std::string& strategy_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    strategy_id_ = strategy_id;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setSignalId(const std::string& signal_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    signal_id_ = signal_id;
    updated_at_ = std::chrono::system_clock::now();
}

void Order::setExtraParam(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    extra_params_[key] = value;
    updated_at_ = std::chrono::system_clock::now();
}

// State transitions
bool Order::open() {
    OrderState expected = OrderState::CREATED;
    if (!state_.compare_exchange_strong(expected, OrderState::OPEN)) {
        expected = OrderState::PENDING;
        if (!state_.compare_exchange_strong(expected, OrderState::OPEN)) {
            return false;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        updated_at_ = std::chrono::system_clock::now();
    }
    
    return true;
}

bool Order::fill(const Fill& fill) {
    // First check if the order is in a state that can be filled
    OrderState current_state = state_.load();
    if (current_state != OrderState::OPEN && 
        current_state != OrderState::PARTIALLY_FILLED) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Add the fill to our list
    fills_.push_back(fill);
    
    // Update the filled quantity
    double previous_filled = filled_quantity_;
    filled_quantity_ += fill.quantity;
    
    // Calculate the new average fill price
    if (filled_quantity_ > 0) {
        average_fill_price_ = ((previous_filled * average_fill_price_) + 
                             (fill.quantity * fill.price)) / filled_quantity_;
    }
    
    // Update the order state
    if (filled_quantity_ >= quantity_) {
        // Order is fully filled
        state_ = OrderState::FILLED;
    } else {
        // Order is partially filled
        state_ = OrderState::PARTIALLY_FILLED;
    }
    
    updated_at_ = std::chrono::system_clock::now();
    return true;
}

bool Order::cancel() {
    // Check if the order can be canceled
    OrderState current_state = state_.load();
    
    if (current_state == OrderState::OPEN || 
        current_state == OrderState::PARTIALLY_FILLED) {
        
        OrderState expected = current_state;
        if (state_.compare_exchange_strong(expected, OrderState::PENDING_CANCEL)) {
            std::lock_guard<std::mutex> lock(mutex_);
            updated_at_ = std::chrono::system_clock::now();
            return true;
        }
    }
    
    // If we're already in PENDING_CANCEL, just return true
    if (current_state == OrderState::PENDING_CANCEL) {
        return true;
    }
    
    // If we're in CANCELED, FILLED, REJECTED or EXPIRED state, we can't cancel
    if (current_state == OrderState::CANCELED ||
        current_state == OrderState::FILLED ||
        current_state == OrderState::REJECTED ||
        current_state == OrderState::EXPIRED) {
        return false;
    }
    
    // If we're in CREATED or PENDING state, we can directly move to CANCELED
    OrderState expected = current_state;
    if (state_.compare_exchange_strong(expected, OrderState::CANCELED)) {
        std::lock_guard<std::mutex> lock(mutex_);
        updated_at_ = std::chrono::system_clock::now();
        return true;
    }
    
    return false;
}

bool Order::reject(const std::string& reason) {
    OrderState current_state = state_.load();
    
    // Can't reject if already FILLED, CANCELED, REJECTED or EXPIRED
    if (current_state == OrderState::FILLED ||
        current_state == OrderState::CANCELED ||
        current_state == OrderState::REJECTED ||
        current_state == OrderState::EXPIRED) {
        return false;
    }
    
    OrderState expected = current_state;
    if (state_.compare_exchange_strong(expected, OrderState::REJECTED)) {
        std::lock_guard<std::mutex> lock(mutex_);
        rejection_reason_ = reason;
        updated_at_ = std::chrono::system_clock::now();
        return true;
    }
    
    return false;
}

bool Order::expire() {
    OrderState current_state = state_.load();
    
    // Can't expire if not OPEN or PARTIALLY_FILLED
    if (current_state != OrderState::OPEN &&
        current_state != OrderState::PARTIALLY_FILLED) {
        return false;
    }
    
    OrderState expected = current_state;
    if (state_.compare_exchange_strong(expected, OrderState::EXPIRED)) {
        std::lock_guard<std::mutex> lock(mutex_);
        updated_at_ = std::chrono::system_clock::now();
        return true;
    }
    
    return false;
}

// Utility methods
bool Order::isActive() const {
    OrderState current_state = state_.load();
    return current_state == OrderState::OPEN ||
           current_state == OrderState::PARTIALLY_FILLED;
}

bool Order::canModify() const {
    OrderState current_state = state_.load();
    return current_state == OrderState::CREATED ||
           current_state == OrderState::OPEN ||
           current_state == OrderState::PARTIALLY_FILLED;
}

bool Order::canCancel() const {
    OrderState current_state = state_.load();
    return current_state == OrderState::OPEN ||
           current_state == OrderState::PARTIALLY_FILLED;
}

std::string Order::toString() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::ostringstream oss;
    oss << "Order[id=" << order_id_ 
        << ", clientId=" << client_order_id_
        << ", exchange=" << exchangeId_.toString()
        << ", exchangeOrderId=" << exchange_order_id_
        << ", pair=" << trading_pair_
        << ", side=" << (side_ == OrderSide::BUY ? "BUY" : "SELL")
        << ", type=";
        
    switch (type_) {
        case OrderType::MARKET: oss << "MARKET"; break;
        case OrderType::LIMIT: oss << "LIMIT"; break;
        case OrderType::STOP: oss << "STOP"; break;
        case OrderType::STOP_LIMIT: oss << "STOP_LIMIT"; break;
        case OrderType::ICEBERG: oss << "ICEBERG"; break;
        case OrderType::TRAILING_STOP: oss << "TRAILING_STOP"; break;
        case OrderType::FILL_OR_KILL: oss << "FILL_OR_KILL"; break;
        case OrderType::IMMEDIATE_OR_CANCEL: oss << "IMMEDIATE_OR_CANCEL"; break;
        case OrderType::POST_ONLY: oss << "POST_ONLY"; break;
    }
        
    oss << ", qty=" << quantity_
        << ", filled=" << filled_quantity_;
        
    if (type_ != OrderType::MARKET) {
        oss << ", price=" << price_;
    }
    
    if (stop_price_.has_value()) {
        oss << ", stopPrice=" << stop_price_.value();
    }
    
    oss << ", state=";
    switch (state_.load()) {
        case OrderState::CREATED: oss << "CREATED"; break;
        case OrderState::PENDING: oss << "PENDING"; break;
        case OrderState::PENDING_CANCEL: oss << "PENDING_CANCEL"; break;
        case OrderState::OPEN: oss << "OPEN"; break;
        case OrderState::PARTIALLY_FILLED: oss << "PARTIALLY_FILLED"; break;
        case OrderState::FILLED: oss << "FILLED"; break;
        case OrderState::CANCELED: oss << "CANCELED"; break;
        case OrderState::REJECTED: oss << "REJECTED"; break;
        case OrderState::EXPIRED: oss << "EXPIRED"; break;
    }
    
    oss << "]";
    return oss.str();
}

} // namespace execution
} // namespace trading
} // namespace models
