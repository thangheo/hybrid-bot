#include "perception/bayesian_trading_model.hpp"
#include <iostream>
#include <random>
#include <vector>

using namespace trading::perception;

/**
 * Example usage of BayesianTradingModel
 * 
 * This demonstrates:
 * 1. Model initialization and configuration
 * 2. Training with simulated detector outputs
 * 3. Making predictions
 * 4. Online learning updates
 * 5. Model persistence
 */

// Simulate detector outputs (your actual detectors would replace this)
std::vector<double> simulate_detector_outputs(int num_detectors) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_real_distribution<> dis(0.0, 1.0);
    
    std::vector<double> features;
    features.reserve(num_detectors);
    
    for (int i = 0; i < num_detectors; ++i) {
        // Simulate various detector types with different ranges
        double value;
        if (i % 3 == 0) {
            // Some detectors output in [0, 100]
            value = dis(gen) * 100.0;
        } else if (i % 3 == 1) {
            // Others in [-1, 1]
            value = dis(gen) * 2.0 - 1.0;
        } else {
            // Others in [0, 1]
            value = dis(gen);
        }
        features.push_back(value);
    }
    
    return features;
}

// Simulate market direction (replace with your actual labeling logic)
int simulate_market_direction(const std::vector<double>& features) {
    // Simulate a scenario where some features are predictive
    double signal = 0.0;
    for (size_t i = 0; i < features.size(); ++i) {
        // First few features are positively predictive
        if (i < 3) {
            signal += features[i] * 0.1;
        }
        // Some are negatively predictive
        else if (i < 6) {
            signal -= features[i] * 0.05;
        }
        // Others are noise
    }
    
    // Add noise
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::normal_distribution<> noise(0.0, 0.5);
    signal += noise(gen);
    
    // Convert to binary label
    return (signal > 0.0) ? 1 : 0;
}

int main() {
    std::cout << "=== Bayesian Trading Model Example ===" << std::endl;
    
    // 1. Configure the model
    BayesianTradingModel::ModelConfig config;
    config.initial_sigma = 1.0;        // Initial uncertainty
    config.beta = 0.5;                 // Learning rate
    config.min_probability = 0.65;     // Trading threshold
    config.base_position_size = 0.2;   // Base position size (20%)
    config.uncertainty_penalty = 0.3;  // Reduce size when uncertain
    config.use_expected_value = true;  // Use EV instead of threshold
    config.payoff_long = 1.2;          // Expected payoff for long
    config.payoff_short = 1.0;         // Expected payoff for short
    
    // 2. Create model
    BayesianTradingModel model(config);
    
    const int num_detectors = 12;  // Your detector count
    
    std::cout << "\n--- Training Phase ---" << std::endl;
    
    // 3. Training loop - simulate receiving detector signals and market feedback
    std::vector<double> training_returns;
    std::vector<int> predictions, actuals;
    
    for (int epoch = 0; epoch < 500; ++epoch) {
        // Get detector outputs
        auto features = simulate_detector_outputs(num_detectors);
        
        // Get market direction (this would be your 2-minute forward return > 0)
        int actual_direction = simulate_market_direction(features);
        
        // Update model with observation
        model.update(features, actual_direction);
        
        // Every 10 updates, make a prediction and see how we're doing
        if (epoch % 50 == 0) {
            auto signal = model.predict(features);
            
            std::cout << "Epoch " << epoch 
                      << " | P(up): " << signal.probability
                      << " | EV: " << signal.expected_value
                      << " | Size: " << signal.position_size
                      << " | Uncertainty: " << signal.uncertainty
                      << " | Should trade: " << signal.should_trade
                      << " | Direction: " << signal.direction << std::endl;
        }
        
        if (epoch > 100) {  // Start making predictions after some training
            auto signal = model.predict(features);
            predictions.push_back(signal.direction > 0 ? 1 : 0);
            actuals.push_back(actual_direction);
            
            // Simulate return if we traded
            if (signal.should_trade) {
                double simulated_return = (actual_direction == (signal.direction > 0 ? 1 : 0)) ? 
                                        signal.position_size * 0.01 : -signal.position_size * 0.01;
                training_returns.push_back(simulated_return);
            }
        }
    }
    
    // 4. Print training results
    model.print_summary();
    
    // Calculate performance metrics
    double accuracy = BayesianTradingUtils::calculate_accuracy(predictions, actuals);
    double sharpe = BayesianTradingUtils::calculate_sharpe_ratio(training_returns);
    
    std::cout << "Training Results:" << std::endl;
    std::cout << "  Accuracy: " << accuracy * 100 << "%" << std::endl;
    std::cout << "  Sharpe Ratio: " << sharpe << std::endl;
    std::cout << "  Total Returns: " << training_returns.size() << std::endl;
    
    // 5. Save model
    std::string model_path = "models/bayesian/bayesian_model.json";
    if (model.save_model(model_path)) {
        std::cout << "Model saved successfully!" << std::endl;
    }
    
    std::cout << "\n--- Live Trading Simulation ---" << std::endl;
    
    // 6. Simulate live trading
    for (int i = 0; i < 20; ++i) {
        auto features = simulate_detector_outputs(num_detectors);
        auto signal = model.predict(features);
        
        std::cout << "Trade " << i+1 << ":" << std::endl;
        std::cout << "  Features: ";
        for (int j = 0; j < 5; ++j) {  // Show first 5 features
            std::cout << features[j] << " ";
        }
        std::cout << "..." << std::endl;
        
        std::cout << "  Signal:" << std::endl;
        std::cout << "    Probability: " << signal.probability << std::endl;
        std::cout << "    Expected Value: " << signal.expected_value << std::endl;
        std::cout << "    Position Size: " << signal.position_size << std::endl;
        std::cout << "    Uncertainty: " << signal.uncertainty << std::endl;
        std::cout << "    Should Trade: " << (signal.should_trade ? "YES" : "NO") << std::endl;
        std::cout << "    Direction: " << (signal.direction > 0 ? "BUY" : "SELL") << std::endl;
        
        // Simulate market feedback and continue learning
        int market_direction = simulate_market_direction(features);
        model.update(features, market_direction);
        
        std::cout << "    Actual Direction: " << (market_direction ? "UP" : "DOWN") << std::endl;
        std::cout << std::endl;
    }
    
    // 7. Demonstrate model loading
    std::cout << "\n--- Model Persistence Test ---" << std::endl;
    BayesianTradingModel loaded_model;
    if (loaded_model.load_model(model_path)) {
        std::cout << "Model loaded successfully!" << std::endl;
        loaded_model.print_summary();
        
        // Test prediction with loaded model
        auto test_features = simulate_detector_outputs(num_detectors);
        auto test_signal = loaded_model.predict(test_features);
        std::cout << "Test prediction probability: " << test_signal.probability << std::endl;
    }
    
    std::cout << "\n=== Example Complete ===" << std::endl;
    return 0;
}
