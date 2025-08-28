
//execution_types.hpp
#pragma once
#include "execution/models/order.hpp"
#include "execution/trading_signal.hpp"
#include <string>
#include <chrono>
#include <map>
namespace trading {
namespace execution {
 
 // Struct definitions for service interfaces
//  struct TradingSignal {
//     std::string signal_id;
//     std::string tradingPair;
//     models::OrderSide direction;
//     double strength;
//     double target_size;
//     std::chrono::milliseconds signal_ttl;
//     std::string strategy_id;
//     std::string strategy_version;
//     std::map<std::string, double> signal_metrics;
// };
// class ExecutionService;  // Still need this
 

struct OrderStatusResponse {
   // Instead of storing the Order by value, store it as a shared_ptr
   std::shared_ptr<models::Order> order;
   bool found;
   std::string error_message;
};

struct CancelResponse {
    std::string client_order_id;
    std::string exchange_order_id;
    bool success;
    std::string error_message;
    std::chrono::system_clock::time_point timestamp;
};

// ExecutionResponse
struct ExecutionResponse {
    std::string client_order_id;
    std::string exchange_order_id;
    bool success;
    std::string error_message;
    std::chrono::system_clock::time_point timestamp;
    std::string order_status;
};
} // namespace execution
} // namespace trading
