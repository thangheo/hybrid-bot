/**
 * @file trading_signal.hpp
 * @brief Execution trading signal type
 * 
 * This file defines the TradingSignal structure used in the execution
 * component of the trading system.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

#pragma once

#include "core/time.hpp"
#include "core/direction.hpp"
#include "core/market_types.hpp"
#include "core/trading_signal.hpp"
#include <string>
#include <map>
#include <chrono>

using namespace trading::core;

namespace trading {
namespace execution {
using TradingSignal = trading::core::TradingSignal;

//  struct TradingSignal;
}
 } // namespace trading