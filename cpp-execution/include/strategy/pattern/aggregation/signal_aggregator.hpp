/**
 * @file signal_aggregator.hpp
 * @brief Aggregates signals from multiple imbalance detectors
 */
#pragma once

#include "strategy/pattern/detectors/imbalance_detector_interface.hpp"
#include "strategy/types.hpp"
#include <vector>
#include <memory>

namespace trading {
namespace strategy {
namespace aggregation {

/**
 * @struct AggregatedSignal
 * @brief Combined signal from multiple detectors
 */
struct AggregatedSignal {
    bool hasSignal = false;
    double overallConfidence = 0.0;
    double overallStrength = 0.0;
    Direction dominantDirection = Direction::NEUTRAL;
    ImbalanceType primaryType = ImbalanceType::VOLUME_IMBALANCE;
    
    // Individual detector results for transparency
    std::vector<detectors::DetectorAnalysisResult> contributingResults;
    
    // Aggregation metadata
    size_t detectorCount = 0;
    size_t activeDetectorCount = 0;
    double consensusLevel = 0.0; // 0.0-1.0, how much detectors agree
    
    bool isValid() const {
        return std::isfinite(overallConfidence) && std::isfinite(overallStrength) &&
               overallConfidence >= 0.0 && overallConfidence <= 1.0 &&
               overallStrength >= 0.0 && detectorCount > 0;
    }
};

/**
 * @enum AggregationStrategy
 * @brief Different strategies for combining detector results
 */
enum class AggregationStrategy {
    AVERAGE,        // Simple average of all signals
    WEIGHTED,       // Weighted by confidence
    CONSENSUS,      // Require majority agreement
    MAX_STRENGTH,   // Take strongest signal
    CONFIDENCE_WEIGHTED // Weight by both confidence and strength
};

/**
 * @class SignalAggregator
 * @brief Combines signals from multiple imbalance detectors
 * 
 * Takes results from multiple detectors and produces a unified signal
 * using configurable aggregation strategies.
 */
class SignalAggregator {
public:
    explicit SignalAggregator(AggregationStrategy strategy = AggregationStrategy::CONFIDENCE_WEIGHTED);
    ~SignalAggregator() = default;
    
    /**
     * @brief Aggregate signals from multiple detectors
     * @param results Vector of detector analysis results
     * @return Aggregated signal combining all inputs
     */
    AggregatedSignal aggregateSignals(const std::vector<detectors::DetectorAnalysisResult>& results) const;
    
    /**
     * @brief Set aggregation strategy
     */
    void setAggregationStrategy(AggregationStrategy strategy);
    
    /**
     * @brief Get current aggregation strategy
     */
    AggregationStrategy getAggregationStrategy() const;
    
    /**
     * @brief Configure minimum consensus level for CONSENSUS strategy
     */
    void setMinConsensusLevel(double level);
    
    /**
     * @brief Configure minimum strength threshold for signals
     */
    void setMinStrengthThreshold(double threshold);
    
    /**
     * @brief Configure weights for different detector types (WEIGHTED strategy)
     */
    void setDetectorWeights(const std::map<ImbalanceType, double>& weights);
    
private:
    AggregationStrategy strategy_;
    double minConsensusLevel_ = 0.6;  // 60% agreement needed for CONSENSUS
    double minStrengthThreshold_ = 0.1;
    std::map<ImbalanceType, double> detectorWeights_;
    
    // Strategy implementations
    AggregatedSignal aggregateAverage(const std::vector<detectors::DetectorAnalysisResult>& results) const;
    AggregatedSignal aggregateWeighted(const std::vector<detectors::DetectorAnalysisResult>& results) const;
    AggregatedSignal aggregateConsensus(const std::vector<detectors::DetectorAnalysisResult>& results) const;
    AggregatedSignal aggregateMaxStrength(const std::vector<detectors::DetectorAnalysisResult>& results) const;
    AggregatedSignal aggregateConfidenceWeighted(const std::vector<detectors::DetectorAnalysisResult>& results) const;
    
    // Helper methods
    std::vector<detectors::DetectorAnalysisResult> filterActiveResults(
        const std::vector<detectors::DetectorAnalysisResult>& results) const;
    Direction calculateDominantDirection(const std::vector<detectors::DetectorAnalysisResult>& results) const;
    ImbalanceType calculatePrimaryType(const std::vector<detectors::DetectorAnalysisResult>& results) const;
    double calculateConsensusLevel(const std::vector<detectors::DetectorAnalysisResult>& results) const;
    double getDetectorWeight(ImbalanceType type) const;
};

} // namespace aggregation
} // namespace strategy
} // namespace trading