/**
 * @file test_signal_publisher.cpp
 * @brief Simple test for SignalPublisher
 */
#include "strategy/pattern/publication/signal_publisher.hpp"
#include <iostream>
#include <cassert>
#include <chrono>

using namespace trading::strategy::publication;
using namespace trading::strategy::aggregation;
using namespace trading::strategy;
using namespace trading::core;
using namespace trading::common;

int main() {
    std::cout << "Testing SignalPublisher..." << std::endl;
    
    // Create publisher in DIRECT mode (no risk validation needed for test)
    SignalPublisher publisher(PublicationMode::DIRECT);
    
    // Test mode setting
    assert(publisher.getPublicationMode() == PublicationMode::DIRECT);
    std::cout << "✓ Publication mode configuration works" << std::endl;
    
    // Create test aggregated signal
    AggregatedSignal aggregatedSignal;
    aggregatedSignal.hasSignal = true;
    aggregatedSignal.overallConfidence = 0.8;
    aggregatedSignal.overallStrength = 0.6;
    aggregatedSignal.dominantDirection = Direction::BUY;
    aggregatedSignal.primaryType = ImbalanceType::VOLUME_IMBALANCE;
    aggregatedSignal.detectorCount = 2;
    aggregatedSignal.activeDetectorCount = 2;
    aggregatedSignal.consensusLevel = 1.0;
    
    // Create test context
    SignalContext context;
    context.exchangeId = ExchangeId::binance();
    context.symbol = Symbol{"BTCUSDT"};
    context.timestamp = std::chrono::system_clock::now();
    context.currentRegime = 1;
    context.currentVolatility = 0.015;
    context.source = "TestPublisher";
    
    // Test publication
    auto result = publisher.publishSignal(aggregatedSignal, context);
    
    // Verify result
    assert(result.published == true);
    assert(result.riskValidationPassed == true); // Direct mode assumes validation passed
    assert(!result.reason.empty());
    std::cout << "✓ Signal published successfully: " << result.reason << std::endl;
    
    // Test thresholds
    publisher.setMinPublicationStrength(0.8); // Higher than our signal
    auto rejectedResult = publisher.publishSignal(aggregatedSignal, context);
    assert(rejectedResult.published == false);
    assert(rejectedResult.reason.find("thresholds") != std::string::npos);
    std::cout << "✓ Threshold filtering works" << std::endl;
    
    // Test callback registration
    bool callbackTriggered = false;
    publisher.registerPublicationCallback([&](const PublicationResult& res, const TradingSignal& sig) {
        callbackTriggered = true;
        assert(res.published == true);
        assert(sig.direction == Direction::BUY);
    });
    
    // Reset threshold and test callback
    publisher.setMinPublicationStrength(0.5);
    publisher.publishSignal(aggregatedSignal, context);
    assert(callbackTriggered == true);
    std::cout << "✓ Publication callback works" << std::endl;
    
    // Test invalid signal rejection
    AggregatedSignal invalidSignal;
    invalidSignal.hasSignal = false; // Invalid
    auto invalidResult = publisher.publishSignal(invalidSignal, context);
    assert(invalidResult.published == false);
    std::cout << "✓ Invalid signal rejection works" << std::endl;
    
    // Test mode switching
    publisher.setPublicationMode(PublicationMode::RISK_ADVISORY);
    assert(publisher.getPublicationMode() == PublicationMode::RISK_ADVISORY);
    
    // This should still publish (no risk manager configured, falls back to direct)
    auto advisoryResult = publisher.publishSignal(aggregatedSignal, context);
    assert(advisoryResult.published == true);
    std::cout << "✓ Advisory mode fallback works" << std::endl;
    
    std::cout << "All signal publisher tests passed! ✓" << std::endl;
    return 0;
}