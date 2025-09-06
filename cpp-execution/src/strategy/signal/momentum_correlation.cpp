/**
 * @file momentum_correlation.cpp
 * @brief Implementation of order book imbalance to price movement correlation
 */

#include "strategy/signal/momentum_correlation.hpp"
#include "strategy/types/constants.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>
#include <thread>
#include <mutex>
 
 namespace trading {
 namespace strategy {
 
 MomentumCorrelation::MomentumCorrelation(size_t window_size)
     : window_size_(window_size) {
     // Initialize correlation parameters with defaults
     params_.min_data_points = 10;
     params_.significance_threshold = 0.05;
     params_.correlation_decay_rate = 0.95;
     params_.regime_detection_window = 20;
 }

    
    MomentumCorrelation::MomentumCorrelation(const MomentumCorrelationConfig& config){
        window_size_=config.correlationWindowMs;
    //  int predictionHorizonMs = 5000;        // Prediction horizon in milliseconds
    }

 void MomentumCorrelation::updateCorrelationModel(
     const OrderBookMetrics& metrics, const PriceMovement& movement) {
     
     // Create a new data point
     DataPoint dataPoint;
     dataPoint.metrics = metrics;
     dataPoint.price_movement = movement;
     dataPoint.timestamp = metrics.timestamp;
     
     // Add to data points
     data_points_.push_back(dataPoint);
     
     // Maintain window size
     while (data_points_.size() > window_size_) {
         data_points_.pop_front();
     }
     //this log never been triggered
     LOG_INFO("updateCorrelationModel:Added data point. Total points: {}", data_points_.size());
     // Only calculate correlations when we have enough data
     if (data_points_.size() >= params_.min_data_points) {
         calculateCorrelations();
         calculateFeatureImportance();
         updateMarketRegime();
     }
 }
 
 double MomentumCorrelation::predictPriceMovement(const ImbalanceEvent& event) {
     // If we don't have enough data, return zero
     if (data_points_.size() < params_.min_data_points) {
         return 0.0;
     }
     
     // Use weighted prediction based on correlation coefficients and feature importance
     return calculateWeightedPrediction(event);
 }
 
 CorrelationStats MomentumCorrelation::getCorrelationStats() const {
     CorrelationStats stats;
     stats.coefficients = correlation_coefficients_;
     stats.feature_importance = feature_importance_;
     stats.data_points = data_points_.size();
     stats.market_regime = current_regime_;
     
     // Calculate overall predictive power
     double sumImportance = 0.0;
     for (const auto& [feature, importance] : feature_importance_) {
         sumImportance += importance * std::abs(correlation_coefficients_.at(feature));
     }
     stats.overall_predictive_power = sumImportance;
     
     return stats;
 }
 
 void MomentumCorrelation::configure(const CorrelationParameters& params) {
     params_ = params;
 }
 
 void MomentumCorrelation::reset() {
     data_points_.clear();
     correlation_coefficients_.clear();
     feature_importance_.clear();
     current_regime_ = MarketRegime::NORMAL;
 }
 
 MarketRegime MomentumCorrelation::detectMarketRegime() const {
     return current_regime_;
 }
 
 double MomentumCorrelation::getCorrelationCoefficient(const std::string& metric_name) const {
     auto it = correlation_coefficients_.find(metric_name);
     if (it != correlation_coefficients_.end()) {
         return it->second;
     }
     return 0.0;
 }
 
 std::map<std::string, double> MomentumCorrelation::getFeatureImportance() const {
     return feature_importance_;
 }
//  updateParameters
void MomentumCorrelation::updateParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Update parameters based on the provided ParameterSet
    params_.min_data_points = params.getInt("min_data_points", params_.min_data_points);
    params_.significance_threshold = params.getDouble("significance_threshold", params_.significance_threshold);
    params_.correlation_decay_rate = params.getDouble("correlation_decay_rate", params_.correlation_decay_rate);
    params_.regime_detection_window = params.getInt("regime_detection_window", params_.regime_detection_window);
    params_.time_decay_factor = params.getDouble("time_decay_factor", params_.time_decay_factor);
    params_.min_stddev_threshold = params.getDouble("min_stddev_threshold", params_.min_stddev_threshold);
    params_.strong_correlation_threshold = params.getDouble("strong_correlation_threshold", params_.strong_correlation_threshold);
    params_.max_level_depth = params.getInt("max_level_depth", params_.max_level_depth);
    params_.top_metrics_count = params.getInt("top_metrics_count", params_.top_metrics_count);
    // Update the timestamp to reflect the last update
}
void MomentumCorrelation::calculateCorrelations() {
    // Check if we have enough data points
    if (data_points_.size() < params_.min_data_points) {
        LOG_WARN("Not enough data points for correlation calculation. Have {} but need at least {}", 
                data_points_.size(), params_.min_data_points);
        return;
    }

    // Extract key metrics for correlation
    std::map<std::string, std::vector<double>> metricValues;
    std::vector<double> priceMovements;
    std::vector<double> timeWeights;  // For time-weighted correlation
    
    // Get current time for time weighting
    auto currentTime = std::chrono::system_clock::now();
    
    for (const auto& dataPoint : data_points_) {
        // Add price movement
        priceMovements.push_back(dataPoint.price_movement.percent_change);
        
        // Calculate time weight (newer points have higher weight)
        auto timeDiff = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - dataPoint.timestamp).count();
        double timeWeight = std::exp(-params_.time_decay_factor * timeDiff / 3600.0); // decay per hour
        timeWeights.push_back(timeWeight);
        
        // Extract and store key metrics from OrderBookMetrics
        // Directly access all relevant metrics using the OrderBookMetrics struct
        metricValues["bid_ask_ratio"].push_back(dataPoint.metrics.bid_ask_volume_ratio);
        metricValues["volume_imbalance"].push_back(dataPoint.metrics.volume_imbalance);
        metricValues["weighted_volume_imbalance"].push_back(dataPoint.metrics.weighted_volume_imbalance);
        metricValues["depth_imbalance"].push_back(dataPoint.metrics.depth_imbalance);
        metricValues["pressure_imbalance"].push_back(dataPoint.metrics.pressure_imbalance);
        metricValues["spread"].push_back(dataPoint.metrics.spread);
        metricValues["spread_percent"].push_back(dataPoint.metrics.spread_percent);
        metricValues["price_level_imbalance"].push_back(dataPoint.metrics.price_level_imbalance);
        metricValues["market_directional_coefficient"].push_back(dataPoint.metrics.market_directional_coefficient);
        metricValues["liquidity_profile_score"].push_back(dataPoint.metrics.liquidity_profile_score);
        
        // Add time-based metrics if they exist
        if (dataPoint.metrics.price_velocity != 0.0) {
            metricValues["price_velocity"].push_back(dataPoint.metrics.price_velocity);
        }
        if (dataPoint.metrics.volume_velocity != 0.0) {
            metricValues["volume_velocity"].push_back(dataPoint.metrics.volume_velocity);
        }
        if (dataPoint.metrics.spread_velocity != 0.0) {
            metricValues["spread_velocity"].push_back(dataPoint.metrics.spread_velocity);
        }
        
        // Process level-specific metrics from time_series_metrics if available
        for (size_t level = 1; level <= params_.max_level_depth; ++level) {
            std::string levelKey = "level_" + std::to_string(level) + "_imbalance";
            // Check if we have level-specific imbalance data
            auto levelData = dataPoint.metrics.time_series_metrics.find("level_imbalances");
            if (levelData != dataPoint.metrics.time_series_metrics.end() && 
                level <= levelData->second.size()) {
                metricValues[levelKey].push_back(levelData->second[level-1]);
            }
        }
        
        // Add any other metrics from the metrics map
        for (const auto& [key, value] : dataPoint.metrics.toMap()) {
            if (metricValues.find(key) == metricValues.end()) {
                metricValues[key].push_back(value);
            }
        }
    }

    // Calculate time-weighted correlation coefficients for each metric
    for (auto& [metric, values] : metricValues) {
        // Skip if we don't have enough data points or sizes don't match
        if (values.size() != priceMovements.size() || values.size() < params_.min_data_points) {
            // LOG_WARN("Skipping correlation for metric {} due to insufficient or mismatched data", metric);
            correlation_coefficients_[metric] = 0.0;
            continue;
        }
        
        try {
            // Normalize the metric values to improve correlation quality
            std::vector<double> normalizedValues;
            normalizedValues.reserve(values.size());
            
            // Calculate mean and stddev for normalization
            double sum = 0.0;
            for (size_t i = 0; i < values.size(); ++i) {
                sum += values[i] * timeWeights[i];
            }
            double weightSum = std::accumulate(timeWeights.begin(), timeWeights.end(), 0.0);
            double mean = weightSum > 0 ? sum / weightSum : 0.0;
            
            double varianceSum = 0.0;
            for (size_t i = 0; i < values.size(); ++i) {
                double diff = values[i] - mean;
                varianceSum += diff * diff * timeWeights[i];
            }
            double stddev = weightSum > 0 ? std::sqrt(varianceSum / weightSum) : 1.0;
            stddev = std::max(stddev, params_.min_stddev_threshold); // Avoid division by very small numbers
            
            // Normalize values
            for (double val : values) {
                normalizedValues.push_back((val - mean) / stddev);
            }
            
            // Also normalize price movements
            double priceMean = 0.0;
            double priceWeightSum = 0.0;
            for (size_t i = 0; i < priceMovements.size(); ++i) {
                priceMean += priceMovements[i] * timeWeights[i];
                priceWeightSum += timeWeights[i];
            }
            priceMean = priceWeightSum > 0 ? priceMean / priceWeightSum : 0.0;
            
            double priceVarianceSum = 0.0;
            for (size_t i = 0; i < priceMovements.size(); ++i) {
                double diff = priceMovements[i] - priceMean;
                priceVarianceSum += diff * diff * timeWeights[i];
            }
            double priceStddev = priceWeightSum > 0 ? std::sqrt(priceVarianceSum / priceWeightSum) : 1.0;
            priceStddev = std::max(priceStddev, params_.min_stddev_threshold);
            
            std::vector<double> normalizedPrices;
            normalizedPrices.reserve(priceMovements.size());
            for (double val : priceMovements) {
                normalizedPrices.push_back((val - priceMean) / priceStddev);
            }
            
            // Calculate time-weighted correlation
            double covariance = 0.0;
            for (size_t i = 0; i < normalizedValues.size(); ++i) {
                covariance += normalizedValues[i] * normalizedPrices[i] * timeWeights[i];
            }
            
            // The normalization simplifies the correlation formula since variances are now 1.0
            double correlation = weightSum > 0 ? covariance / weightSum : 0.0;
            
            // Clamp correlation to valid range [-1, 1]
            correlation = std::max(-1.0, std::min(1.0, correlation));
            
            // Apply exponential decay to smooth correlation over time
            if (correlation_coefficients_.count(metric) > 0) {
                correlation = params_.correlation_decay_rate * correlation_coefficients_[metric] +
                                (1.0 - params_.correlation_decay_rate) * correlation;
            }
            
            // Store the result
            correlation_coefficients_[metric] = correlation;
            
            // Store correlation strength (absolute value) for sorting
            correlation_strengths_[metric] = std::abs(correlation);
            
            // Log strong correlations for debugging
            if (std::abs(correlation) > params_.strong_correlation_threshold) {
                LOG_DEBUG("Strong correlation detected: {} = {:.4f}", metric, correlation);
            }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error calculating correlation for metric {}: {}", metric, e.what());
            correlation_coefficients_[metric] = 0.0;
        }
    }
    
    // Update highest correlated metrics list
    updateHighestCorrelatedMetrics();
}
void MomentumCorrelation::updateHighestCorrelatedMetrics() {
    // Sort metrics by correlation strength
    std::vector<std::pair<std::string, double>> sortedMetrics;
    for (const auto& [metric, strength] : correlation_strengths_) {
        sortedMetrics.push_back({metric, strength});
    }
    
    // Sort in descending order of correlation strength
    std::sort(sortedMetrics.begin(), sortedMetrics.end(), 
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Take top N metrics
    highest_correlated_metrics_.clear();
    for (size_t i = 0; i < std::min(params_.top_metrics_count, sortedMetrics.size()); ++i) {
        highest_correlated_metrics_.push_back({
            sortedMetrics[i].first, 
            correlation_coefficients_[sortedMetrics[i].first]
        });
    }
}
 void MomentumCorrelation::calculateFeatureImportance() {
     // Simple feature importance calculation based on correlation magnitude
     // In a more sophisticated implementation, this could be based on 
     // regression weights, mutual information, or other methods
     
     double totalImportance = 0.0;
     std::map<std::string, double> rawImportance;
     
     for (const auto& [metric, correlation] : correlation_coefficients_) {
         double absCorrelation = std::abs(correlation);
         // Only consider correlations that are statistically significant
         if (isStatisticallySignificant(correlation, data_points_.size())) {
             rawImportance[metric] = absCorrelation * absCorrelation; // Square to emphasize stronger correlations
             totalImportance += rawImportance[metric];
         } else {
             rawImportance[metric] = 0.0;
         }
     }
     
     // Normalize importance values
     if (totalImportance > 0.0) {
         for (auto& [metric, importance] : rawImportance) {
             feature_importance_[metric] = importance / totalImportance;
         }
     } else {
         // If no significant correlations, assign equal importance
         double equalImportance = 1.0 / std::max(1.0, static_cast<double>(correlation_coefficients_.size()));
         for (const auto& [metric, _] : correlation_coefficients_) {
             feature_importance_[metric] = equalImportance;
         }
     }
 }
 
 bool MomentumCorrelation::isStatisticallySignificant(double coefficient, size_t sample_size) const {
     // Calculate t-statistic for Pearson correlation
     double absCorrelation = std::abs(coefficient);
     
     // Critical value depends on significance level and degrees of freedom
     // This is a simplified approximation
     double criticalValue = 0.0;
     
     // Different critical values based on sample size
     if (sample_size <= 5) {
         criticalValue = 0.878; // p = 0.05, df = 3
     } else if (sample_size <= 10) {
         criticalValue = 0.632; // p = 0.05, df = 8
     } else if (sample_size <= 20) {
         criticalValue = 0.444; // p = 0.05, df = 18
     } else if (sample_size <= 50) {
         criticalValue = 0.279; // p = 0.05, df = 48
     } else if (sample_size <= 100) {
         criticalValue = 0.197; // p = 0.05, df = 98
     } else {
         criticalValue = 0.124; // p = 0.05, df = 250
     }
     
     // Adjust based on significance threshold parameter
     criticalValue *= (params_.significance_threshold / 0.05);
     
     return absCorrelation > criticalValue;
 }
 
 void MomentumCorrelation::updateMarketRegime() {
     // We need enough data points for reliable regime detection
     if (data_points_.size() < params_.regime_detection_window) {
         current_regime_ = MarketRegime::NORMAL;
         return;
     }
     std::cout<<"Updating market regime based on recent data...\n";
     
     // Get recent price movements
     std::vector<double> recentMovements;
     size_t startIdx = std::max(0, static_cast<int>(data_points_.size() - params_.regime_detection_window));
     
     for (size_t i = startIdx; i < data_points_.size(); ++i) {
         recentMovements.push_back(data_points_[i].price_movement.percent_change);
     }
     
     // Calculate volatility (standard deviation of price movements)
     double mean = std::accumulate(recentMovements.begin(), recentMovements.end(), 0.0) / recentMovements.size();
     double variance = 0.0;
     
     for (double movement : recentMovements) {
         variance += (movement - mean) * (movement - mean);
     }
     
     variance /= recentMovements.size();
     double volatility = std::sqrt(variance);
     
     // Calculate directional movement (absolute sum of price movements)
     double directionalMovement = std::abs(std::accumulate(recentMovements.begin(), recentMovements.end(), 0.0));
     
     // Calculate oscillation (sum of absolute movements)
     double oscillation = 0.0;
     for (double movement : recentMovements) {
         oscillation += std::abs(movement);
     }
     
     // Determine regime based on volatility and directional movement
     if (volatility > 0.003) { // High volatility threshold (0.3% per data point)
         current_regime_ = MarketRegime::HIGH_VOLATILITY;
     } else if (directionalMovement > 0.005 && oscillation > 0.01) { // Strong trend (0.5% total with oscillations)
         current_regime_ = MarketRegime::TRENDING;
     } else if (oscillation < 0.005) { // Low oscillation (less than 0.5% total movement)
         current_regime_ = MarketRegime::QUIET;
     } else if (directionalMovement < 0.002 && oscillation > 0.008) { // Range-bound (high oscillation, low net movement)
         current_regime_ = MarketRegime::RANGE_BOUND;
     } else {
         current_regime_ = MarketRegime::NORMAL;
     }
     std::cout<<"Current market regime: ";
     switch (current_regime_) {
         case MarketRegime::NORMAL:
             std::cout<<"NORMAL\n";
             break;
         case MarketRegime::TRENDING:
             std::cout<<"TRENDING\n";
             break;
         case MarketRegime::RANGE_BOUND:
             std::cout<<"RANGE_BOUND\n";
             break;
         case MarketRegime::HIGH_VOLATILITY:
             std::cout<<"HIGH_VOLATILITY\n";
             break;
         case MarketRegime::QUIET:
             std::cout<<"QUIET\n";
             break;
     }
 }
 
 double MomentumCorrelation::calculateWeightedPrediction(const ImbalanceEvent& event) const {
     double prediction = 0.0;
     double totalWeight = 0.0;
     std::cout<<"Predicting price movement for event type: "<<ImbalanceTypeToString(event.type)<<"\n";
     // Use imbalance metrics and their correlations to predict price movement
     for (const auto& [metric, value] : event.supportingMetrics) {
         // Skip metrics without correlation data
         if (correlation_coefficients_.count(metric) == 0 || feature_importance_.count(metric) == 0) {
             continue;
         }
         
         double correlation = correlation_coefficients_.at(metric);
         double importance = feature_importance_.at(metric);
         
         // Weight is based on correlation strength and feature importance
         double weight = std::abs(correlation) * importance;
         
         // Direction is based on correlation sign
         double contribution = value * correlation * weight;
         
         prediction += contribution;
         totalWeight += weight;
     }
     
     // Also use primary metric if available
     std::string primaryMetricName = ImbalanceTypeToString(event.type);
     if (correlation_coefficients_.count(primaryMetricName) > 0 && feature_importance_.count(primaryMetricName) > 0) {
         double correlation = correlation_coefficients_.at(primaryMetricName);
         double importance = feature_importance_.at(primaryMetricName);
         double weight = std::abs(correlation) * importance * 1.5; // Extra weight for primary metric
         
         prediction += event.primary_metric * correlation * weight;
         totalWeight += weight;
     }
     
     // Normalize by total weight if possible
     if (totalWeight > 0.0) {
         prediction /= totalWeight;
     }
     
     // Apply strength as a scaling factor
     prediction *= event.strength;
     
     // Apply market regime adjustment
     switch (current_regime_) {
         case MarketRegime::HIGH_VOLATILITY:
             prediction *= 0.8; // Reduce prediction magnitude in high volatility
             break;
         case MarketRegime::TRENDING:
             prediction *= 1.2; // Increase prediction in trending markets
             break;
         case MarketRegime::RANGE_BOUND:
             prediction *= 0.7; // Reduce prediction in range-bound markets
             break;
         case MarketRegime::QUIET:
             prediction *= 0.5; // Reduce prediction in quiet markets
             break;
         case MarketRegime::NORMAL:
         default:
             // No adjustment
             break;
     }
     
     return prediction;
 }
 
 } // namespace strategy
 } // namespace trading