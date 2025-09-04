// ab_testing_framework.cpp
#include "strategy/optimization/ab_testing_framework.hpp"
#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <iostream>
#include <shared_mutex>
#include <numeric>

namespace trading {
namespace strategy {

// Thread-local RNG initialization
thread_local std::mt19937 ABTestingFramework::tlsRng_(std::random_device{}());

std::mt19937& ABTestingFramework::getTLSRng() {
    return tlsRng_;
}

// Experiment Implementation
Experiment::Experiment(const std::string& id, const std::string& name)
    : id_(id), name_(name) {
}

// Copy constructor
Experiment::Experiment(const Experiment& other) {
    std::lock_guard<std::mutex> lock(other.mutex_);
    id_ = other.id_;
    name_ = other.name_;
    active_ = other.active_.load();
    startTime_ = other.startTime_;
    endTime_ = other.endTime_;
    variants_ = other.variants_;
    trafficAllocations_ = other.trafficAllocations_;
}

// Copy assignment operator
Experiment& Experiment::operator=(const Experiment& other) {
    if (this != &other) {
        // Lock both mutexes in consistent order to avoid deadlock
        std::lock(mutex_, other.mutex_);
        std::lock_guard<std::mutex> lock1(mutex_, std::adopt_lock);
        std::lock_guard<std::mutex> lock2(other.mutex_, std::adopt_lock);
        
        id_ = other.id_;
        name_ = other.name_;
        active_ = other.active_.load();
        startTime_ = other.startTime_;
        endTime_ = other.endTime_;
        variants_ = other.variants_;
        trafficAllocations_ = other.trafficAllocations_;
    }
    return *this;
}

// Move constructor
Experiment::Experiment(Experiment&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.mutex_);
    id_ = std::move(other.id_);
    name_ = std::move(other.name_);
    active_ = other.active_.load();
    startTime_ = other.startTime_;
    endTime_ = other.endTime_;
    variants_ = std::move(other.variants_);
    trafficAllocations_ = std::move(other.trafficAllocations_);
}

// Move assignment operator
Experiment& Experiment::operator=(Experiment&& other) noexcept {
    if (this != &other) {
        std::lock(mutex_, other.mutex_);
        std::lock_guard<std::mutex> lock1(mutex_, std::adopt_lock);
        std::lock_guard<std::mutex> lock2(other.mutex_, std::adopt_lock);
        
        id_ = std::move(other.id_);
        name_ = std::move(other.name_);
        active_ = other.active_.load();
        startTime_ = other.startTime_;
        endTime_ = other.endTime_;
        variants_ = std::move(other.variants_);
        trafficAllocations_ = std::move(other.trafficAllocations_);
    }
    return *this;
}

void Experiment::addVariant(const std::string& variantId, const StrategyVariant& variant, double trafficAllocation) {
    std::lock_guard<std::mutex> lock(mutex_);
    variants_[variantId] = variant;
    trafficAllocations_[variantId] = trafficAllocation;
}

void Experiment::setTrafficAllocation(const std::string& variantId, double allocation) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (variants_.find(variantId) != variants_.end() && allocation >= 0.0 && allocation <= 1.0) {
        trafficAllocations_[variantId] = allocation;
    }
}

void Experiment::normalizeTrafficAllocations() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    double total = 0.0;
    for (const auto& allocation : trafficAllocations_) {
        total += allocation.second;
    }
    
    if (total > 0.0 && std::abs(total - 1.0) > 0.001) {
        for (auto& allocation : trafficAllocations_) {
            allocation.second /= total;
        }
    }
}

std::vector<std::string> Experiment::getVariantIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(variants_.size());
    for (const auto& variant : variants_) {
        ids.push_back(variant.first);
    }
    return ids;
}

StrategyVariant Experiment::getVariant(const std::string& variantId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = variants_.find(variantId);
    return (it != variants_.end()) ? it->second : StrategyVariant{};
}

double Experiment::getTrafficAllocation(const std::string& variantId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = trafficAllocations_.find(variantId);
    return (it != trafficAllocations_.end()) ? it->second : 0.0;
}

bool Experiment::isValid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !id_.empty() && !variants_.empty();
}

// ABTestingFramework Implementation
ABTestingFramework::ABTestingFramework() {
    updateLastActivity();
}

void ABTestingFramework::defineExperiment(const Experiment& experiment, const ExperimentConfig& config) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    if (!experiment.isValid()) {
        throw std::invalid_argument("Invalid experiment configuration");
    }
    
    // Make a copy of the experiment since we're storing by value
    experiments_[experiment.getId()] = experiment;
    configs_[experiment.getId()] = config;
    
    // Initialize results
    results_[experiment.getId()] = ExperimentResults{};
    auto& results = results_[experiment.getId()];
    results.experimentId = experiment.getId();
    results.experimentName = experiment.getName();
    
    // Initialize variant results
    for (const auto& variantId : experiment.getVariantIds()) {
        results.variantResults[variantId] = VariantResult{};
        results.variantResults[variantId].variantId = variantId;
    }
    
    updateLastActivity();
}

void ABTestingFramework::removeExperiment(const std::string& experimentId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    experiments_.erase(experimentId);
    results_.erase(experimentId);
    configs_.erase(experimentId);
    
    updateLastActivity();
}

Experiment ABTestingFramework::getExperiment(const std::string& experimentId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = experiments_.find(experimentId);
    return (it != experiments_.end()) ? it->second : Experiment{};
}

std::vector<std::string> ABTestingFramework::getActiveExperimentIds() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    std::vector<std::string> activeIds;
    for (const auto& exp : experiments_) {
        if (exp.second.isActive()) {
            activeIds.push_back(exp.first);
        }
    }
    return activeIds;
}

StrategyVariant ABTestingFramework::assignVariant(const std::string& experimentId, 
                                                const data_ingestion::OrderBookUpdate& snapshot) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto expIt = experiments_.find(experimentId);
    auto configIt = configs_.find(experimentId);
    
    if (expIt == experiments_.end() || !expIt->second.isActive() || configIt == configs_.end()) {
        return StrategyVariant{};
    }
    
    const auto& experiment = expIt->second;
    const auto& config = configIt->second;
    
    std::string selectedVariantId;
    
    if (config.useConsistentHashing) {
        selectedVariantId = selectVariantConsistent(experiment, snapshot, config);
    } else {
        selectedVariantId = selectVariantRandom(experiment);
    }
    
    if (!selectedVariantId.empty()) {
        // Record assignment (need to upgrade to unique lock)
        lock.unlock();
        std::unique_lock<std::shared_mutex> uniqueLock(mutex_);
        
        auto& results = results_[experimentId];
        results.variantResults[selectedVariantId].impressions++;
        totalAssignments_++;
        updateLastActivity();
        
        return experiment.getVariant(selectedVariantId);
    }
    
    return StrategyVariant{};
}

std::string ABTestingFramework::selectVariantConsistent(const Experiment& experiment,
                                                       const data_ingestion::OrderBookUpdate& snapshot,
                                                       const ExperimentConfig& config) const {
    uint64_t hash = computeConsistentHash(snapshot, config.hashFields);
    
    // Get sorted variants with allocations for deterministic selection
    std::vector<std::pair<std::string, double>> variantsWithAllocation;
    double totalAllocation = 0.0;
    
    for (const auto& variantId : experiment.getVariantIds()) {
        double allocation = experiment.getTrafficAllocation(variantId);
        variantsWithAllocation.emplace_back(variantId, allocation);
        totalAllocation += allocation;
    }
    
    // Normalize if needed
    if (totalAllocation > 0.0 && std::abs(totalAllocation - 1.0) > 0.001) {
        for (auto& variant : variantsWithAllocation) {
            variant.second /= totalAllocation;
        }
    }
    
    // Deterministic selection based on hash
    double hashNormalized = static_cast<double>(hash % 10000) / 10000.0;
    double cumulativeProbability = 0.0;
    
    for (const auto& variant : variantsWithAllocation) {
        cumulativeProbability += variant.second;
        if (hashNormalized < cumulativeProbability) {
            return variant.first;
        }
    }
    
    // Fallback to last variant
    return variantsWithAllocation.empty() ? "" : variantsWithAllocation.back().first;
}

std::string ABTestingFramework::selectVariantRandom(const Experiment& experiment) const {
    auto& rng = getTLSRng();
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double random = dist(rng);
    
    std::vector<std::string> variantIds = experiment.getVariantIds();
    double cumulativeProbability = 0.0;
    
    for (const auto& variantId : variantIds) {
        cumulativeProbability += experiment.getTrafficAllocation(variantId);
        if (random < cumulativeProbability) {
            return variantId;
        }
    }
    
    return variantIds.empty() ? "" : variantIds.back();
}

uint64_t ABTestingFramework::computeConsistentHash(const data_ingestion::OrderBookUpdate& snapshot,
                                                  const std::vector<std::string>& hashFields) const {
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    
    for (const auto& field : hashFields) {
        if (field == "exchange") {
            hash = combineHashes(hash, hashString(snapshot.exchangeId.toString()));
        } else if (field == "symbol") {
            hash = combineHashes(hash, hashString(snapshot.symbol.toString()));
        } else if (field == "timeWindow") {
            // Use daily time buckets for consistency
            uint64_t timeBucket = snapshot.exchange_timestamp / (24 * 60 * 60 * 1000);
            hash = combineHashes(hash, timeBucket);
        } else if (field == "price" && !snapshot.bids.empty() && !snapshot.asks.empty()) {
            double midPrice = (snapshot.bids[0].price + snapshot.asks[0].price) / 2.0;
            hash = combineHashes(hash, hashDouble(midPrice));
        }
    }
    
    return hash;
}

uint64_t ABTestingFramework::hashString(const std::string& str) const {
    // FNV-1a hash
    uint64_t hash = 14695981039346656037ULL;
    for (char c : str) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

uint64_t ABTestingFramework::hashDouble(double value, int precision) const {
    // Convert to fixed-point representation for stable hashing
    int64_t fixedPoint = static_cast<int64_t>(value * std::pow(10, precision));
    return static_cast<uint64_t>(fixedPoint);
}

uint64_t ABTestingFramework::combineHashes(uint64_t hash1, uint64_t hash2) const {
    // Better hash combination than XOR
    return hash1 * 31 + hash2;
}

void ABTestingFramework::recordOutcome(const std::string& experimentId,
                                     const std::string& variantId,
                                     const PerformanceMetrics& metrics) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto expIt = results_.find(experimentId);
    if (expIt == results_.end()) {
        return;
    }
    
    auto varIt = expIt->second.variantResults.find(variantId);
    if (varIt == expIt->second.variantResults.end()) {
        return;
    }
    
    VariantResult& result = varIt->second;
    result.conversions++;
    result.metrics.push_back(metrics);
    
    // Clean up old metrics to prevent memory bloat
    cleanupOldMetrics(result);
    
    // Recalculate aggregate metrics
    if (!result.metrics.empty()) {
        size_t count = result.metrics.size();
        result.aggregateMetrics = PerformanceMetrics{};
        
        for (const auto& m : result.metrics) {
            result.aggregateMetrics.sharpeRatio += m.sharpeRatio;
            result.aggregateMetrics.totalReturn += m.totalReturn;
            result.aggregateMetrics.maxDrawdown = std::max(result.aggregateMetrics.maxDrawdown, m.maxDrawdown);
            result.aggregateMetrics.winRate += m.winRate;
        }
        
        result.aggregateMetrics.sharpeRatio /= count;
        result.aggregateMetrics.totalReturn /= count;
        result.aggregateMetrics.winRate /= count;
    }
    
    // Update conversion rate
    if (result.impressions > 0) {
        result.conversionRate = static_cast<double>(result.conversions) / result.impressions;
    }
    
    expIt->second.lastUpdateTime = std::chrono::system_clock::now();
    totalOutcomes_++;
    updateLastActivity();
    
    // Check for automatic optimization and early stopping
    auto configIt = configs_.find(experimentId);
    if (configIt != configs_.end()) {
        const auto& config = configIt->second;
        
        if (config.automaticOptimizationEnabled) {
            updateTrafficAllocation(experimentId);
        }
        
        if (config.earlyStoppingEnabled && 
            isStatisticallySignificant(expIt->second, config.confidenceThreshold, config.minSampleSize)) {
            // Implement early stopping logic here
            // For now, just log it
            std::cout << "Early stopping triggered for experiment: " << experimentId << std::endl;
        }
    }
}

bool ABTestingFramework::isStatisticallySignificant(const ExperimentResults& results,
                                                   double confidenceLevel,
                                                   size_t minSampleSize) const {
    if (results.variantResults.size() < 2) {
        return false;
    }
    
    // Find best performing variant
    std::string bestVariantId;
    double bestSharpe = -std::numeric_limits<double>::infinity();
    
    for (const auto& variant : results.variantResults) {
        if (hasMinimumData(variant.second, minSampleSize) &&
            variant.second.aggregateMetrics.sharpeRatio > bestSharpe) {
            bestVariantId = variant.first;
            bestSharpe = variant.second.aggregateMetrics.sharpeRatio;
        }
    }
    
    if (bestVariantId.empty()) {
        return false;
    }
    
    const auto& bestVariant = results.variantResults.at(bestVariantId);
    
    // Check significance against all other variants
    for (const auto& variant : results.variantResults) {
        if (variant.first == bestVariantId || !hasMinimumData(variant.second, minSampleSize)) {
            continue;
        }
        
        double zScore = calculateZScore(bestVariant, variant.second);
        double zThreshold = (confidenceLevel >= 0.99) ? 2.576 :
                           (confidenceLevel >= 0.95) ? 1.96 :
                           (confidenceLevel >= 0.90) ? 1.645 : 1.28;
        
        if (zScore < zThreshold) {
            return false;
        }
    }
    
    return true;
}

double ABTestingFramework::calculateZScore(const VariantResult& variant1, const VariantResult& variant2) const {
    if (variant1.metrics.empty() || variant2.metrics.empty()) {
        return 0.0;
    }
    
    double mean1 = variant1.aggregateMetrics.sharpeRatio;
    double mean2 = variant2.aggregateMetrics.sharpeRatio;
    
    // Calculate standard deviations
    auto calcStdDev = [](const std::vector<PerformanceMetrics>& metrics, double mean) {
        double sumSq = 0.0;
        for (const auto& m : metrics) {
            double diff = m.sharpeRatio - mean;
            sumSq += diff * diff;
        }
        return std::sqrt(sumSq / metrics.size());
    };
    
    double stdDev1 = calcStdDev(variant1.metrics, mean1);
    double stdDev2 = calcStdDev(variant2.metrics, mean2);
    
    // Calculate pooled standard error
    double stdError = std::sqrt(
        (stdDev1 * stdDev1 / variant1.metrics.size()) +
        (stdDev2 * stdDev2 / variant2.metrics.size())
    );
    
    return (stdError > 0.0) ? (mean1 - mean2) / stdError : 0.0;
}

void ABTestingFramework::updateTrafficAllocation(const std::string& experimentId) {
    auto resultsIt = results_.find(experimentId);
    auto experimentIt = experiments_.find(experimentId);
    auto configIt = configs_.find(experimentId);
    
    if (resultsIt == results_.end() || experimentIt == experiments_.end() || configIt == configs_.end()) {
        return;
    }
    
    const auto& config = configIt->second;
    auto& experiment = experimentIt->second;
    const auto& results = resultsIt->second;
    
    std::vector<std::string> variantIds = experiment.getVariantIds();
    
    // Check if we have sufficient data
    std::vector<VariantResult> validVariants;
    for (const auto& variantId : variantIds) {
        auto varIt = results.variantResults.find(variantId);
        if (varIt != results.variantResults.end() && 
            hasMinimumData(varIt->second, config.minSampleSize)) {
            validVariants.push_back(varIt->second);
        }
    }
    
    if (validVariants.size() < 2) {
        // Not enough data, use equal allocation
        double equalAllocation = 1.0 / variantIds.size();
        for (const auto& variantId : variantIds) {
            experiment.setTrafficAllocation(variantId, equalAllocation);
        }
        return;
    }
    
    // Thompson sampling for multi-armed bandit
    std::vector<double> samples = thompsonSampling(validVariants);
    
    // Find best performing variant
    size_t bestIdx = std::max_element(samples.begin(), samples.end()) - samples.begin();
    
    // Allocate traffic: more to best, but keep exploration
    for (size_t i = 0; i < variantIds.size(); ++i) {
        if (i < validVariants.size()) {
            if (i == bestIdx) {
                experiment.setTrafficAllocation(variantIds[i], 1.0 - config.explorationRate);
            } else {
                double remaining = config.explorationRate / (validVariants.size() - 1);
                experiment.setTrafficAllocation(variantIds[i], remaining);
            }
        } else {
            experiment.setTrafficAllocation(variantIds[i], 0.0);
        }
    }
}

std::vector<double> ABTestingFramework::thompsonSampling(const std::vector<VariantResult>& variants) const {
    auto& rng = getTLSRng();
    std::vector<double> samples;
    samples.reserve(variants.size());
    
    for (const auto& variant : variants) {
        double mean = variant.aggregateMetrics.sharpeRatio;
        
        // Calculate standard deviation
        double sumSq = 0.0;
        for (const auto& metric : variant.metrics) {
            double diff = metric.sharpeRatio - mean;
            sumSq += diff * diff;
        }
        double stdDev = std::sqrt(sumSq / variant.metrics.size()) + 0.001; // Add small epsilon
        
        std::normal_distribution<double> dist(mean, stdDev);
        samples.push_back(dist(rng));
    }
    
    return samples;
}

// Utility functions
void ABTestingFramework::updateLastActivity() const {
    lastActivity_ = std::chrono::system_clock::now();
}

bool ABTestingFramework::hasMinimumData(const VariantResult& variant, size_t minSampleSize) const {
    return variant.metrics.size() >= minSampleSize;
}

void ABTestingFramework::cleanupOldMetrics(VariantResult& variant, size_t maxMetrics) const {
    if (variant.metrics.size() > maxMetrics) {
        // Keep only the most recent metrics
        variant.metrics.erase(
            variant.metrics.begin(),
            variant.metrics.begin() + (variant.metrics.size() - maxMetrics)
        );
    }
}

ExperimentResults ABTestingFramework::getResults(const std::string& experimentId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    auto it = results_.find(experimentId);
    if (it != results_.end()) {
        return it->second;
    }
    
    ExperimentResults emptyResults;
    emptyResults.experimentId = experimentId;
    return emptyResults;
}

void ABTestingFramework::startExperiment(const std::string& experimentId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto it = experiments_.find(experimentId);
    if (it != experiments_.end()) {
        it->second.setActive(true);
        it->second.setStartTime(std::chrono::system_clock::now());
        updateLastActivity();
    }
}

void ABTestingFramework::stopExperiment(const std::string& experimentId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    
    auto it = experiments_.find(experimentId);
    if (it != experiments_.end()) {
        it->second.setActive(false);
        it->second.setEndTime(std::chrono::system_clock::now());
        updateLastActivity();
    }
}

ABTestingFramework::FrameworkStats ABTestingFramework::getStats() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    
    FrameworkStats stats;
    stats.activeExperiments = getActiveExperimentIds().size();
    stats.totalAssignments = totalAssignments_.load();
    stats.totalOutcomes = totalOutcomes_.load();
    stats.lastActivity = lastActivity_;
    
    return stats;
}

} // namespace strategy
} // namespace trading