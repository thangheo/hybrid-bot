/**
 * @file threshold_manager.cpp
 * @brief Implementation of ThresholdManager
 */
#include "strategy/pattern/threshold/threshold_manager.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <cmath>

namespace trading {
namespace strategy {
namespace threshold {

ThresholdManager::ThresholdManager() {
    // Constructor - no special initialization needed
}

DynamicThresholds ThresholdManager::calculateThresholds(int regime, double currentVolatility, 
                                                       double minProfitableOverride) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = regimeStats_.find(regime);
    if (it == regimeStats_.end()) {
        LOG_TRACE("ThresholdManager: No data for regime {}, using defaults", regime);
        return getDefaultThresholds();
    }
    
    const auto& stats = it->second;
    
    // Check for sufficient data
    if (stats.recentReturns.size() < MIN_DATA_POINTS) {
        LOG_TRACE("ThresholdManager: Insufficient data ({}) for regime {}, using defaults", 
                 stats.recentReturns.size(), regime);
        return getDefaultThresholds();
    }
    
    // Calculate percentiles
    double p75_return = stats.percentile(stats.recentReturns, P75);
    double p25_return = stats.percentile(stats.recentReturns, P25);
    double median_vol = stats.percentile(stats.recentVolatilities, 0.50);
    
    // Validate percentile results
    if (!std::isfinite(p75_return) || !std::isfinite(p25_return) || !std::isfinite(median_vol)) {
        LOG_ERROR("ThresholdManager: Invalid percentiles p75={}, p25={}, medVol={}", 
                 p75_return, p25_return, median_vol);
        return getDefaultThresholds();
    }
    
    // Clamp percentile values to reasonable ranges
    p75_return = std::clamp(p75_return, -10.0, 10.0);
    p25_return = std::clamp(p25_return, -10.0, 10.0);
    
    // Calculate volatility adjustment
    double volRef = std::max(median_vol, 1e-6);
    double volAdjust = std::clamp(currentVolatility / volRef, 0.5, 2.0);
    
    // Calculate scaled thresholds
    double scaledBuyThreshold = p75_return * (1.0 + VOL_ADJUST_MULT * (volAdjust - 1.0));
    double scaledSellThreshold = p25_return * (1.0 + VOL_ADJUST_MULT * (volAdjust - 1.0));
    
    // Build result
    DynamicThresholds result;
    result.buyThreshold = std::clamp(std::max(MIN_Z_MAG, scaledBuyThreshold), 0.1, 3.0);
    result.sellThreshold = std::clamp(std::min(-MIN_Z_MAG, scaledSellThreshold), -3.0, -0.1);
    
    // Calculate min profitable return
    std::deque<double> absvals;
    for (double r : stats.recentReturns) {
        absvals.push_back(std::abs(r));
    }
    
    if (!absvals.empty()) {
        double minAbsNorm = stats.percentile(absvals, MIN_ABS_PCT);
        double computed = std::max(0.0005, minAbsNorm * volRef) + 0.003;
        
        // Apply override if provided
        if (minProfitableOverride >= 0.0) {
            result.minProfitableReturn = std::max(computed, minProfitableOverride);
        } else {
            result.minProfitableReturn = computed;
        }
    }
    
    // Final validation
    if (!result.isValid()) {
        LOG_ERROR("ThresholdManager: Invalid output thresholds, using defaults");
        return getDefaultThresholds();
    }
    
    return result;
}

void ThresholdManager::updateStats(int regime, double return_, double volatility) {
    if (!validateInputs(return_, volatility)) {
        LOG_WARN("ThresholdManager: Invalid input return={}, vol={}", return_, volatility);
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    auto& stats = regimeStats_[regime];
    
    // Check for corrupted data
    resetCorruptedStats(stats);
    
    // Clamp volatility to reasonable range
    double volRef = std::clamp(volatility, 1e-6, 1.0);
    
    // Store normalized return (z-like): log-return divided by volatility
    double norm = return_ / volRef;
    
    // Clamp normalized return to prevent accumulation of extreme values
    norm = std::clamp(norm, -20.0, 20.0);
    
    stats.recentReturns.push_back(norm);
    stats.recentVolatilities.push_back(volRef);
    
    // Maintain history size
    while (stats.recentReturns.size() > stats.maxHistory) {
        stats.recentReturns.pop_front();
    }
    while (stats.recentVolatilities.size() > stats.maxHistory) {
        stats.recentVolatilities.pop_front();
    }
}

void ThresholdManager::resetRegimeStats(int regime) {
    std::lock_guard<std::mutex> lock(mutex_);
    regimeStats_.erase(regime);
    LOG_INFO("ThresholdManager: Reset statistics for regime {}", regime);
}

size_t ThresholdManager::getRegimeDataPoints(int regime) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = regimeStats_.find(regime);
    return (it != regimeStats_.end()) ? it->second.recentReturns.size() : 0;
}

bool ThresholdManager::hasSufficientData(int regime) const {
    return getRegimeDataPoints(regime) >= MIN_DATA_POINTS;
}

// Private helper methods
double ThresholdManager::ThresholdStats::percentile(const std::deque<double>& data, double pct) const {
    if (data.empty()) return 0.0;
    
    std::vector<double> sorted(data.begin(), data.end());
    std::sort(sorted.begin(), sorted.end());
    
    // Correct percentile calculation with interpolation
    double exactIdx = pct * (sorted.size() - 1);
    size_t lowerIdx = static_cast<size_t>(std::floor(exactIdx));
    size_t upperIdx = static_cast<size_t>(std::ceil(exactIdx));
    
    if (lowerIdx == upperIdx || upperIdx >= sorted.size()) {
        return sorted[lowerIdx];
    }
    
    // Linear interpolation between two nearest values
    double fraction = exactIdx - lowerIdx;
    return sorted[lowerIdx] * (1.0 - fraction) + sorted[upperIdx] * fraction;
}

bool ThresholdManager::validateInputs(double return_, double volatility) const {
    return std::isfinite(return_) && std::isfinite(volatility) && 
           volatility > 0.0 && std::abs(return_) < 100.0;
}

void ThresholdManager::resetCorruptedStats(ThresholdStats& stats) const {
    if (stats.recentReturns.empty()) return;
    
    double maxAbs = 0.0;
    for (double r : stats.recentReturns) {
        maxAbs = std::max(maxAbs, std::abs(r));
    }
    
    // If we have extreme values, reset the statistics
    if (maxAbs > 100.0) {
        LOG_ERROR("ThresholdManager: Extreme values detected (max={}), resetting statistics", maxAbs);
        stats.recentReturns.clear();
        stats.recentVolatilities.clear();
    }
}

DynamicThresholds ThresholdManager::getDefaultThresholds() const {
    DynamicThresholds defaults;
    defaults.buyThreshold = 0.55;
    defaults.sellThreshold = -0.35;
    defaults.minProfitableReturn = 0.003;
    return defaults;
}

} // namespace threshold
} // namespace strategy
} // namespace trading