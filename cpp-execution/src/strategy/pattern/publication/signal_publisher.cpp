/**
 * @file signal_publisher.cpp
 * @brief Implementation of SignalPublisher
 */
#include "strategy/pattern/publication/signal_publisher.hpp"
#include "common/logger.hpp"
#include "risk/risk_manager.hpp" // Only included in implementation
// #include "common/events.hpp"  // Will be added when events are properly integrated
#include <chrono>

namespace trading {
namespace strategy {
namespace publication {

SignalPublisher::SignalPublisher(PublicationMode mode)
    : mode_(mode) {
}

void SignalPublisher::setRiskManager(std::shared_ptr<trading::risk::RiskManager> riskManager) {
    riskManager_ = riskManager;
}

void SignalPublisher::setRiskValidationCallback(RiskValidationCallback callback) {
    riskValidationCallback_ = callback;
}

PublicationResult SignalPublisher::publishSignal(const aggregation::AggregatedSignal& signal,
                                                 const SignalContext& context) {
    PublicationResult result;
    
    // Check if signal meets publication criteria
    if (!shouldPublish(signal)) {
        result.reason = "Signal below publication thresholds";
        return result;
    }
    
    // Convert to TradingSignal
    TradingSignal tradingSignal = convertToTradingSignal(signal, context);
    
    // Publish based on mode
    switch (mode_) {
        case PublicationMode::DIRECT:
            result = publishDirect(tradingSignal);
            break;
            
        case PublicationMode::RISK_VALIDATED:
            result = publishWithRiskValidation(tradingSignal);
            break;
            
        case PublicationMode::RISK_ADVISORY:
            result = publishWithRiskAdvisory(tradingSignal);
            break;
            
        default:
            result.reason = "Unknown publication mode";
            LOG_ERROR("SignalPublisher: Unknown publication mode");
            return result;
    }
    
    // Emit imbalance event if published successfully
    if (result.published) {
        emitImbalanceEvent(tradingSignal, signal);
        result.eventEmitted = true;
    }
    
    // Notify callback if registered
    if (callback_) {
        callback_(result, tradingSignal);
    }
    
    return result;
}

void SignalPublisher::setPublicationMode(PublicationMode mode) {
    mode_ = mode;
}

PublicationMode SignalPublisher::getPublicationMode() const {
    return mode_;
}

void SignalPublisher::setMinPublicationStrength(double strength) {
    minPublicationStrength_ = std::max(0.0, strength);
}

void SignalPublisher::setMinPublicationConfidence(double confidence) {
    minPublicationConfidence_ = std::clamp(confidence, 0.0, 1.0);
}

void SignalPublisher::registerPublicationCallback(PublicationCallback callback) {
    callback_ = callback;
}

// Private methods
TradingSignal SignalPublisher::convertToTradingSignal(const aggregation::AggregatedSignal& signal,
                                                     const SignalContext& context) const {
    TradingSignal tradingSignal;
    
    // Basic signal information
    tradingSignal.setId("imbalance_" + std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(
        context.timestamp.time_since_epoch()).count()));
    tradingSignal.setExchangeId(context.exchangeId);
    tradingSignal.setTradingPair(context.symbol);
    // tradingSignal.setTimestamp(context.timestamp);
    tradingSignal.setGenerationTime(context.timestamp);
    tradingSignal.setTtl(std::chrono::milliseconds(5000)); // Default TTL

    // Signal direction and strength
    tradingSignal.setDirection(signal.dominantDirection);
    tradingSignal.setConfidence(signal.overallConfidence);
    tradingSignal.setStrength(signal.overallStrength);
    
    // Signal type and metadata
    tradingSignal.sourceType = signal.primaryType;
    // tradingSignal.setSource(context.source);
    
    // Add aggregation metadata
    tradingSignal.metadata["detector_count"] = signal.detectorCount;
    tradingSignal.metadata["active_detectors"] = signal.activeDetectorCount;
    tradingSignal.metadata["consensus_level"] = signal.consensusLevel;
    tradingSignal.metadata["primary_type"] = static_cast<double>(signal.primaryType);
    tradingSignal.metadata["regime"] = context.currentRegime;
    tradingSignal.metadata["volatility"] = context.currentVolatility;
    
    // Add custom metadata from context
    for (const auto& [key, value] : context.metadata) {
        tradingSignal.metadata[key] = value;
    }
    
    return tradingSignal;
}

bool SignalPublisher::shouldPublish(const aggregation::AggregatedSignal& signal) const {
    if (!signal.hasSignal || !signal.isValid()) {
        return false;
    }
    
    if (signal.overallStrength < minPublicationStrength_) {
        return false;
    }
    
    if (signal.overallConfidence < minPublicationConfidence_) {
        return false;
    }
    
    if (signal.activeDetectorCount == 0) {
        return false;
    }
    
    return true;
}

PublicationResult SignalPublisher::publishDirect(const TradingSignal& signal) {
    PublicationResult result;
    
    try {
        // Direct publication bypasses risk validation (for testing only)
        result.published = true;
        result.riskValidationPassed = true; // Assumed for direct mode
        result.reason = "Published directly (bypass risk validation)";
        
        LOG_WARN("SignalPublisher: Publishing signal directly without risk validation - USE FOR TESTING ONLY");
        
    } catch (const std::exception& ex) {
        result.reason = "Direct publication failed: " + std::string(ex.what());
        LOG_ERROR("SignalPublisher: Direct publication failed: {}", ex.what());
    }
    
    return result;
}

PublicationResult SignalPublisher::publishWithRiskValidation(const TradingSignal& signal) {
    PublicationResult result;
    
    // Try callback first (avoids circular dependency)
    if (riskValidationCallback_) {
        try {
            std::string validationError;
            bool isValid = riskValidationCallback_(signal, validationError);
            result.riskValidationPassed = isValid;
            
            if (isValid) {
                result.published = true;
                result.reason = "Signal validated and published successfully (callback)";
                LOG_INFO("SignalPublisher: Signal {} validated and published via callback", signal.id);
            } else {
                result.reason = "Risk validation failed (callback): " + validationError;
                LOG_WARN("SignalPublisher: Signal {} rejected by risk callback: {}", 
                        signal.id, result.reason);
            }
            return result;
        } catch (const std::exception& ex) {
            LOG_ERROR("SignalPublisher: Risk callback failed for signal {}: {}", 
                     signal.id, ex.what());
            // Fall through to try RiskManager
        }
    }
    
    // Fallback to direct RiskManager (if available)
    if (!riskManager_) {
        result.reason = "Neither RiskManager nor validation callback configured for RISK_VALIDATED mode";
        LOG_ERROR("SignalPublisher: Risk validation method required for RISK_VALIDATED mode");
        return result;
    }
    
    try {
        // Validate signal through RiskManager
        auto validationResult = riskManager_->validateSignal(signal);
        result.riskValidationPassed = validationResult.approved;
        result.riskValidationDetails = validationResult.approved ? "Approved" : "Rejected";
        
        if (validationResult.approved) {
            result.published = true;
            result.reason = "Signal validated and published successfully (direct)";
            LOG_INFO("SignalPublisher: Signal {} validated and published via RiskManager", signal.id);
        } else {
            result.reason = "Risk validation failed: " + (validationResult.violations.empty() ? 
                            "Unknown violation" : validationResult.violations.front().description);
            LOG_WARN("SignalPublisher: Signal {} rejected by risk validation: {}", 
                    signal.id, result.reason);
        }
        
    } catch (const std::exception& ex) {
        result.reason = "Risk validation exception: " + std::string(ex.what());
        LOG_ERROR("SignalPublisher: Risk validation failed for signal {}: {}", 
                 signal.id, ex.what());
    }
    
    return result;
}

PublicationResult SignalPublisher::publishWithRiskAdvisory(const TradingSignal& signal) {
    PublicationResult result;
    
    if (!riskManager_) {
        // Fall back to direct publication if no risk manager
        return publishDirect(signal);
    }
    
    try {
        // Run risk validation but publish regardless
        auto validationResult = riskManager_->validateSignal(signal);
        result.riskValidationDetails = validationResult.approved ? "Approved" : "Rejected";
        result.riskValidationPassed = validationResult.approved;
        
        // Always publish in advisory mode
        result.published = true;

        if (validationResult.approved) {
            result.reason = "Signal published (risk validation passed)";
            LOG_INFO("SignalPublisher: Signal {} published with risk approval", signal.id);
        } else {
            result.reason = "Signal published despite risk concerns: " + validationResult.violations.empty() ? 
                            "Unknown violation" : validationResult.violations.front().description;
            LOG_WARN("SignalPublisher: Signal {} published despite risk rejection: {}", 
                    signal.id, result.reason);
        }
        
    } catch (const std::exception& ex) {
        result.published = true; // Still publish
        result.reason = "Signal published despite risk validation exception: " + std::string(ex.what());
        LOG_ERROR("SignalPublisher: Risk validation failed for signal {} but publishing anyway: {}", 
                 signal.id, ex.what());
    }
    
    return result;
}

void SignalPublisher::emitImbalanceEvent(const TradingSignal& signal, const aggregation::AggregatedSignal& original) {
    try {
        auto eventData = createImbalanceEventData(signal, original);
        // For now, just log the event - can be extended to publish to EventBus later
        LOG_INFO("SignalPublisher: ImbalanceEvent for signal {}: {}", signal.id, eventData);
    } catch (const std::exception& ex) {
        LOG_ERROR("SignalPublisher: Failed to emit imbalance event for signal {}: {}", 
                 signal.id, ex.what());
    }
}

std::string SignalPublisher::createImbalanceEventData(const TradingSignal& signal, 
                                                      const aggregation::AggregatedSignal& original) const {
    std::string eventData = "ImbalanceEvent{";
    eventData += "signalId=" + signal.id + ",";
    eventData += "direction=" + std::to_string(static_cast<int>(signal.direction)) + ",";
    eventData += "strength=" + std::to_string(signal.strength) + ",";
    eventData += "confidence=" + std::to_string(static_cast<double>(signal.confidence) / 100.0) + ",";
    eventData += "detectorCount=" + std::to_string(original.detectorCount) + ",";
    eventData += "activeDetectors=" + std::to_string(original.activeDetectorCount) + ",";
    eventData += "consensus=" + std::to_string(original.consensusLevel) + "}";
    return eventData;
}

} // namespace publication
} // namespace strategy
} // namespace trading