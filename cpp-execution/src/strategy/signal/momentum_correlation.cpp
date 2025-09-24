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
#include <mutex>
#include <format>
#include <common/logger.hpp>
 
 namespace trading {
 namespace strategy {
 
MomentumCorrelation::MomentumCorrelation(size_t window_size)
    : window_size_(window_size) {
    // Initialize with more reasonable defaults
    params_.min_data_points = 15;  // Reduced from 10 for faster testing
    params_.significance_threshold = 0.05;
    params_.correlation_decay_rate = 0.9;  // Less aggressive decay
    params_.regime_detection_window = 10;  // Reduced for faster testing
    params_.time_decay_factor = 0.1;  // Less aggressive time decay
    params_.min_stddev_threshold = 1e-6;  // Very small threshold
    params_.strong_correlation_threshold = 0.3;  // Lower threshold for debugging
    params_.max_level_depth = 5;
    params_.top_metrics_count = 10;
    
    LOG_INFO("MomentumCorrelation initialized with window_size: {}", window_size_);
}

MomentumCorrelation::MomentumCorrelation(const MomentumCorrelationConfig& config) {
    window_size_ = config.correlationWindowMs;
    //  int predictionHorizonMs = 5000;        // Prediction horizon in milliseconds
    }

void MomentumCorrelation::updateCorrelationModel(
    const OrderBookMetrics& metrics, const PriceMovement& movement) {
    
    // Add debug logging
    // LOG_INFO("updateCorrelationModel called with price movement: {}", movement.percent_change);
    // 
    // Validate inputs
    if (std::isnan(movement.percent_change) || std::isinf(movement.percent_change)) {
        LOG_WARN("Invalid price movement detected: {}", movement.percent_change);
        return;
    }
    
    // Create data point
    DataPoint dataPoint;
    dataPoint.metrics = metrics;
    dataPoint.price_movement = movement;
    dataPoint.timestamp = metrics.timestamp;

    // Add to data points under lock to avoid concurrent mutation from multiple threads
    size_t currentSize = 0;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        // Emplace to avoid an extra copy of potentially-heavy DataPoint
        data_points_.emplace_back(std::move(dataPoint));

        // Maintain window size
        while (data_points_.size() > window_size_) {
            data_points_.pop_front();
        }

        currentSize = data_points_.size();
    }
    
    // LOG_INFO("Added data point. Total points: {}, Price movement: {}", 
            //  data_points_.size(), movement.percent_change);
    
    // Calculate correlations with fewer data points for testing
    if (currentSize >= params_.min_data_points) {
        // LOG_INFO("Calculating correlations with {} data points", data_points_.size());
        calculateCorrelations();
        calculateFeatureImportance();
        updateMarketRegime();
    }
    //  else {
    //     LOG_INFO("Not enough data points yet. Have: {}, Need: {}", 
    //              data_points_.size(), params_.min_data_points);
    // }
} 

  
 CorrelationStats MomentumCorrelation::getCorrelationStats() const {
    CorrelationStats stats;
    // Snapshot under lock to avoid concurrent mutation while computing stats
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        stats.coefficients = correlation_coefficients_;
        stats.feature_importance = feature_importance_;
        stats.data_points = data_points_.size();
        stats.market_regime = current_regime_;
    }

    // Calculate overall predictive power from the snapshot
    double sumImportance = 0.0;
    for (const auto& [feature, importance] : stats.feature_importance) {
        auto it = stats.coefficients.find(feature);
        if (it != stats.coefficients.end()) {
            sumImportance += importance * std::abs(it->second);
        }
    }
    stats.overall_predictive_power = sumImportance;
    LOG_INFO("Overall predictive power calculated: {}", stats.overall_predictive_power);
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
    // Snapshot pointers to data_points_ under lock to avoid heavy copies
    std::vector<const DataPoint*> snapshotPtrs;
    size_t n = 0;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        if (data_points_.size() < params_.min_data_points) {
            LOG_WARN("Not enough data points for correlation calculation. Have {} but need at least {}", 
                    data_points_.size(), params_.min_data_points);
            return;
        }
        n = data_points_.size();
        snapshotPtrs.reserve(n);
        for (const auto& dp : data_points_) snapshotPtrs.push_back(&dp);
    }

    // Extract key metrics for correlation from the snapshot pointers
    std::map<std::string, std::vector<double>> metricValues;
    std::vector<double> priceMovements;
    std::vector<double> timeWeights;  // For time-weighted correlation
    priceMovements.reserve(n);
    timeWeights.reserve(n);

    // Precompute level keys to avoid repeated string allocations
    std::vector<std::string> levelKeys;
    levelKeys.reserve(params_.max_level_depth);
    for (size_t level = 1; level <= params_.max_level_depth; ++level) {
        levelKeys.push_back("level_" + std::to_string(level) + "_imbalance");
    }

    // Get current time for time weighting
    auto currentTime = std::chrono::system_clock::now();

    for (const auto* dataPointPtr : snapshotPtrs) {
        const auto& dataPoint = *dataPointPtr;

        // Add price movement
        priceMovements.push_back(dataPoint.price_movement.percent_change);

        // Calculate time weight (newer points have higher weight)
        auto timeDiff = std::chrono::duration_cast<std::chrono::seconds>(
            currentTime - dataPoint.timestamp).count();
        double timeWeight = std::exp(-params_.time_decay_factor * timeDiff / 3600.0); // decay per hour
        timeWeights.push_back(timeWeight);

        // Helper to reserve metric vector capacity on first insertion
        auto ensureReserve = [&](std::vector<double>& vec) {
            if (vec.empty()) vec.reserve(n);
        };

        // Extract and store key metrics from OrderBookMetrics with capacity hints
        {
            auto& v = metricValues["bid_ask_ratio"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.bidAskVolumeRatio);
        }
        {
            auto& v = metricValues["volumeImbalance "];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.volumeImbalance);
        }
        {
            auto& v = metricValues["weightedVolumeImbalance"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.weightedVolumeImbalance);
        }
        {
            auto& v = metricValues["depthImbalance"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.depthImbalance);
        }
        {
            auto& v = metricValues["pressureImbalance"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.pressureImbalance);
        }
        {
            auto& v = metricValues["spread"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.spread);
        }
        {
            auto& v = metricValues["spreadPercent"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.spreadPercent);
        }
        {
            auto& v = metricValues["priceLevelImbalance"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.priceLevelImbalance);
        }
        {
            auto& v = metricValues["marketDirectionalCoefficient"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.marketDirectionalCoefficient);
        }
        {
            auto& v = metricValues["liquidityProfileScore"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.liquidityProfileScore);
        }

        // Add time-based metrics if they exist
        if (dataPoint.metrics.priceVelocity != 0.0) {
            auto& v = metricValues["priceVelocity"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.priceVelocity);
        }
        if (dataPoint.metrics.volumeVelocity != 0.0) {
            auto& v = metricValues["volumeVelocity"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.volumeVelocity);
        }
        if (dataPoint.metrics.spreadVelocity != 0.0) {
            auto& v = metricValues["spreadVelocity"];
            ensureReserve(v);
            v.push_back(dataPoint.metrics.spreadVelocity);
        }

        // Process level-specific metrics from timeSeriesMetrics if available
        auto levelIt = dataPoint.metrics.timeSeriesMetrics.find("level_imbalances");
        if (levelIt != dataPoint.metrics.timeSeriesMetrics.end()) {
            const auto& levelDataVec = levelIt->second;
            for (size_t level = 1; level <= params_.max_level_depth; ++level) {
                if (level <= levelDataVec.size()) {
                    auto& v = metricValues[levelKeys[level-1]];
                    ensureReserve(v);
                    v.push_back(levelDataVec[level-1]);
                }
            }
        }

        // Add any other metrics from the metrics map via const-ref accessor to avoid copies
        for (const auto& kv : dataPoint.metrics.metricsMap()) {
            const auto& key = kv.first;
            const auto& value = kv.second;
            auto& v = metricValues[key];
            ensureReserve(v);
            v.push_back(value);
        }
    }

    // Calculate time-weighted correlation coefficients for each metric
    std::map<std::string, double> newCorrelationCoeffs;
    std::map<std::string, double> newCorrelationStrengths;

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
            
            // Store in local maps (will commit under lock later)
            newCorrelationCoeffs[metric] = correlation;
            
            // Store correlation strength (absolute value) for sorting
            newCorrelationStrengths[metric] = std::abs(correlation);
            
            // Log strong correlations for debugging
            // if (std::abs(correlation) > params_.strong_correlation_threshold) {
            //     LOG_DEBUG("Strong correlation detected: metric: {}, correlation: {:.4f}", metric, correlation);
            // }
        }
        catch (const std::exception& e) {
            LOG_ERROR("Error calculating correlation for metric {}: {}", metric, e.what());
            newCorrelationCoeffs[metric] = 0.0;
        }
    }
    // Commit new correlation maps under lock
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        correlation_coefficients_.swap(newCorrelationCoeffs);
        correlation_strengths_.swap(newCorrelationStrengths);
    }

    // Update highest correlated metrics list (it will take the lock internally)
    updateHighestCorrelatedMetrics();
}

double MomentumCorrelation::calculatePearsonCorrelation(
    const std::vector<double>& x, const std::vector<double>& y) const {
    
    if (x.size() != y.size() || x.size() < 2) {
        LOG_WARN("Invalid input for correlation calculation");
        return 0.0;
    }
    
    size_t n = x.size();
    
    // Calculate means
    double meanX = std::accumulate(x.begin(), x.end(), 0.0) / n;
    double meanY = std::accumulate(y.begin(), y.end(), 0.0) / n;
    
    LOG_DEBUG("Correlation calculation - meanX: {}, meanY: {}, n: {}", meanX, meanY, n);
    
    // Calculate numerator and denominators
    double numerator = 0.0;
    double sumXSquared = 0.0;
    double sumYSquared = 0.0;
    
    for (size_t i = 0; i < n; ++i) {
        double deltaX = x[i] - meanX;
        double deltaY = y[i] - meanY;
        
        numerator += deltaX * deltaY;
        sumXSquared += deltaX * deltaX;
        sumYSquared += deltaY * deltaY;
    }
    
    LOG_DEBUG("Correlation components - numerator: {}, sumXSq: {}, sumYSq: {}", 
              numerator, sumXSquared, sumYSquared);
    
    // Calculate correlation
    double denominator = std::sqrt(sumXSquared * sumYSquared);
    
    if (denominator < params_.min_stddev_threshold) {
        LOG_DEBUG("Denominator too small: {}, returning 0", denominator);
        return 0.0;
    }
    
    double correlation = numerator / denominator;
    
    // Clamp to valid range
    correlation = std::max(-1.0, std::min(1.0, correlation));
    
    LOG_DEBUG("Final correlation: {}", correlation);
    return correlation;
}

double MomentumCorrelation::predictPriceMovement(const ImbalanceEvent& event) {
    if (data_points_.size() < params_.min_data_points) {
        // LOG_DEBUG("Not enough data for prediction - have {} points", data_points_.size());
        return 0.0;
    }
    
    // LOG_DEBUG("Predicting price movement for imbalance type: {}", 
    //           static_cast<int>(event.type));
    
    double prediction = calculateWeightedPrediction(event);
    
    // LOG_INFO("Price movement prediction: {}", prediction);
    return prediction;
}

double MomentumCorrelation::getCorrelationCoefficient(const std::string& metric_name) const {
    auto it = correlation_coefficients_.find(metric_name);
    if (it != correlation_coefficients_.end()) {
        //only log if correlation is significant
        // if (std::abs(it->second) > 0) {
        //     LOG_DEBUG("Found correlation for {}: {}", metric_name, it->second);
        // }
        return it->second;
    }
    
    // LOG_DEBUG("No correlation found for metric: {}", metric_name);
    
    // Debug: print all available correlations
    // LOG_DEBUG("Available correlations:");
    // for (const auto& [name, coeff] : correlation_coefficients_) {
    //     LOG_DEBUG("  {}: {}", name, coeff);
    // }
    
    return 0.0;
}

// Add this helper method to your header file
// std::string MomentumCorrelation::imbalanceTypeToString(ImbalanceType type) const {
//     switch (type) {
//         case ImbalanceType::VOLUME_IMBALANCE: return "volumeImbalance ";
//         case ImbalanceType::DEPTH_IMBALANCE: return "depthImbalance";
//         case ImbalanceType::PRESSURE_IMBALANCE: return "pressureImbalance";
//         case ImbalanceType::BID_ASK_RATIO: return "bid_ask_ratio";
//         case ImbalanceType::SPREAD_IMBALANCE: return "spread";
//         default: return "unknown_imbalance";
//     }
// }

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
     
     // Snapshot correlation coefficients and data point count under lock
     std::map<std::string, double> coeffsSnapshot;
     size_t sampleCount = 0;
     {
         std::lock_guard<std::mutex> lock(correlation_mutex_);
         coeffsSnapshot = correlation_coefficients_;
         sampleCount = data_points_.size();
     }

     double totalImportance = 0.0;
     std::map<std::string, double> rawImportance;
     
     for (const auto& [metric, correlation] : coeffsSnapshot) {
         double absCorrelation = std::abs(correlation);
         // Only consider correlations that are statistically significant
         if (isStatisticallySignificant(correlation, sampleCount)) {
             rawImportance[metric] = absCorrelation * absCorrelation; // Square to emphasize stronger correlations
             totalImportance += rawImportance[metric];
         } else {
             rawImportance[metric] = 0.0;
         }
    }
     
     // Normalize importance values
     // Commit feature importance under lock
     {
         std::lock_guard<std::mutex> lock(correlation_mutex_);
         feature_importance_.clear();
         if (totalImportance > 0.0) {
             for (auto& [metric, importance] : rawImportance) {
                 feature_importance_[metric] = importance / totalImportance;
             }
         } else {
             // If no significant correlations, assign equal importance
             double equalImportance = 0.0;
             if (!coeffsSnapshot.empty()) {
                 equalImportance = 1.0 / static_cast<double>(coeffsSnapshot.size());
             }
             for (const auto& [metric, _] : coeffsSnapshot) {
                 feature_importance_[metric] = equalImportance;
             }
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
    
    // Snapshot recent price movements under lock
    std::vector<double> recentMovements;
    std::vector<double> recentVolumes; // If available
    std::vector<double> recentSpreads; // If available
    
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        size_t startIdx = std::max(0, static_cast<int>(data_points_.size() - params_.regime_detection_window));
        
        for (size_t i = startIdx; i < data_points_.size(); ++i) {
            recentMovements.push_back(data_points_[i].price_movement.percent_change);
            // Add volume and spread data if available in your data structure
            // recentVolumes.push_back(data_points_[i].volume_ratio);
            // recentSpreads.push_back(data_points_[i].spread_ratio);
        }
    }
    
    // Enhanced statistical calculations
    double mean = std::accumulate(recentMovements.begin(), recentMovements.end(), 0.0) / recentMovements.size();
    
    // Calculate volatility (standard deviation)
    double variance = 0.0;
    for (double movement : recentMovements) {
        variance += (movement - mean) * (movement - mean);
    }
    variance /= recentMovements.size();
    double volatility = std::sqrt(variance);
    
    // Calculate directional persistence (trend strength)
    double cumulativeMove = std::accumulate(recentMovements.begin(), recentMovements.end(), 0.0);
    double directionalMovement = std::abs(cumulativeMove);
    
    // Calculate oscillation (total absolute movement)
    double oscillation = 0.0;
    for (double movement : recentMovements) {
        oscillation += std::abs(movement);
    }
    
    // Calculate trend persistence (how often price moves in same direction)
    double trendPersistence = calculateTrendPersistence(recentMovements);
    
    // Calculate recent momentum acceleration
    double momentum = calculateMomentumAcceleration(recentMovements);
    
    // Enhanced market stress indicators
    double stressIndicator = calculateMarketStress(recentMovements, volatility);
    
    // Activity level (based on movement frequency and magnitude)
    double activityLevel = oscillation / recentMovements.size();
    
    // Enhanced regime determination with hierarchical logic
    MarketRegime detectedRegime = MarketRegime::NORMAL;
    
    // 1. Check for market stress first (highest priority)
    if (stressIndicator > 0.7) {
        detectedRegime = MarketRegime::STRESS;
    }
    // 2. Check for high volatility conditions
    else if (volatility > 0.004) { // 0.4% threshold
        if (activityLevel < 0.002) {
            // High vol but low activity - might be gapping/sparse trading
            detectedRegime = MarketRegime::VOLATILE;
        } else {
            detectedRegime = MarketRegime::VOLATILE;
        }
    }
    // 3. Check for trending conditions
    else if (directionalMovement > 0.006 && trendPersistence > 0.6 && momentum > 0.3) {
        detectedRegime = MarketRegime::TRENDING;
    }
    // 4. Check for range-bound conditions
    else if (directionalMovement < 0.003 && oscillation > 0.008 && trendPersistence < 0.4) {
        detectedRegime = MarketRegime::RANGE_BOUND;
    }
    // 5. Check for quiet/low activity conditions
    else if (volatility < 0.001 && activityLevel < 0.002) {
        detectedRegime = MarketRegime::QUIET;
    }
    // 6. Default to normal
    else {
        detectedRegime = MarketRegime::NORMAL;
    }
    
    // Apply smoothing to prevent rapid regime switching
    applyRegimeSmoothing(detectedRegime);
    
    // Debug logging (optional)
    if (current_regime_ != detectedRegime) {
        LOG_DEBUG("Market regime change: {} -> {} (vol={:.4f}, dir={:.4f}, osc={:.4f}, trend={:.2f}, stress={:.2f})",
                  marketRegimeToString(current_regime_), marketRegimeToString(detectedRegime),
                  volatility, directionalMovement, oscillation, trendPersistence, stressIndicator);
    }
}

// Helper method to calculate trend persistence
double MomentumCorrelation::calculateTrendPersistence(const std::vector<double>& movements) const {
    if (movements.size() < 2) return 0.5;
    
    int sameDirectionCount = 0;
    int totalTransitions = 0;
    
    for (size_t i = 1; i < movements.size(); ++i) {
        if (std::abs(movements[i]) > 0.0001 && std::abs(movements[i-1]) > 0.0001) { // Ignore tiny movements
            totalTransitions++;
            if ((movements[i] > 0 && movements[i-1] > 0) || (movements[i] < 0 && movements[i-1] < 0)) {
                sameDirectionCount++;
            }
        }
    }
    
    return totalTransitions > 0 ? static_cast<double>(sameDirectionCount) / totalTransitions : 0.5;
}

// Helper method to calculate momentum acceleration
double MomentumCorrelation::calculateMomentumAcceleration(const std::vector<double>& movements) const {
    if (movements.size() < 3) return 0.0;
    
    // Calculate if recent movements are accelerating
    size_t halfSize = movements.size() / 2;
    double firstHalfMean = 0.0, secondHalfMean = 0.0;
    
    for (size_t i = 0; i < halfSize; ++i) {
        firstHalfMean += std::abs(movements[i]);
    }
    firstHalfMean /= halfSize;
    
    for (size_t i = halfSize; i < movements.size(); ++i) {
        secondHalfMean += std::abs(movements[i]);
    }
    secondHalfMean /= (movements.size() - halfSize);
    
    // Return normalized acceleration (-1 to 1)
    double maxMean = std::max(firstHalfMean, secondHalfMean);
    if (maxMean < 0.0001) return 0.0;
    
    return (secondHalfMean - firstHalfMean) / maxMean;
}

// Helper method to calculate market stress indicator
double MomentumCorrelation::calculateMarketStress(const std::vector<double>& movements, double volatility) const {
    if (movements.empty()) return 0.0;
    
    double stressScore = 0.0;
    
    // Factor 1: Extreme volatility (normalized to 0-1)
    double volStress = std::min(1.0, volatility / 0.01); // 1% vol = max stress from volatility
    stressScore += volStress * 0.4;
    
    // Factor 2: Extreme movements (count of movements > 2 standard deviations)
    double stdDev = volatility;
    int extremeMovements = 0;
    for (double movement : movements) {
        if (std::abs(movement) > 2.0 * stdDev) {
            extremeMovements++;
        }
    }
    double extremeStress = std::min(1.0, static_cast<double>(extremeMovements) / movements.size() * 10.0);
    stressScore += extremeStress * 0.3;
    
    // Factor 3: Rapid direction changes (whipsaws)
    int directionChanges = 0;
    for (size_t i = 1; i < movements.size(); ++i) {
        if ((movements[i] > 0 && movements[i-1] < 0) || (movements[i] < 0 && movements[i-1] > 0)) {
            if (std::abs(movements[i]) > stdDev && std::abs(movements[i-1]) > stdDev) {
                directionChanges++;
            }
        }
    }
    double whipsawStress = std::min(1.0, static_cast<double>(directionChanges) / movements.size() * 5.0);
    stressScore += whipsawStress * 0.3;
    
    return std::clamp(stressScore, 0.0, 1.0);
}

// Helper method to apply regime smoothing
void MomentumCorrelation::applyRegimeSmoothing(MarketRegime& detectedRegime) {
    // Simple smoothing: require 2-3 consecutive detections before switching
    static MarketRegime lastDetection = MarketRegime::NORMAL;
    static int consecutiveCount = 0;
    
    if (detectedRegime == lastDetection) {
        consecutiveCount++;
    } else {
        lastDetection = detectedRegime;
        consecutiveCount = 1;
    }
    
    // Different thresholds for different regime changes
    int requiredConsecutive = 2; // Default
    
    // Require more evidence for stress regime
    if (detectedRegime == MarketRegime::STRESS) {
        requiredConsecutive = 3;
    }
    // Require less evidence when moving from stress back to normal
    else if (current_regime_ == MarketRegime::STRESS && detectedRegime != MarketRegime::STRESS) {
        requiredConsecutive = 1;
    }
    
    // Only update if we have enough consecutive detections
    if (consecutiveCount >= requiredConsecutive) {
        current_regime_ = detectedRegime;
    }
    // If not enough evidence, keep current regime but update the working detection
    else {
        detectedRegime = current_regime_;
    }
}



double MomentumCorrelation::calculateWeightedPrediction(const ImbalanceEvent& event) const {
    // Snapshot correlation data under lock for thread-safe reads
    std::map<std::string, double> coeffsSnapshot;
    std::map<std::string, double> importanceSnapshot;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        coeffsSnapshot = correlation_coefficients_;
        importanceSnapshot = feature_importance_;
    }

    if (coeffsSnapshot.empty()) {
        LOG_DEBUG("No correlation data available for prediction");
        return 0.0;
    }

    double prediction = 0.0;
    double totalWeight = 0.0;
    
    // Process supporting metrics
    for (const auto& [metricName, metricValue] : event.supportingMetrics) {
        auto corrIt = coeffsSnapshot.find(metricName);
        auto impIt = importanceSnapshot.find(metricName);
        
        if (corrIt == coeffsSnapshot.end() || impIt == importanceSnapshot.end()) {
            continue;
        }
        
        double correlation = corrIt->second;
        double importance = impIt->second;
        
        // Simple normalization: clamp extreme values
        double normalizedValue = std::clamp(metricValue, -10.0, 10.0);
        
        // Weight combines correlation strength and feature importance
        double weight = std::abs(correlation) * importance;
        
        // Contribution: normalized_value * correlation_sign * weight
        double contribution = normalizedValue * std::copysign(1.0, correlation) * weight;
        
        prediction += contribution;
        totalWeight += weight;
    }
    
    // Process primary metric with extra weight
    std::string primaryMetricName = imbalanceTypeToString(event.type);
    auto corrIt = coeffsSnapshot.find(primaryMetricName);
    auto impIt = importanceSnapshot.find(primaryMetricName);
    
    if (corrIt != coeffsSnapshot.end() && impIt != importanceSnapshot.end()) {
        double correlation = corrIt->second;
        double importance = impIt->second;
        
        // Simple normalization for primary metric
        double normalizedPrimary = std::clamp(event.primary_metric, -10.0, 10.0);
        
        // Extra weight for primary metric
        double weight = std::abs(correlation) * importance * 1.5;
        double contribution = normalizedPrimary * std::copysign(1.0, correlation) * weight;
        
        prediction += contribution;
        totalWeight += weight;
        
        // LOG_DEBUG("Primary metric {}: value={}, normalized={}, correlation={}, contribution={}", 
        //          primaryMetricName, event.primary_metric, normalizedPrimary, correlation, contribution);
    }
    
    // Normalize by total weight
    if (totalWeight > 0.0) {
        prediction /= totalWeight;
    }
    
    // Scale by event strength (0.0 to 1.0)
    prediction *= event.strength;
    
    // Snapshot current market regime to avoid data race
    MarketRegime regimeSnapshot = MarketRegime::NORMAL;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        regimeSnapshot = current_regime_;
    }

    // Apply market regime adjustments
    double regimeMultiplier = 1.0;
    switch (regimeSnapshot) {
        case MarketRegime::VOLATILE: regimeMultiplier = 0.8; break;
        case MarketRegime::TRENDING:        regimeMultiplier = 1.2; break;
        case MarketRegime::RANGE_BOUND:     regimeMultiplier = 0.7; break;
        case MarketRegime::QUIET:           regimeMultiplier = 0.6; break;
        case MarketRegime::NORMAL:      regimeMultiplier = 1.0; break;
        case MarketRegime::STRESS:         regimeMultiplier = 0.5; break;
        default:                            regimeMultiplier = 1.0; break;
    }
    prediction *= regimeMultiplier;
    
    // Convert to reasonable percentage range and clamp
    prediction *= 0.01; // Scale down to percentage units
    prediction = std::clamp(prediction, -0.05, 0.05); // Clamp to ±5%
    
    // LOG_INFO("Final prediction: {:.4f}% ({} basis points)", 
    //          prediction * 100.0, static_cast<int>(prediction * 10000));
    
    return prediction; // Return as percentage (e.g., 0.01 = 1%)
}

 
 } // namespace strategy
 } // namespace trading