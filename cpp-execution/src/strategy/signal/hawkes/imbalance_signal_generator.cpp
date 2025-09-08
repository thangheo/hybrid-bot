/**
 * @file imbalance_signal_generator.hpp (Complete Implementation)
 * @brief Enhanced signal generator with Hawkes process prediction capabilities
 * 
 * @author Trading System Team
 * @date September 07, 2025
 */

#pragma once

#include "strategy/types.hpp"
#include "signal/hawkes/imbalance_signal_generator.hpp"
#include "core/time.hpp"
#include "core/direction.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <optional>
#include <map>
#include <string>
#include <atomic>
#include <deque>
#include <iomanip>

using namespace trading;
using namespace trading::core;
namespace trading {
namespace strategy {

/**
 * Configuration for signal generation
 */

/**
 * Enhanced ImbalanceSignalGenerator with Hawkes process capabilities
 * 
 * Generates trading signals from detected imbalances and optionally enhances
 * them using Hawkes process predictions for clustering and persistence.
 */
// class ImbalanceSignalGenerator {
// public:
ImbalanceSignalGenerator::ImbalanceSignalGenerator(const SignalGenerationConfig& config){
    if (config.isValid()) {
        config_ = config;
    }
}

/**
 * Constructor
 */
// ImbalanceSignalGenerator::ImbalanceSignalGenerator(const SignalGenerationConfig& config = SignalGenerationConfig());

/**
 * Destructor
 */
// ~ImbalanceSignalGenerator::ImbalanceSignalGenerator() = default;

// ImbalanceSignalGenerator::ImbalanceSignalGenerator(const ImbalanceSignalGenerator&) = delete;

/**
 * Process detected imbalances and add to signal generation queue
 * Thread-safe
 */
void ImbalanceSignalGenerator::processImbalances(const std::vector<ImbalanceEvent>& imbalances) {
    std::lock_guard<std::mutex> lock(imbalancesMutex_);
    pendingImbalances_.insert(pendingImbalances_.end(), imbalances.begin(), imbalances.end());
}

/**
 * Generate trading signals from processed imbalances
 * Returns vector of generated signals
 */
std::vector<TradingSignal> ImbalanceSignalGenerator::generateSignals() {
    std::lock_guard<std::mutex> lock(imbalancesMutex_);
    std::vector<TradingSignal> signals;
    for (const auto& imbalance : pendingImbalances_) {
        TradingSignal signal = generateBaseSignal(imbalance);
        signals.push_back(signal);
    }
    pendingImbalances_.clear();
    return signals;
}

/**
 * Configure signal generation parameters
 * Thread-safe
 */
void ImbalanceSignalGenerator::configureSignalParameters(const SignalGenerationConfig& config){
    std::lock_guard<std::mutex> lock(configMutex_);
    if (config.isValid()) {
        config_ = config;
    }
}

/**
 * Enable/disable Hawkes process enhancement
 * Thread-safe
 */
void ImbalanceSignalGenerator::enableHawkesEnhancement(bool enable){
    hawkesEnabled_.store(enable);
    if (enable && !hawkesPredictor_) {
        std::lock_guard<std::mutex> lock(hawkesMutex_);
        if (!hawkesPredictor_) {
            hawkesPredictor_ = std::make_unique<HawkesPredictor>();
        }
    }
}

/**
 * Configure Hawkes process parameters
 * Only effective if Hawkes enhancement is enabled
 * Thread-safe
 */
void ImbalanceSignalGenerator::configureHawkesParameters(const HawkesParameters& params){
    if (hawkesEnabled_.load() && hawkesPredictor_) {
        std::lock_guard<std::mutex> lock(hawkesMutex_);
        hawkesPredictor_->configureParameters(params);
    }
}

/**
 * Get current Hawkes parameters (if enhancement is enabled)
 */
std::optional<HawkesParameters> ImbalanceSignalGenerator::getHawkesParameters() const {
    if (hawkesEnabled_.load() && hawkesPredictor_) {
        std::lock_guard<std::mutex> lock(hawkesMutex_);
        return hawkesPredictor_->getParameters();
    }
    return std::nullopt;
}

/**
 * Register a callback for new signals
 * Callback is called immediately when signals are generated
 */
void ImbalanceSignalGenerator::registerSignalCallback(const std::function<void(const TradingSignal&)>& callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    signalCallbacks_.push_back(callback);
}

/**
 * Clear all pending imbalances and reset state
 */
void ImbalanceSignalGenerator::clear() {
    std::lock_guard<std::mutex> lockImbalances(imbalancesMutex_);
    pendingImbalances_.clear();
    
    std::lock_guard<std::mutex> lockRecent(recentSignalsMutex_);
    recentSignals_.clear();
    
    if (hawkesEnabled_.load() && hawkesPredictor_) {
        std::lock_guard<std::mutex> lockHawkes(hawkesMutex_);
        hawkesPredictor_->clearHistory();
    }
    
    resetStats();
}

GenerationStats ImbalanceSignalGenerator::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return stats_;
}

/**
 * Reset statistics
 */
void ImbalanceSignalGenerator::resetStats() {
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_ = GenerationStats();
}

/**
 * Check if generator is ready to produce quality signals
 */
bool ImbalanceSignalGenerator::isReady() const {
    // Check if we have processed some imbalances
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        if (stats_.totalImbalancesProcessed < 10) {
            return false;
        }
    }
    
    // If Hawkes is enabled, check if it's ready
    if (hawkesEnabled_.load()) {
        return withHawkesPredictor([](const HawkesPredictor& predictor) {
            return predictor.isReady();
        });
    }
    
    return true;
}

/**
 * Get current signal generation rate (signals per second)
 */
double ImbalanceSignalGenerator::getCurrentSignalRate() const {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    
    auto now = std::chrono::system_clock::now();
    auto oneSecondAgo = now - std::chrono::seconds(1);
    
    // Count signals in last second
    size_t signalsInLastSecond = 0;
    for (const auto& timestamp : rateLimitState_.recentSignals) {
        if (timestamp >= oneSecondAgo) {
            signalsInLastSecond++;
        }
    }
    
    return static_cast<double>(signalsInLastSecond);
}

// private:
// Configuration

/**
 * Generate base signal from imbalance event
 */
TradingSignal ImbalanceSignalGenerator::generateBaseSignal(const ImbalanceEvent& imbalance) const{
    TradingSignal signal;
    signal.id = generateSignalId();
    signal.sourceEventId = imbalance.eventId;
    // signal.correlationId = imbalance.correlationId;
    signal.direction = determineSignalDirection(imbalance);
    signal.strength = normalizeSignalStrength(imbalance.strength);
    signal.confidence = 0.5; // Base confidence
    signal.generation_time = core::Timestamp(std::chrono::system_clock::now());
    signal.ttl = std::chrono::milliseconds(0); // To be set later
    signal.source_type = imbalance.type;
    signal.symbol = imbalance.symbol;
    signal.exchangeId = imbalance.exchangeId;
    
    // Basic targets based on imbalance
    double targetSize = calculateTargetSize(imbalance);
    if (targetSize > 0.0) {
        PriceTarget target;
        target.size = targetSize;
        target.price = 0.0; // To be set by downstream logic
        signal.targets.push_back(target);
    }
    
    // Initial metrics
    signal.signal_metrics = createSignalMetrics(imbalance);
    
    bool hawkesEnhanced = false;
    double clusteringProbability = 0.0;
    double persistence = 0.0;
    
    // Enhance with Hawkes if enabled
    if (hawkesEnabled_.load() && hawkesPredictor_) {
        hawkesEnhanced = true;
        withHawkesPredictor([&](HawkesPredictor& predictor) {
            predictor.updateWithImbalanceEvent(imbalance);
            clusteringProbability = predictor.getClusteringProbability(signal.direction, signal.strength);
            persistence = predictor.predictPersistence(signal);
        });
        
        applyHawkesEnhancements(signal, clusteringProbability, persistence);
    }
    
    // Set TTL based on strength and persistence
    signal.ttl = calculateSignalTtl(signal.strength, persistence);
    
    // Set expiration time
    signal.expirationTime = signal.generation_time + core::Duration(signal.ttl.count());
    
    // Update metrics with Hawkes info
    if (hawkesEnhanced) {
        signal.signal_metrics["clustering_probability"] = clusteringProbability;
        signal.signal_metrics["predicted_persistence"] = persistence;
    }
    
    // Validate before proceeding
    if (!validateSignal(signal)) {
        return TradingSignal(); // Return default invalid signal
    }
    
    // Rate limiting check
    if (isRateLimitedCheck()) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_.rateLimitedSignals++;
        return TradingSignal(); // Return default invalid signal
    }
}

/**
 * Enhance signal using Hawkes predictions (if enabled)
 */
TradingSignal ImbalanceSignalGenerator::enhanceSignalWithHawkes(const TradingSignal& baseSignal) const{
    TradingSignal enhancedSignal = baseSignal;
    if (hawkesEnabled_.load() && hawkesPredictor_) {
        double clusteringProbability = 0.0;
        double persistence = 0.0;
        withHawkesPredictor([&](HawkesPredictor& predictor) {
            clusteringProbability = predictor.getClusteringProbability(baseSignal.direction, baseSignal.strength);
            persistence = predictor.predictPersistence(baseSignal);
        });
        applyHawkesEnhancements(enhancedSignal, clusteringProbability, persistence);
        
        // Update TTL based on new persistence
        enhancedSignal.ttl = calculateSignalTtl(enhancedSignal.strength, persistence);
        enhancedSignal.expirationTime = enhancedSignal.generation_time + core::Duration(enhancedSignal.ttl.count());
        
        // Update metrics
        enhancedSignal.signal_metrics["clustering_probability"] = clusteringProbability;
        enhancedSignal.signal_metrics["predicted_persistence"] = persistence;
    }
    return enhancedSignal;
}

/**
 * Apply signal enhancements based on Hawkes predictions
 */
void ImbalanceSignalGenerator::applyHawkesEnhancements(TradingSignal& signal, 
                            double clusteringProbability,
                            double persistence) const{
    // Increase confidence based on clustering probability
    signal.confidence = std::min(1.0, signal.confidence + clusteringProbability * 0.5); // Max +0.5 boost
}

/**
 * Check if signal should be filtered due to rate limiting
 */
// bool ImbalanceSignalGenerator::isRateLimited()  {
//     std::lock_guard<std::mutex> lock(rateLimitMutex_);
    
//     auto now = std::chrono::system_clock::now();
//     auto oneSecondAgo = now - std::chrono::seconds(1);
//     auto oneMinuteAgo = now - std::chrono::minutes(1);
    
//     // Clean up old timestamps
//     rateLimitState_.recentSignals.erase(
//         std::remove_if(rateLimitState_.recentSignals.begin(), rateLimitState_.recentSignals.end(),
//                         [oneMinuteAgo](const auto& ts) { return ts < oneMinuteAgo; }),
//         rateLimitState_.recentSignals.end());
    
//     // Count signals in last second and minute
//     size_t signalsInLastSecond = 0;
//     size_t signalsInLastMinute = rateLimitState_.recentSignals.size();
//     for (const auto& timestamp : rateLimitState_.recentSignals) {
//         if (timestamp >= oneSecondAgo) {
//             signalsInLastSecond++;
//         }
//     }
    
//     // Check against limits
//     if (signalsInLastSecond >= static_cast<size_t>(config_.maxSignalsPerSecond) ||
//         signalsInLastMinute >= static_cast<size_t>(config_.maxSignalsPerMinute)) {
//         return true; // Rate limited
//     }
    
//     // Not rate limited, record this signal
//     rateLimitState_.recentSignals.push_back(now);
//     return false;
// }

// In cpp:
bool ImbalanceSignalGenerator::isRateLimitedCheck() const {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    auto now = std::chrono::system_clock::now();
    auto oneSecondAgo = now - std::chrono::seconds(1);
    auto oneMinuteAgo = now - std::chrono::minutes(1);

    size_t signalsInLastSecond = 0;
    size_t signalsInLastMinute = 0;
    for (const auto& timestamp : rateLimitState_.recentSignals) {
        if (timestamp >= oneSecondAgo) signalsInLastSecond++;
        if (timestamp >= oneMinuteAgo) signalsInLastMinute++;
    }
    return (signalsInLastSecond >= config_.maxSignalsPerSecond ||
            signalsInLastMinute >= config_.maxSignalsPerMinute);
}

bool ImbalanceSignalGenerator::isRateLimited() {
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    auto now = std::chrono::system_clock::now();
    auto oneSecondAgo = now - std::chrono::seconds(1);
    auto oneMinuteAgo = now - std::chrono::minutes(1);

    // Clean up old timestamps
    rateLimitState_.recentSignals.erase(
        std::remove_if(rateLimitState_.recentSignals.begin(), rateLimitState_.recentSignals.end(),
                       [oneMinuteAgo](const auto& ts) { return ts < oneMinuteAgo; }),
        rateLimitState_.recentSignals.end());

    size_t signalsInLastSecond = 0;
    size_t signalsInLastMinute = rateLimitState_.recentSignals.size();
    for (const auto& timestamp : rateLimitState_.recentSignals) {
        if (timestamp >= oneSecondAgo) signalsInLastSecond++;
    }
    if (signalsInLastSecond >= config_.maxSignalsPerSecond ||
        signalsInLastMinute >= config_.maxSignalsPerMinute) {
        return true;
    }
    rateLimitState_.recentSignals.push_back(now);
    return false;
}
/**
 * Update rate limiting state
 */
void ImbalanceSignalGenerator::updateRateLimitState(){
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    auto now = std::chrono::system_clock::now();
    rateLimitState_.recentSignals.push_back(now);
}

/**
 * Check if signal is duplicate of recent signal
 */
bool ImbalanceSignalGenerator::isDuplicateSignal(const TradingSignal& signal) {
    std::lock_guard<std::mutex> lock(recentSignalsMutex_);
    auto now = std::chrono::system_clock::now();
    auto windowStart = now - config_.duplicateWindow;
    
    // Clean up old signals
    recentSignals_.erase(
        std::remove_if(recentSignals_.begin(), recentSignals_.end(),
                        [windowStart](const TradingSignal& s) { return s.generation_time < windowStart; }),
        recentSignals_.end());
    
    // Check for duplicates
    for (const auto& recent : recentSignals_) {
        if (recent.symbol == signal.symbol &&
            recent.direction == signal.direction &&
            std::abs(recent.strength - signal.strength) < 0.05 && // Similar strength
            std::abs(recent.confidence - signal.confidence) < 0.1) { // Similar confidence
            return true; // Duplicate found
        }
    }
    
    return false; // No duplicates found
}

/**
 * Add signal to recent signals for duplicate filtering
 */
void ImbalanceSignalGenerator::addToRecentSignals(const TradingSignal& signal){
    std::lock_guard<std::mutex> lock(recentSignalsMutex_);
    recentSignals_.push_back(signal);
}

/**
 * Clean up old signals from recent signals list
 */
void ImbalanceSignalGenerator::cleanupRecentSignals(){
    std::lock_guard<std::mutex> lock(recentSignalsMutex_);
    auto now = std::chrono::system_clock::now();
    auto windowStart = now - config_.duplicateWindow;
    
    recentSignals_.erase(
        std::remove_if(recentSignals_.begin(), recentSignals_.end(),
                        [windowStart](const TradingSignal& s) { return s.generation_time < windowStart; }),
        recentSignals_.end());
}

/**
 * Validate signal before generation
 */
bool ImbalanceSignalGenerator::validateSignal(const TradingSignal& signal) const {
    // Check if signal is within valid range
    if (signal.strength < config_.minSignalStrength || signal.strength > config_.maxSignalStrength) {
        return false;
    }
    return true;
}

/**
 * Calculate signal TTL based on strength and persistence
 */
std::chrono::milliseconds ImbalanceSignalGenerator::calculateSignalTtl(double strength, double persistence) const {
    // Base TTL scaled by strength
    auto baseTtl = config_.baseTtl;
    double strengthFactor = strength * 2.0; // 0.5 strength = 1x TTL, 1.0 strength = 2x TTL
    
    // Apply persistence factor if available
    double persistenceFactor = (persistence > 0.0) ? (1.0 + persistence) : 1.0;
    
    auto calculatedTtl = std::chrono::duration_cast<std::chrono::milliseconds>(
        baseTtl * strengthFactor * persistenceFactor);
    
    return std::clamp(calculatedTtl, baseTtl, config_.maxTtl);
}

/**
 * Generate unique signal ID
 */
std::string ImbalanceSignalGenerator::generateSignalId() const {
    static std::atomic<uint64_t> counter{0};
    auto id = ++counter;
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::ostringstream oss;
    oss << "SIG-" << ms << "-" << std::setw(6) << std::setfill('0') << id;
    return oss.str();
}

/**
 * Notify signal callbacks
 */
void ImbalanceSignalGenerator::notifySignalCallbacks(const TradingSignal& signal) const{
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    for (const auto& callback : signalCallbacks_) {
        if (callback) {
            callback(signal);
        }
    }
}

/**
 * Update statistics
 */
void ImbalanceSignalGenerator::updateStats(const TradingSignal& signal, bool hawkesEnhanced) const {
    // Update direction stats
    updateDirectionStats(signal.direction);

    //  source_type can be one of:{
    //      VOLUME_IMBALANCE,              // Significant difference between bid and ask volume
    //      PRICE_LEVEL_IMBALANCE,         // Imbalance across multiple price levels
    //      LIQUIDITY_REMOVAL,             // Sudden removal of liquidity from one side
    //      ORDER_FLOW_IMBALANCE,          // Directional bias in order submissions
    //      LARGE_ORDER_APPEARANCE,        // Large orders appearing in the book
    //      LARGE_ORDER_DISAPPEARANCE,     // Large orders being removed from the book
    //      PRICE_LEVEL_CONCENTRATION,     // Concentration of volume at specific price levels
    //      BID_ASK_SPREAD_CHANGE,         // Significant change in the bid-ask spread
    //      MARKET_DEPTH_CHANGE,           // Significant change in market depth
    //      LIQUIDITY_FLOW_IMBALANCE,      // Imbalance in liquidity flow
    //      ABNORMAL_SPREAD,               // Abnormal spread between bid and ask prices
    //      LIQUIDITY_IMBALANCE,           // General liquidity imbalance
    //      TREND_CONTINUATION,            // Continuation of a price trend
    //      TREND_REVERSAL,                // Reversal of a price trend
    //      CUSTOM                         // Custom imbalance type defined by strategy
    //  };
    // Update type stats
    updateTypeStats(imbalanceTypeToString(signal.source_type));

    // Update averages
    updateAverages();
}

/**
 * Map imbalance type to signal metrics
 */
std::map<std::string, double> ImbalanceSignalGenerator::createSignalMetrics(const ImbalanceEvent& imbalance,
                                                    double clusteringProbability,
                                                    double persistence) const{
    std::map<std::string, double> metrics;
    metrics["imbalanceStrength"] = imbalance.strength;
    //rewrite this line to avoid potential exception
    auto volumeIt = imbalance.supportingMetrics.find("volume");
    metrics["imbalanceVolume"] = volumeIt != imbalance.supportingMetrics.end() ? volumeIt->second : 0.0;
    metrics["imbalanceType"] = (imbalance.direction == Direction::BUY ? 1.0 : (imbalance.direction == Direction::SELL ? -1.0 : 0.0));
    if (clusteringProbability > 0.0) {
        metrics["clusteringProbability"] = clusteringProbability;
    }
    if (persistence > 0.0) {
        metrics["persistence"] = persistence;
    }
    return metrics;
}

/**
 * Calculate target size based on imbalance strength
 */
double ImbalanceSignalGenerator::calculateTargetSize(const ImbalanceEvent& imbalance) const {
    // Base size scaled by imbalance strength
    double baseSize = 1.0; // Default base size
    return baseSize * imbalance.strength;
}

/**
 * Create Hawkes predictor with current parameters
 */
void ImbalanceSignalGenerator::createHawkesPredictor(){
    std::lock_guard<std::mutex> lock(hawkesMutex_);
    if (!hawkesPredictor_) {
        hawkesPredictor_ = std::make_unique<HawkesPredictor>();
    }
}

/**
 * Thread-safe access to Hawkes predictor
 */

// Statistics helpers
void ImbalanceSignalGenerator::updateDirectionStats(core::Direction direction) const{
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.signalsByDirection[direction]++;
}
void ImbalanceSignalGenerator::updateTypeStats(const std::string& type) const{
    std::lock_guard<std::mutex> lock(statsMutex_);
    stats_.signalsByType[type]++;
}
void ImbalanceSignalGenerator::updateAverages() const{
    std::lock_guard<std::mutex> lock(statsMutex_);
    // Update average signal strength
    if (stats_.totalSignalsGenerated > 0) {
        stats_.averageSignalStrength = 
            ((stats_.averageSignalStrength * (stats_.totalSignalsGenerated - 1)) + stats_.averageSignalStrength) 
            / stats_.totalSignalsGenerated;
    }
}


// Inline implementations for performance-critical methods

inline bool ImbalanceSignalGenerator::isHawkesEnhancementEnabled() const {
    return hawkesEnabled_.load();
}

inline double ImbalanceSignalGenerator::normalizeSignalStrength(double strength) const {
    return std::clamp(strength, config_.minSignalStrength, config_.maxSignalStrength);
}

// inline std::chrono::milliseconds ImbalanceSignalGenerator::calculateSignalTtl(
//     double strength, double persistence) const {
    
//     // Base TTL scaled by strength
//     auto baseTtl = config_.baseTtl;
//     double strengthFactor = strength * 2.0; // 0.5 strength = 1x TTL, 1.0 strength = 2x TTL
    
//     // Apply persistence factor if available
//     double persistenceFactor = (persistence > 0.0) ? (1.0 + persistence) : 1.0;
    
//     auto calculatedTtl = std::chrono::duration_cast<std::chrono::milliseconds>(
//         baseTtl * strengthFactor * persistenceFactor);
    
//     return std::clamp(calculatedTtl, baseTtl, config_.maxTtl);
// }

inline core::Direction ImbalanceSignalGenerator::determineSignalDirection(
    const ImbalanceEvent& imbalance) const {
    return imbalance.direction;
}

// inline double ImbalanceSignalGenerator::calculateTargetSize(
//     const ImbalanceEvent& imbalance) const {
//     // Base size scaled by imbalance strength
//     double baseSize = 1.0; // Default base size
//     return baseSize * imbalance.strength;
// }
} // namespace strategy
} // namespace trading