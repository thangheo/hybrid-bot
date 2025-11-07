#include <iostream>
#include <vector>
#include <cmath>
#include "perception/bayesian_trading_model.hpp"

int main() {
    using namespace trading::perception;
    
    BayesianTradingModel model;
    
    std::cout << "Testing BayesianTradingModel numerical stability fixes..." << std::endl;
    
    // Test 1: Normal features
    std::vector<double> normal_features = {1.0, 2.0, 3.0};
    auto result1 = model.predict_multiclass(normal_features);
    std::cout << "Normal features: " << std::get<0>(result1) << ", " 
              << std::get<1>(result1) << ", " << std::get<2>(result1) << std::endl;
    
    // Test 2: Extreme features that might cause issues
    std::vector<double> extreme_features = {1e10, -1e10, 0.0};
    auto result2 = model.predict_multiclass(extreme_features);
    std::cout << "Extreme features: " << std::get<0>(result2) << ", " 
              << std::get<1>(result2) << ", " << std::get<2>(result2) << std::endl;
    
    // Test 3: Train with some data to ensure update doesn't crash
    for (int i = 0; i < 10; ++i) {
        std::vector<double> train_features = {
            static_cast<double>(i) * 0.1, 
            static_cast<double>(i) * 0.2, 
            static_cast<double>(i) * 0.3
        };
        model.update(train_features, i % 3); // Cycle through classes 0, 1, 2
    }
    
    // Test 4: Predict after training
    auto result3 = model.predict_multiclass(normal_features);
    std::cout << "After training: " << std::get<0>(result3) << ", " 
              << std::get<1>(result3) << ", " << std::get<2>(result3) << std::endl;
    
    // Test 5: Extreme features after training (this was likely causing the NaN issues)
    auto result4 = model.predict_multiclass(extreme_features);
    std::cout << "Extreme after training: " << std::get<0>(result4) << ", " 
              << std::get<1>(result4) << ", " << std::get<2>(result4) << std::endl;
    
    std::cout << "All tests completed without crashes!" << std::endl;
    
    return 0;
}