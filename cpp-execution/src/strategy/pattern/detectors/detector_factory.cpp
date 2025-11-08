/**
 * @file detector_factory.cpp
 * @brief Implementation of DetectorFactory
 */
#include "strategy/pattern/detectors/detector_factory.hpp"
#include "strategy/pattern/detectors/volume_imbalance_detector.hpp"
#include "strategy/pattern/detectors/spread_imbalance_detector.hpp"
#include "strategy/pattern/detectors/bayesian_detector.hpp"
#include "common/logger.hpp"
#include <algorithm>

namespace trading {
namespace strategy {
namespace detectors {

DetectorFactory::DetectorFactory() {
    // Enable basic detectors + Bayesian ML by default
    enabledTypes_ = {
        ImbalanceType::VOLUME_IMBALANCE,
        ImbalanceType::ABNORMAL_SPREAD,
        ImbalanceType::BAYESIAN_ML  // ✅ Enable Bayesian ML
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
    
    // Bayesian ML detector configuration
    detectorConfigs_[ImbalanceType::BAYESIAN_ML] = {
        {"initial_sigma", 1.0},
        {"beta", 0.081},
        {"min_probability", 0.65},
        {"base_position_size", 0.1},
        {"uncertainty_penalty", 0.5}
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
            
        case ImbalanceType::BAYESIAN_ML:
            detector = createBayesianDetector();
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
        ImbalanceType::ABNORMAL_SPREAD,
        ImbalanceType::BAYESIAN_ML  // ✅ Add Bayesian ML to supported types
    };
}

// Private helper methods
std::unique_ptr<IImbalanceDetector> DetectorFactory::createVolumeDetector() const {
    return std::make_unique<VolumeImbalanceDetector>();
}

std::unique_ptr<IImbalanceDetector> DetectorFactory::createSpreadDetector() const {
    return std::make_unique<SpreadImbalanceDetector>();
}

std::unique_ptr<IImbalanceDetector> DetectorFactory::createBayesianDetector() const {
    // Create Bayesian detector with default configuration
    BayesianDetector::BayesianConfig config;
    
    // Apply any custom configuration from detectorConfigs_
    auto configIt = detectorConfigs_.find(ImbalanceType::BAYESIAN_ML);
    if (configIt != detectorConfigs_.end()) {
        const auto& params = configIt->second;
        
        auto getParam = [&params](const std::string& key, double defaultVal) {
            auto it = params.find(key);
            return (it != params.end()) ? it->second : defaultVal;
        };
        
        config.initialSigma = getParam("initial_sigma", config.initialSigma);
        config.beta = getParam("beta", config.beta);
        config.minProbability = getParam("min_probability", config.minProbability);
        config.basePositionSize = getParam("base_position_size", config.basePositionSize);
        config.uncertaintyPenalty = getParam("uncertainty_penalty", config.uncertaintyPenalty);
    }
    
    return std::make_unique<BayesianDetector>(config);
}

} // namespace detectors
} // namespace strategy
} // namespace trading