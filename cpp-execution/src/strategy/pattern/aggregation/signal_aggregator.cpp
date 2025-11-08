/**
 * @file signal_aggregator.cpp
 * @brief Implementation of SignalAggregator
 */
#include "strategy/pattern/aggregation/signal_aggregator.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace trading {
namespace strategy {
namespace aggregation {

SignalAggregator::SignalAggregator(AggregationStrategy strategy)
    : strategy_(strategy) {
    
    // Initialize default detector weights (equal weights)
    detectorWeights_[ImbalanceType::VOLUME_IMBALANCE] = 1.0;
    detectorWeights_[ImbalanceType::ABNORMAL_SPREAD] = 1.0;
    // Add more detector types as they are implemented
}

AggregatedSignal SignalAggregator::aggregateSignals(const std::vector<detectors::DetectorAnalysisResult>& results) const {
    if (results.empty()) {
        return AggregatedSignal{}; // Return empty signal
    }
    
    // Filter to active (signal-producing) results
    auto activeResults = filterActiveResults(results);
    
    AggregatedSignal aggregated;
    aggregated.detectorCount = results.size();
    aggregated.activeDetectorCount = activeResults.size();
    aggregated.contributingResults = activeResults;
    
    if (activeResults.empty()) {
        return aggregated; // No active signals
    }
    
    // Apply aggregation strategy
    switch (strategy_) {
        case AggregationStrategy::AVERAGE:
            aggregated = aggregateAverage(activeResults);
            break;
        case AggregationStrategy::WEIGHTED:
            aggregated = aggregateWeighted(activeResults);
            break;
        case AggregationStrategy::CONSENSUS:
            aggregated = aggregateConsensus(activeResults);
            break;
        case AggregationStrategy::MAX_STRENGTH:
            aggregated = aggregateMaxStrength(activeResults);
            break;
        case AggregationStrategy::CONFIDENCE_WEIGHTED:
            aggregated = aggregateConfidenceWeighted(activeResults);
            break;
        default:
            LOG_ERROR("SignalAggregator: Unknown aggregation strategy");
            return AggregatedSignal{};
    }
    
    // Set metadata
    aggregated.detectorCount = results.size();
    aggregated.activeDetectorCount = activeResults.size();
    aggregated.contributingResults = activeResults;
    aggregated.consensusLevel = calculateConsensusLevel(activeResults);
    
    return aggregated;
}

void SignalAggregator::setAggregationStrategy(AggregationStrategy strategy) {
    strategy_ = strategy;
}

AggregationStrategy SignalAggregator::getAggregationStrategy() const {
    return strategy_;
}

void SignalAggregator::setMinConsensusLevel(double level) {
    minConsensusLevel_ = std::clamp(level, 0.0, 1.0);
}

void SignalAggregator::setMinStrengthThreshold(double threshold) {
    minStrengthThreshold_ = std::max(0.0, threshold);
}

void SignalAggregator::setDetectorWeights(const std::map<ImbalanceType, double>& weights) {
    detectorWeights_ = weights;
}

// Strategy implementations
AggregatedSignal SignalAggregator::aggregateAverage(const std::vector<detectors::DetectorAnalysisResult>& results) const {
    AggregatedSignal signal;
    
    if (results.empty()) return signal;
    
    double totalConfidence = 0.0;
    double totalStrength = 0.0;
    
    for (const auto& result : results) {
        totalConfidence += result.confidence;
        totalStrength += result.strength;
    }
    
    signal.hasSignal = true;
    signal.overallConfidence = totalConfidence / results.size();
    signal.overallStrength = totalStrength / results.size();
    signal.dominantDirection = calculateDominantDirection(results);
    signal.primaryType = calculatePrimaryType(results);
    
    return signal;
}

AggregatedSignal SignalAggregator::aggregateWeighted(const std::vector<detectors::DetectorAnalysisResult>& results) const {
    AggregatedSignal signal;
    
    if (results.empty()) return signal;
    
    double totalWeightedConfidence = 0.0;
    double totalWeightedStrength = 0.0;
    double totalWeight = 0.0;
    
    for (const auto& result : results) {
        double weight = getDetectorWeight(result.type);
        totalWeightedConfidence += result.confidence * weight;
        totalWeightedStrength += result.strength * weight;
        totalWeight += weight;
    }
    
    if (totalWeight > 0.0) {
        signal.hasSignal = true;
        signal.overallConfidence = totalWeightedConfidence / totalWeight;
        signal.overallStrength = totalWeightedStrength / totalWeight;
        signal.dominantDirection = calculateDominantDirection(results);
        signal.primaryType = calculatePrimaryType(results);
    }
    
    return signal;
}

AggregatedSignal SignalAggregator::aggregateConsensus(const std::vector<detectors::DetectorAnalysisResult>& results) const {
    AggregatedSignal signal;
    
    if (results.empty()) return signal;
    
    double consensus = calculateConsensusLevel(results);
    
    if (consensus >= minConsensusLevel_) {
        // Use average aggregation if consensus is sufficient
        signal = aggregateAverage(results);
    }
    
    return signal;
}

AggregatedSignal SignalAggregator::aggregateMaxStrength(const std::vector<detectors::DetectorAnalysisResult>& results) const {
    AggregatedSignal signal;
    
    if (results.empty()) return signal;
    
    // Find result with maximum strength
    auto maxIt = std::max_element(results.begin(), results.end(),
        [](const auto& a, const auto& b) {
            return a.strength < b.strength;
        });
    
    if (maxIt != results.end()) {
        signal.hasSignal = true;
        signal.overallConfidence = maxIt->confidence;
        signal.overallStrength = maxIt->strength;
        signal.dominantDirection = maxIt->direction;
        signal.primaryType = maxIt->type;
    }
    
    return signal;
}

AggregatedSignal SignalAggregator::aggregateConfidenceWeighted(const std::vector<detectors::DetectorAnalysisResult>& results) const {
    AggregatedSignal signal;
    
    if (results.empty()) return signal;
    
    double totalWeightedConfidence = 0.0;
    double totalWeightedStrength = 0.0;
    double totalWeight = 0.0;
    
    for (const auto& result : results) {
        // Weight = detector_weight * confidence * strength
        double detectorWeight = getDetectorWeight(result.type);
        double weight = detectorWeight * result.confidence * std::max(result.strength, 0.1);
        
        totalWeightedConfidence += result.confidence * weight;
        totalWeightedStrength += result.strength * weight;
        totalWeight += weight;
    }
    
    if (totalWeight > 0.0) {
        signal.hasSignal = true;
        signal.overallConfidence = totalWeightedConfidence / totalWeight;
        signal.overallStrength = totalWeightedStrength / totalWeight;
        signal.dominantDirection = calculateDominantDirection(results);
        signal.primaryType = calculatePrimaryType(results);
    }
    
    return signal;
}

// Helper methods
std::vector<detectors::DetectorAnalysisResult> SignalAggregator::filterActiveResults(
    const std::vector<detectors::DetectorAnalysisResult>& results) const {
    
    std::vector<detectors::DetectorAnalysisResult> active;
    
    for (const auto& result : results) {
        if (result.hasSignal && result.strength >= minStrengthThreshold_) {
            active.push_back(result);
        }
    }
    
    return active;
}

Direction SignalAggregator::calculateDominantDirection(const std::vector<detectors::DetectorAnalysisResult>& results) const {
    if (results.empty()) return Direction::NEUTRAL;
    
    int buyCount = 0, sellCount = 0;
    double buyStrength = 0.0, sellStrength = 0.0;
    
    for (const auto& result : results) {
        if (result.direction == Direction::BUY) {
            buyCount++;
            buyStrength += result.strength;
        } else if (result.direction == Direction::SELL) {
            sellCount++;
            sellStrength += result.strength;
        }
    }
    
    if (buyStrength > sellStrength) {
        return Direction::BUY;
    } else if (sellStrength > buyStrength) {
        return Direction::SELL;
    } else {
        return Direction::NEUTRAL;
    }
}

ImbalanceType SignalAggregator::calculatePrimaryType(const std::vector<detectors::DetectorAnalysisResult>& results) const {
    if (results.empty()) return ImbalanceType::VOLUME_IMBALANCE;
    
    // Find type with highest strength
    auto maxIt = std::max_element(results.begin(), results.end(),
        [](const auto& a, const auto& b) {
            return a.strength < b.strength;
        });
    
    return maxIt->type;
}

double SignalAggregator::calculateConsensusLevel(const std::vector<detectors::DetectorAnalysisResult>& results) const {
    if (results.size() <= 1) return 1.0;
    
    // Calculate direction consensus
    Direction dominant = calculateDominantDirection(results);
    if (dominant == Direction::NEUTRAL) return 0.0;
    
    int agreementCount = 0;
    for (const auto& result : results) {
        if (result.direction == dominant) {
            agreementCount++;
        }
    }
    
    return static_cast<double>(agreementCount) / results.size();
}

double SignalAggregator::getDetectorWeight(ImbalanceType type) const {
    auto it = detectorWeights_.find(type);
    return (it != detectorWeights_.end()) ? it->second : 1.0;
}

} // namespace aggregation
} // namespace strategy
} // namespace trading