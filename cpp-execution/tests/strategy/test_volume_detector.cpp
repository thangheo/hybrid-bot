/**
 * @file test_volume_detector.cpp 
 * @brief Simple test for VolumeImbalanceDetector
 */
#include "strategy/pattern/detectors/volume_imbalance_detector.hpp"
#include "strategy/types.hpp"
#include <iostream>
#include <cassert>

using namespace trading::strategy::detectors;
using namespace trading::strategy;
using namespace trading::core;
using namespace trading::common;

int main() {
    std::cout << "Testing VolumeImbalanceDetector..." << std::endl;
    
    // Create detector
    VolumeImbalanceDetector detector;
    
    // Test basic functionality
    assert(detector.getName() == "VolumeImbalanceDetector");
    assert(detector.getDetectorType() == ImbalanceType::VOLUME_IMBALANCE);
    assert(detector.isEnabled() == true);
    
    // Create test metrics
    OrderBookMetrics metrics;
    metrics.exchangeId = ExchangeId::binance();
    metrics.symbol = Symbol{"BTCUSDT"};
    metrics.timestamp = std::chrono::system_clock::now();
    metrics.metrics["bidVolume"] = 1000.0;
    metrics.metrics["askVolume"] = 500.0;  // 2:1 ratio
    
    // Create test thresholds
    ThresholdSet thresholds;
    ImbalanceThreshold volThresh;
    volThresh.activation_threshold = 1.5;  // Should trigger
    thresholds.setTypeThreshold(ImbalanceType::VOLUME_IMBALANCE, volThresh);
    
    // Create context
    DetectorContext context;
    context.metrics = &metrics;
    context.thresholds = &thresholds;
    context.currentRegime = 0;
    context.currentVolatility = 0.1;
    context.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Analyze
    auto result = detector.analyze(context);
    
    // Verify result
    assert(result.hasSignal == true);
    assert(result.direction == Direction::BUY);  // More bid than ask
    assert(result.type == ImbalanceType::VOLUME_IMBALANCE);
    assert(result.strength > 0.0);
    assert(result.confidence > 0.0);
    assert(result.rawValue == 2.0);  // bidVolume/askVolume
    
    std::cout << "✓ Signal detected: strength=" << result.strength 
              << ", confidence=" << result.confidence 
              << ", direction=" << (result.direction == Direction::BUY ? "BUY" : "SELL")
              << std::endl;
    
    // Test disable functionality
    detector.setEnabled(false);
    auto disabledResult = detector.analyze(context);
    assert(disabledResult.hasSignal == false);
    std::cout << "✓ Disabled detector returns no signal" << std::endl;
    
    // Test config update
    detector.setEnabled(true);
    std::map<std::string, double> config;
    config["min_strength_threshold"] = 0.5;
    detector.updateConfig(config);
    std::cout << "✓ Configuration updated successfully" << std::endl;
    
    std::cout << "All tests passed! ✓" << std::endl;
    return 0;
}