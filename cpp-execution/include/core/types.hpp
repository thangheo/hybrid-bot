/**
 * @file core/types.hpp
 * @brief Common typedefs for the strategy component
 * 
 * Basic type definitions and aliases used throughout the strategy component.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include <string>
 #include <chrono>
 #include <functional>
//  #include <core/confidence.hpp>
// #include <core/exchange_types.hpp>
//     #include <core/market_types.hpp>
//     #include <core/market_state.hpp>
//     #include <core/price_level.hpp>
//     #include <core
 namespace trading {
 namespace core {
 
 /**
  * Common identifier types.
  */
 using StrategyId = std::string;
 using SignalId = std::string;
 using ExperimentId = std::string;
//  using Symbol = std::string;
// Forward declaration of Symbol struct defined in market_types.hpp
struct Symbol;
 
 /**
  * Time-related typedefs
  */
 using Timestamp = std::chrono::system_clock::time_point;
 using Duration = std::chrono::system_clock::duration;
 
 
 } // namespace core
 } // namespace trading