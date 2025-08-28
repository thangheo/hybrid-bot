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
    return signal.exchange;
}

inline std::string getSymbol(const TradingSignal& signal) noexcept {
    return signal.getTradingPair();
}

inline double getQuantity(const TradingSignal& signal) noexcept {
    return signal.getTargetSize();
}

inline std::string getStrategyId(const TradingSignal& signal) noexcept {
    return signal.getStrategyId();
}

// Helper to extract base asset from trading pair
inline std::string getBaseAsset(const TradingSignal& signal) noexcept {
    const std::string& pair = signal.getTradingPair();
    const auto delimiter = pair.find('/');
    if (delimiter != std::string::npos) {
        return pair.substr(0, delimiter);
    }
    // Fallback for pairs without '/' delimiter
    return pair.length() >= 6 ? pair.substr(0, 3) : pair;
}

// Helper to extract quote asset from trading pair
inline std::string getQuoteAsset(const TradingSignal& signal) noexcept {
    const std::string& pair = signal.getTradingPair();
    const auto delimiter = pair.find('/');
    if (delimiter != std::string::npos) {
        return pair.substr(delimiter + 1);
    }
    // Fallback for pairs without '/' delimiter
    return pair.length() >= 6 ? pair.substr(3) : "";
}

} // namespace trading::risk