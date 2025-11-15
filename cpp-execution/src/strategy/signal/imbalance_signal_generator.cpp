/**
 * @file imbalance_signal_generator.cpp
 * @brief Advanced implementation of the imbalance signal generator
 * 
 * @details
 * Implements sophisticated trading signal generation from detected imbalance events
 * with market regime awareness, dynamic parameter adjustment, and multi-factor
 * quality assessment.
 * 
 * Created on: May 13, 2025
 * Refactored: September 24, 2025
 */

#include "strategy/signal/imbalance_signal_generator.hpp"
#include "common/event_bus.hpp"
#include "common/trading_events.hpp"
#include "core/trading_signal.hpp"
#include "strategy/types/constants.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>
#include <future>
#include <common/logger.hpp>

#include "common/thread_pool.hpp"

// Thread pool singleton cho momentum prediction
static ThreadPool& getPredictionThreadPool() {
    // 2-4 threads cho prediction tasks
    static ThreadPool pool(4);
    return pool;
}


namespace trading {
namespace strategy {
// Static lookup tables for regime-aware scaling
const std::array<std::array<double, 7>, 19> ImbalanceSignalGenerator::REGIME_TYPE_SCALING_MATRIX = {{
    // NORMAL, VOLATILE, TRENDING, MEAN_REVERTING, STRESS, QUIET, UNKNOWN
    {{1.0, 0.8, 1.1, 0.9, 0.7, 1.1, 0.9}}, // VOLUME_IMBALANCE
    {{0.8, 0.7, 0.9, 0.8, 0.6, 0.9, 0.8}}, // PRICE_LEVEL_IMBALANCE
    {{1.2, 1.4, 1.3, 1.0, 1.5, 0.8, 1.1}}, // LIQUIDITY_REMOVAL
    {{1.0, 1.2, 1.1, 0.9, 1.3, 0.7, 1.0}}, // ORDER_FLOW_IMBALANCE
    {{1.1, 1.3, 1.2, 0.8, 1.4, 0.6, 1.0}}, // LARGE_ORDER_APPEARANCE
    {{1.2, 1.4, 1.3, 0.9, 1.5, 0.7, 1.1}}, // LARGE_ORDER_DISAPPEARANCE
    {{0.9, 1.1, 1.0, 1.2, 1.0, 1.3, 0.9}}, // PRICE_LEVEL_CONCENTRATION
    {{0.7, 1.0, 0.8, 0.6, 1.2, 1.0, 0.8}}, // BID_ASK_SPREAD_CHANGE
    {{1.0, 1.2, 1.1, 0.9, 1.3, 0.8, 1.0}}, // MARKET_DEPTH_CHANGE
    {{1.1, 1.3, 1.2, 1.0, 1.4, 0.7, 1.1}}, // LIQUIDITY_FLOW_IMBALANCE
    {{0.7, 1.0, 0.8, 0.6, 1.2, 1.0, 0.8}}, // ABNORMAL_SPREAD
    {{1.0, 1.2, 1.1, 0.9, 1.3, 0.8, 1.0}}, // LIQUIDITY_IMBALANCE
    {{0.9, 0.7, 1.3, 0.6, 0.8, 0.5, 0.9}}, // TREND_CONTINUATION
    {{1.1, 1.4, 0.8, 1.3, 1.2, 1.4, 1.1}}, // TREND_REVERSAL
    {{1.0, 1.2, 1.1, 0.9, 1.3, 0.7, 1.0}}, // TRADE_IMBALANCE
    {{1.0, 1.1, 1.0, 0.9, 1.2, 0.8, 1.0}}, // CANCELLATION_IMBALANCE
    {{0.8, 1.3, 0.9, 0.7, 1.4, 1.2, 0.9}}, // PRICE_VOLATILITY_IMBALANCE
    {{1.1, 0.9, 1.2, 1.0, 0.8, 1.0, 1.0}}, // HIDDEN_LIQUIDITY_IMBALANCE
    {{0.9, 0.9, 0.9, 0.9, 0.9, 0.9, 0.9}}  // CUSTOM
}};

const std::array<double, 7> ImbalanceSignalGenerator::REGIME_BASE_ADJUSTMENTS = {{
    1.0,  // NORMAL
    0.8,  // VOLATILE (reduce base strength in volatile conditions)
    1.1,  // TRENDING (boost trending signals)
    0.9,  // MEAN_REVERTING (slight reduction in range-bound markets)
    0.7,  // STRESS (significant reduction during market stress)
    1.0,  // QUIET (normal base strength, but different type scaling)
    0.95  // UNKNOWN (slight conservative adjustment)
}};

// Utility function implementations
std::string SignalStrengthComponents::toString() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "Strength[base=" << baseStrength 
        << ", type=" << typeAdjustment 
        << ", conf=" << confidenceBoost 
        << ", regime=" << regimeAdjustment 
        << ", momentum=" << momentumAlignment 
        << ", final=" << finalStrength << "]";
    return oss.str();
}

std::string SignalQualityMetrics::toString() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3);
    oss << "Quality[str=" << strengthComponent 
        << ", conf=" << confidenceComponent 
        << ", pred=" << predictionComponent 
        << ", corr=" << correlationComponent 
        << ", regime=" << regimeConsistency 
        << ", hist=" << historicalPerformance 
        << ", final=" << finalQuality << "]";
    return oss.str();
}

// Constructor implementations
ImbalanceSignalGenerator::ImbalanceSignalGenerator()
    : signalCount_(0), nextCallbackId_(1) {
}

ImbalanceSignalGenerator::ImbalanceSignalGenerator(const SignalGeneratorConfig& config) 
    : signalCount_(0), nextCallbackId_(1) {
    initialize(config);
}

// Initialization
bool ImbalanceSignalGenerator::initialize(const SignalGeneratorConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    config_ = config;
    signalCount_ = 0;
    nextCallbackId_ = 1;
    
    // Clear state
    pendingImbalances_.clear();
    generatedSignals_.clear();
    {
        std::lock_guard<std::mutex> cbLock(callbacksMutex_);
        callbacks_.clear();
    }
    
    // Reset component pointers
    momentumCorrelation_ = nullptr;
    thresholdManager_ = nullptr;
    regimeDetector_ = nullptr;
    performanceTracker_ = nullptr;
    
    LOG_INFO("ImbalanceSignalGenerator initialized with enhanced configuration");
    
    return true;
}

void ImbalanceSignalGenerator::setStrategyId(const core::StrategyId& strategyId) {
    std::lock_guard<std::mutex> lock(mutex_);
    strategyId_ = strategyId;
}

// Core processing methods
int ImbalanceSignalGenerator::processImbalances(const std::vector<ImbalanceEvent>& imbalances) {
    auto t0 = std::chrono::steady_clock::now();
    int signalsGenerated = 0;

    for (const auto& imbalance : imbalances) {
        if (processImbalance(imbalance)) {
            signalsGenerated++;
        }
    }

    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();
    if (dur > 5) {
        LOG_WARN("ImbalanceSignalGenerator::processImbalances slow: {} ms for {} imbalances", dur, imbalances.size());
    } else {
        // LOG_DEBUG("ImbalanceSignalGenerator::processImbalances took {} ms", dur);
    }

    return signalsGenerated;
}

bool ImbalanceSignalGenerator::processImbalance(const ImbalanceEvent& imbalance) {
    // Apply enhanced filtering
    if (!passesSignalFilters(imbalance)) {
      
        return false;
    }
    
    // Store for potential batch processing and snapshot component pointers
    MomentumCorrelationPtr momentumCopy;
    ThresholdManagerPtr thresholdCopy;
    std::shared_ptr<MarketRegimeDetector> regimeDetectorCopy;
    std::shared_ptr<SignalPerformanceTracker> perfTrackerCopy;
    SignalGeneratorConfig configCopy;

    {
        // Lock only for short duration to mutate shared containers and copy pointers
        std::lock_guard<std::mutex> lock(mutex_);
        pendingImbalances_.push_back(imbalance);
        // snapshot pointers and config so heavy processing can proceed without holding mutex_
        momentumCopy = momentumCorrelation_;
        thresholdCopy = thresholdManager_;
        regimeDetectorCopy = regimeDetector_;
        perfTrackerCopy = performanceTracker_;
        configCopy = config_;
    }

    // Generate signal with advanced algorithms (no main mutex_ held)
    TradingSignal signal = createSignalFromImbalance(imbalance, momentumCopy, thresholdCopy, regimeDetectorCopy, perfTrackerCopy, configCopy);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        generatedSignals_.push_back(signal);
    }
    
    // Enhanced logging with market context (variables intentionally unused here)
    auto price = imbalance.getSupportingMetric("midPrice", 0.0);
    auto regime = getCurrentMarketRegime(imbalance);

    // Log current regime and price for debugging
    // LOG_DEBUG("Generated signal from imbalance {} at price {:.2f} in regime {}",
    //     imbalance.eventId, price, marketRegimeTypeToString(static_cast<MarketRegimeType>(static_cast<int>(regime_number))));
    (void)price; (void)regime;
    
    // Notify callbacks (do not hold main mutex_ while notifying)
    notifySignalCallbacks(signal);

    // Increment counter under lock
    signalCount_++;

    return true;
}

std::vector<TradingSignal> ImbalanceSignalGenerator::generateSignals() {
    std::lock_guard<std::mutex> lock(mutex_);
    return generatedSignals_;
}

#define DEBUG_BUILD 0
// Enhanced signal creation
// New signature: accept optional snapshots of component pointers and config to avoid
// accessing shared state while holding the main mutex.
TradingSignal ImbalanceSignalGenerator::createSignalFromImbalance(
    const ImbalanceEvent& imbalance,
    MomentumCorrelationPtr momentumCopy,
    ThresholdManagerPtr thresholdCopy,
    std::shared_ptr<MarketRegimeDetector> regimeDetectorCopy,
    std::shared_ptr<SignalPerformanceTracker> perfTrackerCopy,
    SignalGeneratorConfig configCopy) {
    TradingSignal signal;
    auto t0 = std::chrono::steady_clock::now();
    // Basic signal properties
    signal.id = generateSignalId();
    // auto t1 = std::chrono::steady_clock::now();
    signal.sourceEventId = imbalance.eventId;
    signal.correlationId = imbalance.eventId;
    signal.direction = imbalance.direction;
    signal.sourceType = imbalance.type;
    signal.symbol = imbalance.symbol;
    signal.exchangeId = imbalance.exchangeId;
    
    signal.generationTime = std::chrono::system_clock::now();
    signal.confidence = trading::core::confidenceLevelToValue(imbalance.confidence);
    // Reserve containers to avoid repeated allocations on hot path
    signal.reserveContainers(4, 16);
    // auto t2 = std::chrono::steady_clock::now();
    // Advanced strength calculation (measure sub-step latency)
    auto sub_t0 = std::chrono::steady_clock::now();
    auto strengthComponents = calculateSignalStrength(imbalance);
    auto sub_elapsed_strength = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - sub_t0).count();
    // record calculation timing so it is not unused and is available for debugging
    signal.setMetric("timing_strength_calc_us", static_cast<double>(sub_elapsed_strength));
    signal.strength = strengthComponents.finalStrength;
    // Avoid expensive string serialization on hot path; expose numeric components as metrics
    signal.setMetric("strength_base", strengthComponents.baseStrength);
    signal.setMetric("strength_type_adjust", strengthComponents.typeAdjustment);
    signal.setMetric("strength_conf_boost", strengthComponents.confidenceBoost);
    signal.setMetric("strength_regime_adj", strengthComponents.regimeAdjustment);
    signal.setMetric("strength_momentum_align", strengthComponents.momentumAlignment);
    // set bid ask spread metric if available
    double bidPrice = imbalance.getSupportingMetric("bidPrice", 0.0);
    double askPrice = imbalance.getSupportingMetric("askPrice", 0.0);
    double volatility = imbalance.getSupportingMetric("volatility", 0.0);
    double askVolume = imbalance.getSupportingMetric("askVolume", 0.0);
    double bidVolume = imbalance.getSupportingMetric("bidVolume", 0.0);    
    double liquidityProfileScore = imbalance.getSupportingMetric("liquidityProfileScore", 0.0);

    signal.setMetric("volatility", volatility);
    if (bidPrice > 0.0 && askPrice > 0.0) {
        double spread = askPrice - bidPrice;
        signal.setMetric("bidAskSpread", spread);
    }
    signal.setMetric("bidVolume", bidVolume);
    signal.setMetric("askVolume", askVolume);
    signal.setMetric("liquidityProfileScore", liquidityProfileScore);


    // auto t3 = std::chrono::steady_clock::now();
    // Generate sophisticated price targets
    signal.targets = generatePriceTargets(imbalance);  
    
    // Calculate adaptive TTL
    auto deltaPrice = signal.targets.empty() ? 0.001 : 
                     std::abs(signal.targets.back().price - signal.price);

    signal.ttl = calculateSignalTTL(imbalance, deltaPrice); // không khả nghi
    signal.expirationTime = imbalance.detectionTime + signal.ttl;
    
    // Copy and enhance metrics
    for (const auto& metric : imbalance.supportingMetrics) {
        signal.setMetric(metric.first, metric.second);
    }
    double predictedMove = 0.0;
    double correlation = 0.0;
    auto sub_t1 = std::chrono::steady_clock::now();    
    bool predictionTimedOut = false;

    if (momentumCopy) {
        if (configCopy.predictTimeoutUs == 0) {
            // Blocking call (backward-compatible)
            predictedMove = momentumCopy->predictPriceMovement(imbalance);
            // Create an owning string here so any view passed to other APIs
            // does not dangle when the temporary returned from
            // imbalanceTypeToString() is destroyed.
            std::string imbalanceTypeStr = imbalanceTypeToString(imbalance.type);
            correlation = momentumCopy->getCorrelationCoefficient(imbalanceTypeStr);

        } else {
            // Non-blocking with thread pool
            try {
                auto& pool = getPredictionThreadPool();
                
                // Submit task to thread pool. Capture imbalance by value to avoid
                // referencing a stack object from another thread which can lead
                // to undefined behavior if the caller returns before the task runs.
                auto future = pool.enqueue([momentumCopy, imbalance]() {
                    return momentumCopy->predictPriceMovement(imbalance);
                });
                
                // Wait with timeout
                auto status = future.wait_for(std::chrono::microseconds(configCopy.predictTimeoutUs));
                
                if (status == std::future_status::ready) {
                    predictedMove = future.get();
                    // Keep an owning std::string to ensure lifetime while used
                    // in getCorrelationCoefficient.
                    std::string imbalanceTypeStr = imbalanceTypeToString(imbalance.type);
                    correlation = momentumCopy->getCorrelationCoefficient(imbalanceTypeStr);
                } else {
                    predictionTimedOut = true;
                    // Future will be cleaned up by pool
                }
            } catch (const std::exception& ex) {
                // Log only in debug build
                #ifdef DEBUG_BUILD
                LOG_WARN("ImbalanceSignalGenerator: predictor exception: {}", ex.what());
                #endif
                predictedMove = 0.0;
                correlation = 0.0;
            }
        }
        
        // Set metrics in batch
        signal.setMetric("predictedPriceMove", predictedMove);
        signal.setMetric("momentumCorrelation", correlation);
        signal.setMetric("predict_timed_out", predictionTimedOut ? 1.0 : 0.0);
        
        // Handle prediction alignment
        bool alignmentCorrect = checkPredictionAlignment(imbalance.direction, predictedMove);
        signal.setMetric("predictionAligned", alignmentCorrect ? 1.0 : 0.0);
        
        if (!alignmentCorrect) {
            handlePredictionConflict(signal, imbalance, predictedMove, correlation);
        }
        
        // Apply confidence adjustment
        double absCorrelation = std::abs(correlation);
        double confidenceMultiplier = calculateConfidenceMultiplier(absCorrelation, alignmentCorrect);
        signal.confidence = std::clamp(signal.confidence * confidenceMultiplier, 0.0, 1.0);
    }

    auto sub_elapsed_predict = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - sub_t1).count();
    signal.setMetric("timing_predict_calc_us", static_cast<double>(sub_elapsed_predict));
    // auto t5 = std::chrono::steady_clock::now();
    //if direction is BUY then set price to best ask else if SELL set to best bid
    if (imbalance.direction == core::Direction::BUY) {
        signal.price = imbalance.getSupportingMetric("askPrice", 0.0);
    } else if (imbalance.direction == core::Direction::SELL) {
        signal.price = imbalance.getSupportingMetric("bidPrice", 0.0);
    } else {
        signal.price = imbalance.getSupportingMetric("midPrice", 0.0);
    }
    //if price is 0 then we should get from something else
    // Calculate comprehensive quality metrics (timed). Avoid string serialization.
    auto sub_t2 = std::chrono::steady_clock::now();
    auto qualityMetrics = calculateSignalQuality(signal, predictedMove, correlation);
    auto sub_elapsed_quality = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - sub_t2).count();
    signal.setMetric("timing_quality_calc_us", static_cast<double>(sub_elapsed_quality));
    signal.quality = qualityMetrics.finalQuality;
    signal.setMetric("quality_strengthComp", qualityMetrics.strengthComponent);
    signal.setMetric("quality_confComp", qualityMetrics.confidenceComponent);
    signal.setMetric("quality_predComp", qualityMetrics.predictionComponent);
    signal.setMetric("quality_corrComp", qualityMetrics.correlationComponent);
    signal.setMetric("quality_regime", qualityMetrics.regimeConsistency);
    signal.setMetric("quality_hist", qualityMetrics.historicalPerformance);
    signal.setMetric("quality_final", qualityMetrics.finalQuality);
    
    // Calculate adaptive target size
    signal.targetSize = calculateTargetSize(imbalance, signal.strength, signal.confidence, signal.quality);
    auto t6 = std::chrono::steady_clock::now();
    // Apply threshold manager adjustments
    if (thresholdCopy) {
        double sensitivity = thresholdCopy->getGlobalSensitivity();
        signal.setMetric("thresholdSensitivity", sensitivity);
        
        if (sensitivity != 1.0) {
            signal.strength = std::min(1.0, signal.strength * sensitivity);
        }
    }
    

    // auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
    // auto dur_us6 = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t6).count();
    

    // if (dur_us > 20000) {
    //     // LOG_WARN("createSignalFromImbalance: signal id={} creation took {} us", signal.id, dur_us);
    //     LOG_WARN("createSignalFromImbalance: id={} total={}us (strength={}us, predict={}us, quality={}us)",
    //               signal.id, dur_us, sub_elapsed_strength, sub_elapsed_predict, sub_elapsed_quality);
    // }
    // If the generator has a configured strategy id, ensure the signal carries it
    // when the creator didn't set one. This avoids downstream components seeing
    // UNKNOWN strategy identifiers.
    if (!strategyId_.isEmpty() && !signal.strategyId.isValid()) {
        signal.strategyId = strategyId_;
    }
    
    return signal;
}

// Enhanced filtering with regime awareness
bool ImbalanceSignalGenerator::passesSignalFilters(const ImbalanceEvent& imbalance) const {
    // Basic strength check
    // if (imbalance.strength < config_.minSignalStrength) {
    //     return false;
    // }
    
    // Basic confidence check
    if (trading::core::confidenceLevelToValue(imbalance.confidence) < config_.confidenceThreshold) {
        return false;
    }
    
    // Direction check
    if (imbalance.direction == core::Direction::NEUTRAL) {
        return false;
    }
    
    // Regime-specific additional filtering
    auto regime = getCurrentMarketRegime(imbalance);
    if (regime == MarketRegimeType::STRESS) {
        // More stringent filtering during market stress
        if (imbalance.strength < config_.minSignalStrength * 1.5) {
            return false;
        }
        if (trading::core::confidenceLevelToValue(imbalance.confidence) < config_.confidenceThreshold * 1.2) {
            return false;
        }
    }
    
    return true;
}

// Sophisticated strength calculation
SignalStrengthComponents ImbalanceSignalGenerator::calculateSignalStrength(const ImbalanceEvent& imbalance) const {
    SignalStrengthComponents components;
    
    components.baseStrength = imbalance.strength;
    
    // Get current market regime
    auto regime = getCurrentMarketRegime(imbalance);
    size_t regimeIndex = static_cast<size_t>(regime);
    size_t typeIndex = static_cast<size_t>(imbalance.type);
    
    // Apply regime-aware type scaling
    if (typeIndex < REGIME_TYPE_SCALING_MATRIX.size()) {
        components.typeAdjustment = REGIME_TYPE_SCALING_MATRIX[typeIndex][regimeIndex];
    }
    
    // Apply regime base adjustment
    components.regimeAdjustment = REGIME_BASE_ADJUSTMENTS[regimeIndex];
    
    // Enhanced confidence boost with non-linear scaling
    double confidence = trading::core::confidenceLevelToValue(imbalance.confidence);
    components.confidenceBoost = 0.6 + 0.4 * std::pow(confidence, 1.5); // Non-linear boost
    
    // Momentum alignment factor (will be updated if momentum correlation is available)
    components.momentumAlignment = 1.0;
    
    // Historical performance adjustment
    double histAdjustment = getHistoricalPerformanceAdjustment(imbalance.type);
    
    // Calculate final strength
    components.finalStrength = components.baseStrength * 
                              components.typeAdjustment * 
                              components.regimeAdjustment * 
                              components.confidenceBoost * 
                              components.momentumAlignment * 
                              histAdjustment;
    
    // Ensure strength is in valid range
    components.finalStrength = std::clamp(components.finalStrength, 0.0, 1.0);
    
    return components;
}

// Advanced quality calculation
SignalQualityMetrics ImbalanceSignalGenerator::calculateSignalQuality(
    const TradingSignal& signal, double predictedMove, double correlation) const {
    
    SignalQualityMetrics metrics;
    
    // Component calculations with non-linear transformations
    metrics.strengthComponent = std::pow(signal.strength, 0.8); // Slight compression
    metrics.confidenceComponent = signal.confidence;
    metrics.predictionComponent = std::min(1.0, std::abs(predictedMove) * 50.0); // More aggressive scaling
    metrics.correlationComponent = std::pow(std::abs(correlation), 1.2); // Emphasize strong correlations
    
    // Regime consistency (how well this signal type performs in current regime)
    auto regime = getCurrentMarketRegime(ImbalanceEvent{}); // Simplified for now
    size_t regimeIndex = static_cast<size_t>(regime);
    size_t typeIndex = static_cast<size_t>(signal.sourceType);
    
    if (typeIndex < REGIME_TYPE_SCALING_MATRIX.size()) {
        metrics.regimeConsistency = std::min(1.0, REGIME_TYPE_SCALING_MATRIX[typeIndex][regimeIndex]);
    }
    
    // Historical performance component
    metrics.historicalPerformance = getHistoricalPerformanceAdjustment(signal.sourceType);
    
    // Non-linear weighted combination
    std::array<double, 6> weights = {{0.25, 0.25, 0.15, 0.15, 0.10, 0.10}};
    std::array<double, 6> components = {{
        metrics.strengthComponent,
        metrics.confidenceComponent,
        metrics.predictionComponent,
        metrics.correlationComponent,
        metrics.regimeConsistency,
        metrics.historicalPerformance
    }};
    
    // Calculate weighted geometric mean for more conservative quality assessment
    double logSum = 0.0;
    double totalWeight = 0.0;
    
    for (size_t i = 0; i < weights.size(); ++i) {
        if (components[i] > 0.0) {
            logSum += weights[i] * std::log(std::max(0.001, components[i]));
            totalWeight += weights[i];
        }
    }
    
    if (totalWeight > 0.0) {
        metrics.finalQuality = std::exp(logSum / totalWeight);
    } else {
        metrics.finalQuality = 0.0;
    }
    
    // Apply quality caps based on regime
    if (regime == MarketRegimeType::MEAN_REVERTING || regime == MarketRegimeType::HIGH_VOLATILITY) {
        metrics.finalQuality *= 0.8; // Reduce quality during difficult conditions
    }
    
    metrics.finalQuality = std::clamp(metrics.finalQuality, 0.0, 1.0);
    
    return metrics;
}

// Enhanced target size calculation
// Returns a fractional target in [0,1] that represents the portion of the USD
// notional (positionSize) to allocate for this signal. This function uses the
// optional `volatility` parameter if provided (volatility > 0), otherwise it
// falls back to reading the "volatility" supporting metric from the imbalance.
double ImbalanceSignalGenerator::calculateTargetSize(
    const ImbalanceEvent& imbalance, double strength, double confidence, double quality, double volatility) const {
    
    // Normalize strength to avoid absolute price / depth bias across symbols.
    // If `strength` is already in [0,1] this is effectively a clamp; if it's
    // an absolute quantity (e.g. volume) we normalize by midPrice to get a
    // relative USD-normalized measure. We defensively guard against zero.
    double midPrice = imbalance.getSupportingMetric("midPrice", 0.0001);
    double normStrength = strength;
    if (normStrength > 1.0) {
        // Heuristic: treat >1 as an absolute size and normalize by price to
        // obtain a relative metric. This keeps small-price tokens from being
        // starved compared to large-price tokens.
        normStrength = normStrength / std::max(midPrice, 0.0001);
    }
    normStrength = std::clamp(normStrength, 0.0, 1.0);

    // Base size calculation with quality emphasis (operate on normalized strength)
    double baseSize = std::pow(normStrength * confidence * quality, 0.9); // Slight compression
    
    // Regime-aware scaling
    auto regime = getCurrentMarketRegime(imbalance);
    double regimeScaling = 1.0;
    
    switch (regime) {
        case MarketRegimeType::NORMAL:
            regimeScaling = 1.0;
            break;
        case MarketRegimeType::HIGH_VOLATILITY:
            regimeScaling = 0.7; // Reduce size in volatile conditions
            break;
        case MarketRegimeType::TRENDING:
            regimeScaling = 1.2; // Increase size in trending markets
            break;
        case MarketRegimeType::MEAN_REVERTING:
            regimeScaling = 0.8; // Slight reduction in range-bound markets
            break;
        case MarketRegimeType::STRESS:
            regimeScaling = 0.5; // Significant reduction during stress
            break;
        case MarketRegimeType::QUIET:
            regimeScaling = 1.0;
            break;
        default:
            regimeScaling = 0.9;
            break;
    }
    
    // Type-specific adjustments (more nuanced than original)
    double typeScaling = 1.0;
    switch (imbalance.type) {
        case ImbalanceType::VOLUME_IMBALANCE:
            typeScaling = 1.0;
            break;
        case ImbalanceType::PRICE_LEVEL_IMBALANCE:
            typeScaling = 0.8;
            break;
        case ImbalanceType::LIQUIDITY_REMOVAL:
            typeScaling = (regime == MarketRegimeType::STRESS) ? 0.8 : 1.3; // Reduce during stress
            break;
        case ImbalanceType::ABNORMAL_SPREAD:
            typeScaling = 0.6;
            break;
        case ImbalanceType::TREND_CONTINUATION:
            typeScaling = (regime == MarketRegimeType::TRENDING) ? 1.4 : 0.9;
            break;
        case ImbalanceType::TREND_REVERSAL:
            typeScaling = (regime == MarketRegimeType::MEAN_REVERTING) ? 1.2 : 0.8;
            break;
        default:
            typeScaling = 0.9;
            break;
    }
    
    double finalSize = baseSize * regimeScaling * typeScaling;

    // Determine volatility to use: prefer explicit parameter if > 0, otherwise
    // fall back to the supporting metric inside the event. Use a small positive
    // default to avoid divide-by-zero.
    double currentVol = volatility;
    if (currentVol <= 0.0) {
        currentVol = imbalance.getSupportingMetric("volatility", 0.001);
    }

    // Normalize volatility cross-symbol. We avoid raw volatility values which
    // can differ dramatically between assets. Use a smoothing rule based on
    // square-root scaling which is stable across magnitudes and then clamp to
    // a reasonable band so adjustments are not extreme.
    double volNorm = std::max(0.0001, currentVol);
    // Smoother adjustment: smaller effect around typical vol, reduce ~30% for
    // assets with vol ~4x typical (tunable offline).
    double volAdjustment = 1.0 / std::sqrt(volNorm * 100.0); // heuristic smoothing
    volAdjustment = std::clamp(volAdjustment, 0.5, 1.2);
    finalSize *= volAdjustment;

    // Ensure we return a cross-symbol-stable fraction. Enforce a small minimum
    // allocation so low-price tokens are not always starved; this floor can be
    // tuned per deployment. Keep upper bound at 1.0.
    return std::clamp(finalSize, 0.05, 1.0);
}

std::chrono::milliseconds ImbalanceSignalGenerator::calculateSignalTTL(
    const ImbalanceEvent& imbalance,
    double deltaPrice) const 
{
    // ---------------------------
    // 1. Extract volatility rate
    // ---------------------------
    double volRate = imbalance.getSupportingMetric("volatilityPerUnitTime", 0.001);
    if (volRate <= 0.0) volRate = 0.001;

    // ---------------------------
    // 2. Normalize target move
    // ---------------------------
    double midPrice = imbalance.getSupportingMetric("midPrice", 1.0);
    double relMove = deltaPrice / midPrice;
    if (relMove < 1e-6) relMove = 1e-6;

    // ---------------------------
    // 3. Core TTL model:
    //    time = move / (volRate * strength)
    // ---------------------------
    double strength = std::clamp(imbalance.strength, 0.05, 1.0);
    double strengthFactor = std::pow(strength, 0.8);  // smoother than old 0.7

    double ttlSeconds = relMove / (volRate * strengthFactor);

    // ---------------------------
    // 4. Apply regime effects
    // ---------------------------
    auto regime = getCurrentMarketRegime(imbalance);
    double regimeFactor = 1.0;

    switch (regime) {
        case MarketRegimeType::HIGH_VOLATILITY:
            regimeFactor = 0.55;   // targets hit faster
            break;
        case MarketRegimeType::STRESS:
            regimeFactor = 0.45;   // even faster TTL, market unstable
            break;
        case MarketRegimeType::TRENDING:
            regimeFactor = 1.30;   // trending signals last longer
            break;
        case MarketRegimeType::MEAN_REVERTING:
            regimeFactor = 1.10;   // slower move toward target
            break;
        // case MarketRegimeType::NORMAL:
        //     regimeFactor = 1;
        //     break;
        case MarketRegimeType::LOW_LIQUIDITY:
            regimeFactor = 0.75;
            break;
        case MarketRegimeType::QUIET:
            regimeFactor = 1.20;
            break;
        default:
            break;
    }

    ttlSeconds *= regimeFactor;

    // ---------------------------
    // 5. Convert to ms and clamp
    // ---------------------------
    uint64_t ttlMs = static_cast<uint64_t>(ttlSeconds * 1000.0);

    // dynamic windows per regime
    uint64_t minTTL = 0;
    uint64_t maxTTL = 0;

    switch (regime) {
        case MarketRegimeType::HIGH_VOLATILITY:
        case MarketRegimeType::STRESS:
            minTTL = 150;
            maxTTL = 25000;
            break;

        case MarketRegimeType::TRENDING:
            minTTL = 500;
            maxTTL = 120000;   // up to 2 minutes for long trends
            break;

        case MarketRegimeType::MEAN_REVERTING:
        case MarketRegimeType::QUIET:
            minTTL = 400;
            maxTTL = 60000;
            break;

        default:
            minTTL = 300;
            maxTTL = 60000;
            break;
    }

    ttlMs = std::clamp(ttlMs, minTTL, maxTTL);

    return std::chrono::milliseconds(ttlMs);
}
 

// Enhanced price target generation
std::vector<PriceTarget> ImbalanceSignalGenerator::generatePriceTargets(const ImbalanceEvent& imbalance) const {
    std::vector<PriceTarget> targets;
    
    auto midPriceIt = imbalance.supportingMetrics.find("midPrice");
    if (midPriceIt == imbalance.supportingMetrics.end()) {
        return targets;
    }
    
    double midPrice = midPriceIt->second;
    
    // Enhanced volatility calculation
    double volatility = 0.001;
    auto volatilityIt = imbalance.supportingMetrics.find("volatility");
    if (volatilityIt != imbalance.supportingMetrics.end()) {
        volatility = volatilityIt->second;
    }
    
    // Regime-aware volatility adjustment
    auto regime = getCurrentMarketRegime(imbalance);
    double regimeVolAdjustment = 1.0;
    
    // Adjust regime scaling conservatively. Note: LOW_LIQUIDITY should not
    // scale volatility (we will increase impact instead), and STRESS uses a
    // moderate uplift rather than doubling volatility.
    double lowLiquidityImpactBoost = 1.0;
    switch (regime) {
        case MarketRegimeType::HIGH_VOLATILITY:
            regimeVolAdjustment = 1.3; // was 1.5 -> moderate
            break;
        case MarketRegimeType::TRENDING:
            regimeVolAdjustment = 1.1; // was 1.2 -> slight reduction
            break;
        case MarketRegimeType::MEAN_REVERTING:
            regimeVolAdjustment = 0.8;
            break;
        case MarketRegimeType::NORMAL:
            regimeVolAdjustment = 1.0;
            break;
        case MarketRegimeType::QUIET:
            regimeVolAdjustment = 0.9;
            break;
        case MarketRegimeType::LOW_LIQUIDITY:
            // Do NOT scale volatility for low-liquidity; instead boost impact
            regimeVolAdjustment = 1.0;
            lowLiquidityImpactBoost = 1.20; // increase final impact by ~20%
            break;
        case MarketRegimeType::STRESS:
            regimeVolAdjustment = 1.4; // increase modestly (was 1.3, avoid x2)
            break;
        default:
            regimeVolAdjustment = 1.0;
            break;
    }

    // Only scale volatility for regimes other than LOW_LIQUIDITY (we handled that above)
    volatility *= regimeVolAdjustment;
    
    // Calculate impact with enhanced methodology
    // double conservativeVolatility = std::min(volatility, 0.04); // Increased cap to 4%
    // double baseImpactPercent = conservativeVolatility * 0.25; // Increased base impact
    // Conservative volatility cap to avoid extreme targets (1.5% cap)
    double conservativeVolatility = std::min(volatility, 0.015);   // cap 1.5%
    // Base impact reduced so typical imbalance yields bps-level moves
    double baseImpactPercent = conservativeVolatility * 0.15; // conservative scaling

    // Strength multiplier tuned to produce moderate amplification for strong signals
    double normalizedStrength = std::clamp(imbalance.strength, 0.0, 1.0);
    double strengthMultiplier = 0.7 + std::pow(normalizedStrength, 1.1) * 0.6;
    double finalImpactPercent = baseImpactPercent * strengthMultiplier;

    // For low liquidity we boost impact directly (depth-driven), not volatility
    if (lowLiquidityImpactBoost > 1.0) {
        finalImpactPercent *= lowLiquidityImpactBoost;
    }

    // Ensure final impact is within realistic microstructure bounds: 0.05% - 0.30%
    const double MIN_IMPACT = 0.0005; // 0.05%
    const double MAX_IMPACT = 0.0030; // 0.30%
    finalImpactPercent = std::clamp(finalImpactPercent, MIN_IMPACT, MAX_IMPACT);
    
    // Enhanced type-specific adjustments
    double typeMultiplier = getImbalanceTypeScaling(imbalance.type, regime);
    finalImpactPercent *= typeMultiplier;
    
    // Apply direction
    if (imbalance.direction == core::Direction::SELL) {
        finalImpactPercent = -finalImpactPercent;
    }
    
    // Dynamic target generation based on regime
    std::vector<double> targetMultipliers;
    std::vector<double> targetProbabilities;
    
    if (regime == MarketRegimeType::TRENDING) {
        targetMultipliers = {0.15, 0.40, 0.80, 1.20}; // More aggressive targets
        targetProbabilities = {0.8, 0.6, 0.4, 0.2};
    } else if (regime == MarketRegimeType::HIGH_VOLATILITY) {
        targetMultipliers = {0.40, 0.70, 1.00}; // Fewer, closer targets
        targetProbabilities = {0.6, 0.4, 0.2};
    } else {
        targetMultipliers = {0.25, 0.60, 1.00}; // Standard targets
        targetProbabilities = {0.7, 0.5, 0.3};
    }
    
    // Generate targets
    for (size_t i = 0; i < targetMultipliers.size(); ++i) {
        PriceTarget target;
        target.percentage = finalImpactPercent * targetMultipliers[i];
        target.price = midPrice * (1.0 + target.percentage);
        target.probability = targetProbabilities[i];
        target.expected_time = calculateSignalTTL(imbalance, std::abs(target.percentage * midPrice));
        
        //----------------------------------------------------
        // NEW INTEGRATION: refine using Volume Profile (VP)
        //----------------------------------------------------
        // volumeProfileMetrics_ is optional and may not exist in this class
        // We check for its presence and for an entry for this symbol. If
        // present we refine the impact-based price target by blending it
        // with a VP-structural target.
        // Do a short locked lookup for VP metrics for this symbol and copy
        std::map<std::string, double> vpEntry;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto vpIt = volumeProfileMetrics_.find(imbalance.getTradingPair());
            if (vpIt != volumeProfileMetrics_.end()) {
                vpEntry = vpIt->second;
            }
        }
        if (!vpEntry.empty()) {
            const auto& vp = vpEntry;
            double poc  = getVP(vp, "poc");
            double vah  = getVP(vp, "value_area_high");
            double val  = getVP(vp, "value_area_low");
            double vwap = getVP(vp, "vwap");
            double hvnAbove = getVP(vp, "hvn_above");
            double hvnBelow = getVP(vp, "hvn_below");
            
            double imbalanceDir = imbalance.supportingMetrics.count("imbalance_direction")
                ? imbalance.supportingMetrics.at("imbalance_direction")
                : 0.0;

            // Only refine when VP metrics look valid (poc/vwap present)
            if (poc > 0.0 || vwap > 0.0) {
                target.price = refineTargetWithVP(
                    target.price,
                    midPrice,
                    poc, vah, val,
                    vwap,
                    imbalanceDir,
                    hvnAbove, hvnBelow
                );
            }
        }

        targets.push_back(target);
    }
    
    return targets;
}

// Small helper to safely extract VP metrics from map
double ImbalanceSignalGenerator::getVP(const std::map<std::string,double>& m,
                                      const std::string& k) const {
    auto it = m.find(k);
    return (it != m.end() ? it->second : -1.0);
}

// Hybrid VP refinement: blends impact-based target with a structural VP target
double ImbalanceSignalGenerator::refineTargetWithVP(
    double impactPrice,
    double midPrice,
    double poc,
    double vah,
    double val,
    double vwap,
    double imbalanceDir,
    double hvnAbove,
    double hvnBelow) const {

    // Basic VP-driven target selection. Prefer POC for mean-reversion,
    // VAH/VAL extensions for breakouts, and HVN nodes as magnets when available.
    double vpTarget = impactPrice;

    // If we have POC and we're likely mean-reverting (imbalanceDir small or
    // price near POC), push to POC.
    double distMidToPOC = (poc > 0.0) ? std::abs(midPrice - poc) / midPrice : 1.0;

    if (distMidToPOC < 0.01 && std::abs(imbalanceDir) < 0.5) {
        vpTarget = poc;
    }

    // Breakout preference: if price above VAH and direction is buy, aim for
    // VAH extension or HVN above if available. Symmetric for short.
    if (midPrice > vah && imbalanceDir > 0) {
        if (hvnAbove > 0.0) vpTarget = hvnAbove;
        else vpTarget = vah * (1.0 + 0.005); // small extension beyond VAH
    } else if (midPrice < val && imbalanceDir < 0) {
        if (hvnBelow > 0.0) vpTarget = hvnBelow;
        else vpTarget = val * (1.0 - 0.005);
    }

    // If the impact target sits inside noise clusters (close to POC/VWAP),
    // prefer VP structural target
    double distImpact = std::abs(impactPrice - midPrice) / midPrice;
    double distVP     = std::abs(vpTarget    - midPrice) / midPrice;

    double wVp = 0.0;
    if (distVP < 0.003) {
        wVp = 0.75;
    } else if ((midPrice > vah && imbalanceDir > 0) || (midPrice < val && imbalanceDir < 0)) {
        wVp = 0.60;
    } else {
        wVp = 0.35;
    }

    double wImpact = 1.0 - wVp;

    return impactPrice * wImpact + vpTarget * wVp;
}

// Enhanced momentum correlation methods
double ImbalanceSignalGenerator::calculateConfidenceMultiplier(double absCorrelation, bool aligned) const {
    if (!aligned) {
        return std::max(0.5, 1.0 - 0.3 * absCorrelation); // Larger penalty for strong conflicting correlations
    }
    
    // Non-linear confidence boost for aligned predictions
    if (absCorrelation >= 0.8) {
        return 1.0 + 0.4 * std::pow((absCorrelation - 0.8) / 0.2, 1.5);
    } else if (absCorrelation >= 0.6) {
        return 1.0 + 0.2 * ((absCorrelation - 0.6) / 0.2);
    } else if (absCorrelation >= 0.4) {
        return 1.0 + 0.1 * ((absCorrelation - 0.4) / 0.2);
    }
    
    return 1.0;
}

bool ImbalanceSignalGenerator::checkPredictionAlignment(core::Direction signalDirection, double predictedMove) const {
    if (std::abs(predictedMove) < config_.minPredictionThreshold) {
        return true;
    }
    
    return (signalDirection == core::Direction::BUY && predictedMove > 0) ||
           (signalDirection == core::Direction::SELL && predictedMove < 0);
}

void ImbalanceSignalGenerator::handlePredictionConflict(
    TradingSignal& signal, const ImbalanceEvent& imbalance,
    double predictedMove, double correlation) {
    
    double absCorrelation = std::abs(correlation);
    double absPrediction = std::abs(predictedMove);
    
    // Enhanced conflict resolution
    if (absCorrelation >= config_.minCorrelationForOverride && 
        absPrediction >= config_.minPredictionThreshold) {
        
        // Consider signal strength vs prediction strength
        double signalWeight = signal.strength * signal.confidence;
        double predictionWeight = absCorrelation * std::min(1.0, absPrediction * 100.0);
        
        if (predictionWeight > signalWeight * 1.5) { // Require strong prediction advantage
            core::Direction newDirection = (predictedMove > 0) ? core::Direction::BUY : core::Direction::SELL;
            
            LOG_INFO("Direction override: {} -> {} (correlation={:.3f}, prediction={:.4f}%, weights: signal={:.3f}, pred={:.3f})",
                    directionToString(signal.direction), directionToString(newDirection),
                    correlation, predictedMove * 100, signalWeight, predictionWeight);
            
            signal.direction = newDirection;
            signal.setMetric("directionOverridden", 1.0);
            signal.setMetric("overrideReason", "strongPrediction");
        } else {
            // Apply graduated penalty based on conflict strength
            double conflictStrength = absCorrelation * absPrediction;
            double penalty = config_.conflictConfidencePenalty * (0.5 + 0.5 * conflictStrength);
            
            signal.confidence *= penalty;
            signal.setMetric("predictionConflictPenalty", penalty);
        }
    }
}

// Market regime methods
MarketRegimeType ImbalanceSignalGenerator::getCurrentMarketRegime(const ImbalanceEvent& imbalance) const {
    if (regimeDetector_) {
        // Use actual regime detector if available
        // return regimeDetector_->getCurrentRegime();
    }

    auto regime_number = imbalance.getSupportingMetric("marketRegime", static_cast<double>(MarketRegimeType::UNKNOWN));
    // sanity check for regime number if it's within valid range from 0-8
    if (regime_number >= 0 && regime_number <= 8) {
        // The market regime can be injected into the ImbalanceEvent by
        // OrderBookAnalyzer via currentMetrics_.metrics["marketRegime"] = (double)int(regime).
        // Use that metric value as the authoritative regime when present.
        // LOG_DEBUG("Current market regime (from metric): {} {}",
                //  regime_number,
                //   marketRegimeToString(static_cast<MarketRegimeType>(static_cast<int>(regime_number))));
        return static_cast<MarketRegimeType>(static_cast<int>(regime_number));
    }

    return MarketRegimeType::UNKNOWN;
}

double ImbalanceSignalGenerator::getImbalanceTypeScaling(ImbalanceType type, MarketRegimeType regime) const {
    size_t typeIndex = static_cast<size_t>(type);
    size_t regimeIndex = static_cast<size_t>(regime);
    
    if (typeIndex < REGIME_TYPE_SCALING_MATRIX.size()) {
        return REGIME_TYPE_SCALING_MATRIX[typeIndex][regimeIndex];
    }
    
    return 0.9; // Conservative default
}

double ImbalanceSignalGenerator::getRegimeConfidenceAdjustment(MarketRegimeType regime, double baseConfidence) const {
    size_t regimeIndex = static_cast<size_t>(regime);
    return baseConfidence * REGIME_BASE_ADJUSTMENTS[regimeIndex];
}

double ImbalanceSignalGenerator::getHistoricalPerformanceAdjustment(ImbalanceType type) const {
    if (performanceTracker_) {
        // Use actual historical performance if available
        // return performanceTracker_->getTypePerformance(type);
    }
    
    // Fallback to empirically-derived adjustments
    switch (type) {
        case ImbalanceType::VOLUME_IMBALANCE:
            return 1.0;
        case ImbalanceType::LIQUIDITY_REMOVAL:
            return 1.1; // Historically strong signal
        case ImbalanceType::LARGE_ORDER_APPEARANCE:
            return 1.05;
        case ImbalanceType::TREND_CONTINUATION:
            return 0.95; // Slightly lower historical performance
        case ImbalanceType::ABNORMAL_SPREAD:
            return 0.85; // Lower reliability
        default:
            return 1.0;
    }
}

// Configuration and state management
void ImbalanceSignalGenerator::configureSignalParameters(const SignalGeneratorConfig& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = params;
}

void ImbalanceSignalGenerator::updateParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (thresholdManager_) {
        // Update threshold manager with new market conditions
        // thresholdManager_->updateMarketConditions(params);
    }
}

SignalGeneratorConfig ImbalanceSignalGenerator::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

std::string ImbalanceSignalGenerator::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostringstream oss;
    
    oss << "Enhanced ImbalanceSignalGenerator State:\n";
    oss << "  Configuration:\n";
    oss << "    Min Signal Strength: " << config_.minSignalStrength << "\n";
    oss << "    Confidence Threshold: " << config_.confidenceThreshold << "\n";
    oss << "    Signal TTL (ms): " << config_.signalTtlMs << "\n";
    oss << "    Min Prediction Threshold: " << config_.minPredictionThreshold << "\n";
    oss << "    Min Correlation for Override: " << config_.minCorrelationForOverride << "\n";
    oss << "    Conflict Confidence Penalty: " << config_.conflictConfidencePenalty << "\n";
    
    oss << "  Internal State:\n";
    oss << "    Pending Imbalances: " << pendingImbalances_.size() << "\n";
    oss << "    Generated Signals: " << generatedSignals_.size() << "\n";
    oss << "    Total Signals Generated: " << signalCount_ << "\n";
    {
        std::lock_guard<std::mutex> cbLock(callbacksMutex_);
        oss << "    Registered Callbacks: " << callbacks_.size() << "\n";
    }
    
    oss << "  Component Status:\n";
    oss << "    Momentum Correlation: " << (momentumCorrelation_ ? "Active" : "Inactive") << "\n";
    oss << "    Threshold Manager: " << (thresholdManager_ ? "Active" : "Inactive") << "\n";
    oss << "    Regime Detector: " << (regimeDetector_ ? "Active" : "Inactive") << "\n";
    oss << "    Performance Tracker: " << (performanceTracker_ ? "Active" : "Inactive") << "\n";
    
    return oss.str();
}

// Component setters
void ImbalanceSignalGenerator::setMomentumCorrelation(MomentumCorrelationPtr momentumCorrelation) {
    std::lock_guard<std::mutex> lock(mutex_);
    momentumCorrelation_ = momentumCorrelation;
}
// setSignalDecayModel
void ImbalanceSignalGenerator::setSignalDecayModel(SignalDecayModelPtr signalDecayModel) {
    std::lock_guard<std::mutex> lock(mutex_);
    signalDecayModel_ = signalDecayModel;
}
void ImbalanceSignalGenerator::setThresholdManager(ThresholdManagerPtr thresholdManager) {
    std::lock_guard<std::mutex> lock(mutex_);
    thresholdManager_ = thresholdManager;
}

void ImbalanceSignalGenerator::setMarketRegimeDetector(std::shared_ptr<MarketRegimeDetector> regimeDetector) {
    std::lock_guard<std::mutex> lock(mutex_);
    regimeDetector_ = regimeDetector;
}


void ImbalanceSignalGenerator::setSignalPerformanceTracker(std::shared_ptr<SignalPerformanceTracker> performanceTracker) {
    std::lock_guard<std::mutex> lock(mutex_);
    performanceTracker_ = performanceTracker;
}

void ImbalanceSignalGenerator::setVolumeProfileMetrics(const std::map<std::string, std::map<std::string, double>>& vp) {
    std::lock_guard<std::mutex> lock(mutex_);
    volumeProfileMetrics_ = vp;
}

// Callback management
size_t ImbalanceSignalGenerator::registerSignalCallback(const SignalCallback& callback) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    size_t id = nextCallbackId_++;
    callbacks_[id] = callback;
    return id;
}

bool ImbalanceSignalGenerator::unregisterSignalCallback(size_t callbackId) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    return callbacks_.erase(callbackId) > 0;
}

void ImbalanceSignalGenerator::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    pendingImbalances_.clear();
    generatedSignals_.clear();
}

uint64_t ImbalanceSignalGenerator::getSignalCount() const {
    return signalCount_.load();
}

// Utility methods
core::SignalId ImbalanceSignalGenerator::generateSignalId() const {
    // Fast, low-allocation signal id generation using snprintf instead of
    // std::ostringstream to avoid iostream overhead on the hot path.
    auto now = std::chrono::system_clock::now();
    auto now_us = std::chrono::time_point_cast<std::chrono::microseconds>(now);
    auto epoch = now_us.time_since_epoch();
    uint64_t us_count = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(epoch).count());

    uint64_t counter = signalCount_.load();

    // Use a thread-local PRNG seeded from a time-based seed to avoid
    // per-call std::random_device usage which can block on some platforms.
    static thread_local std::mt19937 rng(
        static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ reinterpret_cast<uintptr_t>(&rng)));
    std::uniform_int_distribution<uint32_t> distrib(0, 0xFFFF);
    uint32_t rnd = distrib(rng);

    // Format: <12-hex-us>-<8-dec-counter>-<4-hex-rand>
    char buf[64];
    // Use snprintf (C-style) which is generally faster than ostringstream here.
    int len = std::snprintf(buf, sizeof(buf), "%012llx-%08llu-%04x",
                            static_cast<unsigned long long>(us_count),
                            static_cast<unsigned long long>(counter),
                            static_cast<unsigned int>(rnd));

    if (len <= 0) {
        return core::SignalId("");
    }

    return core::SignalId(std::string(buf, static_cast<size_t>(len)));
}

void ImbalanceSignalGenerator::notifySignalCallbacks(const TradingSignal& signal) {
    // Copy callbacks under callbacksMutex_ only; avoid holding main mutex_ to
    // prevent deadlocks if callbacks try to call back into this generator.
    std::vector<SignalCallback> callbacksToNotify;
    {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        callbacksToNotify.reserve(callbacks_.size());
        for (const auto& [id, callback] : callbacks_) {
            callbacksToNotify.push_back(callback);
        }
    }

    // Notify without any locks held
    for (const auto& callback : callbacksToNotify) {
        try {
            callback(signal);
        } catch (const std::exception& e) {
            LOG_ERROR("Signal callback failed: {}", e.what());
        }
    }
} 

} // namespace strategy
} // namespace trading