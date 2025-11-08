/**
 * @file test_threshold_manager.cpp
 * @brief Simple test for ThresholdManager
 */
#include "strategy/pattern/threshold/threshold_manager.hpp"
#include <iostream>
#include <cassert>

using namespace trading::strategy::threshold;

int main() {
    std::cout << "Testing ThresholdManager..." << std::endl;
    
    // Create manager
    ThresholdManager manager;
    
    // Test default thresholds (no data)
    auto defaultThresholds = manager.calculateThresholds(0, 0.1);
    assert(defaultThresholds.isValid());
    assert(defaultThresholds.buyThreshold > 0.0);
    assert(defaultThresholds.sellThreshold < 0.0);
    std::cout << "✓ Default thresholds: buy=" << defaultThresholds.buyThreshold 
              << ", sell=" << defaultThresholds.sellThreshold << std::endl;
    
    // Test insufficient data
    assert(!manager.hasSufficientData(0));
    assert(manager.getRegimeDataPoints(0) == 0);
    std::cout << "✓ Insufficient data detection works" << std::endl;
    
    // Add some statistical data
    const int regime = 1;
    for (int i = 0; i < 50; i++) {
        double return_ = 0.001 * (i % 10 - 5); // Returns from -0.005 to +0.004
        double volatility = 0.01 + 0.001 * (i % 5); // Vol from 0.01 to 0.014
        manager.updateStats(regime, return_, volatility);
    }
    
    // Test data accumulation
    assert(manager.hasSufficientData(regime));
    assert(manager.getRegimeDataPoints(regime) == 50);
    std::cout << "✓ Data accumulation works: " << manager.getRegimeDataPoints(regime) << " points" << std::endl;
    
    // Test threshold calculation with data
    auto calculatedThresholds = manager.calculateThresholds(regime, 0.012);
    assert(calculatedThresholds.isValid());
    assert(calculatedThresholds.buyThreshold > 0.0);
    assert(calculatedThresholds.sellThreshold < 0.0);
    std::cout << "✓ Calculated thresholds: buy=" << calculatedThresholds.buyThreshold 
              << ", sell=" << calculatedThresholds.sellThreshold 
              << ", minProfit=" << calculatedThresholds.minProfitableReturn << std::endl;
    
    // Test override functionality
    auto overrideThresholds = manager.calculateThresholds(regime, 0.012, 0.01);
    assert(overrideThresholds.minProfitableReturn >= 0.01);
    std::cout << "✓ Override works: minProfit=" << overrideThresholds.minProfitableReturn << std::endl;
    
    // Test reset functionality
    manager.resetRegimeStats(regime);
    assert(!manager.hasSufficientData(regime));
    assert(manager.getRegimeDataPoints(regime) == 0);
    std::cout << "✓ Reset functionality works" << std::endl;
    
    // Test invalid inputs (should be ignored)
    manager.updateStats(regime, std::numeric_limits<double>::infinity(), 0.01);
    manager.updateStats(regime, 0.01, -1.0);
    assert(manager.getRegimeDataPoints(regime) == 0);
    std::cout << "✓ Invalid input rejection works" << std::endl;
    
    std::cout << "All threshold manager tests passed! ✓" << std::endl;
    return 0;
}