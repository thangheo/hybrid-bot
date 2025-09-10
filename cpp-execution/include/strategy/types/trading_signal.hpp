/**
 * @file trading_signal.hpp
 * @brief Strategy trading signal type
 * 
 * This file defines the TradingSignal structure used in the strategy
 * component of the trading system.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

//  #include "core/time.hpp"
//  #include "core/direction.hpp"
//  #include "core/market_types.hpp"
//  #include "core/confidence.hpp"
 #include "core/trading_signal.hpp"
//  #include "strategy/types/imbalance_type.hpp"
//  #include <execution/trading_signal.hpp>
 #include <string>
 #include <vector>
 #include <map>
 #include <cmath>
 
 namespace trading {
 namespace strategy {
 using TradingSignal = trading::core::TradingSignal;

} // namespace strategy
} // namespace trading