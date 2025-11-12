#pragma once

// #include "strategy/exit/adaptive_exit_strategy.hpp"
#include <deque>
#include <vector>
#include <string>
#include "strategy/types/market_regime.hpp"

// Forward declaration to avoid heavy include and break circular includes
namespace trading { namespace strategy { struct RegimeDetectorConfig; } }

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
    

namespace trading {
namespace strategy {
namespace core_analysis {
// static std::string regimeToString(MarketRegimeType regime);

class OrderbookRegimeDetector {
public:
    OrderbookRegimeDetector() = default;
    // OrderbookRegimeDetector(const RegimeDetectorConfig& config) : config_(config) {}
    ~OrderbookRegimeDetector() = default;

    // Detect market regime using orderbook-derived microstructure and simple history windows
    MarketRegimeType detect(
        const MarketMicrostructure& micro,
        const std::deque<double>& volatility_samples,
        const std::deque<double>& spread_samples,
        const std::deque<double>& price_returns);
    // function to get current regime as string

    // get current regime as enum
    static MarketRegimeType getCurrentRegimeEnum(const std::string& regimeStr) {
        if (regimeStr == "STRESS") return MarketRegimeType::STRESS;
        if (regimeStr == "HIGH_VOLATILITY") return MarketRegimeType::HIGH_VOLATILITY;
        if (regimeStr == "LOW_LIQUIDITY") return MarketRegimeType::LOW_LIQUIDITY;
        if (regimeStr == "TRENDING") return MarketRegimeType::TRENDING;
        if (regimeStr == "MEAN_REVERTING") return MarketRegimeType::MEAN_REVERTING;
        if (regimeStr == "QUIET") return MarketRegimeType::QUIET;
        if (regimeStr == "NORMAL") return MarketRegimeType::NORMAL;
        return MarketRegimeType::UNKNOWN;
    }
private:
    // RegimeDetectorConfig config_;
    // Smoothing state (mutable)
    MarketRegimeType m_lastRegime = MarketRegimeType::NORMAL;
    MarketRegimeType m_candidateRegime = MarketRegimeType::NORMAL;
    int m_stableCount = 0;
    // Statistical helpers (public for testing)
    static double calculatePercentile(const std::deque<double>& data, double percentile);
    static double calculateStandardDeviation(const std::deque<double>& data);
    static double calculateAutoCorrelation(const std::deque<double>& data, int lag);
    // helpers functions
    static double calculateMean(const std::deque<double>& data);
    static double calculateMedian(const std::deque<double>& data);

};

} // namespace core_analysis
} // namespace strategy
} // namespace trading
