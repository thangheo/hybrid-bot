/**
 * @file detector_factory.hpp
 * @brief Factory for creating and managing imbalance detectors
 */
#pragma once

#include "strategy/pattern/detectors/imbalance_detector_interface.hpp"
#include "strategy/types.hpp"
#include <memory>
#include <vector>
#include <map>
#include <string>

namespace trading {
namespace strategy {
namespace detectors {

/**
 * @class DetectorFactory
 * @brief Factory for creating and managing detector instances
 * 
 * Provides centralized creation and configuration of all detector types.
 * Supports runtime configuration and lifecycle management.
 */
class DetectorFactory {
public:
    DetectorFactory();
    ~DetectorFactory() = default;
    
    /**
     * @brief Create a detector of the specified type
     */
    std::unique_ptr<IImbalanceDetector> createDetector(ImbalanceType type) const;
    
    /**
     * @brief Create all enabled detector types
     */
    std::vector<std::unique_ptr<IImbalanceDetector>> createAllDetectors() const;
    
    /**
     * @brief Configure which detector types are enabled
     */
    void setEnabledDetectors(const std::vector<ImbalanceType>& types);
    
    /**
     * @brief Check if a detector type is enabled
     */
    bool isDetectorEnabled(ImbalanceType type) const;
    
    /**
     * @brief Update configuration for all detectors of a type
     */
    void updateDetectorConfig(ImbalanceType type, const std::map<std::string, double>& params);
    
    /**
     * @brief Get list of all supported detector types
     */
    std::vector<ImbalanceType> getSupportedTypes() const;
    
private:
    std::vector<ImbalanceType> enabledTypes_;
    std::map<ImbalanceType, std::map<std::string, double>> detectorConfigs_;
    
    // Helper to create specific detector instances
    std::unique_ptr<IImbalanceDetector> createVolumeDetector() const;
    std::unique_ptr<IImbalanceDetector> createSpreadDetector() const;
    std::unique_ptr<IImbalanceDetector> createBayesianDetector() const;
    // Add more create methods as we extract more detectors
};

} // namespace detectors
} // namespace strategy
} // namespace trading