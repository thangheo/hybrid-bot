#include "strategy/core_analysis/orderbook_regime_detector.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

using namespace trading::strategy::core_analysis;

namespace trading {
namespace strategy {
namespace core_analysis {

double OrderbookRegimeDetector::calculatePercentile(const std::deque<double>& data, double percentile) {
    if (data.empty()) return 0.0;
    std::vector<double> sorted(data.begin(), data.end());
    std::sort(sorted.begin(), sorted.end());
    size_t index = static_cast<size_t>(percentile * (sorted.size() - 1));
    return sorted[index];
}
// calculateMedian
double OrderbookRegimeDetector::calculateMedian(const std::deque<double>& data) {
    if (data.empty()) return 0.0;
    std::vector<double> sorted(data.begin(), data.end());
    std::sort(sorted.begin(), sorted.end());
    size_t mid = sorted.size() / 2;
    return sorted.size() % 2 == 0 ? (sorted[mid - 1] + sorted[mid]) / 2.0 : sorted[mid];
}

double OrderbookRegimeDetector::calculateStandardDeviation(const std::deque<double>& data) {
    if (data.size() < 2) return 0.0;
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    double sq_sum = 0.0;
    for (double v : data) sq_sum += (v - mean) * (v - mean);
    return std::sqrt(sq_sum / (data.size() - 1));
}

double OrderbookRegimeDetector::calculateAutoCorrelation(const std::deque<double>& data, int lag) {
    if (data.size() < static_cast<size_t>(lag) + 2) return 0.0;
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    double num = 0.0, den = 0.0;
    for (size_t i = lag; i < data.size(); ++i) {
        num += (data[i] - mean) * (data[i - lag] - mean);
    }
    for (double v : data) den += (v - mean) * (v - mean);
    if (den == 0.0) return 0.0;
    return num / den;
}

MarketRegimeType OrderbookRegimeDetector::detect(
    const MarketMicrostructure& micro,
    const std::deque<double>& vol_samples,
    const std::deque<double>& spread_samples,
    const std::deque<double>& returns_samples) const
{
    if (vol_samples.size() < 20) return MarketRegimeType::NORMAL;

    // ---------- 1. Statistical baselines ----------
    double vol_p75 = calculatePercentile(vol_samples, 0.75);
    double vol_p90 = calculatePercentile(vol_samples, 0.90);
    double spread_p75 = calculatePercentile(spread_samples, 0.75);
    double spread_p90 = calculatePercentile(spread_samples, 0.90);
    double vol_now = micro.volatility_1m;
    double spread_now = micro.spread_ema;

    // ---------- 2. Derived dynamics ----------
    double returns_ac = calculateAutoCorrelation(returns_samples, 1);
    double mean_ret = calculateMean(returns_samples);
    double std_ret = calculateStandardDeviation(returns_samples);

    // momentum & oscillation
    double cumulative_move = std::accumulate(returns_samples.begin(), returns_samples.end(), 0.0);
    double oscillation = 0.0;
    for (double r : returns_samples) oscillation += std::abs(r);
    double trend_strength = std::abs(cumulative_move) / (oscillation + 1e-9);

    // stress indicator: volatility / average volatility + spread ratio
    double stress = (vol_now / (vol_p90 + 1e-9)) + (spread_now / (spread_p90 + 1e-9));
    stress *= 0.5;

    // activity: mean(|returns|)
    double activity = oscillation / returns_samples.size();

    // ---------- 3. Regime logic ----------
    MarketRegimeType detected = MarketRegimeType::NORMAL;

    // Highest priority: stress / dislocation
    if (stress > 1.2) {
        detected = MarketRegimeType::STRESS;
    }
    // High volatility / instability
    else if (vol_now > vol_p90 * 1.2) {
        detected = MarketRegimeType::HIGH_VOLATILITY;
    }
    // Thin book
    else if (spread_now > spread_p90 * 2.0) {
        detected = MarketRegimeType::LOW_LIQUIDITY;
    }
    // Trending regime (directional persistence + orderbook imbalance)
    else if (returns_ac > 0.25 && trend_strength > 0.6 && std::abs(micro.orderbook_imbalance) > 0.3) {
        detected = MarketRegimeType::TRENDING;
    }
    // Mean reversion regime
    else if (returns_ac < -0.25 && trend_strength < 0.4) {
        detected = MarketRegimeType::MEAN_REVERTING;
    }
    // Quiet
    else if (vol_now < vol_p75 * 0.5 && activity < 0.001) {
        detected = MarketRegimeType::QUIET;
    }

    // ---------- 4. Regime smoothing ----------
    static MarketRegimeType last = MarketRegimeType::NORMAL;
    static int stable_count = 0;

    if (detected != last) {
        if (++stable_count > 3) { // require consistency over 3 ticks
            last = detected;
            stable_count = 0;
        }
    } else {
        stable_count = 0;
    }

    return last;
}

double OrderbookRegimeDetector::calculateMean(const std::deque<double>& data) {
    if (data.empty()) return 0.0;
    double sum = std::accumulate(data.begin(), data.end(), 0.0);
    return sum / data.size();
}
// // static double calculateMedian(const std::deque<double>& data);
// double OrderbookRegimeDetector::calculateMedian(const std::deque<double>& data) {
//     if (data.empty()) return 0.0;
//     std::vector<double> sorted(data.begin(), data.end());
//     std::sort(sorted.begin(), sorted.end());
//     size_t mid = sorted.size() / 2;
//     return sorted.size() % 2 == 0 ? (sorted[mid - 1] + sorted[mid]) / 2.0 : sorted[mid];
// }

// // function to get current regime as string
std::string regimeToString(MarketRegimeType regime) {
    switch (regime) {
        case MarketRegimeType::TRENDING: return "TRENDING";
        case MarketRegimeType::MEAN_REVERTING: return "MEAN_REVERTING";
        case MarketRegimeType::HIGH_VOLATILITY: return "HIGH_VOLATILITY";
        case MarketRegimeType::LOW_LIQUIDITY: return "LOW_LIQUIDITY";
        case MarketRegimeType::NORMAL: return "NORMAL";
        case MarketRegimeType::QUIET: return "QUIET";
        case MarketRegimeType::STRESS: return "STRESS";
        default: return "UNKNOWN";
    }
}

} // namespace core_analysis
} // namespace strategy
} // namespace trading
