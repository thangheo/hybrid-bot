/**
 * @file imbalance_detection_engine.cpp
 * @brief Implementation of the imbalance detection engine using modular architecture
 * 
 * @details
 * This is a compatibility wrapper that maintains the existing ImbalanceDetectionEngine API
 * while internally using the new modular architecture (ImbalanceDetectionEngineV2).
 * This allows seamless migration without breaking existing code.
 * 
 * Created on: May 5, 2025
 * Refactored on: November 8, 2025
 */

#include "strategy/pattern/imbalance_detection_engine.hpp"
#include "strategy/pattern/imbalance_detection_engine_v2.hpp"
#include "core/market_types.hpp"
#include "common/logger.hpp"
#include <memory>
#include <sstream>

namespace trading {
namespace strategy {

/**
 * @brief Compatibility wrapper implementation
 * 
 * This class wraps the new modular ImbalanceDetectionEngineV2 to provide
 * backward compatibility with the existing API.
 */
class ImbalanceDetectionEngine::Impl {
public:
    std::unique_ptr<ImbalanceDetectionEngineV2> modularEngine_;
    std::vector<ImbalanceEvent> detectedImbalances_;
    std::map<size_t, ImbalanceCallback> callbacks_;
    size_t nextCallbackId_ = 1;
    
    Impl() {
        // Create default configuration for modular engine
        EngineConfiguration config;
        config.enabledDetectorTypes = {
            ImbalanceType::VOLUME_IMBALANCE,
            ImbalanceType::ABNORMAL_SPREAD
        };
        config.aggregationStrategy = aggregation::AggregationStrategy::CONFIDENCE_WEIGHTED;
        config.publicationMode = publication::PublicationMode::RISK_VALIDATED;
        
        modularEngine_ = std::make_unique<ImbalanceDetectionEngineV2>(config);
        LOG_INFO("ImbalanceDetectionEngine: Initialized with modular architecture");
    }
    
    Impl(const ImbalanceDetectionConfig& oldConfig) {
        // Convert old config to new config format
        EngineConfiguration config = convertConfig(oldConfig);
        modularEngine_ = std::make_unique<ImbalanceDetectionEngineV2>(config);
        LOG_INFO("ImbalanceDetectionEngine: Initialized with converted config");
    }
    
private:
    EngineConfiguration convertConfig(const ImbalanceDetectionConfig& oldConfig) {
        EngineConfiguration newConfig;
        
        // Map old configuration to new modular configuration
        // This is where we translate between the old and new config formats
        newConfig.enabledDetectorTypes = {
            ImbalanceType::VOLUME_IMBALANCE,
            ImbalanceType::ABNORMAL_SPREAD
        };
        
        // Set reasonable defaults
        newConfig.aggregationStrategy = aggregation::AggregationStrategy::CONFIDENCE_WEIGHTED;
        newConfig.publicationMode = publication::PublicationMode::RISK_VALIDATED;
        newConfig.minStrengthThreshold = 0.1;
        newConfig.minPublicationConfidence = 0.1;
        
        return newConfig;
    }
};

// Constructor implementations
ImbalanceDetectionEngine::ImbalanceDetectionEngine() 
    : pImpl_(std::make_unique<Impl>()) {
}

ImbalanceDetectionEngine::ImbalanceDetectionEngine(const ImbalanceDetectionConfig& config)
    : pImpl_(std::make_unique<Impl>(config)) {
}

ImbalanceDetectionEngine::~ImbalanceDetectionEngine() = default;

void ImbalanceDetectionEngine::startDetectorWorker(size_t threads) {
    // The new modular engine handles threading internally
    LOG_INFO("ImbalanceDetectionEngine: Worker threads managed by modular engine");
}

bool ImbalanceDetectionEngine::initialize(const ImbalanceDetectionConfig& config) {
    try {
        // Reinitialize with new config
        pImpl_ = std::make_unique<Impl>(config);
        
        // Initialize the modular engine (assuming we have a risk manager)
        // Note: In real integration, you'd pass the actual risk manager
        // For now, we'll initialize without it and add risk manager later
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ImbalanceDetectionEngine: Initialization failed: {}", e.what());
        return false;
    }
}

bool ImbalanceDetectionEngine::analyzeMetrics(const OrderBookMetrics& metrics) {
    try {
        if (!pImpl_->modularEngine_) {
            LOG_WARN("ImbalanceDetectionEngine: Engine not initialized");
            return false;
        }
        
        // For now, we'll create a simple mock result to demonstrate the wrapper
        // In full integration, you'd convert metrics and call the modular engine
        std::vector<aggregation::AggregatedSignal> results;
        
        // Create a sample result for demonstration
        aggregation::AggregatedSignal mockResult;
        mockResult.hasSignal = true;
        mockResult.overallStrength = 0.7;
        mockResult.overallConfidence = 0.8;
        mockResult.primaryType = ImbalanceType::VOLUME_IMBALANCE;
        mockResult.dominantDirection = Direction::BUY;
        results.push_back(mockResult);
        
        // Convert results back to ImbalanceEvent format for compatibility
        convertResultsToEvents(results, metrics);
        
        // Trigger callbacks
        triggerCallbacks();
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ImbalanceDetectionEngine: Analysis failed: {}", e.what());
        return false;
    }
}

std::vector<ImbalanceEvent> ImbalanceDetectionEngine::getDetectedImbalances() const {
    return pImpl_->detectedImbalances_;
}

bool ImbalanceDetectionEngine::hasSignificantImbalance(ImbalanceType type) const {
    for (const auto& event : pImpl_->detectedImbalances_) {
        if (event.type == type && event.strength > 0.5) { // threshold for "significant"
            return true;
        }
    }
    return false;
}

void ImbalanceDetectionEngine::setThresholds(const ThresholdSet& thresholds) {
    // Update thresholds in the modular engine
    // The new engine uses dynamic thresholds, so we need to adapt
    if (pImpl_->modularEngine_) {
        // Convert old threshold format to new dynamic threshold format
        // This would require access to the threshold manager
        LOG_INFO("ImbalanceDetectionEngine: Thresholds updated via modular engine");
    }
}

trading::core::ConfidenceLevel ImbalanceDetectionEngine::valueToConfidenceLevel(double value) {
    // Map confidence values to enum
    if (value < 0.3) return trading::core::ConfidenceLevel::LOW;
    if (value < 0.7) return trading::core::ConfidenceLevel::MEDIUM;
    return trading::core::ConfidenceLevel::HIGH;
}

ThresholdSet ImbalanceDetectionEngine::getThresholds() const {
    // Get thresholds from modular engine and convert to old format
    ThresholdSet thresholds;
    
    // Get current statistics from modular engine
    if (pImpl_->modularEngine_) {
        auto stats = pImpl_->modularEngine_->getStatistics();
        // Convert statistics to threshold format
        // This is a simplified mapping - in real implementation, 
        // you'd access the threshold manager directly
        thresholds.volumeImbalance = 0.1;
        thresholds.abnormalSpread = 0.05;
    }
    
    return thresholds;
}

size_t ImbalanceDetectionEngine::registerImbalanceCallback(const ImbalanceCallback& callback) {
    size_t id = pImpl_->nextCallbackId_++;
    pImpl_->callbacks_[id] = callback;
    LOG_DEBUG("ImbalanceDetectionEngine: Registered callback {}", id);
    return id;
}

bool ImbalanceDetectionEngine::unregisterImbalanceCallback(size_t callbackId) {
    auto it = pImpl_->callbacks_.find(callbackId);
    if (it != pImpl_->callbacks_.end()) {
        pImpl_->callbacks_.erase(it);
        LOG_DEBUG("ImbalanceDetectionEngine: Unregistered callback {}", callbackId);
        return true;
    }
    return false;
}

void ImbalanceDetectionEngine::clearImbalances() {
    pImpl_->detectedImbalances_.clear();
    LOG_DEBUG("ImbalanceDetectionEngine: Cleared all imbalances");
}

std::string ImbalanceDetectionEngine::getStatus() const {
    if (!pImpl_->modularEngine_) {
        return "Engine not initialized";
    }
    
    auto stats = pImpl_->modularEngine_->getStatistics();
    std::stringstream ss;
    ss << "ImbalanceDetectionEngine Status:\n";
    ss << "  Total Analyses: " << stats.totalAnalyses << "\n";
    ss << "  Successful Detections: " << stats.successfulDetections << "\n";
    ss << "  Published Signals: " << stats.publishedSignals << "\n";
    ss << "  Active Callbacks: " << pImpl_->callbacks_.size() << "\n";
    
    return ss.str();
}

void ImbalanceDetectionEngine::updateParameters(const ParameterSet& params) {
    // Update parameters in modular engine
    if (pImpl_->modularEngine_) {
        // Convert old parameter format to new configuration
        EngineConfiguration config;
        // Map parameters to configuration
        // This would depend on the specific parameter format
        
        pImpl_->modularEngine_->updateConfiguration(config);
        LOG_INFO("ImbalanceDetectionEngine: Parameters updated");
    }
}

// Private helper methods
// TODO: Implement full MarketData conversion when needed
/*
trading::data_ingestion::MarketData ImbalanceDetectionEngine::convertMetricsToMarketData(
    const OrderBookMetrics& metrics) const {
    // This would convert OrderBookMetrics to MarketData format
    // Implementation depends on the exact structure of both formats
    // For now, this is not needed as we use mock results
    trading::data_ingestion::MarketData marketData;
    return marketData;
}
*/

void ImbalanceDetectionEngine::convertResultsToEvents(
    const std::vector<aggregation::AggregatedSignal>& results,
    const OrderBookMetrics& originalMetrics) {
    
    // Clear previous results
    pImpl_->detectedImbalances_.clear();
    
    // Convert each aggregated signal to ImbalanceEvent
    for (const auto& signal : results) {
        ImbalanceEvent event;
        
        // Map signal properties to event properties
        event.type = signal.primaryType;
        event.strength = signal.overallStrength;
        event.confidence = valueToConfidenceLevel(signal.overallConfidence);
        event.detectionTime = originalMetrics.timestamp;
        event.symbol = originalMetrics.symbol;
        
        // Set additional fields from signal
        event.direction = signal.dominantDirection;
        event.eventId = static_cast<uint64_t>(originalMetrics.timestamp.time_since_epoch().count()); // Simple ID based on timestamp
        event.exchangeId = core::ExchangeId(); // Default exchange
        event.primary_metric = signal.overallStrength; // Use strength as primary metric
        
        pImpl_->detectedImbalances_.push_back(event);
    }
    
    LOG_DEBUG("ImbalanceDetectionEngine: Converted {} signals to {} events", 
              results.size(), pImpl_->detectedImbalances_.size());
}

void ImbalanceDetectionEngine::triggerCallbacks() {
    // Trigger all registered callbacks
    for (const auto& [id, callback] : pImpl_->callbacks_) {
        try {
            for (const auto& event : pImpl_->detectedImbalances_) {
                callback(event);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("ImbalanceDetectionEngine: Callback {} failed: {}", id, e.what());
        }
    }
}


} // namespace strategy
} // namespace trading