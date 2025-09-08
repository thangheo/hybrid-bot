#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <core/market_types.hpp>
#include <core/direction.hpp>

namespace trading {
namespace execution {
namespace models {
/**
 * Represents the side of an order (buy or sell)
 */
// enum class OrderSide {
//     BUY,  // Buy order
//     SELL  // Sell order
// };
using OrderSide = core::Direction;

/**
 * Represents different order types supported by exchanges
 */
enum class OrderType {
    MARKET,           // Market order
    LIMIT,            // Limit order
    STOP,             // Stop order
    STOP_LIMIT,       // Stop limit order
    ICEBERG,          // Iceberg/hidden order
    TRAILING_STOP,    // Trailing stop order
    TAKE_PROFIT,      // Take profit order
    TAKE_PROFIT_LIMIT,// Take profit limit order
    FILL_OR_KILL,     // Fill or kill order
    IMMEDIATE_OR_CANCEL, // IOC order
    POST_ONLY         // Post only order
};

/**
 * Defines how long an order should remain active
 */
enum class TimeInForce {
    GTC,   // Good Till Canceled
    IOC,   // Immediate Or Cancel
    FOK,   // Fill Or Kill
    GTD    // Good Till Date
};

/**
 * Represents the current state of an order
 */
enum class OrderState {
    CREATED,          // Order created locally
    PENDING,          // Sent to exchange but not yet confirmed
    PENDING_CANCEL,   // Cancel request sent but not confirmed
    OPEN,             // Active on the exchange
    PARTIALLY_FILLED, // Partially executed
    FILLED,           // Fully executed
    CANCELED,         // Canceled by user or system
    REJECTED,         // Rejected by exchange
    EXPIRED           // Expired according to time constraints
};

/**
 * Represents a fill (execution) of an order
 */

struct Fill {
    std::string fill_id;
    std::string orderId;
    std::string exchangeOrderId;
    core::ExchangeId exchangeId;                 // Which exchange (e.g., ExchangeId::binance())
    double quantity;
    double price;
    double fee;
    std::string fee_currency;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * Main Order class representing a trading order
 */
class Order {
public:
    // Constructors
    Order();
    explicit Order(const std::string& client_order_id);
    Order(const Order& other);
    
    // Factory methods for common order types
    static std::shared_ptr<Order> createMarketOrder(
        const std::string& tradingPair, 
        OrderSide side, 
        double quantity);
        
    static std::shared_ptr<Order> createLimitOrder(
        const std::string& tradingPair, 
        OrderSide side, 
        double quantity, 
        double price);

    // Getters
    const std::string& getOrderId() const;
    const std::string& getClientOrderId() const;
    const core::ExchangeId& getExchangeId() const;
    const std::string& getExchangeOrderId() const;
    const std::string& getTradingPair() const;
    const std::string& getSymbol() const;
    OrderSide getSide() const;
    OrderType getType() const;
    double getQuantity() const;
    double getPrice() const;
    std::string getExtraParam(const std::string& key) const;
    std::optional<double> getStopPrice() const;
    TimeInForce getTimeInForce() const;
    OrderState getState() const;    
    const std::string& getStrategyId() const;
    const std::string& getSignalId() const;
    std::chrono::system_clock::time_point getCreatedTime() const;
    std::chrono::system_clock::time_point getUpdatedTime() const;
    std::optional<std::chrono::system_clock::time_point> getExpiryTime() const;
    const std::vector<Fill>& getFills() const;
    double getFilledQuantity() const;
    double getRemainingQuantity() const;
    double getAverageFillPrice() const;
    const std::unordered_map<std::string, std::string>& getExtraParams() const;
    
    // Setters
    void setOrderId(const std::string& order_id);
    void setExchangeId(const core::ExchangeId& exchangeId);
    void setExchangeOrderId(const std::string& exchange_order_id);
    void setClientOrderId(const std::string& client_order_id);
    void setTradingPair(const std::string& tradingPair);
    void setSide(OrderSide side);
    void setType(OrderType type);
    void setQuantity(double quantity);
    void setState(OrderState state);
    void setPrice(double price);
    void setStopPrice(std::optional<double> stop_price);
    void setTimeInForce(TimeInForce time_in_force);
    void setExpiryTime(std::optional<std::chrono::system_clock::time_point> expiry_time);
    void setStrategyId(const std::string& strategy_id);
    void setSignalId(const std::string& signal_id);
    void setExtraParam(const std::string& key, const std::string& value);
    
    // State transitions
    bool open();
    bool fill(const Fill& fill);
    bool cancel();
    bool reject(const std::string& reason);
    bool expire();
    
    // Utility methods
    bool isActive() const;
    bool canModify() const;
    bool canCancel() const;
    std::string toString() const;
    
private:
    // Order identification
    std::string order_id_;
    std::string client_order_id_;
    core::ExchangeId exchangeId_;
    std::string exchange_order_id_;
    
    // Order details
    std::string trading_pair_;
    OrderSide side_ = OrderSide::BUY;
    OrderType type_ = OrderType::MARKET;
    double quantity_ = 0.0;
    double price_ = 0.0;
    std::optional<double> stop_price_;
    TimeInForce time_in_force_ = TimeInForce::GTC;
    std::optional<std::chrono::system_clock::time_point> expiry_time_;
    
    // Order state
    std::atomic<OrderState> state_{OrderState::CREATED};
    std::string rejection_reason_;
    std::chrono::system_clock::time_point created_at_;
    std::chrono::system_clock::time_point updated_at_;
    
    // Strategy information
    std::string strategy_id_;
    std::string signal_id_;
    
    // Execution information
    std::vector<Fill> fills_;
    double filled_quantity_ = 0.0;
    double average_fill_price_ = 0.0;
    
    // Additional exchange-specific parameters
    std::unordered_map<std::string, std::string> extra_params_;
    
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Allow execution report to access private members
    friend class ExecutionReport;
};

// Type aliases for convenience
using OrderPtr = std::shared_ptr<Order>;
using OrderConstPtr = std::shared_ptr<const Order>;
using OrderWeakPtr = std::weak_ptr<Order>;

} // namespace models
} // namespace execution
} // namespace trading