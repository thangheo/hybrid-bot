#pragma once

#include <chrono>
#include <string>
#include <core/market_types.hpp>
namespace trading {
namespace execution {
namespace algorithms {

using namespace core;
/**
 * Configuration parameters for POV (Percentage of Volume) execution algorithm
 */
struct PovConfig {
    // Required parameters
    double total_quantity = 0.0;         // Total quantity to execute
    double participation_rate = 0.05;    // Target participation rate (0.0-1.0)
    double price_limit = 0.0;            // Maximum price for buy or minimum price for sell
    
    // Optional parameters
    std::chrono::seconds duration = std::chrono::hours(1);  // Maximum execution duration
    std::chrono::seconds check_interval = std::chrono::seconds(15);  // Interval to check market volume
    std::chrono::minutes volume_lookback = std::chrono::minutes(5);  // Lookback window for volume calculation
    
    double min_order_size = 0.0;         // Minimum order size (if > 0)
    double max_order_size = 0.0;         // Maximum order size (if > 0)
    bool dynamic_rate = false;           // Whether to adjust participation rate dynamically
    
    double urgency_factor = 1.0;         // Factor to adjust participation rate (higher = more urgent)
    double deviation_pct = 0.0;          // Allowed deviation from participation rate
    
    ExchangeId exchangeId;             // Optional exchange ID
};

} // namespace algorithms
} // namespace execution
} // namespace trading