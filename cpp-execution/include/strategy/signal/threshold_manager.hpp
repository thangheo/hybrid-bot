/**
 * @file threshold_manager.hpp
 * @brief Dynamically adjusts detection thresholds based on market conditions
 * 
 * @details
 * This component manages and dynamically adjusts thresholds used for
 * imbalance detection based on market conditions, volatility, and time patterns.
 * 
 * Created on: May 6, 2025
 */
#pragma once

#include "strategy/types.hpp"
#include "core/types.hpp"
#include <map>
#include <vector>
#include <memory>
#include <mutex>

namespace trading {
namespace strategy {

/**
 * @class ThresholdManager
 * @brief Manages dynamic thresholds for imbalance detection
 */
class ThresholdManager {
public:
    /**
     * @brief Constructor
     */
    ThresholdManager();
    ThresholdManager(const ThresholdManagerConfig& config);
    /**
     * @brief Destructor
     */
    virtual ~ThresholdManager() = default;
    
    /**
     * @brief Update market conditions used for threshold adjustments
     * @param state Current market state
     */
    virtual void updateMarketConditions(const core::MarketState& state);
    
    /**
     * @brief Get the current set of thresholds
     * @return Threshold set for current market conditions
     */
    virtual ThresholdSet getCurrentThresholds();
    
    /**
     * @brief Set the base thresholds
     * @param base_thresholds Base threshold values
     */
    virtual void setBaseThresholds(const ThresholdSet& base_thresholds);
    
    /**
     * @brief Get thresholds for a specific exchange
     * @param exchangeId Exchange identifier
     * @return Exchange-specific thresholds
     */
    ThresholdSet getExchangeThresholds(const ExchangeId& exchangeId);
    
    /**
     * @brief Set exchange-specific thresholds
     * @param exchangeId Exchange identifier
     * @param thresholds Threshold set for this exchange
     */
    void setExchangeThresholds(const ExchangeId& exchangeId, const ThresholdSet& thresholds);
    
    /**
     * @brief Set volatility adjustment factors
     * @param factors Mapping of volatility levels to adjustment factors
     */
    void setVolatilityFactors(const std::map<VolatilityLevel, double>& factors);
    
    /**
     * @brief Set time-of-day adjustment factors
     * @param factors Mapping of hour of day to adjustment factors
     */
    void setTimeOfDayFactors(const std::map<int, double>& factors);
    
    /**
     * @brief Update thresholds based on signal performance
     * @param performance Signal performance metrics
     */
    void adjustFromPerformance(const SignalPerformance& performance);
    
    /**
     * @brief Lock thresholds to prevent automatic adjustments
     * @param lock True to lock thresholds, false to allow adjustments
     */
    void lockThresholds(bool lock);
    
    /**
     * @brief Check if thresholds are currently locked
     * @return True if thresholds are locked
     */
    bool areThresholdsLocked() const;
    
    /**
     * @brief Save the current thresholds to persistent storage
     * @param identifier Optional identifier for the saved threshold set
     * @return True if save was successful
     */
    bool saveThresholds(const std::string& identifier = "");
    
    /**
     * @brief Load thresholds from persistent storage
     * @param identifier Identifier of the threshold set to load
     * @return True if load was successful
     */
    bool loadThresholds(const std::string& identifier = "");
    // getGlobalSensitivity
    double getGlobalSensitivity() const;
    // updateParameters
    /**
     * @brief Update parameters for the threshold manager
     * @param params New parameters
     */
    void updateParameters(const ParameterSet& params);
private:
    ThresholdSet base_thresholds_;
    std::map<ExchangeId, ThresholdSet> exchange_thresholds_;
    core::MarketState current_market_state_;
    std::map<VolatilityLevel, double> volatility_factors_;
    std::map<int, double> time_of_day_factors_;
    std::map<std::string, double> performance_adjustment_factors_;
    bool thresholds_locked_ = false;
    mutable std::mutex mutex_;
    
    /**
     * @brief Apply volatility adjustments to thresholds
     * @param thresholds Base thresholds to adjust
     * @return Volatility-adjusted thresholds
     */
    ThresholdSet applyVolatilityAdjustment(const ThresholdSet& thresholds);
    
    /**
     * @brief Apply time-of-day adjustments to thresholds
     * @param thresholds Base thresholds to adjust
     * @return Time-adjusted thresholds
     */
    ThresholdSet applyTimeOfDayAdjustment(const ThresholdSet& thresholds);
    
    /**
     * @brief Apply performance-based adjustments to thresholds
     * @param thresholds Base thresholds to adjust
     * @return Performance-adjusted thresholds
     */
    ThresholdSet applyPerformanceAdjustment(const ThresholdSet& thresholds);
    
    /**
     * @brief Ensure threshold values stay within safe bounds
     * @param thresholds Thresholds to check
     * @return Thresholds with values clamped to safe ranges
     */
    ThresholdSet enforceSafeBounds(const ThresholdSet& thresholds);
    
    /**
     * @brief Get the current volatility level from market state
     * @return Current volatility level
     */
    VolatilityLevel getCurrentVolatilityLevel() const;
    
    /**
     * @brief Get the current time factor based on hour of day
     * @return Time adjustment factor
     */
    double getCurrentTimeFactor() const;
};

} // namespace strategy
} // namespace trading