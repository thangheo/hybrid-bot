#pragma once

#include <string>
#include <chrono>

namespace trading {
namespace execution {
namespace algorithms {

/**
 * Configuration parameters for TWAP algorithm
 */
struct TwapConfig {
    double total_quantity;               // Total quantity to execute
    std::chrono::seconds duration;       // Duration of execution
    std::chrono::seconds slice_interval; // Time between order slices
    double price_limit = 0.0;            // Optional price limit, 0 for market orders
    double deviation_pct = 0.0;          // Max allowed deviation from target price
    bool allow_partial = true;           // Whether to allow partial fills
};

} // namespace algorithms
} // namespace execution
} // namespace trading