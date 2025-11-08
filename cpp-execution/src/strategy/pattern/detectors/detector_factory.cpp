/**
 * @file detector_factory.cpp
 * @brief Implementation of DetectorFactory
 */
#include "strategy/pattern/detectors/detector_factory.hpp"
#include "strategy/pattern/detectors/volume_imbalance_detector.hpp"
#include "strategy/pattern/detectors/spread_imbalance_detector.hpp"
#include "common/logger.hpp"
#include <algorithm>

namespace trading {
namespace strategy {
namespace detectors {

DetectorFactory::DetectorFactory() {
    // Enable basic detectors by default
    enabledTypes_ = {
        ImbalanceType::VOLUME_IMBALANCE,
        ImbalanceType::ABNORMAL_SPREAD
    };
    
    // Initialize default configurations
    detectorConfigs_[ImbalanceType::VOLUME_IMBALANCE] = {
        {"ema_blend_alpha", 0.7},
        {"min_change_threshold", 0.01},
        {"min_strength_threshold", 0.2}
    };
    
    detectorConfigs_[ImbalanceType::ABNORMAL_SPREAD] = {
        {"min_strength_threshold", 0.2},
        {"volume_bias_threshold", 1.2}
    };
}

std::unique_ptr<IImbalanceDetector> DetectorFactory::createDetector(ImbalanceType type) const {
    if (!isDetectorEnabled(type)) {
        return nullptr;
    }
    
    std::unique_ptr<IImbalanceDetector> detector;
    
    switch (type) {
        case ImbalanceType::VOLUME_IMBALANCE:
            detector = createVolumeDetector();
            break;
            
        case ImbalanceType::ABNORMAL_SPREAD:
            detector = createSpreadDetector();
            break;
            
        default:
            LOG_WARN("DetectorFactory: Unsupported detector type requested: {}", static_cast<int>(type));
            return nullptr;
    }
    
    // Apply configuration if detector was created
    if (detector) {
        auto configIt = detectorConfigs_.find(type);
        if (configIt != detectorConfigs_.end()) {
            detector->updateConfig(configIt->second);
        }
    }
    
    return detector;
}

std::vector<std::unique_ptr<IImbalanceDetector>> DetectorFactory::createAllDetectors() const {
    std::vector<std::unique_ptr<IImbalanceDetector>> detectors;
    
    for (ImbalanceType type : enabledTypes_) {
        auto detector = createDetector(type);
        if (detector) {
            detectors.push_back(std::move(detector));
        }
    }
    
    LOG_INFO("DetectorFactory: Created {} detectors", detectors.size());
    return detectors;
}

void DetectorFactory::setEnabledDetectors(const std::vector<ImbalanceType>& types) {
    enabledTypes_ = types;
    LOG_INFO("DetectorFactory: Enabled {} detector types", types.size());
}

bool DetectorFactory::isDetectorEnabled(ImbalanceType type) const {
    return std::find(enabledTypes_.begin(), enabledTypes_.end(), type) != enabledTypes_.end();
}

void DetectorFactory::updateDetectorConfig(ImbalanceType type, const std::map<std::string, double>& params) {
    detectorConfigs_[type] = params;
    LOG_INFO("DetectorFactory: Updated config for detector type {}", static_cast<int>(type));
}

std::vector<ImbalanceType> DetectorFactory::getSupportedTypes() const {
    return {
        ImbalanceType::VOLUME_IMBALANCE,
        ImbalanceType::ABNORMAL_SPREAD
        // Add more types as we implement them
    };
}

// Private helper methods
std::unique_ptr<IImbalanceDetector> DetectorFactory::createVolumeDetector() const {
    return std::make_unique<VolumeImbalanceDetector>();
}

std::unique_ptr<IImbalanceDetector> DetectorFactory::createSpreadDetector() const {
    return std::make_unique<SpreadImbalanceDetector>();
}

} // namespace detectors
} // namespace strategy
} // namespace trading