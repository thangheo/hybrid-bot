/**
 * @file spread_imbalance_detector.cpp
 * @brief Implementation of SpreadImbalanceDetector
 */
#include "strategy/pattern/detectors/spread_imbalance_detector.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <cmath>

namespace trading {
namespace strategy {
namespace detectors {

SpreadImbalanceDetector::SpreadImbalanceDetector()
    : AbstractDetector("SpreadImbalanceDetector", ImbalanceType::ABNORMAL_SPREAD) {
}

void SpreadImbalanceDetector::updateConfig(const std::map<std::string, double>& params) {
    auto it = params.find("min_strength_threshold");
    if (it != params.end() && it->second >= 0.0) {
        minStrengthThreshold_ = it->second;
    }
    
    it = params.find("volume_bias_threshold");
    if (it != params.end() && it->second > 1.0) {
        volumeBiasThreshold_ = it->second;
    }
}

DetectorAnalysisResult SpreadImbalanceDetector::detectImpl(const DetectorContext& context) {
    DetectorAnalysisResult result;
    result.type = ImbalanceType::ABNORMAL_SPREAD;
    
    // Calculate spread ratio
    double spreadRatio = calculateSpreadRatio(*context.metrics);
    if (spreadRatio <= 0.0 || !std::isfinite(spreadRatio)) {
        return result; // Invalid ratio
    }
    
    // Get threshold
    double threshold = 1.0;
    if (context.thresholds) {
        threshold = context.thresholds->getTypeThreshold(ImbalanceType::ABNORMAL_SPREAD)
                                     .activation_threshold;
    }
    
    // Calculate strength using original calculateImbalanceStrength logic
    double strength;
    if (spreadRatio > threshold) {
        strength = (spreadRatio - threshold) / threshold;
    } else if (spreadRatio < 1.0 / threshold) {
        strength = (1.0/threshold - spreadRatio) / (1.0/threshold);
    } else {
        strength = 0.0;
    }
    
    // Clamp strength to [0,1] range
    strength = std::clamp(strength, 0.0, 1.0);
    
    if (strength <= 0.0) {
        return result; // Below detection threshold
    }
    
    // Build result
    result.hasSignal = true;
    result.confidence = std::min(strength / minStrengthThreshold_, 1.0);
    result.strength = strength;
    result.direction = determineDirection(*context.metrics);
    result.rawValue = spreadRatio;
    result.reason = "Abnormal spread detected: spread ratio = " + std::to_string(spreadRatio);
    
    return result;
}

bool SpreadImbalanceDetector::validateContext(const DetectorContext& context) const {
    if (!AbstractDetector::validateContext(context)) {
        return false;
    }
    
    // Check for required metrics
    const auto& metrics = context.metrics->metrics;
    if (metrics.find("spreadPercent") == metrics.end() || 
        metrics.find("averageSpreadPercent") == metrics.end()) {
        return false;
    }
    
    return true;
}

double SpreadImbalanceDetector::calculateSpreadRatio(const OrderBookMetrics& metrics) const {
    auto spreadIt = metrics.metrics.find("spreadPercent");
    auto avgSpreadIt = metrics.metrics.find("averageSpreadPercent");
    
    if (spreadIt == metrics.metrics.end() || avgSpreadIt == metrics.metrics.end()) {
        return 0.0;
    }
    
    double currentSpread = spreadIt->second;
    double averageSpread = std::max(avgSpreadIt->second, 1e-10); // Avoid division by zero
    
    return currentSpread / averageSpread;
}

Direction SpreadImbalanceDetector::determineDirection(const OrderBookMetrics& metrics) const {
    auto bidVolumeIt = metrics.metrics.find("bidVolume");
    auto askVolumeIt = metrics.metrics.find("askVolume");
    
    if (bidVolumeIt == metrics.metrics.end() || askVolumeIt == metrics.metrics.end()) {
        return Direction::NEUTRAL;
    }
    
    double bidVolume = bidVolumeIt->second;
    double askVolume = askVolumeIt->second;
    
    if (bidVolume > askVolume * volumeBiasThreshold_) {
        return Direction::BUY;
    } else if (askVolume > bidVolume * volumeBiasThreshold_) {
        return Direction::SELL;
    } else {
        return Direction::NEUTRAL;
    }
}

} // namespace detectors
} // namespace strategy
} // namespace trading