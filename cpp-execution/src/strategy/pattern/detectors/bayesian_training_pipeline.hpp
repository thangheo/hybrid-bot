#pragma once

#include "strategy/pattern/detectors/bayesian_detector.hpp"
#include "data_interface/market_data_interface.hpp"
#include <deque>
#include <mutex>
#include <atomic>
#include <chrono>

namespace trading::strategy::detectors {

// Optional training pipeline for online learning
// This can be added later if you want the monolithic training capabilities
struct PendingTrainingSample {
    std::chrono::system_clock::time_point timestamp;
    std::vector<double> features;
    double midPrice;
    double vwap;
    double sma;
    double referenceValue;
    int referenceType;
    trading::core::ExchangeId exchangeId;
    trading::core::Symbol symbol;
};

class BayesianTrainingPipeline {
public:
    explicit BayesianTrainingPipeline(BayesianDetector& detector);
    
    // Add sample for delayed labeling
    void addTrainingSample(const OrderBookMetrics& metrics, 
                          const std::vector<double>& features);
    
    // Process pending samples and train models
    void processTraining();
    
    // Configure training parameters
    void setLabelingDelay(std::chrono::milliseconds delay);
    void setTrainingWarmup(std::chrono::milliseconds startDelay, int minOrderbookLevels);
    
private:
    BayesianDetector& detector_;
    std::deque<PendingTrainingSample> pendingSamples_;
    std::mutex samplesMutex_;
    
    std::chrono::milliseconds labelingDelay_{std::chrono::minutes(5)};
    std::atomic<bool> trainingEnabled_{false};
    std::chrono::steady_clock::time_point trainingStartTime_;
    int minOrderbookLevels_ = 0;
    
    // Helper functions from monolithic system
    std::pair<double, int> selectReference(const OrderBookMetrics& m, 
                                         double vwap, double sma);
    double computeMicroPrice(const OrderBookMetrics& m);
};

} // namespace trading::strategy::detectors