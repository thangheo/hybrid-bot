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

// Patch: Refactored detect with improved smoothing, input guards, and regime logic
MarketRegimeType OrderbookRegimeDetector::detect(
    const MarketMicrostructure& micro,
    const std::deque<double>& vol_samples,
    const std::deque<double>& spread_samples,
    const std::deque<double>& returns_samples)
{
    // Configurable parameters (should move to struct in production)
    constexpr size_t MIN_SAMPLES = 20;
    constexpr double VOL_THRESHOLD = 1.2;
    constexpr double SPREAD_THRESHOLD = 2.0;
    constexpr double STRESS_VOL = 1.2;
    constexpr double STRESS_SPREAD = 2.0;
    constexpr double TREND_AC = 0.25;
    constexpr double TREND_STRENGTH = 0.6;
    constexpr double IMBALANCE = 0.3;
    constexpr double MEANREV_AC = -0.25;
    constexpr double MEANREV_STRENGTH = 0.4;
    constexpr double QUIET_VOL = 0.5;
    constexpr double QUIET_ACTIVITY = 0.001;
    constexpr int SMOOTHING_TICKS = 3;

    if (vol_samples.size() < MIN_SAMPLES ||
        spread_samples.size() < MIN_SAMPLES ||
        returns_samples.size() < MIN_SAMPLES) {
        return MarketRegimeType::NORMAL;
    }

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

    // activity: mean(|returns|)
    double activity = oscillation / returns_samples.size();

    // ---------- 3. Regime logic ----------
    MarketRegimeType detected = MarketRegimeType::NORMAL;

    bool high_vol = (vol_now > vol_p90 * VOL_THRESHOLD);
    bool high_spread = (spread_now > spread_p90 * SPREAD_THRESHOLD);

    // Priority 1: STRESS (both high volatility and high spread)
    if (high_vol && high_spread) {
        detected = MarketRegimeType::STRESS;
    }
    // Priority 2: Single-factor regimes
    else if (high_vol) {
        detected = MarketRegimeType::HIGH_VOLATILITY;
    }
    else if (high_spread) {
        detected = MarketRegimeType::LOW_LIQUIDITY;
    }
    // Priority 3: Dynamic regimes
    else if (returns_ac > TREND_AC && trend_strength > TREND_STRENGTH && std::abs(micro.orderbook_imbalance) > IMBALANCE) {
        detected = MarketRegimeType::TRENDING;
    }
    else if (returns_ac < MEANREV_AC && trend_strength < MEANREV_STRENGTH) {
        detected = MarketRegimeType::MEAN_REVERTING;
    }
    else if (vol_now < vol_p75 * QUIET_VOL && activity < QUIET_ACTIVITY) {
        detected = MarketRegimeType::QUIET;
    }

    // ---------- 4. Regime smoothing (state machine) ----------
    if (detected == m_lastRegime) {
        m_stableCount = 0;
        m_candidateRegime = detected;
    } else if (detected == m_candidateRegime) {
        m_stableCount++;
    } else {
        m_candidateRegime = detected;
        m_stableCount = 1;
    }
    if (m_stableCount > SMOOTHING_TICKS) {
        m_lastRegime = m_candidateRegime;
        m_stableCount = 0;
    }
    return m_lastRegime;
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
