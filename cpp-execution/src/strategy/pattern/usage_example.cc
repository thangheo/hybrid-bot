#include "strategy/pattern/hawkes_enhanced_detection_engine.hpp"
#include <iostream>

using namespace trading::strategy;

// Callback function for imbalance events
void onImbalanceDetected(const ImbalanceEvent& event) {
    std::cout << "Imbalance detected: " 
              << (event.direction == Direction::BUY ? "BUY" : 
                  (event.direction == Direction::SELL ? "SELL" : "NEUTRAL"))
              << ", strength: " << event.strength
              << ", confidence: " << event.confidence << std::endl;
}

int main() {
    // Create and initialize enhanced detection engine
    auto engine = std::make_shared<HawkesEnhancedDetectionEngine>();
    
    // Configure standard imbalance detection
    ImbalanceDetectionConfig imbalanceConfig;
    // Set config parameters...
    
    // Configure Hawkes-Markov model
    HawkesMarkovConfig hawkesConfig;
    hawkesConfig.numStates = 20;
    hawkesConfig.markovLearningRate = 0.12;
    hawkesConfig.hawkesBaseIntensity = 15.0;
    hawkesConfig.eventWindowSize = 300;
    hawkesConfig.timeWindow = 3.0;
    hawkesConfig.alphaShort = 1.2;
    hawkesConfig.betaShort = 10.0;
    hawkesConfig.alphaMedium = 0.5;
    hawkesConfig.betaMedium = 1.5;
    hawkesConfig.alphaLong = 0.15;
    hawkesConfig.betaLong = 0.2;
    hawkesConfig.predictionThreshold = 0.65;
    hawkesConfig.marketRegimeThreshold = 0.3;
    
    // Initialize
    if (!engine->initialize(imbalanceConfig, hawkesConfig)) {
        std::cerr << "Failed to initialize enhanced detection engine" << std::endl;
        return 1;
    }
    
    // Register callback
    engine->registerImbalanceCallback(onImbalanceDetected);
    
    // Process order book updates
    // ... Your order book processing loop
    
    return 0;
}