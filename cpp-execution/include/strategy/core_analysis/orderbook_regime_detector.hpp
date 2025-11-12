#pragma once

// #include "strategy/exit/adaptive_exit_strategy.hpp"
#include <deque>
#include <vector>
#include <string>

// Tín hiệu đầu ra gợi ý sử dụng
// Regime	Exit strategy suggestion
// STRESS	Force exit / disable trading
// HIGH_VOLATILITY	Tight stop or reduce exposure
// LOW_LIQUIDITY	Avoid entering, slippage risk
// TRENDING	Allow longer holding, momentum-based exit
// MEAN_REVERTING	Use short-term countertrend exit
// QUIET	Skip trades or widen thresholds
// NORMAL	Default operation
// more sophisticated regime types
enum class MarketRegimeType {
    STRESS,         // Normal market conditions
    HIGH_VOLATILITY,       // High volatility regime
    LOW_LIQUIDITY,       // Strong trending market
    TRENDING,    // Sideways/consolidating market
    MEAN_REVERTING,         // Market stress conditions
    QUIET,          // Low activity/volatility
    NORMAL,
    UNKNOWN         // Fallback/unknown regime
};

struct MarketMicrostructure {
        double volatility_1m = 0.0;      // 1-minute realized volatility
        double spread_ema = 0.0;          // EMA of bid-ask spread
        double spread_std = 0.0;          // Spread standard deviation
        double orderbook_imbalance = 0.0; // Bid/ask volume imbalance
        double trade_intensity = 0.0;     // Trades per second
        double price_impact = 0.0;        // Estimated price impact
        double tick_size = 0.0;           // Minimum price increment
        double volume_profile_skew = 0.0; // Volume distribution asymmetry
};
    

namespace trading {
namespace strategy {
namespace core_analysis {

class OrderbookRegimeDetector {
public:
    OrderbookRegimeDetector() = default;

    // Detect market regime using orderbook-derived microstructure and simple history windows
    MarketRegimeType detect(
        const MarketMicrostructure& micro,
        const std::deque<double>& volatility_samples,
        const std::deque<double>& spread_samples,
        const std::deque<double>& price_returns) const;
private:
    // Statistical helpers (public for testing)
    static double calculatePercentile(const std::deque<double>& data, double percentile);
    static double calculateStandardDeviation(const std::deque<double>& data);
    static double calculateAutoCorrelation(const std::deque<double>& data, int lag);
    // helpers functions
    static double calculateMean(const std::deque<double>& data);
    static double calculateMedian(const std::deque<double>& data);
    // function to get current regime as string
    static std::string regimeToString(MarketRegimeType regime);

};

} // namespace core_analysis
} // namespace strategy
} // namespace trading
