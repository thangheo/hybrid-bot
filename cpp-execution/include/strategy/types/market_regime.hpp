/**
 * @file market_regime.hpp
 * @brief Shared definitions for market regime types and microstructure
 */
#pragma once

namespace trading {
namespace strategy {

// Publicly visible regime enum used across strategy components
enum class MarketRegimeType {
    STRESS,
    HIGH_VOLATILITY,
    LOW_LIQUIDITY,
    TRENDING,
    MEAN_REVERTING,
    QUIET,
    NORMAL,
    UNKNOWN
};

// Lightweight market microstructure descriptor (kept in strategy namespace)
struct MarketMicrostructure {
    double volatility_1m = 0.0;
    double spread_ema = 0.0;
    double spread_std = 0.0;
    double orderbook_imbalance = 0.0;
    double trade_intensity = 0.0;
    double price_impact = 0.0;
    double tick_size = 0.0;
    double volume_profile_skew = 0.0;
};

} // namespace strategy
} // namespace trading
