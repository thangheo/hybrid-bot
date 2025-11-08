/**
 * @file threshold_manager.hpp
 * @brief Dynamic threshold management for imbalance detection
 */
#pragma once

#include "strategy/types.hpp"
#include <deque>
#include <map>
#include <mutex>

namespace trading {
namespace strategy {
namespace threshold {

/**
 * @struct DynamicThresholds
 * @brief Dynamically calculated thresholds for trading decisions
 */
struct DynamicThresholds {
    double buyThreshold = 0.55;
    double sellThreshold = -0.35;
    double minProfitableReturn = 0.003; // default trading cost
    
    bool isValid() const {
        return std::isfinite(buyThreshold) && std::isfinite(sellThreshold) && 
               std::isfinite(minProfitableReturn) && buyThreshold > 0.0 && 
               sellThreshold < 0.0 && minProfitableReturn > 0.0;
    }
};

/**
 * @class ThresholdManager
 * @brief Manages dynamic threshold calculation based on market regime statistics
 * 
 * This class adapts trading thresholds based on historical return and volatility
 * patterns for different market regimes. It maintains statistical models per regime
 * and calculates appropriate thresholds dynamically.
 */
class ThresholdManager {
public:
    ThresholdManager();
    ~ThresholdManager() = default;
    
    /**
     * @brief Calculate dynamic thresholds for a specific regime
     * @param regime Market regime identifier
     * @param currentVolatility Current market volatility
     * @param minProfitableOverride Override for minimum profitable return (optional)
     * @return Dynamic thresholds for the regime
     */
    DynamicThresholds calculateThresholds(int regime, double currentVolatility, 
                                        double minProfitableOverride = -1.0) const;
    
    /**
     * @brief Update statistics with new market data
     * @param regime Market regime identifier
     * @param return_ Normalized return value
     * @param volatility Market volatility
     */
    void updateStats(int regime, double return_, double volatility);
    
    /**
     * @brief Reset statistics for a specific regime
     * @param regime Market regime to reset
     */
    void resetRegimeStats(int regime);
    
    /**
     * @brief Get number of data points for a regime
     * @param regime Market regime identifier
     * @return Number of statistical samples
     */
    size_t getRegimeDataPoints(int regime) const;
    
    /**
     * @brief Check if regime has sufficient data for reliable thresholds
     * @param regime Market regime identifier
     * @return True if sufficient data available
     */
    bool hasSufficientData(int regime) const;
    
private:
    struct ThresholdStats {
        std::deque<double> recentReturns;
        std::deque<double> recentVolatilities;
        size_t maxHistory = 1000;
        
        double percentile(const std::deque<double>& data, double pct) const;
    };
    
    std::map<int, ThresholdStats> regimeStats_;
    mutable std::mutex mutex_;
    
    // Configuration constants
    static constexpr double P75 = 0.60;
    static constexpr double P25 = 0.40;
    static constexpr double MIN_ABS_PCT = 0.30;
    static constexpr double VOL_ADJUST_MULT = 0.15;
    static constexpr double MIN_Z_MAG = 0.20;
    static constexpr size_t MIN_DATA_POINTS = 10;
    
    // Helper methods
    bool validateInputs(double return_, double volatility) const;
    void resetCorruptedStats(ThresholdStats& stats) const;
    DynamicThresholds getDefaultThresholds() const;
};

} // namespace threshold
} // namespace strategy
} // namespace trading