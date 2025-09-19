#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <strategy/types.hpp>
#include <execution/models/order.hpp>
namespace trading {
namespace common {

using Timestamp = std::chrono::time_point<std::chrono::system_clock>;
using TradingSignal = trading::strategy::TradingSignal;
using OrderType = trading::execution::models::OrderType;
using OrderSide = trading::execution::models::OrderSide;
// enum class OrderSide {
//     BUY,
//     SELL
// };
 
// using OrderBook = std::unordered_map<double, PriceLevel>;

struct ImbalanceMetrics {
    double bid_ask_ratio;
    double volume_delta;
    double price_pressure;
    double acceleration;
    Timestamp timestamp;
};


struct Order {
    std::string instrument;
    std::string exchange;
    std::string id;
    OrderSide side;
    OrderType type;
    double quantity;
    double price;
    Timestamp timestamp;
};

} // namespace common
}