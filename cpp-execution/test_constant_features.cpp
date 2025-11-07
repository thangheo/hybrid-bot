#include "perception/bayesian_trading_model.hpp"
#include <iostream>
#include <vector>

using namespace trading::perception;

int main() {
    std::cout << "Testing BayesianTradingModel numerical stability fixes..." << std::endl;
    
    // Create model
    BayesianTradingModel model;
    
    // Test with features where feature[8] is constant (always 1.4)
    std::vector<std::vector<double>> test_features = {
        {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 1.4, 0.9, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9},
        {0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.4, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.1},
        {0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.1, 1.4, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.1, 0.2}
    };
    
    std::vector<int> labels = {0, 1, 2}; // SELL, NEUTRAL, BUY
    
    // Train model
    for (int epoch = 0; epoch < 100; ++epoch) {
        for (size_t i = 0; i < test_features.size(); ++i) {
            model.update(test_features[i], labels[i]);
        }
    }
    
    // Test prediction
    auto signal = model.predict(test_features[0]);
    std::cout << "Prediction successful!" << std::endl;
    std::cout << "Signal: direction=" << static_cast<int>(signal.direction) 
              << ", probability=" << signal.probability 
              << ", should_trade=" << signal.should_trade << std::endl;
    
    // Print model stats
    model.print_summary();
    
    std::cout << "Test completed successfully!" << std::endl;
    return 0;
}