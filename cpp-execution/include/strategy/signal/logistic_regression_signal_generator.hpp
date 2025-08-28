// logistic_regression_signal_generator.hpp
#pragma once

#include "strategy/signal/imbalance_signal_generator.hpp"
#include "strategy/types.hpp"
#include <vector>
#include <memory>

namespace trading {
namespace strategy {

class LogisticRegressionSignalGenerator {
public:
    LogisticRegressionSignalGenerator();
    ~LogisticRegressionSignalGenerator() = default;
    
    // Initialize with model parameters
    void initialize(const ModelParameters& params);
    
    // Process order book metrics and other features
    void processFeatures(const OrderBookMetrics& metrics, 
                         const AdditionalFeatures& features);
    
    // Generate trading signals based on regression results
    std::vector<TradingSignal> generateSignals();
    
    // Configure signal generation parameters
    void configureSignalParameters(const SignalParameters& params);
    
    // Register callback for new signals
    void registerSignalCallback(const SignalCallback& callback);
    
private:
    // Model coefficients
    std::vector<double> weights_;
    double bias_;
    
    // Prediction function (logistic)
    double predict(const std::vector<double>& features);
    
    // Signal generation parameters
    double confidence_threshold_;
    double signal_strength_multiplier_;
    
    // Cached feature data
    std::vector<std::vector<double>> recent_features_;
    
    // Signal callback
    SignalCallback callback_;
    
    // Model state
    bool is_initialized_ = false;
};

} // namespace strategy
} // namespace trading