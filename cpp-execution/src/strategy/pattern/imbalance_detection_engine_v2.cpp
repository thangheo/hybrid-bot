/**
 * @file imbalance_detection_engine.cpp
 * @brief Implementation of refactored ImbalanceDetectionEngine
 */
#include "strategy/pattern/imbalance_detection_engine_v2.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <chrono>

namespace trading {
namespace strategy {

ImbalanceDetectionEngineV2::ImbalanceDetectionEngineV2(const EngineConfiguration& config)
    : config_(config) {
    
    // Create core components
    detectorFactory_ = std::make_unique<detectors::DetectorFactory>();
    thresholdManager_ = std::make_unique<threshold::ThresholdManager>();
    signalAggregator_ = std::make_unique<aggregation::SignalAggregator>(config_.aggregationStrategy);
    signalPublisher_ = std::make_unique<publication::SignalPublisher>(config_.publicationMode);
    
    LOG_INFO("ImbalanceDetectionEngineV2: Created with {} enabled detector types", 
             config_.enabledDetectorTypes.size());
}

bool ImbalanceDetectionEngineV2::initialize(std::shared_ptr<trading::risk::RiskManager> riskManager) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        LOG_WARN("ImbalanceDetectionEngineV2: Already initialized");
        return true;
    }
    
    try {
        // Initialize components
        if (!initializeComponents()) {
            LOG_ERROR("ImbalanceDetectionEngineV2: Failed to initialize components");
            return false;
        }
        
        // Configure risk manager if provided
        if (riskManager) {
            signalPublisher_->setRiskManager(riskManager);
            LOG_INFO("ImbalanceDetectionEngineV2: Risk manager configured");
        } else if (config_.publicationMode == publication::PublicationMode::RISK_VALIDATED) {
            LOG_WARN("ImbalanceDetectionEngineV2: RISK_VALIDATED mode requires RiskManager");
        }
        
        // Create detector instances
        createDetectors();
        
        initialized_ = true;
        LOG_INFO("ImbalanceDetectionEngineV2: Successfully initialized with {} active detectors", 
                 detectors_.size());
        
    } catch (const std::exception& ex) {
        LOG_ERROR("ImbalanceDetectionEngineV2: Initialization failed: {}", ex.what());
        return false;
    }
    
    return true;
}

bool ImbalanceDetectionEngineV2::analyzeMetrics(const OrderBookMetrics& metrics) {
    if (!initialized_) {
        LOG_ERROR("ImbalanceDetectionEngineV2: Engine not initialized");
        return false;
    }
    
    auto startTime = std::chrono::steady_clock::now();
    bool detected = false;
    bool published = false;
    
    try {
        // Update statistics
        statistics_.totalAnalyses++;
        statistics_.lastAnalysis = startTime;
        
        // Step 1: Get dynamic thresholds for current regime
        ThresholdSet thresholds;
        // Note: In real implementation, we'd populate thresholds from ThresholdManager
        // For now, using default thresholds
        
        // Step 2: Run all detectors
        auto detectorResults = runDetectors(metrics, thresholds);
        
        if (detectorResults.empty()) {
            LOG_DEBUG("ImbalanceDetectionEngineV2: No detector results");
            return true; // Not an error, just no signals
        }
        
        // Step 3: Aggregate results
        auto aggregatedSignal = aggregateResults(detectorResults);
        
        if (!aggregatedSignal.hasSignal) {
            LOG_DEBUG("ImbalanceDetectionEngineV2: No aggregated signal");
            return true;
        }
        
        detected = true;
        statistics_.successfulDetections++;
        
        // Step 4: Create context and publish
        auto context = createSignalContext(metrics);
        published = publishSignal(aggregatedSignal, context);
        
        if (published) {
            statistics_.publishedSignals++;
            LOG_INFO("ImbalanceDetectionEngineV2: Signal published - strength: {}, confidence: {}, direction: {}",
                     aggregatedSignal.overallStrength, aggregatedSignal.overallConfidence,
                     static_cast<int>(aggregatedSignal.dominantDirection));
        } else {
            statistics_.rejectedByRisk++;
            LOG_DEBUG("ImbalanceDetectionEngineV2: Signal rejected during publication");
        }
        
    } catch (const std::exception& ex) {
        LOG_ERROR("ImbalanceDetectionEngineV2: Analysis failed: {}", ex.what());
        return false;
    }
    
    // Update timing statistics
    auto endTime = std::chrono::steady_clock::now();
    auto processingTime = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
    updateStatistics(detected, published, processingTime);
    
    return true;
}

void ImbalanceDetectionEngineV2::updateConfiguration(const EngineConfiguration& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    config_ = config;
    
    // Update component configurations
    if (signalAggregator_) {
        signalAggregator_->setAggregationStrategy(config_.aggregationStrategy);
        signalAggregator_->setMinConsensusLevel(config_.minConsensusLevel);
        signalAggregator_->setMinStrengthThreshold(config_.minStrengthThreshold);
    }
    
    if (signalPublisher_) {
        signalPublisher_->setPublicationMode(config_.publicationMode);
        signalPublisher_->setMinPublicationStrength(config_.minPublicationStrength);
        signalPublisher_->setMinPublicationConfidence(config_.minPublicationConfidence);
    }
    
    if (detectorFactory_) {
        detectorFactory_->setEnabledDetectors(config_.enabledDetectorTypes);
        // Recreate detectors with new configuration
        createDetectors();
    }
    
    LOG_INFO("ImbalanceDetectionEngineV2: Configuration updated");
}

const EngineConfiguration& ImbalanceDetectionEngineV2::getConfiguration() const {
    return config_;
}

EngineStatistics ImbalanceDetectionEngineV2::getStatistics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return statistics_;
}

void ImbalanceDetectionEngineV2::resetStatistics() {
    std::lock_guard<std::mutex> lock(mutex_);
    statistics_ = EngineStatistics{};
    LOG_INFO("ImbalanceDetectionEngineV2: Statistics reset");
}

void ImbalanceDetectionEngineV2::updateThresholdStats(int regime, double return_, double volatility) {
    if (thresholdManager_) {
        thresholdManager_->updateStats(regime, return_, volatility);
    }
}

size_t ImbalanceDetectionEngineV2::getActiveDetectorCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return detectors_.size();
}

bool ImbalanceDetectionEngineV2::isInitialized() const {
    return initialized_;
}

// Private methods
bool ImbalanceDetectionEngineV2::initializeComponents() {
    // Configure aggregator
    signalAggregator_->setAggregationStrategy(config_.aggregationStrategy);
    signalAggregator_->setMinConsensusLevel(config_.minConsensusLevel);
    signalAggregator_->setMinStrengthThreshold(config_.minStrengthThreshold);
    
    // Configure publisher
    signalPublisher_->setPublicationMode(config_.publicationMode);
    signalPublisher_->setMinPublicationStrength(config_.minPublicationStrength);
    signalPublisher_->setMinPublicationConfidence(config_.minPublicationConfidence);
    
    // Configure detector factory
    detectorFactory_->setEnabledDetectors(config_.enabledDetectorTypes);
    
    return true;
}

void ImbalanceDetectionEngineV2::createDetectors() {
    detectors_.clear();
    
    if (!detectorFactory_) {
        LOG_ERROR("ImbalanceDetectionEngineV2: DetectorFactory not available");
        return;
    }
    
    detectors_ = detectorFactory_->createAllDetectors();
    
    LOG_INFO("ImbalanceDetectionEngineV2: Created {} detector instances", detectors_.size());
}

publication::SignalContext ImbalanceDetectionEngineV2::createSignalContext(const OrderBookMetrics& metrics) const {
    publication::SignalContext context;
    context.exchangeId = metrics.exchangeId;
    context.symbol = metrics.symbol;
    context.timestamp = metrics.timestamp;
    context.currentRegime = 0; // Would be determined from regime detection
    context.currentVolatility = metrics.volatilityExpectation;
    context.source = "ImbalanceDetectionEngineV2";
    
    return context;
}

void ImbalanceDetectionEngineV2::updateStatistics(bool detected, bool published, 
                                                  std::chrono::milliseconds processingTime) {
    // Update average processing time
    if (statistics_.totalAnalyses > 1) {
        auto totalTime = statistics_.averageProcessingTime.count() * (statistics_.totalAnalyses - 1) + 
                        processingTime.count();
        statistics_.averageProcessingTime = std::chrono::milliseconds(totalTime / statistics_.totalAnalyses);
    } else {
        statistics_.averageProcessingTime = processingTime;
    }
}

std::vector<detectors::DetectorAnalysisResult> ImbalanceDetectionEngineV2::runDetectors(
    const OrderBookMetrics& metrics, const ThresholdSet& thresholds) {
    
    std::vector<detectors::DetectorAnalysisResult> results;
    
    if (detectors_.empty()) {
        LOG_WARN("ImbalanceDetectionEngineV2: No detectors available");
        return results;
    }
    
    // Create detector context
    detectors::DetectorContext context;
    context.metrics = &metrics;
    context.thresholds = &thresholds;
    context.currentRegime = 0; // Would be from regime detection
    context.currentVolatility = metrics.volatilityExpectation;
    context.timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
        metrics.timestamp.time_since_epoch()).count();
    
    // Run all detectors
    for (auto& detector : detectors_) {
        if (!detector || !detector->isEnabled()) {
            continue;
        }
        
        try {
            auto result = detector->analyze(context);
            if (result.hasSignal) {
                results.push_back(result);
                LOG_DEBUG("ImbalanceDetectionEngineV2: Detector {} produced signal with strength {}",
                         detector->getName(), result.strength);
            }
        } catch (const std::exception& ex) {
            LOG_ERROR("ImbalanceDetectionEngineV2: Detector {} failed: {}", 
                     detector->getName(), ex.what());
        }
    }
    
    return results;
}

aggregation::AggregatedSignal ImbalanceDetectionEngineV2::aggregateResults(
    const std::vector<detectors::DetectorAnalysisResult>& results) const {
    
    if (!signalAggregator_) {
        LOG_ERROR("ImbalanceDetectionEngineV2: SignalAggregator not available");
        return aggregation::AggregatedSignal{};
    }
    
    return signalAggregator_->aggregateSignals(results);
}

bool ImbalanceDetectionEngineV2::publishSignal(const aggregation::AggregatedSignal& signal, 
                                               const publication::SignalContext& context) {
    
    if (!signalPublisher_) {
        LOG_ERROR("ImbalanceDetectionEngineV2: SignalPublisher not available");
        return false;
    }
    
    auto result = signalPublisher_->publishSignal(signal, context);
    
    if (!result.published) {
        if (result.reason.find("thresholds") != std::string::npos) {
            statistics_.rejectedByThreshold++;
        } else {
            statistics_.rejectedByRisk++;
        }
        
        LOG_DEBUG("ImbalanceDetectionEngineV2: Publication failed: {}", result.reason);
    }
    
    return result.published;
}

} // namespace strategy
} // namespace trading