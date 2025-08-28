// ab_testing_framework.hpp
#pragma once

#include "strategy/types.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include <shared_mutex>
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <random>
#include <chrono>
#include <atomic>

namespace trading {
namespace strategy {

/**
 * Class representing an experiment for A/B testing
 */
class Experiment {
public:
    Experiment(const std::string& id, const std::string& name);
    Experiment() = default;
    
    // Explicitly define copy constructor and assignment operator
    Experiment(const Experiment& other);
    Experiment& operator=(const Experiment& other);
    
    // Move semantics
    Experiment(Experiment&& other) noexcept;
    Experiment& operator=(Experiment&& other) noexcept;
    
    // Add a variant to the experiment
    void addVariant(const std::string& variantId, const StrategyVariant& variant, double trafficAllocation = 0.0);
    
    // Set traffic allocation for a variant (validates allocation sums to 1.0)
    void setTrafficAllocation(const std::string& variantId, double allocation);
    void normalizeTrafficAllocations();
    
    // Get information
    const std::string& getId() const { return id_; }
    const std::string& getName() const { return name_; }
    std::vector<std::string> getVariantIds() const;
    StrategyVariant getVariant(const std::string& variantId) const;
    double getTrafficAllocation(const std::string& variantId) const;
    
    // Status management
    bool isActive() const { return active_; }
    void setActive(bool active) { active_ = active; }
    
    // Duration management
    void setStartTime(std::chrono::system_clock::time_point time) { startTime_ = time; }
    void setEndTime(std::chrono::system_clock::time_point time) { endTime_ = time; }
    std::chrono::system_clock::time_point getStartTime() const { return startTime_; }
    std::chrono::system_clock::time_point getEndTime() const { return endTime_; }
    
    // Validation
    bool isValid() const;

private:
    std::string id_;
    std::string name_;
    std::atomic<bool> active_{false};
    std::chrono::system_clock::time_point startTime_;
    std::chrono::system_clock::time_point endTime_;
    std::map<std::string, StrategyVariant> variants_;
    std::map<std::string, double> trafficAllocations_;
    mutable std::mutex mutex_;
};

/**
 * Configuration for experiment optimization
 */
struct ExperimentConfig {
    bool automaticOptimizationEnabled = false;
    bool earlyStoppingEnabled = false;
    double confidenceThreshold = 0.95;
    size_t minSampleSize = 30;
    double explorationRate = 0.2; // For multi-armed bandit
    
    // Hash-based assignment settings
    bool useConsistentHashing = true;
    std::vector<std::string> hashFields = {"exchange", "symbol", "timeWindow"}; // Which fields to include in hash
};

/**
 * Improved A/B testing framework for trading strategies
 */
class ABTestingFramework {
public:
    ABTestingFramework();
    ~ABTestingFramework() = default;
    
    // Experiment management
    void defineExperiment(const Experiment& experiment, const ExperimentConfig& config = ExperimentConfig{});
    void removeExperiment(const std::string& experimentId);
    Experiment getExperiment(const std::string& experimentId) const;
    std::vector<std::string> getActiveExperimentIds() const;
    
    // Variant assignment
    StrategyVariant assignVariant(const std::string& experimentId, 
                                const data_ingestion::OrderBookUpdate& snapshot);
    
    // Performance tracking
    void recordOutcome(const std::string& experimentId, 
                      const std::string& variantId, 
                      const PerformanceMetrics& metrics);
    
    // Results and analysis
    ExperimentResults getResults(const std::string& experimentId) const;
    std::map<std::string, ExperimentResults> getAllResults() const;
    
    // Experiment lifecycle
    void startExperiment(const std::string& experimentId);
    void stopExperiment(const std::string& experimentId);
    
    // Configuration updates
    void updateConfig(const std::string& experimentId, const ExperimentConfig& config);
    ExperimentConfig getConfig(const std::string& experimentId) const;
    
    // Health monitoring
    struct FrameworkStats {
        size_t activeExperiments = 0;
        size_t totalAssignments = 0;
        size_t totalOutcomes = 0;
        std::chrono::system_clock::time_point lastActivity;
    };
    FrameworkStats getStats() const;

private:
    // Core data structures
    std::map<std::string, Experiment> experiments_;
    std::map<std::string, ExperimentResults> results_;
    std::map<std::string, ExperimentConfig> configs_;
    
    // Statistics tracking
    std::atomic<size_t> totalAssignments_{0};
    std::atomic<size_t> totalOutcomes_{0};
    mutable std::chrono::system_clock::time_point lastActivity_;
    
    // Thread safety
    mutable std::shared_mutex mutex_; // Use shared_mutex for reader-writer access
    
    // Hash-based assignment
    uint64_t computeConsistentHash(const data_ingestion::OrderBookUpdate& snapshot,
                                  const std::vector<std::string>& hashFields) const;
    uint64_t hashString(const std::string& str) const;
    uint64_t hashDouble(double value, int precision = 5) const; // Fixed precision hashing
    uint64_t combineHashes(uint64_t hash1, uint64_t hash2) const;
    
    // Variant selection algorithms
    std::string selectVariantConsistent(const Experiment& experiment, 
                                       const data_ingestion::OrderBookUpdate& snapshot,
                                       const ExperimentConfig& config) const;
    std::string selectVariantRandom(const Experiment& experiment) const;
    
    // Statistical analysis
    bool isStatisticallySignificant(const ExperimentResults& results, 
                                   double confidenceLevel,
                                   size_t minSampleSize) const;
    double calculateZScore(const VariantResult& variant1, const VariantResult& variant2) const;
    
    // Multi-armed bandit optimization
    void updateTrafficAllocation(const std::string& experimentId);
    std::vector<double> thompsonSampling(const std::vector<VariantResult>& variants) const;
    
    // Utility functions
    void updateLastActivity() const;
    bool hasMinimumData(const VariantResult& variant, size_t minSampleSize) const;
    void cleanupOldMetrics(VariantResult& variant, size_t maxMetrics = 1000) const;
    
    // Thread-local random number generator for better performance
    thread_local static std::mt19937 tlsRng_;
    static std::mt19937& getTLSRng();
};

} // namespace strategy
} // namespace trading
