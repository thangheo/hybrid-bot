// imbalance_parameter_tuner.hpp
#pragma once

#include "strategy/types.hpp"
#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <map>
#include <mutex>
#include <atomic>

namespace trading {
namespace strategy {

// Define ParameterSpace
using ParameterSpace = std::map<std::string, std::vector<double>>;


/**
 * Optimization method to use for parameter tuning
 */
enum class OptimizationMethod {
    GRID_SEARCH,            // Exhaustive search over parameter space
    BAYESIAN_OPTIMIZATION,  // Bayesian optimization (efficient for expensive evaluation)
    GENETIC_ALGORITHM,      // Evolutionary approach
    SIMULATED_ANNEALING     // Probabilistic technique for global optimization
};

/**
 * Class responsible for optimizing strategy parameters
 */
class ImbalanceParameterTuner {
public:
    // Constructor
    ImbalanceParameterTuner();
    ~ImbalanceParameterTuner() = default;

    // Define parameter search space
    void defineParameterSpace(const ParameterSpace& space);
    
    // Evaluate a specific parameter set
    void evaluateParameterSet(const ParameterSet& params, const PerformanceMetrics& metrics);
    
    // Get the best parameters found so far
    ParameterSet getBestParameters() const;
    
    // Start optimization process with specified method
    void startOptimization(OptimizationMethod method);
    
    // Stop ongoing optimization
    void stopOptimization();
    
    // Check if optimization is running
    bool isOptimizationRunning() const;
    
    // Get optimization progress (0.0 - 1.0)
    double getOptimizationProgress() const;
    
    // Reset optimization state
    void reset();
    
    // Register callback for best parameter updates
    using BestParameterCallback = std::function<void(const ParameterSet&)>;
    void registerBestParameterCallback(BestParameterCallback callback);

private:
    // Parameter space definition
    ParameterSpace parameterSpace_;
    
    // Create a string key from a ParameterSet for use in maps
    std::string createParameterKey(const ParameterSet& params) const;
    
    // Evaluation results - using string keys instead of ParameterSet objects
    std::map<std::string, PerformanceMetrics> evaluationResults_;
    std::map<std::string, ParameterSet> parameterSets_;  // Store actual parameter sets by key
    
    // Best parameters found
    ParameterSet bestParameters_;
    PerformanceMetrics bestMetrics_;
    
    // Optimization status
    std::atomic<bool> optimizationRunning_{false};
    std::atomic<double> optimizationProgress_{0.0};
    
    // Optimization algorithms
    void runGridSearch();
    void runBayesianOptimization();
    void runGeneticAlgorithm();
    void runSimulatedAnnealing();
    
    // Helper methods
    bool isBetter(const PerformanceMetrics& current, const PerformanceMetrics& best) const;
    void updateBestParameters(const ParameterSet& params, const PerformanceMetrics& metrics);
    
    // Callbacks
    BestParameterCallback bestParameterCallback_;
    
    // Thread safety
    mutable std::mutex mutex_;
};

} // namespace strategy
} // namespace trading