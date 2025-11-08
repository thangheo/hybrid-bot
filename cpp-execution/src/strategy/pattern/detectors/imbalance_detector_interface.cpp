/**
 * @file imbalance_detector_interface.cpp
 * @brief Implementation of base detector functionality
 */
#include "strategy/pattern/detectors/imbalance_detector_interface.hpp"
#include "common/logger.hpp"
#include "strategy/types.hpp"
#include <algorithm>
#include <cmath>

namespace trading {
namespace strategy {
namespace detectors {

AbstractDetector::AbstractDetector(const std::string& name, ImbalanceType type, bool enabled)
    : name_(name), detectorType_(type), enabled_(enabled) {
}

DetectorAnalysisResult AbstractDetector::analyze(const DetectorContext& context) {
    ++totalAnalyses_;
    
    // Early return if disabled
    if (!enabled_) {
        return DetectorAnalysisResult{}; // Default empty result
    }
    
    // Validate context
    if (!validateContext(context)) {
        LOG_WARN("Detector {}: Invalid context provided", name_);
        return DetectorAnalysisResult{};
    }
    
    try {
        // Call subclass implementation
        auto result = detectImpl(context);
        
        // Validate result
        if (!result.isValid()) {
            LOG_WARN("Detector {}: Invalid result from detectImpl", name_);
            return DetectorAnalysisResult{};
        }
        
        // Post-process result
        postProcessResult(result, context);
        
        // Update statistics
        if (result.hasSignal) {
            ++successfulDetections_;
        }
        
        return result;
        
    } catch (const std::exception& ex) {
        LOG_ERROR("Detector {} analysis failed: {}", name_, ex.what());
        return DetectorAnalysisResult{};
    } catch (...) {
        LOG_ERROR("Detector {} analysis failed with unknown exception", name_);
        return DetectorAnalysisResult{};
    }
}

bool AbstractDetector::validateContext(const DetectorContext& context) const {
    if (!context.isValid()) {
        return false;
    }
    
    // Additional validation
    if (context.currentVolatility < 0.0 || !std::isfinite(context.currentVolatility)) {
        return false;
    }
    
    if (context.currentRegime < 0) {
        return false;
    }
    
    return true;
}

void AbstractDetector::postProcessResult(DetectorAnalysisResult& result, const DetectorContext& context) const {
    // Clamp confidence to valid range
    result.confidence = clampConfidence(result.confidence);
    
    // Ensure strength is non-negative
    result.strength = std::max(0.0, result.strength);
    
    // Set detector type if not already set
    if (result.hasSignal && result.type == ImbalanceType::VOLUME_IMBALANCE && 
        detectorType_ != ImbalanceType::VOLUME_IMBALANCE) {
        result.type = detectorType_;
    }
    
    // Add context to reason if signal detected
    if (result.hasSignal && result.reason.empty()) {
        result.reason = "Signal detected by " + name_;
    }
}

bool AbstractDetector::isValidThreshold(double value) const {
    return std::isfinite(value) && value >= 0.0;
}

double AbstractDetector::clampConfidence(double confidence) const {
    if (!std::isfinite(confidence)) {
        return 0.0;
    }
    return std::clamp(confidence, 0.0, 1.0);
}

} // namespace detectors
} // namespace strategy  
} // namespace trading