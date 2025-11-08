/**
 * @file imbalance_detection_engine_v2.hpp
 * @brief Refactored ImbalanceDetectionEngine using modular architecture
 */
#pragma once

#include "strategy/pattern/detectors/detector_factory.hpp"
#include "strategy/pattern/threshold/threshold_manager.hpp"
#include "strategy/pattern/aggregation/signal_aggregator.hpp"
#include "strategy/pattern/publication/signal_publisher.hpp"
#include "strategy/types.hpp"
#include "risk/risk_manager.hpp"
#include <memory>
#include <vector>
#include <mutex>

namespace trading {
namespace strategy {

/**
 * @struct EngineConfiguration
 * @brief Configuration for the refactored engine
 */
struct EngineConfiguration {
    // Detector settings
    std::vector<ImbalanceType> enabledDetectorTypes = {
        ImbalanceType::VOLUME_IMBALANCE,
        ImbalanceType::ABNORMAL_SPREAD
    };
    
    // Aggregation settings
    aggregation::AggregationStrategy aggregationStrategy = aggregation::AggregationStrategy::CONFIDENCE_WEIGHTED;
    double minConsensusLevel = 0.6;
    double minStrengthThreshold = 0.1;
    //detectorTypes
    std::vector<std::string> detectorTypes = {"volume", "spread"};
    // Publication settings
    publication::PublicationMode publicationMode = publication::PublicationMode::RISK_VALIDATED;
    double minPublicationStrength = 0.2;
    double minPublicationConfidence = 0.1;
    
    // Processing settings
    size_t maxConcurrentDetections = 4;
    bool enableAsyncProcessing = true;
};

/**
 * @struct EngineStatistics
 * @brief Runtime statistics for monitoring
 */
struct EngineStatistics {
    size_t totalAnalyses = 0;
    size_t successfulDetections = 0;
    size_t publishedSignals = 0;
    size_t rejectedByThreshold = 0;
    size_t rejectedByRisk = 0;
    
    std::chrono::milliseconds averageProcessingTime{0};
    std::chrono::time_point<std::chrono::steady_clock> lastAnalysis;
    
    double getDetectionRate() const {
        return totalAnalyses > 0 ? static_cast<double>(successfulDetections) / totalAnalyses : 0.0;
    }
    
    double getPublicationRate() const {
        return successfulDetections > 0 ? static_cast<double>(publishedSignals) / successfulDetections : 0.0;
    }
};

/**
 * @class ImbalanceDetectionEngineV2
 * @brief Refactored engine using modular architecture
 * 
 * This is the new architecture demonstrating clean separation of concerns:
 * - DetectorFactory: Creates and manages detector instances
 * - ThresholdManager: Handles dynamic threshold calculation
 * - SignalAggregator: Combines results from multiple detectors
 * - SignalPublisher: Handles risk validation and event publishing
 */
class ImbalanceDetectionEngineV2 {
public:
    explicit ImbalanceDetectionEngineV2(const EngineConfiguration& config = EngineConfiguration{});
    ~ImbalanceDetectionEngineV2() = default;
    
    /**
     * @brief Initialize the engine with optional risk manager
     */
    bool initialize(std::shared_ptr<trading::risk::RiskManager> riskManager = nullptr);
    
    /**
     * @brief Main analysis method - replaces the monolithic original
     * @param metrics Order book metrics to analyze
     * @return True if analysis completed successfully
     */
    bool analyzeMetrics(const OrderBookMetrics& metrics);
    
    /**
     * @brief Update configuration at runtime
     */
    void updateConfiguration(const EngineConfiguration& config);
    
    /**
     * @brief Get current configuration
     */
    const EngineConfiguration& getConfiguration() const;
    
    /**
     * @brief Get runtime statistics
     */
    EngineStatistics getStatistics() const;
    
    /**
     * @brief Reset statistics
     */
    void resetStatistics();
    
    /**
     * @brief Update threshold statistics (for dynamic adaptation)
     */
    void updateThresholdStats(int regime, double return_, double volatility);
    
    /**
     * @brief Get number of active detectors
     */
    size_t getActiveDetectorCount() const;
    
    /**
     * @brief Check if engine is properly initialized
     */
    bool isInitialized() const;
    
private:
    // Configuration and state
    EngineConfiguration config_;
    bool initialized_ = false;
    mutable std::mutex mutex_;
    
    // Core components
    std::unique_ptr<detectors::DetectorFactory> detectorFactory_;
    std::unique_ptr<threshold::ThresholdManager> thresholdManager_;
    std::unique_ptr<aggregation::SignalAggregator> signalAggregator_;
    std::unique_ptr<publication::SignalPublisher> signalPublisher_;
    
    // Detector instances (created by factory)
    std::vector<std::unique_ptr<detectors::IImbalanceDetector>> detectors_;
    
    // Statistics and monitoring
    mutable EngineStatistics statistics_;
    
    // Helper methods
    bool initializeComponents();
    void createDetectors();
    publication::SignalContext createSignalContext(const OrderBookMetrics& metrics) const;
    void updateStatistics(bool detected, bool published, 
                         std::chrono::milliseconds processingTime);
    
    // Analysis pipeline
    std::vector<detectors::DetectorAnalysisResult> runDetectors(
        const OrderBookMetrics& metrics, const ThresholdSet& thresholds);
    aggregation::AggregatedSignal aggregateResults(
        const std::vector<detectors::DetectorAnalysisResult>& results) const;
    bool publishSignal(const aggregation::AggregatedSignal& signal, 
                      const publication::SignalContext& context);
};

} // namespace strategy
} // namespace trading