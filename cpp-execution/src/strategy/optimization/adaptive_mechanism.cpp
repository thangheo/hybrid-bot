// adaptive_mechanism.cpp
#include "strategy/optimization/adaptive_mechanism.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>

namespace trading {
namespace strategy {

AdaptiveMechanism::AdaptiveMechanism() {
    // Initialize with all adaptation types enabled
    enabledTypes_[AdaptationType::PARAMETER_DRIFT] = true;
    enabledTypes_[AdaptationType::MARKET_REGIME] = true;
    enabledTypes_[AdaptationType::PARAMETER_BLENDING] = true;
    enabledTypes_[AdaptationType::ANTI_FRAGILITY] = true;
    
    // Initialize status
    status_.active = false;
    status_.lastAdaptationTime = std::chrono::system_clock::now();
    status_.adaptationConfidence = 0.0;
    status_.currentRegime = "UNKNOWN";
}
AdaptiveMechanism::AdaptiveMechanism(const AdaptiveMechanismConfig& config)
    : config_(config) {

    // Initialize with all adaptation types enabled
    enabledTypes_[AdaptationType::PARAMETER_DRIFT] = true;
    enabledTypes_[AdaptationType::MARKET_REGIME] = true;
    enabledTypes_[AdaptationType::PARAMETER_BLENDING] = true;
    enabledTypes_[AdaptationType::ANTI_FRAGILITY] = true;
    
    // Initialize status
    status_.active = false;
    status_.lastAdaptationTime = std::chrono::system_clock::now();
    status_.adaptationConfidence = 0.0;
    status_.currentRegime = "UNKNOWN";
    // Initialize base parameters
    // baseParameters_ = config_.baseParameters;
    adaptedParameters_ = baseParameters_;
    // Initialize performance history
}
void AdaptiveMechanism::updatePerformanceMetrics(const PerformanceMetrics& metrics) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Add metrics to history
    performanceHistory_.push_back(metrics);
    
    // Limit history size
    const size_t MAX_HISTORY = 100;
    if (performanceHistory_.size() > MAX_HISTORY) {
        performanceHistory_.erase(performanceHistory_.begin());
    }
}

void AdaptiveMechanism::applyAdaptations() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if adaptation is needed
    if (!isAdaptationNeeded()) {
        return;
    }
    
    // Apply different adaptation strategies based on enabled types
    if (enabledTypes_[AdaptationType::PARAMETER_DRIFT]) {
        applyParameterDrift();
    }
    
    if (enabledTypes_[AdaptationType::MARKET_REGIME]) {
        applyMarketRegimeAdaptation();
    }
    
    if (enabledTypes_[AdaptationType::PARAMETER_BLENDING]) {
        applyParameterBlending();
    }
    
    if (enabledTypes_[AdaptationType::ANTI_FRAGILITY]) {
        applyAntiFragility();
    }
    
    // Update adaptation status
    status_.active = true;
    status_.lastAdaptationTime = std::chrono::system_clock::now();
    
    // Check for failed adaptation
    checkForFailedAdaptation();
    
    // Notify via callback if registered
    if (adaptationCallback_) {
        adaptationCallback_(adaptedParameters_);
    }
}

AdaptationStatus AdaptiveMechanism::getStatus() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return status_;
}

void AdaptiveMechanism::configureAdaptationRules(const AdaptationRules& rules) {
    std::lock_guard<std::mutex> lock(mutex_);
    rules_ = rules;
    
    // Configure parameter bounds based on rules
    for (const auto& rule : rules.parameterRules) {
        parameterBounds_[rule.parameterName] = std::make_pair(rule.minValue, rule.maxValue);
    }
}

void AdaptiveMechanism::enableAdaptationType(AdaptationType type, bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    enabledTypes_[type] = enable;
}

void AdaptiveMechanism::setBaseParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    baseParameters_ = params;
    // Initialize adapted parameters with base parameters
    adaptedParameters_ = params;
    
    // Reset adaptation status
    status_.parameterChanges.clear();
    status_.adaptationConfidence = 0.0;
}

ParameterSet AdaptiveMechanism::getAdaptedParameters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return adaptedParameters_;
}
// getAdjustmentRecommendations
std::map<std::string, double> AdaptiveMechanism::getAdjustmentRecommendations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Return the parameter changes made during adaptation
    return status_.parameterChanges;
}
void AdaptiveMechanism::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Reset adapted parameters to base parameters
    adaptedParameters_ = baseParameters_;
    
    // Clear performance history
    performanceHistory_.clear();
    
    // Reset adaptation status
    status_.active = false;
    status_.lastAdaptationTime = std::chrono::system_clock::now();
    status_.parameterChanges.clear();
    status_.adaptationConfidence = 0.0;
    status_.currentRegime = "UNKNOWN";
}

void AdaptiveMechanism::registerAdaptationCallback(AdaptationCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    adaptationCallback_ = callback;
}

std::string AdaptiveMechanism::detectMarketRegime(const core::MarketState& state) const {
    // Simple market regime detection based on volatility and trend
    
    // Check volatility level
    std::string volatilityRegime;
    if (state.volatility < 0.5) {
        volatilityRegime = "LOW_VOL";
    } else if (state.volatility < 1.5) {
        volatilityRegime = "MED_VOL";
    } else {
        volatilityRegime = "HIGH_VOL";
    }
    
    // Check trend direction and strength
    std::string trendRegime;
    if (std::abs(state.trendStrength) < 0.3) {
        trendRegime = "SIDEWAYS";
    } else if (state.trendStrength > 0) {
        trendRegime = "UPTREND";
    } else {
        trendRegime = "DOWNTREND";
    }
    
    // Combine regimes
    return volatilityRegime + "_" + trendRegime;
}
void AdaptiveMechanism::updateSignalResult(const std::string& signalId, const SignalExecutionResult& result) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update performance metrics based on signal result
    if (result.status == SignalExecutionStatus::COMPLETED) {
        status_.adaptationConfidence += result.pnl;  // Example of using PnL as confidence
    } else if (result.status == SignalExecutionStatus::FAILED) {
        status_.adaptationConfidence -= result.pnl;  // Penalize for failures
    }
    
    // Update current regime based on market state
    status_.currentRegime = detectMarketRegime(result.marketState);
}

void AdaptiveMechanism::applyParameterDrift() {
    // Apply gradual parameter changes based on performance gradient
    
    // Check if we have enough history
    if (performanceHistory_.size() < 2) {
        return;
    }
    
    // Calculate recent performance trends
    double recentSharpeAvg = 0.0;
    double olderSharpeAvg = 0.0;
    
    size_t halfPoint = performanceHistory_.size() / 2;
    
    for (size_t i = halfPoint; i < performanceHistory_.size(); ++i) {
        recentSharpeAvg += performanceHistory_[i].sharpeRatio;
    }
    
    for (size_t i = 0; i < halfPoint; ++i) {
        olderSharpeAvg += performanceHistory_[i].sharpeRatio;
    }
    
    recentSharpeAvg /= (performanceHistory_.size() - halfPoint);
    olderSharpeAvg /= halfPoint;
    
    // Determine drift direction based on performance trend
    double performanceTrend = recentSharpeAvg - olderSharpeAvg;
    
    // Get performance attribution to determine which parameters to adjust
    std::map<std::string, double> attribution = attributePerformance();
    
    // Apply drift to parameters
    for (auto it = adaptedParameters_.numeric_params.begin(); it != adaptedParameters_.numeric_params.end(); ++it) {
        // Check if we have attribution for this parameter
        if (attribution.find(it->first) != attribution.end()) {
            // Drift rate depends on performance attribution
            double driftRate = 0.01 * attribution[it->first] * performanceTrend;
            
            // Apply drift
            double oldValue = it->second;
            it->second *= (1.0 + driftRate);
            
            // Ensure parameter stays within bounds
            if (parameterBounds_.find(it->first) != parameterBounds_.end()) {
                it->second = std::max(parameterBounds_[it->first].first, 
                                      std::min(parameterBounds_[it->first].second, it->second));
            }
            
            // Record change
            status_.parameterChanges[it->first] = it->second - oldValue;
        }
    }
}

void AdaptiveMechanism::applyMarketRegimeAdaptation() {
    // Adapt parameters based on detected market regime
    
    // Check if we have market regime-specific rules
    bool hasRegimeRules = false;
    for (const auto& rule : rules_.regimeRules) {
        if (rule.regime == status_.currentRegime) {
            hasRegimeRules = true;
            break;
        }
    }
    
    if (!hasRegimeRules) {
        return;  // No rules for current regime
    }
    
    // Apply regime-specific parameter adjustments
    for (const auto& rule : rules_.regimeRules) {
        if (rule.regime == status_.currentRegime) {
            // Apply parameter adjustments for this regime
            for (const auto& paramAdjustment : rule.parameterAdjustments) {
                std::string paramName = paramAdjustment.first;
                double adjustmentFactor = paramAdjustment.second;
                
                auto it = adaptedParameters_.numeric_params.find(paramName);
                if (it != adaptedParameters_.numeric_params.end()) {
                    double oldValue = it->second;
                    
                    // Apply adjustment
                    auto baseIt = baseParameters_.numeric_params.find(paramName);
                    if (baseIt != baseParameters_.numeric_params.end()) {
                        it->second = baseIt->second * adjustmentFactor;
                        
                        // Ensure parameter stays within bounds
                        if (parameterBounds_.find(paramName) != parameterBounds_.end()) {
                            it->second = std::max(parameterBounds_[paramName].first, 
                                                 std::min(parameterBounds_[paramName].second, 
                                                        it->second));
                        }
                        
                        // Record change
                        status_.parameterChanges[paramName] = it->second - oldValue;
                    }
                }
            }
        }
    }
}

void AdaptiveMechanism::applyParameterBlending() {
    // Blend parameters from different sources based on performance
    
    // This is a simplified implementation - in a real system, this would 
    // blend parameters from multiple sources (e.g., different optimization runs)
    
    // Check if we have blending rules
    if (rules_.blendingSources.empty()) {
        return;
    }
    
    // Calculate blending weights based on recent performance
    std::map<std::string, double> blendingWeights;
    double totalWeight = 0.0;
    
    for (const auto& source : rules_.blendingSources) {
        // In a real implementation, we would evaluate each source's recent performance
        // Here we'll use a simplified approach with random weights
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<> dist(0.1, 1.0);
        
        double weight = dist(gen);
        blendingWeights[source.first] = weight;
        totalWeight += weight;
    }
    
    // Normalize weights
    for (auto& weight : blendingWeights) {
        weight.second /= totalWeight;
    }
    
    // Apply blending to numeric parameters
    for (auto& param : adaptedParameters_.numeric_params) {
        double blendedValue = 0.0;
        
        for (const auto& source : rules_.blendingSources) {
            // Get parameter value from this source
            double sourceValue = 0.0;
            auto sourceParamIt = source.second.find(param.first);
            if (sourceParamIt != source.second.end()) {
                sourceValue = sourceParamIt->second;  // Directly use the value if it's already numeric
            } else {
                auto baseIt = baseParameters_.numeric_params.find(param.first);
                sourceValue = (baseIt != baseParameters_.numeric_params.end()) ? 
                              baseIt->second : 0.0;  // Fallback to base parameter
            }
            
            // Add weighted value
            blendedValue += sourceValue * blendingWeights[source.first];
        }
        
        // Record change
        double oldValue = param.second;
        param.second = blendedValue;
        
        // Ensure parameter stays within bounds
        if (parameterBounds_.find(param.first) != parameterBounds_.end()) {
            param.second = std::max(parameterBounds_[param.first].first, 
                                   std::min(parameterBounds_[param.first].second, 
                                          param.second));
        }
        
        status_.parameterChanges[param.first] = param.second - oldValue;
    }
}

void AdaptiveMechanism::applyAntiFragility() {
    // Apply anti-fragility adjustments to make the system more robust
    
    // This is a simplified implementation - in a real system, this would be more sophisticated
    
    // Calculate recent volatility of performance
    if (performanceHistory_.size() < 5) {
        return;  // Not enough history
    }
    
    double meanSharpe = 0.0;
    for (const auto& perf : performanceHistory_) {
        meanSharpe += perf.sharpeRatio;
    }
    meanSharpe /= performanceHistory_.size();
    
    double varianceSharpe = 0.0;
    for (const auto& perf : performanceHistory_) {
        double diff = perf.sharpeRatio - meanSharpe;
        varianceSharpe += diff * diff;
    }
    varianceSharpe /= performanceHistory_.size();
    
    double stdDevSharpe = std::sqrt(varianceSharpe);
    
    // Adjust parameters to reduce sensitivity to volatile conditions
    for (auto& param : adaptedParameters_.numeric_params) {
        // Check if this parameter affects sensitivity
        bool isSensitivityParameter = false;
        for (const auto& rule : rules_.antifragilityRules) {
            if (rule.parameterName == param.first) {
                isSensitivityParameter = true;
                break;
            }
        }
        
        if (isSensitivityParameter) {
            // Record old value
            double oldValue = param.second;
            
            // Adjust parameter based on performance volatility
            // Higher volatility -> reduce sensitivity
            double adjustmentFactor = 1.0 / (1.0 + stdDevSharpe);
            param.second *= adjustmentFactor;
            
            // Ensure parameter stays within bounds
            if (parameterBounds_.find(param.first) != parameterBounds_.end()) {
                param.second = std::max(parameterBounds_[param.first].first, 
                                      std::min(parameterBounds_[param.first].second, param.second));
            }
            
            // Record change
            status_.parameterChanges[param.first] = param.second - oldValue;
        }
    }
}

std::map<std::string, double> AdaptiveMechanism::attributePerformance() const {
    // Determine which parameters contributed to performance
    // This is a simplified implementation - in a real system, this would use 
    // more sophisticated methods like sensitivity analysis
    
    std::map<std::string, double> attribution;
    
    // Assign random attribution for demonstration
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dist(0.0, 1.0);
    
    double totalAttribution = 0.0;
    
    for (const auto& param : adaptedParameters_.numeric_params) {
        double randAttribution = dist(gen);
        attribution[param.first] = randAttribution;
        totalAttribution += randAttribution;
    }
    
    // Normalize attribution
    for (auto& attr : attribution) {
        attr.second /= totalAttribution;
    }
    
    return attribution;
}

bool AdaptiveMechanism::isAdaptationNeeded() const {
    // Check if adaptation is needed based on performance and time
    
    // If no performance history, no adaptation needed
    if (performanceHistory_.empty()) {
        return false;
    }
    
    // Check time since last adaptation
    auto now = std::chrono::system_clock::now();
    auto timeSinceLastAdaptation = std::chrono::duration_cast<std::chrono::hours>(
        now - status_.lastAdaptationTime).count();
    
    // Minimum time between adaptations: 24 hours
    if (timeSinceLastAdaptation < 24 && status_.active) {
        return false;
    }
    
    // Check for performance deterioration
    if (performanceHistory_.size() >= 10) {
        // Calculate recent vs. earlier performance
        double recentPerf = 0.0;
        double earlierPerf = 0.0;
        
        size_t halfPoint = performanceHistory_.size() / 2;
        
        for (size_t i = halfPoint; i < performanceHistory_.size(); ++i) {
            recentPerf += performanceHistory_[i].sharpeRatio;
        }
        
        for (size_t i = 0; i < halfPoint; ++i) {
            earlierPerf += performanceHistory_[i].sharpeRatio;
        }
        
        recentPerf /= (performanceHistory_.size() - halfPoint);
        earlierPerf /= halfPoint;
        
        // If recent performance is significantly worse, adaptation is needed
        if (recentPerf < earlierPerf * 0.8) {
            return true;
        }
    }
    
    // Otherwise, periodically adapt based on time
    return timeSinceLastAdaptation >= 168;  // Weekly adaptation
}

void AdaptiveMechanism::checkForFailedAdaptation() {
    // Check if the most recent adaptation made things worse
    
    // Need at least some history after adaptation
    if (performanceHistory_.size() < 5) {
        return;
    }
    
    // Calculate performance before and after most recent adaptation
    auto now = std::chrono::system_clock::now();
    auto timeSinceLastAdaptation = std::chrono::duration_cast<std::chrono::hours>(
        now - status_.lastAdaptationTime).count();
    
    // Need some time to evaluate adaptation
    if (timeSinceLastAdaptation < 12) {
        return;
    }
    
    // Find performance samples before and after adaptation
    std::vector<double> beforeSharpe;
    std::vector<double> afterSharpe;
    
    for (const auto& perf : performanceHistory_) {
        // Convert timestamp to time_point
        // auto perfTime = std::chrono::system_clock::from_time_t(perf.timestamp);
        //  std::chrono::system_clock::time_point timestamp; // Timestamp of the metrics
        auto perfTime = perf.timestamp;
        if (perfTime < status_.lastAdaptationTime) {
            beforeSharpe.push_back(perf.sharpeRatio);
        } else {
            afterSharpe.push_back(perf.sharpeRatio);
        }
    }
    
    // Need enough samples to compare
    if (beforeSharpe.size() < 3 || afterSharpe.size() < 3) {
        return;
    }
    
    // Calculate average performance
    double avgBefore = 0.0;
    for (double sharpe : beforeSharpe) {
        avgBefore += sharpe;
    }
    avgBefore /= beforeSharpe.size();
    
    double avgAfter = 0.0;
    for (double sharpe : afterSharpe) {
        avgAfter += sharpe;
    }
    avgAfter /= afterSharpe.size();
    
    // Check if performance deteriorated significantly
    if (avgAfter < avgBefore * 0.8) {
        // Adaptation failed, revert to safer parameters
        revertToSafeParameters();
    }
}

void AdaptiveMechanism::revertToSafeParameters() {
    // Revert to safer parameters if adaptation failed
    
    // Simple approach: revert halfway back to base parameters
    for (auto& param : adaptedParameters_.numeric_params) {
        auto baseIt = baseParameters_.numeric_params.find(param.first);
        if (baseIt != baseParameters_.numeric_params.end()) {
            param.second = (param.second + baseIt->second) / 2.0;
        }
    }
    
    // Update status
    status_.parameterChanges.clear();
    status_.adaptationConfidence = 0.5;  // Reduced confidence
    
    // Notify via callback if registered
    if (adaptationCallback_) {
        adaptationCallback_(adaptedParameters_);
    }
}

} // namespace strategy
} // namespace trading