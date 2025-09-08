#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <strategy/types.hpp>
namespace trading {
namespace common {

using Timestamp = std::chrono::time_point<std::chrono::system_clock>;
using TradingSignal = trading::strategy::TradingSignal;
enum class OrderSide {
    BUY,
    SELL
};

enum class OrderType {
    MARKET,
    LIMIT
};
 
// using OrderBook = std::unordered_map<double, PriceLevel>;

struct ImbalanceMetrics {
    double bid_ask_ratio;
    double volume_delta;
    double price_pressure;
    double acceleration;
    Timestamp timestamp;
};

// struct TradingSignal {
//     std::string instrument;
//     OrderSide side;
//     double confidence;
//     double target_price;
//     double stop_price;
//     Timestamp expiry;
//     Timestamp timestamp;
// };

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