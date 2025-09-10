/**
 * @file trading_signal_extensions.hpp
 * @brief Extension functions for TradingSignal compatibility
 * 
 * Provides extension functions to bridge different TradingSignal definitions
 * across the system components.
 * 
 * @author Trading System Team
 * @date May 28, 2025
 */

#pragma once

#include "execution/trading_signal.hpp"

namespace trading::risk {

// Use the execution TradingSignal as the primary type
using TradingSignal = execution::TradingSignal;

// Extension functions for easier access to signal properties
inline core::ExchangeId getExchangeId(const TradingSignal& signal) noexcept {
    return signal.exchangeId;
}

inline core::Symbol getSymbol(const TradingSignal& signal) noexcept {
    return signal.getTradingPair();
}

inline double getQuantity(const TradingSignal& signal) noexcept {
    return signal.getTargetSize();
}

inline StrategyId getStrategyId(const TradingSignal& signal) noexcept {
    return signal.getStrategyId();
}

// Helper to extract base asset from trading pair
inline std::string getBaseAsset(const TradingSignal& signal) noexcept {
    return signal.getTradingPair().base;
}

// Helper to extract quote asset from trading pair
inline std::string getQuoteAsset(const TradingSignal& signal) noexcept {
    return signal.getTradingPair().quote;
}

} // namespace trading::risk