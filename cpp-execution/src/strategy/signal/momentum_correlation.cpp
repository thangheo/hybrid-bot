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

// Define a sane default for maximum metrics reserved by the correlation calculator.
// This keeps the previous intent (reserve capacity) and prevents an undefined identifier.
constexpr size_t MAX_METRICS = 64;
 
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

    // Setup default hysteresis counts for regimes (tunable)
    regime_required_counts_[MarketRegimeType::NORMAL] = 1;
    regime_required_counts_[MarketRegimeType::HIGH_VOLATILITY] = 2;
    regime_required_counts_[MarketRegimeType::TRENDING] = 3;
    regime_required_counts_[MarketRegimeType::MEAN_REVERTING] = 3;
    regime_required_counts_[MarketRegimeType::QUIET] = 2;
    regime_required_counts_[MarketRegimeType::STRESS] = 4;
    regime_required_counts_[MarketRegimeType::LOW_LIQUIDITY] = 2;
}

MomentumCorrelation::MomentumCorrelation(const MomentumCorrelationConfig& config) {
    window_size_ = config.correlationWindowMs;
    if(window_size_ <= 1000) {
        window_size_ = 1000; // Default if not set
    }
    //  int predictionHorizonMs = 5000;        // Prediction horizon in milliseconds
    }

// Helper: compute EWMA alpha from half-life (milliseconds) and dt (milliseconds)
static double ewma_alpha(double half_life_ms, double dt_ms) {
    if (half_life_ms <= 0.0) return 1.0;
    // alpha such that after dt, weight = 2^{-dt/half_life}
    double exponent = - (dt_ms / half_life_ms) * std::log(2.0);
    double decay = std::exp(exponent);
    return 1.0 - decay; // blending factor for new value
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
    
    DataPoint dataPoint;
    dataPoint.metrics = metrics;
    dataPoint.price_movement = movement;
    dataPoint.timestamp = metrics.timestamp;

    // Add to data points under lock to avoid concurrent mutation from multiple threads
    size_t currentSize = 0;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        // Emplace to avoid an extra copy of potentially-heavy DataPoint
        data_points_.push(std::move(dataPoint));

        // // Maintain window size
        // while (data_points_.size() > window_size_) {
        //     data_points_.pop();
        // }

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
    //  data_points_.clear();
     correlation_coefficients_.clear();
     feature_importance_.clear();
     current_regime_ = MarketRegimeType::NORMAL;
 }
 
 MarketRegimeType MomentumCorrelation::detectMarketRegime() const {
    std::lock_guard<std::mutex> lock(correlation_mutex_);
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
    params_.ewma_half_life_ms = params.getDouble("ewma_half_life_ms", params_.ewma_half_life_ms);
    // Update the timestamp to reflect the last update
}

void MomentumCorrelation::calculateCorrelations() {
    try {
        // Simple fix: reduce data size FIRST
        size_t n = data_points_.size();
        if (n < params_.min_data_points) {
            LOG_WARN("Not enough data points: have {} need {}", n, params_.min_data_points);
            return;
        }
        //we use circular buffer, no need to trim

        // Take a safe copy of the snapshot. Using pointer snapshots into the
        // internal circular buffer can race with concurrent writes and may cause
        // instability; copying is slightly more expensive but deterministic.
        std::vector<DataPoint> snapshotCopy = data_points_.snapshot();
        size_t total = snapshotCopy.size();
        if (total < params_.min_data_points) {
            LOG_WARN("Not enough data points: have {} need {}", total, params_.min_data_points);
            return;
        }

        // Cap the number of points we process to avoid large allocations under
        // memory pressure. This keeps latency bounded and prevents OOMs.
        const size_t HARD_POINT_CAP = 2000; // tuned conservative cap
        size_t startIdx = 0;
        size_t capN = total;
        if (total > HARD_POINT_CAP) {
            startIdx = total - HARD_POINT_CAP;
            capN = HARD_POINT_CAP;
            // LOG_WARN("Capping correlation data points from {} -> {}", total, capN);
        }

        // Pre-allocate with cap size
        std::vector<double> priceMovements;
        std::vector<double> timeWeights;
        priceMovements.reserve(capN);
        timeWeights.reserve(capN);

        // Simple metric storage - no fancy maps
        struct SimpleMetric {
            std::string name;
            std::vector<double> values;
            SimpleMetric(const std::string& n, size_t reserve_size) : name(n) {
                values.reserve(reserve_size);
            }
        };
        
        std::vector<SimpleMetric> metrics;
        metrics.reserve(MAX_METRICS);
        
        // Add core metrics only
        metrics.emplace_back("bid_ask_ratio", capN);
        metrics.emplace_back("volumeImbalance", capN);
        metrics.emplace_back("weightedVolumeImbalance", capN);
        metrics.emplace_back("depthImbalance", capN);
        metrics.emplace_back("pressureImbalance", capN);


        auto currentTime = std::chrono::system_clock::now();

        // Single pass data collection over the copied snapshot
        for (size_t idx = startIdx; idx < startIdx + capN; ++idx) {
            const DataPoint& dp = snapshotCopy[idx];

            priceMovements.push_back(dp.price_movement.percent_change);

            auto timeDiff = std::chrono::duration_cast<std::chrono::seconds>(
                currentTime - dp.timestamp).count();
            double timeWeight = std::exp(-params_.time_decay_factor * timeDiff / 3600.0);
            timeWeights.push_back(timeWeight);

            // Add metric values
            if (metrics.size() > 0) metrics[0].values.push_back(dp.metrics.bidAskVolumeRatio);
            if (metrics.size() > 1) metrics[1].values.push_back(dp.metrics.volumeImbalance);
            if (metrics.size() > 2) metrics[2].values.push_back(dp.metrics.weightedVolumeImbalance);
            if (metrics.size() > 3) metrics[3].values.push_back(dp.metrics.depthImbalance);
            if (metrics.size() > 4) metrics[4].values.push_back(dp.metrics.pressureImbalance);
        }

        // Simple correlation calculation
        std::map<std::string, double> newCorrelationCoeffs;
        std::map<std::string, double> newCorrelationStrengths;

        for (const auto& metric : metrics) {
            if (metric.values.size() != priceMovements.size() || 
                metric.values.size() < params_.min_data_points) {
                newCorrelationCoeffs[metric.name] = 0.0;
                continue;
            }

            try {
                // Calculate means
                double metricSum = 0.0, priceSum = 0.0, weightSum = 0.0;
                for (size_t i = 0; i < metric.values.size(); ++i) {
                    double w = timeWeights[i];
                    metricSum += metric.values[i] * w;
                    priceSum += priceMovements[i] * w;
                    weightSum += w;
                }
                
                double metricMean = weightSum > 0 ? metricSum / weightSum : 0.0;
                double priceMean = weightSum > 0 ? priceSum / weightSum : 0.0;

                // Calculate standard deviations
                double metricVar = 0.0, priceVar = 0.0;
                for (size_t i = 0; i < metric.values.size(); ++i) {
                    double w = timeWeights[i];
                    double md = metric.values[i] - metricMean;
                    double pd = priceMovements[i] - priceMean;
                    metricVar += md * md * w;
                    priceVar += pd * pd * w;
                }
                
                double metricStd = weightSum > 0 ? std::sqrt(metricVar / weightSum) : 1.0;
                double priceStd = weightSum > 0 ? std::sqrt(priceVar / weightSum) : 1.0;
                
                metricStd = std::max(metricStd, params_.min_stddev_threshold);
                priceStd = std::max(priceStd, params_.min_stddev_threshold);

                // Calculate correlation
                double covariance = 0.0;
                for (size_t i = 0; i < metric.values.size(); ++i) {
                    double normalizedMetric = (metric.values[i] - metricMean) / metricStd;
                    double normalizedPrice = (priceMovements[i] - priceMean) / priceStd;
                    covariance += normalizedMetric * normalizedPrice * timeWeights[i];
                }

                double correlation = weightSum > 0 ? covariance / weightSum : 0.0;
                correlation = std::clamp(correlation, -1.0, 1.0);

                // Time-aware EWMA smoothing using configured half-life (ms)
                uint64_t now_ms = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());

                double blended = correlation;
                if (correlation_coefficients_.count(metric.name)) {
                    uint64_t last_ms = 0;
                    auto itlu = last_update_ms_.find(metric.name);
                    if (itlu != last_update_ms_.end()) last_ms = itlu->second;
                    double dt_ms = static_cast<double>(now_ms > last_ms ? (now_ms - last_ms) : 0);

                    double half_life = params_.ewma_half_life_ms > 0.0 ? params_.ewma_half_life_ms : 10000.0;
                    double alpha = ewma_alpha(half_life, dt_ms);
                    blended = (1.0 - alpha) * correlation_coefficients_[metric.name] + alpha * correlation;
                }

                newCorrelationCoeffs[metric.name] = blended;
                last_update_ms_[metric.name] = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
                newCorrelationStrengths[metric.name] = std::abs(correlation);
            }
            catch (const std::bad_alloc& e) {
                LOG_ERROR("Memory error on metric {}", metric.name);
                newCorrelationCoeffs[metric.name] = 0.0;
                newCorrelationStrengths[metric.name] = 0.0;
            }
        }

        // Update results
        {
            std::lock_guard<std::mutex> lock(correlation_mutex_);
            correlation_coefficients_.swap(newCorrelationCoeffs);
            correlation_strengths_.swap(newCorrelationStrengths);
        }

        updateHighestCorrelatedMetrics();
    }
    catch (const std::bad_alloc& e) {
        LOG_ERROR("Critical memory error in calculateCorrelations(): {}", e.what());
        // Emergency: clear some data
        {
            std::lock_guard<std::mutex> lock(correlation_mutex_);
            correlation_coefficients_.clear();
            correlation_strengths_.clear();
        }
    }
    catch (const std::exception& e) {
        LOG_ERROR("Exception in calculateCorrelations(): {}", e.what());
    }

    // Initialize hysteresis defaults too
    regime_required_counts_[MarketRegimeType::NORMAL] = 1;
    regime_required_counts_[MarketRegimeType::HIGH_VOLATILITY] = 2;
    regime_required_counts_[MarketRegimeType::TRENDING] = 3;
    regime_required_counts_[MarketRegimeType::MEAN_REVERTING] = 3;
    regime_required_counts_[MarketRegimeType::QUIET] = 2;
    regime_required_counts_[MarketRegimeType::STRESS] = 4;
    regime_required_counts_[MarketRegimeType::LOW_LIQUIDITY] = 2;
}

// void MomentumCorrelation::calculateCorrelations() {
//     try {
//         // Take a safe copy of the data point snapshot. Prior code took addresses of
//         // elements inside a temporary returned by data_points_.snapshot(), which
//         // produced dangling pointers when the temporary was destroyed. Copying the
//         // snapshot ensures stable storage for the remainder of the computation.
//         std::vector<DataPoint> snapshotCopy = data_points_.snapshot();
//         size_t n = snapshotCopy.size();
//         if (n < params_.min_data_points) {
//             LOG_WARN("Not enough data points: have {} need {}", n, params_.min_data_points);
//             return;
//         }

//         // Apply optional hard cap and determine start index (keeps behavior similar to
//         // previous implementation which intended to cap at a HARD_POINT_CAP).
//         const size_t HARD_POINT_CAP = 5000;
//         size_t capN = n;
//         size_t startIndex = 0;
//         if (n > HARD_POINT_CAP) {
//             capN = HARD_POINT_CAP;
//             startIndex = n - capN;
//             LOG_WARN("Capping correlation data points from {} → {}", n, capN);
//         }

//         // Adjust snapshot view to the capped range
//         if (startIndex != 0 || capN != n) {
//             std::vector<DataPoint> tmp;
//             tmp.reserve(capN);
//             for (size_t i = startIndex; i < startIndex + capN; ++i) tmp.push_back(std::move(snapshotCopy[i]));
//             snapshotCopy.swap(tmp);
//             n = snapshotCopy.size();
//         }

//         // Use unordered_map to reduce overhead
//         std::unordered_map<std::string, std::vector<double>> metricValues;
//         metricValues.reserve(64); // expected metric count

//         std::vector<double> priceMovements;
//         std::vector<double> timeWeights;
//         priceMovements.reserve(n);
//         timeWeights.reserve(n);

//         // Precompute keys
//         std::vector<std::string> levelKeys;
//         levelKeys.reserve(params_.max_level_depth);
//         for (size_t level = 1; level <= params_.max_level_depth; ++level) {
//             levelKeys.push_back("level_" + std::to_string(level) + "_imbalance");
//         }

//         auto currentTime = std::chrono::system_clock::now();

//         // CAP for metrics to avoid runaway memory
//         const size_t HARD_METRIC_CAP = 64;

//         for (const auto& dp : snapshotCopy) {

//             priceMovements.push_back(dp.price_movement.percent_change);

//             auto timeDiff = std::chrono::duration_cast<std::chrono::seconds>(
//                 currentTime - dp.timestamp).count();
//             double timeWeight = std::exp(-params_.time_decay_factor * timeDiff / 3600.0);
//             timeWeights.push_back(timeWeight);

//             auto ensureReserve = [&](std::vector<double>& v) {
//                 if (v.capacity() == 0) v.reserve(n);
//             };

//             // Selected metrics (whitelist – you can tune later)
//             auto& v1 = metricValues["bid_ask_ratio"]; ensureReserve(v1); v1.push_back(dp.metrics.bidAskVolumeRatio);
//             auto& v2 = metricValues["volumeImbalance"]; ensureReserve(v2); v2.push_back(dp.metrics.volumeImbalance);
//             auto& v3 = metricValues["weightedVolumeImbalance"]; ensureReserve(v3); v3.push_back(dp.metrics.weightedVolumeImbalance);
//             auto& v4 = metricValues["depthImbalance"]; ensureReserve(v4); v4.push_back(dp.metrics.depthImbalance);
//             auto& v5 = metricValues["pressureImbalance"]; ensureReserve(v5); v5.push_back(dp.metrics.pressureImbalance);

//             // Optional additional metrics (only if under cap)
//             if (metricValues.size() < HARD_METRIC_CAP) {
//                 for (const auto& kv : dp.metrics.metricsMap()) {
//                     auto& v = metricValues[kv.first];
//                     ensureReserve(v);
//                     v.push_back(kv.second);
//                 }
//             }

//             // Level metrics
//             auto levelIt = dp.metrics.timeSeriesMetrics.find("level_imbalances");
//             if (levelIt != dp.metrics.timeSeriesMetrics.end()) {
//                 const auto& levelDataVec = levelIt->second;
//                 for (size_t level = 1; level <= params_.max_level_depth; ++level) {
//                     if (level <= levelDataVec.size()) {
//                         auto& v = metricValues[levelKeys[level - 1]];
//                         ensureReserve(v);
//                         v.push_back(levelDataVec[level - 1]);
//                     }
//                 }
//             }
//         }

//         // Reusable scratch buffers (thread_local avoids reallocs each call)
//         thread_local std::vector<double> scratch1;
//         thread_local std::vector<double> scratch2;
//         scratch1.clear();
//         scratch2.clear();

//         // Use the same associative container type as the member variables so swap() works
//         std::map<std::string, double> newCorrelationCoeffs;
//         std::map<std::string, double> newCorrelationStrengths;

//         for (auto& [metric, values] : metricValues) {
//             if (values.size() != priceMovements.size() || values.size() < params_.min_data_points) {
//                 newCorrelationCoeffs[metric] = 0.0;
//                 continue;
//             }

//             try {
//                 // --- normalize metric ---
//                 scratch1.clear();
//                 scratch1.reserve(values.size());

//                 double sum = 0.0, weightSum = 0.0;
//                 for (size_t i = 0; i < values.size(); ++i) {
//                     sum += values[i] * timeWeights[i];
//                     weightSum += timeWeights[i];
//                 }
//                 double mean = weightSum > 0 ? sum / weightSum : 0.0;

//                 double varSum = 0.0;
//                 for (size_t i = 0; i < values.size(); ++i) {
//                     double d = values[i] - mean;
//                     varSum += d * d * timeWeights[i];
//                 }
//                 double stddev = weightSum > 0 ? std::sqrt(varSum / weightSum) : 1.0;
//                 stddev = std::max(stddev, params_.min_stddev_threshold);

//                 for (double val : values) scratch1.push_back((val - mean) / stddev);

//                 // --- normalize prices ---
//                 scratch2.clear();
//                 scratch2.reserve(priceMovements.size());

//                 double pSum = 0.0, pWeightSum = 0.0;
//                 for (size_t i = 0; i < priceMovements.size(); ++i) {
//                     pSum += priceMovements[i] * timeWeights[i];
//                     pWeightSum += timeWeights[i];
//                 }
//                 double pMean = pWeightSum > 0 ? pSum / pWeightSum : 0.0;

//                 double pVarSum = 0.0;
//                 for (size_t i = 0; i < priceMovements.size(); ++i) {
//                     double d = priceMovements[i] - pMean;
//                     pVarSum += d * d * timeWeights[i];
//                 }
//                 double pStd = pWeightSum > 0 ? std::sqrt(pVarSum / pWeightSum) : 1.0;
//                 pStd = std::max(pStd, params_.min_stddev_threshold);

//                 for (double val : priceMovements) scratch2.push_back((val - pMean) / pStd);

//                 // --- correlation ---
//                 double covariance = 0.0;
//                 for (size_t i = 0; i < scratch1.size(); ++i)
//                     covariance += scratch1[i] * scratch2[i] * timeWeights[i];

//                 double correlation = weightSum > 0 ? covariance / weightSum : 0.0;
//                 correlation = std::clamp(correlation, -1.0, 1.0);

//                 if (correlation_coefficients_.count(metric)) {
//                     correlation = params_.correlation_decay_rate * correlation_coefficients_[metric] +
//                                   (1.0 - params_.correlation_decay_rate) * correlation;
//                 }

//                 newCorrelationCoeffs[metric] = correlation;
//                 newCorrelationStrengths[metric] = std::abs(correlation);
//             }
//             catch (const std::bad_alloc& e) {
//                 LOG_ERROR("bad_alloc on metric {}: {}", metric, e.what());
//                 newCorrelationCoeffs[metric] = 0.0;
//                 newCorrelationStrengths[metric] = 0.0;
//             }
//         }

//         {
//             std::lock_guard<std::mutex> lock(correlation_mutex_);
//             correlation_coefficients_.swap(newCorrelationCoeffs);
//             correlation_strengths_.swap(newCorrelationStrengths);
//         }

//         updateHighestCorrelatedMetrics();
//     }
//     catch (const std::bad_alloc& e) {
//         LOG_ERROR("bad_alloc in calculateCorrelations(): {}", e.what());
//     }
//     catch (const std::exception& e) {
//         LOG_ERROR("Exception in calculateCorrelations(): {}", e.what());
//     }
// }


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
      
        return it->second;
    }   
    
    return 0.0;
}


void MomentumCorrelation::updateHighestCorrelatedMetrics() {
    // Snapshot the correlation maps under lock so we don't iterate over
    // containers that may be concurrently modified by another thread.
    std::map<std::string, double> strengthsSnapshot;
    std::map<std::string, double> coeffsSnapshot;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        strengthsSnapshot = correlation_strengths_;
        coeffsSnapshot = correlation_coefficients_;
    }

    // Sort metrics by correlation strength (work on the local snapshot)
    std::vector<std::pair<std::string, double>> sortedMetrics;
    sortedMetrics.reserve(strengthsSnapshot.size());
    for (const auto& [metric, strength] : strengthsSnapshot) {
        sortedMetrics.push_back({metric, strength});
    }

    // Sort in descending order of correlation strength
    std::sort(sortedMetrics.begin(), sortedMetrics.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Build new highest correlated list from snapshots (no lock held)
    std::vector<std::pair<std::string, double>> newHighest;
    newHighest.reserve(std::min<size_t>(params_.top_metrics_count, sortedMetrics.size()));
    for (size_t i = 0; i < std::min(params_.top_metrics_count, sortedMetrics.size()); ++i) {
        const auto& name = sortedMetrics[i].first;
        double coeff = 0.0;
        auto it = coeffsSnapshot.find(name);
        if (it != coeffsSnapshot.end()) coeff = it->second;
        newHighest.push_back({name, coeff});
    }

    // Commit the update under lock to avoid races on highest_correlated_metrics_
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        highest_correlated_metrics_ = std::move(newHighest);
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
    // Publish lock-free snapshots for fast readers
    try {
        auto newCoeffs = std::make_shared<std::map<std::string,double>>();
        auto newImportance = std::make_shared<std::map<std::string,double>>();
        {
            std::lock_guard<std::mutex> lock(correlation_mutex_);
            *newCoeffs = correlation_coefficients_;
            *newImportance = feature_importance_;
        }
        std::atomic_store(&coeffs_snapshot_, newCoeffs);
        std::atomic_store(&importance_snapshot_, newImportance);
    } catch (...) {
        // swallow snapshot failures
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
 
MarketRegimeType MomentumCorrelation::getCurrentMarketRegime() const {
    std::lock_guard<std::mutex> lock(correlation_mutex_);
    return current_regime_;
}

void MomentumCorrelation::updateMarketRegime() {
    // We need enough data points for reliable regime detection
    if (data_points_.size() < params_.regime_detection_window) {
        current_regime_ = MarketRegimeType::NORMAL;
        return;
    }
    
    // Snapshot recent price movements under lock
    std::vector<double> recentMovements;
    std::vector<double> recentVolumes; // If available
    std::vector<double> recentSpreads; // If available
    
    // Copy snapshot under lock so it is available after the lock scope
    std::vector<DataPoint> snapshot;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        // Create a snapshot which provides index access; avoid using operator[] on the custom container directly
        snapshot = data_points_.snapshot();
    }

    size_t total = snapshot.size();
    size_t startIdx = total > params_.regime_detection_window ? total - params_.regime_detection_window : 0;
    
    for (size_t i = startIdx; i < total; ++i) {
        recentMovements.push_back(snapshot[i].price_movement.percent_change);
        // Add volume and spread data if available in your data structure
        recentVolumes.push_back(snapshot[i].volume_ratio);
        recentSpreads.push_back(snapshot[i].spread_ratio);
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
    
    // Enhanced regime determination with adaptive thresholds and hierarchical logic
    MarketRegimeType detectedRegime = MarketRegimeType::NORMAL;

    // Dynamically compute thresholds based on observed volatility and sample size
    // Baseline volatility used for normalization (0.1% typical tick)
    const double baselineVol = 0.001;
    double volRatio = volatility / (baselineVol + 1e-12);

    // Stress threshold: make it harder to trigger when volatility is low
    double stressThreshold = 0.78; // base
    // If stressIndicator is already high by design, require stronger evidence when vol is low
    if (volatility < baselineVol) stressThreshold = 0.85;

    // Trending thresholds scaled with volRatio and window length
    double trendDirectionalThreshold = std::max(0.002, 0.003 * volRatio); // normalized
    double trendPersistenceThreshold = 0.55; // slightly less strict
    double momentumThreshold = 0.15; // less strict

    // Range thresholds (mean-reverting) - require small net drift but high oscillation
    double rangeDirectionalMax = std::max(0.001, 0.002 * volRatio);
    double rangeOscillationMin = std::max(0.004, 0.008 * (baselineVol / (volatility + 1e-12)) );

    // Quiet threshold scaled to volatility
    double quietVolThreshold = std::max(0.0002, baselineVol * 0.5);
    double quietActivityThreshold = 0.001;

    // 1. Stress (highest priority)
    if (stressIndicator > stressThreshold) {
        detectedRegime = MarketRegimeType::STRESS;
    }
    // 2. Low liquidity (check early as it affects signal quality)
    else if (LowLiquidityRegimeDetector(snapshot)) {
        detectedRegime = MarketRegimeType::LOW_LIQUIDITY;
    }
    // 3. High volatility (scaled)
    else if (volRatio > 3.0) { // volatility more than ~3x baseline
        detectedRegime = MarketRegimeType::HIGH_VOLATILITY;
    }
    // 4. Trending
    else if (directionalMovement > trendDirectionalThreshold && trendPersistence > trendPersistenceThreshold && momentum > momentumThreshold) {
        detectedRegime = MarketRegimeType::TRENDING;
    }
    // 5. Mean reversion / range bound
    else if (directionalMovement < rangeDirectionalMax && oscillation > rangeOscillationMin && trendPersistence < 0.45) {
        detectedRegime = MarketRegimeType::MEAN_REVERTING;
    }
    // 6. Quiet
    else if (volatility < quietVolThreshold && activityLevel < quietActivityThreshold) {
        detectedRegime = MarketRegimeType::QUIET;
    }
    else {
        detectedRegime = MarketRegimeType::NORMAL;
    }
    
    applyRegimeSmoothing(detectedRegime);
    
}

//Helper function to calculate Low liquidity index
double MomentumCorrelation::calculateLiquidityIndex() {
    // Placeholder implementation
    return 0.0;
}

bool MomentumCorrelation::LowLiquidityRegimeDetector(std::vector<DataPoint> snapshot){
    // Additional: Low liquidity detection using orderbook microstructure
    if (snapshot.empty()) return false;
    // Compute low-liquidity indicators from the most recent snapshot
    // Use param-driven level depth (k) and window for depth volatility (M)
    size_t k = std::min<size_t>(params_.max_level_depth, 5);
    size_t M = std::min<size_t>(params_.regime_detection_window, 20);

    // Take the latest snapshot if available
    const OrderBookMetrics* latestOB = nullptr;
    if (!snapshot.empty()) latestOB = &snapshot.back().metrics;

    double relSpread = 0.0;
    double totalDepth = 0.0;
    double thinness = 0.0;
    double depthStd = 0.0;
    double meanDepth = 0.0;

    if (latestOB) {
        double bidP = latestOB->bidPrice;
        double askP = latestOB->askPrice;
        double mid = latestOB->midPrice;
        if (mid <= 0.0) mid = (askP + bidP) / 2.0;
        double spreadNow = latestOB->spread;
        if (spreadNow <= 0.0) spreadNow = askP - bidP;
        if (mid > 0.0) relSpread = spreadNow / mid;

        // depth at top k levels
        double bidDepth = 0.0, askDepth = 0.0;
        if (!latestOB->bidVolumesByLevel.empty()) {
            for (size_t i = 0; i < k && i < latestOB->bidVolumesByLevel.size(); ++i) {
                bidDepth += latestOB->bidVolumesByLevel[i];
            }
        }
        if (!latestOB->askVolumesByLevel.empty()) {
            for (size_t i = 0; i < k && i < latestOB->askVolumesByLevel.size(); ++i) {
                askDepth += latestOB->askVolumesByLevel[i];
            }
        }
        totalDepth = bidDepth + askDepth;

        // thinness: slope of depth across levels (simple finite difference)
        double bidSlope = 0.0, askSlope = 0.0;
        if (latestOB->bidVolumesByLevel.size() >= 2) {
            double first = latestOB->bidVolumesByLevel.front();
            double last = latestOB->bidVolumesByLevel.size() >= k ? latestOB->bidVolumesByLevel[k-1] : latestOB->bidVolumesByLevel.back();
            bidSlope = (last - first) / static_cast<double>(k);
        }
        if (latestOB->askVolumesByLevel.size() >= 2) {
            double first = latestOB->askVolumesByLevel.front();
            double last = latestOB->askVolumesByLevel.size() >= k ? latestOB->askVolumesByLevel[k-1] : latestOB->askVolumesByLevel.back();
            askSlope = (last - first) / static_cast<double>(k);
        }
        thinness = (bidSlope + askSlope) / 2.0;
    }

    // Build recent depth series (total depth over last M snapshots)
    std::deque<double> depthSeries;
    size_t totalSnapshots = snapshot.size();
    size_t startIdx = totalSnapshots > M ? totalSnapshots - M : 0;
    for (size_t i = startIdx; i < totalSnapshots; ++i) {
        const auto& obm = snapshot[i].metrics;
        double b = 0.0, a = 0.0;
        for (size_t j = 0; j < k && j < obm.bidVolumesByLevel.size(); ++j) b += obm.bidVolumesByLevel[j];
        for (size_t j = 0; j < k && j < obm.askVolumesByLevel.size(); ++j) a += obm.askVolumesByLevel[j];
        depthSeries.push_back(b + a);
    }

    if (!depthSeries.empty()) {
        // mean
        double sum = std::accumulate(depthSeries.begin(), depthSeries.end(), 0.0);
        meanDepth = sum / static_cast<double>(depthSeries.size());
        // stddev
        double var = 0.0;
        for (double v : depthSeries) var += (v - meanDepth) * (v - meanDepth);
        depthStd = depthSeries.size() > 1 ? std::sqrt(var / static_cast<double>(depthSeries.size() - 1)) : 0.0;
    }

    // Normalized thinness: relative to mean per-level depth to make threshold scale-free
    double thinnessRel = 0.0;
    if (meanDepth > 0.0) {
        thinnessRel = thinness / (meanDepth / static_cast<double>(std::max<size_t>(1, k)));
    }

    // Thresholds (tunable per-symbol) - example defaults taken from user's guidance
    const double SPREAD_THRESHOLD_REL = 0.0015;   // ~15 bps
    const double DEPTH_THRESHOLD = 300.0;         // e.g., notional-like units (tune per symbol)
    const double THINNESS_THRESHOLD_REL = 0.10;   // thinness less than 10% of average per-level depth
    const double INSTABILITY_THRESHOLD_RATIO = 0.40; // std > 40% of mean depth

    bool lowSpreadLiquidity = relSpread > SPREAD_THRESHOLD_REL;
    bool lowDepth = totalDepth > 0.0 ? (totalDepth < DEPTH_THRESHOLD) : false;
    bool thinBook = thinnessRel > 0.0 ? (thinnessRel < THINNESS_THRESHOLD_REL) : false;
    bool unstableDepth = meanDepth > 0.0 ? (depthStd / meanDepth > INSTABILITY_THRESHOLD_RATIO) : false;

    if (lowSpreadLiquidity || lowDepth || thinBook || unstableDepth) {
        return true;
    } else {
        return false;
    }

    return false;
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

    // Baseline volatility to normalize against. If not configured elsewhere,
    // assume a small baseline (0.1% = 0.001). This keeps the measure scale-free.
    const double baselineVol = 0.001; // 0.1%

    // Helper smooth function: a simple ease (smoothstep-like) to damp extremes.
    auto smooth = [](double x) {
        // clamp first
        x = std::clamp(x, 0.0, 1.0);
        // smoothstep cubic: 3x^2 - 2x^3
        return x * x * (3.0 - 2.0 * x);
    };

    // Compute extreme movement counts and direction changes
    double stdDev = volatility > 0.0 ? volatility : baselineVol;
    int extremeMovements = 0;
    for (double movement : movements) {
        if (std::abs(movement) > 2.0 * stdDev) {
            extremeMovements++;
        }
    }

    int directionChanges = 0;
    for (size_t i = 1; i < movements.size(); ++i) {
        if ((movements[i] > 0 && movements[i-1] < 0) || (movements[i] < 0 && movements[i-1] > 0)) {
            if (std::abs(movements[i]) > stdDev && std::abs(movements[i-1]) > stdDev) {
                directionChanges++;
            }
        }
    }

    // New formulas from user's request
    double volStress = std::min(1.0, volatility / (3.0 * baselineVol));
    double extremeStress = std::min(1.0, static_cast<double>(extremeMovements) / static_cast<double>(movements.size()) * 2.0);
    double whipsawStress = std::min(1.0, static_cast<double>(directionChanges) / static_cast<double>(movements.size()) * 1.5);

    double stressScore = 0.5 * volStress
                       + 0.25 * extremeStress
                       + 0.25 * whipsawStress;

    // Apply smoothing twice as requested
    stressScore = smooth(smooth(stressScore));

    return std::clamp(stressScore, 0.0, 1.0);
}

// Helper method to apply regime smoothing
void MomentumCorrelation::applyRegimeSmoothing(MarketRegimeType& detectedRegime) {
    // Simple smoothing: require 2-3 consecutive detections before switching
    static MarketRegimeType lastDetection = MarketRegimeType::NORMAL;
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
    if (detectedRegime == MarketRegimeType::STRESS) {
        requiredConsecutive = 3;
    }
    // Require less evidence when moving from stress back to normal
    else if (current_regime_ == MarketRegimeType::STRESS && detectedRegime != MarketRegimeType::STRESS) {
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
    // Fast path: read lock-free snapshots published by the updater
    auto coeffsPtr = std::atomic_load(&coeffs_snapshot_);
    auto importancePtr = std::atomic_load(&importance_snapshot_);
    if (!coeffsPtr || !importancePtr || coeffsPtr->empty()) {
        // Not enough data yet
        LOG_DEBUG("No correlation data available for prediction");
        return 0.0;
    }

    double prediction = 0.0;
    double totalWeight = 0.0;

    // Process supporting metrics using snapshots (no lock)
    for (const auto& [metricName, metricValue] : event.supportingMetrics) {
        auto corrIt = coeffsPtr->find(metricName);
        auto impIt = importancePtr->find(metricName);
        
        if (corrIt == coeffsPtr->end() || impIt == importancePtr->end()) {
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
    auto corrIt = coeffsPtr->find(primaryMetricName);
    auto impIt = importancePtr->find(primaryMetricName);
    
    if (corrIt != coeffsPtr->end() && impIt != importancePtr->end()) {
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
    MarketRegimeType regimeSnapshot = MarketRegimeType::NORMAL;
    {
        std::lock_guard<std::mutex> lock(correlation_mutex_);
        regimeSnapshot = current_regime_;
    }

    // Apply market regime adjustments
    double regimeMultiplier = 1.0;
    switch (regimeSnapshot) {
        case MarketRegimeType::HIGH_VOLATILITY: regimeMultiplier = 0.8; break;
        case MarketRegimeType::TRENDING:        regimeMultiplier = 1.2; break;
        case MarketRegimeType::MEAN_REVERTING:     regimeMultiplier = 0.7; break;
        case MarketRegimeType::QUIET:           regimeMultiplier = 0.6; break;
        case MarketRegimeType::NORMAL:      regimeMultiplier = 1.0; break;
        case MarketRegimeType::STRESS:         regimeMultiplier = 0.5; break;
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