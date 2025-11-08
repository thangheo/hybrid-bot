/**
 * @file volume_imbalance_detector.cpp
 * @brief Implementation of VolumeImbalanceDetector
 */
#include "strategy/pattern/detectors/volume_imbalance_detector.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <cmath>

namespace trading {
namespace strategy {
namespace detectors {

VolumeImbalanceDetector::VolumeImbalanceDetector()
    : AbstractDetector("VolumeImbalanceDetector", ImbalanceType::VOLUME_IMBALANCE) {
}

void VolumeImbalanceDetector::updateConfig(const std::map<std::string, double>& params) {
    auto it = params.find("ema_blend_alpha");
    if (it != params.end() && it->second > 0.0 && it->second <= 1.0) {
        emaBlendAlpha_ = it->second;
    }
    
    it = params.find("min_change_threshold");
    if (it != params.end() && it->second >= 0.0) {
        minChangeThreshold_ = it->second;
    }
    
    it = params.find("min_strength_threshold");
    if (it != params.end() && it->second >= 0.0) {
        minStrengthThreshold_ = it->second;
    }
}

DetectorAnalysisResult VolumeImbalanceDetector::detectImpl(const DetectorContext& context) {
    DetectorAnalysisResult result;
    result.type = ImbalanceType::VOLUME_IMBALANCE;
    
    // Calculate volume ratio
    double volumeRatio = calculateVolumeRatio(*context.metrics);
    if (volumeRatio <= 0.0 || !std::isfinite(volumeRatio)) {
        return result; // Invalid ratio
    }
    
    // Apply EMA smoothing
    volumeRatio = applyEmaSmoothing(volumeRatio, *context.metrics);
    
    // Check for significant change to avoid spam
    if (!hasSignificantChange(volumeRatio, context.metrics->exchangeId, context.metrics->symbol)) {
        return result; // No significant change
    }
    
    // Get threshold
    double threshold = 1.0;
    if (context.thresholds) {
        threshold = context.thresholds->getTypeThreshold(ImbalanceType::VOLUME_IMBALANCE)
                                     .activation_threshold;
    }
    
    // Calculate strength and direction
    double strength = calculateStrength(volumeRatio, threshold);
    if (strength <= 0.0) {
        return result; // Below detection threshold
    }
    
    // Build result
    result.hasSignal = true;
    result.confidence = std::min(strength / minStrengthThreshold_, 1.0);
    result.strength = strength;
    result.direction = (volumeRatio > 1.0) ? Direction::BUY : Direction::SELL;
    result.rawValue = volumeRatio;
    result.reason = "Volume imbalance detected: bid/ask ratio = " + std::to_string(volumeRatio);
    
    return result;
}

bool VolumeImbalanceDetector::validateContext(const DetectorContext& context) const {
    if (!AbstractDetector::validateContext(context)) {
        return false;
    }
    
    // Check for required metrics
    const auto& metrics = context.metrics->metrics;
    if (metrics.find("bidVolume") == metrics.end() || 
        metrics.find("askVolume") == metrics.end()) {
        return false;
    }
    
    return true;
}

double VolumeImbalanceDetector::calculateVolumeRatio(const OrderBookMetrics& metrics) const {
    auto bidVolIt = metrics.metrics.find("bidVolume");
    auto askVolIt = metrics.metrics.find("askVolume");
    
    if (bidVolIt == metrics.metrics.end() || askVolIt == metrics.metrics.end()) {
        return 0.0;
    }
    
    double bidVolume = bidVolIt->second;
    double askVolume = std::max(askVolIt->second, 1e-10); // Avoid division by zero
    
    return bidVolume / askVolume;
}

double VolumeImbalanceDetector::applyEmaSmoothing(double rawRatio, const OrderBookMetrics& metrics) const {
    auto emaIt = metrics.metrics.find("emaVolumeImbalance");
    if (emaIt == metrics.metrics.end()) {
        return rawRatio; // No EMA data available
    }
    
    double emaVal = emaIt->second;
    if (!std::isfinite(emaVal) || emaVal <= 0.0) {
        return rawRatio; // Invalid EMA data
    }
    
    return emaBlendAlpha_ * emaVal + (1.0 - emaBlendAlpha_) * rawRatio;
}

double VolumeImbalanceDetector::calculateStrength(double ratio, double threshold) const {
    if (threshold <= 0.0 || !std::isfinite(threshold) || !std::isfinite(ratio)) {
        return 0.0;
    }
    
    // Match original calculateImbalanceStrength logic
    double strength;
    if (ratio > threshold) {
        strength = (ratio - threshold) / threshold;
    } else if (ratio < 1.0 / threshold) {
        strength = (1.0/threshold - ratio) / (1.0/threshold);
    } else {
        strength = 0.0;
    }
    
    // Clamp to [0,1] range
    return std::clamp(strength, 0.0, 1.0);
}

bool VolumeImbalanceDetector::hasSignificantChange(double currentRatio, 
                                                  const core::ExchangeId& exchange,
                                                  const core::Symbol& symbol) {
    std::pair<core::ExchangeId, core::Symbol> key{exchange, symbol};
    
    std::lock_guard<std::mutex> lock(volumeRatioMutex_);
    
    auto it = lastVolumeRatio_.find(key);
    if (it == lastVolumeRatio_.end()) {
        // First time seeing this symbol
        lastVolumeRatio_[key] = currentRatio;
        return true;
    }
    
    double lastRatio = it->second;
    lastVolumeRatio_[key] = currentRatio; // Update for next time
    
    // Calculate logarithmic change
    if (lastRatio <= 0.0 || !std::isfinite(lastRatio) ||
        currentRatio <= 0.0 || !std::isfinite(currentRatio)) {
        return false;
    }
    
    double change = std::abs(std::log(currentRatio) - std::log(lastRatio));
    return change >= minChangeThreshold_;
}

} // namespace detectors
} // namespace strategy
} // namespace trading