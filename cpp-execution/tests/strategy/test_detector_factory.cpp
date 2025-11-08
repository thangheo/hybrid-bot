/**
 * @file test_detector_factory.cpp
 * @brief Simple test for DetectorFactory
 */
#include "strategy/pattern/detectors/detector_factory.hpp"
#include <iostream>
#include <cassert>

using namespace trading::strategy::detectors;
using namespace trading::strategy;

int main() {
    std::cout << "Testing DetectorFactory..." << std::endl;
    
    // Create factory
    DetectorFactory factory;
    
    // Test supported types
    auto supportedTypes = factory.getSupportedTypes();
    assert(!supportedTypes.empty());
    std::cout << "✓ Factory supports " << supportedTypes.size() << " detector types" << std::endl;
    
    // Test enabled detectors
    assert(factory.isDetectorEnabled(ImbalanceType::VOLUME_IMBALANCE));
    assert(factory.isDetectorEnabled(ImbalanceType::ABNORMAL_SPREAD));
    std::cout << "✓ Default detectors are enabled" << std::endl;
    
    // Test create individual detector
    auto volumeDetector = factory.createDetector(ImbalanceType::VOLUME_IMBALANCE);
    assert(volumeDetector != nullptr);
    assert(volumeDetector->getDetectorType() == ImbalanceType::VOLUME_IMBALANCE);
    assert(volumeDetector->getName() == "VolumeImbalanceDetector");
    std::cout << "✓ Volume detector created successfully" << std::endl;
    
    auto spreadDetector = factory.createDetector(ImbalanceType::ABNORMAL_SPREAD);
    assert(spreadDetector != nullptr);
    assert(spreadDetector->getDetectorType() == ImbalanceType::ABNORMAL_SPREAD);
    assert(spreadDetector->getName() == "SpreadImbalanceDetector");
    std::cout << "✓ Spread detector created successfully" << std::endl;
    
    // Test create all detectors
    auto allDetectors = factory.createAllDetectors();
    assert(allDetectors.size() == 2);  // Volume + Spread
    std::cout << "✓ All detectors created: " << allDetectors.size() << " detectors" << std::endl;
    
    // Test configuration
    std::map<std::string, double> config;
    config["min_strength_threshold"] = 0.5;
    factory.updateDetectorConfig(ImbalanceType::VOLUME_IMBALANCE, config);
    std::cout << "✓ Configuration updated successfully" << std::endl;
    
    // Test disable detector
    factory.setEnabledDetectors({ImbalanceType::VOLUME_IMBALANCE});
    assert(factory.isDetectorEnabled(ImbalanceType::VOLUME_IMBALANCE));
    assert(!factory.isDetectorEnabled(ImbalanceType::ABNORMAL_SPREAD));
    
    auto reducedDetectors = factory.createAllDetectors();
    assert(reducedDetectors.size() == 1);  // Only Volume
    std::cout << "✓ Detector enable/disable works correctly" << std::endl;
    
    std::cout << "All factory tests passed! ✓" << std::endl;
    return 0;
}