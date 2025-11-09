// imbalance_parameter_tuner.cpp
#include "strategy/optimization/imbalance_parameter_tuner.hpp"
#include <algorithm>
#include <random>
#include <thread>
#include <iostream>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace trading {
namespace strategy {

ImbalanceParameterTuner::ImbalanceParameterTuner() {
    // Initialize with default values
    reset();
}

void ImbalanceParameterTuner::defineParameterSpace(const ParameterSpace& space) {
    std::lock_guard<std::mutex> lock(mutex_);
    parameterSpace_ = space;
}

std::string ImbalanceParameterTuner::createParameterKey(const ParameterSet& params) const {
    // Create a unique key based on parameter values
    std::stringstream ss;
    ss << std::fixed << std::setprecision(6);
    
    // Use numeric parameters to create a unique key
    std::vector<std::pair<std::string, double>> sortedParams;
    for (const auto& pair : params.numeric_params) {
        sortedParams.push_back(pair);
    }
    
    // Sort for consistent keys
    std::sort(sortedParams.begin(), sortedParams.end());
    
    for (const auto& pair : sortedParams) {
        ss << pair.first << "=" << pair.second << ";";
    }
    
    return ss.str();
}

void ImbalanceParameterTuner::evaluateParameterSet(const ParameterSet& params, const PerformanceMetrics& metrics) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create a unique key for this parameter set
    std::string paramKey = createParameterKey(params);
    
    // Store evaluation result
    evaluationResults_[paramKey] = metrics;
    parameterSets_[paramKey] = params;
    
    // Check if this is better than our current best
    if (isBetter(metrics, bestMetrics_)) {
        updateBestParameters(params, metrics);
    }
}

ParameterSet ImbalanceParameterTuner::getBestParameters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bestParameters_;
}

void ImbalanceParameterTuner::startOptimization(OptimizationMethod method) {
    // Don't start if already running
    if (isOptimizationRunning()) {
        return;
    }
    
    // Set running flag
    optimizationRunning_ = true;
    optimizationProgress_ = 0.0;
    
    // Start optimization in a new thread
    // Store thread in member so it can be joined later. Capture by value where
    // necessary to avoid referencing 'this' after destruction.
    optimizationThread_ = std::thread([this, method]() {
        try {
            switch (method) {
                case OptimizationMethod::GRID_SEARCH:
                    runGridSearch();
                    break;
                case OptimizationMethod::BAYESIAN_OPTIMIZATION:
                    runBayesianOptimization();
                    break;
                case OptimizationMethod::GENETIC_ALGORITHM:
                    runGeneticAlgorithm();
                    break;
                case OptimizationMethod::SIMULATED_ANNEALING:
                    runSimulatedAnnealing();
                    break;
                default:
                    runGridSearch(); // Default to grid search
                    break;
            }
        } catch (const std::exception& e) {
            // Log exception
            std::cerr << "Exception in optimization thread: " << e.what() << std::endl;
        }
        
        // Set running flag to false when done
        optimizationRunning_ = false;
        optimizationProgress_ = 1.0;
    });
    
    // Leave thread running; it will be joined in stopOptimization() or the
    // destructor. This prevents detached threads from outliving the object
    // and capturing dangling references.
}

void ImbalanceParameterTuner::stopOptimization() {
    optimizationRunning_ = false;

    // If an optimization thread exists, attempt to join it. Avoid joining
    // from within the optimization thread itself.
    try {
        if (optimizationThread_.joinable()) {
            if (optimizationThread_.get_id() == std::this_thread::get_id()) {
                // Called from within the optimization thread; cannot join.
                // Leave the thread to exit naturally.
            } else {
                optimizationThread_.join();
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception while joining optimization thread: " << e.what() << std::endl;
    }
}

ImbalanceParameterTuner::~ImbalanceParameterTuner() {
    // Ensure optimization is stopped and thread joined before destruction
    stopOptimization();
}

bool ImbalanceParameterTuner::isOptimizationRunning() const {
    return optimizationRunning_;
}

double ImbalanceParameterTuner::getOptimizationProgress() const {
    return optimizationProgress_;
}

void ImbalanceParameterTuner::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Stop any running optimization
    stopOptimization();
    
    // Clear all data
    parameterSpace_.clear();
    evaluationResults_.clear();
    parameterSets_.clear();
    
    // Reset best parameters to empty
    bestParameters_ = ParameterSet();
    
    // Reset metrics to worst possible values
    bestMetrics_.sharpeRatio = -std::numeric_limits<double>::infinity();
    bestMetrics_.totalReturn = -std::numeric_limits<double>::infinity();
    bestMetrics_.maxDrawdown = std::numeric_limits<double>::infinity();
    bestMetrics_.winRate = 0.0;
    
    // Reset progress
    optimizationProgress_ = 0.0;
}

void ImbalanceParameterTuner::registerBestParameterCallback(BestParameterCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    bestParameterCallback_ = callback;
}

void ImbalanceParameterTuner::runGridSearch() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Extract parameter ranges from parameter space
    std::vector<std::string> paramNames;
    std::vector<std::vector<double>> paramValues;
    std::vector<size_t> currentIndices;
    size_t totalCombinations = 1;
    
    for (const auto& param : parameterSpace_) {
        paramNames.push_back(param.first);
        paramValues.push_back(param.second);
        currentIndices.push_back(0);
        totalCombinations *= param.second.size();
    }
    
    size_t evaluatedCombinations = 0;
    
    // Iterate through all parameter combinations
    bool done = false;
    while (!done && optimizationRunning_) {
        // Create parameter set for current indices
        ParameterSet currentParams;
        for (size_t i = 0; i < paramNames.size(); ++i) {
            // Add to numeric_params map instead of direct indexing
            currentParams.setParameter(paramNames[i], paramValues[i][currentIndices[i]]);
        }
        
        // Create a unique key for this parameter set
        std::string paramKey = createParameterKey(currentParams);
        
        // Check if we have already evaluated this combination
        if (evaluationResults_.find(paramKey) == evaluationResults_.end()) {
            // Release lock temporarily to allow other threads to access
            mutex_.unlock();
            
            // This is where actual evaluation would happen in a real system
            // For this implementation, we'll simulate it with a sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            // Re-acquire lock
            mutex_.lock();
            
            // Check if we should continue after sleep
            if (!optimizationRunning_) {
                break;
            }
            
            // Simulate random performance metrics
            // In a real system, this would come from backtesting or live testing
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_real_distribution<> sharpe_dist(-1.0, 3.0);
            std::uniform_real_distribution<> return_dist(-10.0, 50.0);
            std::uniform_real_distribution<> drawdown_dist(0.0, 30.0);
            std::uniform_real_distribution<> winrate_dist(0.3, 0.7);
            
            PerformanceMetrics metrics;
            metrics.sharpeRatio = sharpe_dist(gen);
            metrics.totalReturn = return_dist(gen);
            metrics.maxDrawdown = drawdown_dist(gen);
            metrics.winRate = winrate_dist(gen);
            
            // Store evaluation
            evaluationResults_[paramKey] = metrics;
            parameterSets_[paramKey] = currentParams;
            
            // Check if this is better than our current best
            if (isBetter(metrics, bestMetrics_)) {
                updateBestParameters(currentParams, metrics);
            }
        }
        
        // Increment indices for next combination
        for (int i = static_cast<int>(paramNames.size()) - 1; i >= 0; --i) {
            currentIndices[i]++;
            if (currentIndices[i] < paramValues[i].size()) {
                break;
            }
            // This dimension rolled over, reset it and continue to the next dimension
            currentIndices[i] = 0;
            if (i == 0) {
                done = true; // All combinations have been evaluated
            }
        }
        
        evaluatedCombinations++;
        optimizationProgress_ = static_cast<double>(evaluatedCombinations) / totalCombinations;
    }
}

void ImbalanceParameterTuner::runBayesianOptimization() {
    // In a real implementation, this would use Bayesian optimization techniques
    // Simplified version for this example:
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // For this example, we'll just use a random sampling approach
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Extract parameter ranges
    std::vector<std::string> paramNames;
    std::vector<std::pair<double, double>> paramRanges;
    
    for (const auto& param : parameterSpace_) {
        paramNames.push_back(param.first);
        
        // Find min and max values in the parameter space
        double min_val = *std::min_element(param.second.begin(), param.second.end());
        double max_val = *std::max_element(param.second.begin(), param.second.end());
        
        paramRanges.push_back(std::make_pair(min_val, max_val));
    }
    
    // Sample points
    const int numSamples = 100;
    for (int sample = 0; sample < numSamples && optimizationRunning_; ++sample) {
        ParameterSet currentParams;
        
        // Generate random values within parameter ranges
        for (size_t i = 0; i < paramNames.size(); ++i) {
            std::uniform_real_distribution<> dist(paramRanges[i].first, paramRanges[i].second);
            // Use setParameter instead of direct indexing
            currentParams.setParameter(paramNames[i], dist(gen));
        }
        
        // Create a unique key for this parameter set
        std::string paramKey = createParameterKey(currentParams);
        
        // Check if we have already evaluated this combination
        if (evaluationResults_.find(paramKey) == evaluationResults_.end()) {
            // Release lock temporarily to allow other threads to access
            mutex_.unlock();
            
            // Simulate evaluation
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            // Re-acquire lock
            mutex_.lock();
            
            // Check if we should continue after sleep
            if (!optimizationRunning_) {
                break;
            }
            
            // Simulate performance metrics
            std::uniform_real_distribution<> sharpe_dist(-1.0, 3.0);
            std::uniform_real_distribution<> return_dist(-10.0, 50.0);
            std::uniform_real_distribution<> drawdown_dist(0.0, 30.0);
            std::uniform_real_distribution<> winrate_dist(0.3, 0.7);
            
            PerformanceMetrics metrics;
            metrics.sharpeRatio = sharpe_dist(gen);
            metrics.totalReturn = return_dist(gen);
            metrics.maxDrawdown = drawdown_dist(gen);
            metrics.winRate = winrate_dist(gen);
            
            // Store evaluation
            evaluationResults_[paramKey] = metrics;
            parameterSets_[paramKey] = currentParams;
            
            // Check if this is better than our current best
            if (isBetter(metrics, bestMetrics_)) {
                updateBestParameters(currentParams, metrics);
            }
        }
        
        optimizationProgress_ = static_cast<double>(sample + 1) / numSamples;
    }
}

// Similarly update runGeneticAlgorithm() and runSimulatedAnnealing() - not showing full code for brevity

bool ImbalanceParameterTuner::isBetter(const PerformanceMetrics& current, const PerformanceMetrics& best) const {
    // Primary metric for comparison: Sharpe ratio
    if (current.sharpeRatio > best.sharpeRatio + 0.1) {
        return true;
    }
    
    // If Sharpe ratios are similar, compare other metrics
    if (std::abs(current.sharpeRatio - best.sharpeRatio) <= 0.1) {
        // Check total return if Sharpe is similar
        if (current.totalReturn > best.totalReturn * 1.05) {
            return true;
        }
        
        // Check drawdown if Sharpe and return are similar
        if (std::abs(current.totalReturn - best.totalReturn) <= best.totalReturn * 0.05 && 
            current.maxDrawdown < best.maxDrawdown * 0.95) {
            return true;
        }
    }
    
    return false;
}

void ImbalanceParameterTuner::updateBestParameters(const ParameterSet& params, const PerformanceMetrics& metrics) {
    bestParameters_ = params;
    bestMetrics_ = metrics;
    
    // Call the callback if registered
    if (bestParameterCallback_) {
        bestParameterCallback_(bestParameters_);
    }
}
void ImbalanceParameterTuner::runGeneticAlgorithm() {
    // Simplified genetic algorithm implementation for parameter optimization
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    
    // Extract parameter ranges
    std::vector<std::string> paramNames;
    std::vector<std::pair<double, double>> paramRanges;
    
    for (const auto& param : parameterSpace_) {
        paramNames.push_back(param.first);
        
        // Find min and max values in the parameter space
        double min_val = *std::min_element(param.second.begin(), param.second.end());
        double max_val = *std::max_element(param.second.begin(), param.second.end());
        
        paramRanges.push_back(std::make_pair(min_val, max_val));
    }
    
    // GA parameters
    const int populationSize = 20;
    const int generations = 10;
    const double mutationRate = 0.1;
    const double crossoverRate = 0.7;
    
    // Initialize population
    std::vector<ParameterSet> population;
    
    for (int i = 0; i < populationSize; ++i) {
        ParameterSet individual;
        
        for (size_t j = 0; j < paramNames.size(); ++j) {
            std::uniform_real_distribution<> dist(paramRanges[j].first, paramRanges[j].second);
            // Use setParameter instead of direct array access
            individual.setParameter(paramNames[j], dist(gen));
        }
        
        population.push_back(individual);
    }
    
    // Run generations
    // for (int gen = 0; gen < generations && optimizationRunning_; ++gen) {
    for (int generation = 0; generation < generations && optimizationRunning_; ++generation) {
        // Evaluate population
        std::vector<PerformanceMetrics> fitnessValues;
        
        for (const auto& individual : population) {
            // Create key for the individual
            std::string paramKey = createParameterKey(individual);
            
            // Check if we have already evaluated this individual
            if (evaluationResults_.find(paramKey) == evaluationResults_.end()) {
                // Release lock temporarily
                mutex_.unlock();
                
                // Simulate evaluation
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                
                // Re-acquire lock
                mutex_.lock();
                
                // Check if we should continue
                if (!optimizationRunning_) {
                    break;
                }
                
                // Simulate performance metrics
                std::uniform_real_distribution<> sharpe_dist(-1.0, 3.0);
                std::uniform_real_distribution<> return_dist(-10.0, 50.0);
                std::uniform_real_distribution<> drawdown_dist(0.0, 30.0);
                std::uniform_real_distribution<> winrate_dist(0.3, 0.7);
                
                PerformanceMetrics metrics;
                metrics.sharpeRatio = sharpe_dist(gen);
                metrics.totalReturn = return_dist(gen);
                metrics.maxDrawdown = drawdown_dist(gen);
                metrics.winRate = winrate_dist(gen);
                
                // Store evaluation
                evaluationResults_[paramKey] = metrics;
                parameterSets_[paramKey] = individual;
            }
            
            fitnessValues.push_back(evaluationResults_[createParameterKey(individual)]);
            
            // Check if this is better than our current best
            if (isBetter(evaluationResults_[createParameterKey(individual)], bestMetrics_)) {
                updateBestParameters(individual, evaluationResults_[createParameterKey(individual)]);
            }
        }
        
        if (!optimizationRunning_) {
            break;
        }
        
        // Create new population
        std::vector<ParameterSet> newPopulation;
        
        // Elitism: keep best individual
        int bestIdx = 0;
        for (size_t i = 1; i < fitnessValues.size(); ++i) {
            if (isBetter(fitnessValues[i], fitnessValues[bestIdx])) {
                bestIdx = i;
            }
        }
        newPopulation.push_back(population[bestIdx]);
        
        // Selection, crossover, and mutation to fill rest of new population
        while (newPopulation.size() < populationSize) {
            // Tournament selection
            std::uniform_int_distribution<> idx_dist(0, populationSize - 1);
            // int idx1 = idx_dist(gen);
            // int idx2 = idx_dist(gen);
            int idx1 = std::uniform_int_distribution<>(0, populationSize - 1)(gen);
            int idx2 = std::uniform_int_distribution<>(0, populationSize - 1)(gen);
            while (idx1 == idx2) {
                idx2 = idx_dist(gen);
            }
            // Select the better of the two
            
            int selectedIdx = isBetter(fitnessValues[idx1], fitnessValues[idx2]) ? idx1 : idx2;
            ParameterSet offspring = population[selectedIdx];
            
            // Crossover
            std::uniform_real_distribution<> prob_dist(0.0, 1.0);
            if (prob_dist(gen) < crossoverRate && newPopulation.size() < populationSize - 1) {
                int crossIdx = idx_dist(gen);
                while (crossIdx == selectedIdx) {
                    crossIdx = idx_dist(gen);
                }
                
                ParameterSet partner = population[crossIdx];
                ParameterSet offspring2 = offspring;
                
                // Perform crossover for each parameter with 50% chance
                for (const auto& paramName : paramNames) {
                    if (prob_dist(gen) < 0.5) {
                        // Get values from both parents
                        double val1 = offspring.getNumericParam(paramName);
                        double val2 = partner.getNumericParam(paramName);
                        
                        // Swap values
                        offspring.setParameter(paramName, val2);
                        partner.setParameter(paramName, val1);
                    }
                }
                
                newPopulation.push_back(partner);
            }
            
            // Mutation
            for (const auto& paramName : paramNames) {
                if (prob_dist(gen) < mutationRate) {
                    int paramIdx = std::distance(paramNames.begin(), 
                                    std::find(paramNames.begin(), paramNames.end(), paramName));
                    std::uniform_real_distribution<> param_dist(
                        paramRanges[paramIdx].first,
                        paramRanges[paramIdx].second
                    );
                    offspring.setParameter(paramName, param_dist(gen));
                }
            }
            
            newPopulation.push_back(offspring);
        }
        
        // Ensure we don't exceed population size (in case we added both offspring and partner)
        while (newPopulation.size() > populationSize) {
            newPopulation.pop_back();
        }
        
        // Replace population
        population = newPopulation;
        
        optimizationProgress_ = static_cast<double>(generation + 1) / generations;
    }
}

void ImbalanceParameterTuner::runSimulatedAnnealing() {
    // Simplified simulated annealing implementation
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dist(0.0, 1.0);
    
    // Extract parameter ranges
    std::vector<std::string> paramNames;
    std::vector<std::pair<double, double>> paramRanges;
    
    for (const auto& param : parameterSpace_) {
        paramNames.push_back(param.first);
        
        // Find min and max values in the parameter space
        double min_val = *std::min_element(param.second.begin(), param.second.end());
        double max_val = *std::max_element(param.second.begin(), param.second.end());
        
        paramRanges.push_back(std::make_pair(min_val, max_val));
    }
    
    // SA parameters
    const int maxIterations = 100;
    const double initialTemp = 10.0;
    const double coolingRate = 0.95;
    
    // Initialize with random solution
    ParameterSet currentSolution;
    for (size_t i = 0; i < paramNames.size(); ++i) {
        std::uniform_real_distribution<> param_dist(paramRanges[i].first, paramRanges[i].second);
        currentSolution.setParameter(paramNames[i], param_dist(gen));
    }
    
    // Create key for current solution
    std::string currentKey = createParameterKey(currentSolution);
    
    // Evaluate initial solution
    PerformanceMetrics currentMetrics;
    if (evaluationResults_.find(currentKey) == evaluationResults_.end()) {
        // Simulate performance metrics
        std::uniform_real_distribution<> sharpe_dist(-1.0, 3.0);
        std::uniform_real_distribution<> return_dist(-10.0, 50.0);
        std::uniform_real_distribution<> drawdown_dist(0.0, 30.0);
        std::uniform_real_distribution<> winrate_dist(0.3, 0.7);
        
        currentMetrics.sharpeRatio = sharpe_dist(gen);
        currentMetrics.totalReturn = return_dist(gen);
        currentMetrics.maxDrawdown = drawdown_dist(gen);
        currentMetrics.winRate = winrate_dist(gen);
        
        evaluationResults_[currentKey] = currentMetrics;
        parameterSets_[currentKey] = currentSolution;
    } else {
        currentMetrics = evaluationResults_[currentKey];
    }
    
    ParameterSet bestSolution = currentSolution;
    PerformanceMetrics bestMetrics = currentMetrics;
    
    // Check if this is better than our global best
    if (isBetter(bestMetrics, bestMetrics_)) {
        updateBestParameters(bestSolution, bestMetrics);
    }
    
    // Simulated annealing process
    double temp = initialTemp;
    
    for (int iter = 0; iter < maxIterations && optimizationRunning_; ++iter) {
        // Generate neighbor solution by perturbing one random parameter
        ParameterSet neighborSolution = currentSolution;
        
        int paramIdx = std::uniform_int_distribution<>(0, paramNames.size() - 1)(gen);

        std::string paramName = paramNames[paramIdx];
        
        // Get current value
        double currentValue = neighborSolution.getNumericParam(paramName);
        
        // Perturb parameter within bounds
        double perturbation = std::normal_distribution<>(0.0, (paramRanges[paramIdx].second - paramRanges[paramIdx].first) * 0.1)(gen);
        double newValue = currentValue + perturbation;
        
        // Ensure it stays within bounds
        newValue = std::max(paramRanges[paramIdx].first, 
                           std::min(paramRanges[paramIdx].second, newValue));
        
        // Set new value
        neighborSolution.setParameter(paramName, newValue);
        
        // Create key for neighbor solution
        std::string neighborKey = createParameterKey(neighborSolution);
        
        // Evaluate neighbor
        PerformanceMetrics neighborMetrics;
        if (evaluationResults_.find(neighborKey) == evaluationResults_.end()) {
            // Release lock temporarily
            mutex_.unlock();
            
            // Simulate evaluation
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            
            // Re-acquire lock
            mutex_.lock();
            
            // Check if we should continue
            if (!optimizationRunning_) {
                break;
            }
            
            // Simulate performance metrics
            std::uniform_real_distribution<> sharpe_dist(-1.0, 3.0);
            std::uniform_real_distribution<> return_dist(-10.0, 50.0);
            std::uniform_real_distribution<> drawdown_dist(0.0, 30.0);
            std::uniform_real_distribution<> winrate_dist(0.3, 0.7);
            
            neighborMetrics.sharpeRatio = sharpe_dist(gen);
            neighborMetrics.totalReturn = return_dist(gen);
            neighborMetrics.maxDrawdown = drawdown_dist(gen);
            neighborMetrics.winRate = winrate_dist(gen);
            
            evaluationResults_[neighborKey] = neighborMetrics;
            parameterSets_[neighborKey] = neighborSolution;
        } else {
            neighborMetrics = evaluationResults_[neighborKey];
        }
        
        // Decide whether to accept neighbor
        bool accept = false;
        
        if (isBetter(neighborMetrics, currentMetrics)) {
            // Always accept better solutions
            accept = true;
        } else {
            // Accept worse solutions with a probability based on temperature
            double delta = (neighborMetrics.sharpeRatio - currentMetrics.sharpeRatio);
            double acceptProb = std::exp(delta / temp);
            accept = (dist(gen) < acceptProb);
        }
        
        if (accept) {
            currentSolution = neighborSolution;
            currentMetrics = neighborMetrics;
            currentKey = neighborKey;
            
            // Update best solution if needed
            if (isBetter(currentMetrics, bestMetrics)) {
                bestSolution = currentSolution;
                bestMetrics = currentMetrics;
                
                // Check if this is better than our global best
                if (isBetter(bestMetrics, bestMetrics_)) {
                    updateBestParameters(bestSolution, bestMetrics);
                }
            }
        }
        
        // Cool down temperature
        temp *= coolingRate;
        
        optimizationProgress_ = static_cast<double>(iter + 1) / maxIterations;
    }
}
} // namespace strategy
} // namespace trading