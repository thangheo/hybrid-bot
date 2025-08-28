/**
 * @file correlation_types.hpp
 * @brief Type definitions for correlation-related components
 * 
 * This file defines types used by the MomentumCorrelation class
 * and related components.
 * 
 * @author Trading System Team
 * @date May 13, 2025
 */

 #pragma once

 #include <string>
 #include <map>
 #include <chrono>
 #include "core/types.hpp"
 #include "strategy/types/imbalance_type.hpp"
 namespace trading {
 namespace strategy {
 
 /**
  * Enum for different market regimes identified by correlation analysis
  */
 enum class MarketRegime {
     NORMAL,           // Normal market conditions
     TRENDING,         // Strong directional movement
     RANGE_BOUND,      // Price oscillating within a range
     HIGH_VOLATILITY,  // Elevated volatility
     QUIET             // Low volatility and activity
 };
 
 /**
  * Convert MarketRegime to string representation
  */
 inline std::string marketRegimeToString(MarketRegime regime) {
     switch (regime) {
         case MarketRegime::NORMAL: return "normal";
         case MarketRegime::TRENDING: return "trending";
         case MarketRegime::RANGE_BOUND: return "range_bound";
         case MarketRegime::HIGH_VOLATILITY: return "high_volatility";
         case MarketRegime::QUIET: return "quiet";
         default: return "unknown";
     }
 }
 
 /**
  * Parameters for correlation analysis
  */
 struct CorrelationParameters {
     size_t min_data_points;          // Minimum data points required for correlation
     double significance_threshold;    // Statistical significance threshold (p-value)
     double correlation_decay_rate;    // Decay rate for smoothing correlation values
     size_t regime_detection_window;   // Window size for market regime detection
     double time_decay_factor;         // Factor for time decay in correlation calculation
     double min_stddev_threshold;      // Minimum standard deviation threshold for correlation
     double  strong_correlation_threshold; // Threshold for strong correlation
     size_t max_level_depth;                // Maximum depth for level-specific metrics
     size_t top_metrics_count;             // Number of top metrics to consider for correlation~
     // Constructor with defaults
     CorrelationParameters() 
         : min_data_points(10),
           significance_threshold(0.05),
           correlation_decay_rate(0.95),
           regime_detection_window(20),
            time_decay_factor(0.1),
           min_stddev_threshold(1e-6),
           strong_correlation_threshold(0.7),
            max_level_depth(10) ,
            top_metrics_count(5) {}
        // Constructor with custom values
        CorrelationParameters(size_t min_points, double significance, double decay_rate,
                            size_t window, double time_decay, double min_stddev, double strong_correlation, size_t max_depth,
                            size_t top_metrics)
            : min_data_points(min_points),
            significance_threshold(significance),
            correlation_decay_rate(decay_rate),
            regime_detection_window(window),
            time_decay_factor(time_decay),
            min_stddev_threshold(min_stddev) ,
            strong_correlation_threshold(strong_correlation),
            max_level_depth(max_depth) ,
            top_metrics_count(top_metrics) {}
    };
 
 /**
  * Represents price movement information
  */
 struct PriceMovement {
     double price_before;       // Price before the movement
     double price_after;        // Price after the movement
     double percent_change;     // Percentage change
     double absolute_change;    // Absolute change
     core::Timestamp time_start; // Start time of movement
     core::Timestamp time_end;   // End time of movement
     core::Duration duration;    // Duration of movement
     core::Symbol symbol;          // Trading pair symbol
     
     // Default constructor
     PriceMovement()
         : price_before(0.0),
           price_after(0.0),
           percent_change(0.0),
           absolute_change(0.0),
           duration(std::chrono::milliseconds(0)) {}
     
     // Constructor with price information
     PriceMovement(double before, double after)
         : price_before(before),
           price_after(after),
           duration(std::chrono::milliseconds(0)) {
         absolute_change = after - before;
         percent_change = (price_before != 0.0) ? 
             (absolute_change / price_before) : 0.0;
     }
 };
 
 /**
  * Correlation statistics
  */
 struct CorrelationStats {
     std::map<std::string, double> coefficients;  // Correlation coefficients
     std::map<std::string, double> feature_importance; // Feature importance scores
     size_t data_points;                         // Number of data points used
     MarketRegime market_regime;                 // Detected market regime
     double overall_predictive_power;            // Overall predictive power
     
     // Default constructor
     CorrelationStats()
         : data_points(0),
           market_regime(MarketRegime::NORMAL),
           overall_predictive_power(0.0) {}
 };
 
 /**
  * Helper function to convert ImbalanceType to string for metrics
  */
 inline std::string ImbalanceTypeToString(ImbalanceType type) {
     return imbalanceTypeToString(type);
 }
 
 } // namespace strategy
 } // namespace trading