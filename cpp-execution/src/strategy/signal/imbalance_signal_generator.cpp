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
#include <common/logger.hpp>

namespace trading {
namespace strategy {
// Static lookup tables for regime-aware scaling
const std::array<std::array<double, 7>, 19> ImbalanceSignalGenerator::REGIME_TYPE_SCALING_MATRIX = {{
    // NORMAL, VOLATILE, TRENDING, RANGE_BOUND, STRESS, QUIET, UNKNOWN
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
    0.9,  // RANGE_BOUND (slight reduction in range-bound markets)
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

// Core processing methods
int ImbalanceSignalGenerator::processImbalances(const std::vector<ImbalanceEvent>& imbalances) {
    auto t0 = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);
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
        LOG_DEBUG("ImbalanceSignalGenerator::processImbalances took {} ms", dur);
    }

    return signalsGenerated;
}

bool ImbalanceSignalGenerator::processImbalance(const ImbalanceEvent& imbalance) {
    // Apply enhanced filtering
    if (!passesSignalFilters(imbalance)) {
        return false;
    }
    
    // Store for potential batch processing
    pendingImbalances_.push_back(imbalance);
    
    // Generate signal with advanced algorithms
    TradingSignal signal = createSignalFromImbalance(imbalance);
    generatedSignals_.push_back(signal);
    
    // Enhanced logging with market context
    auto price = imbalance.getSupportingMetric("midPrice", 0.0);
    auto regime = getCurrentMarketRegime(imbalance);
    
    LOG_DEBUG("Enhanced signal generated: Type={}, Direction={}, Strength={:.3f}, "
             "Confidence={:.2f}, Quality={:.3f}, Price={:.2f}, Regime={}, SignalID={}",
             imbalanceTypeToString(imbalance.type),
             (imbalance.direction == core::Direction::BUY ? "BUY" : 
              (imbalance.direction == core::Direction::SELL ? "SELL" : "NEUTRAL")),
             signal.strength, signal.confidence, signal.quality, price,
                marketRegimeToString(regime), signal.id);
    
    // Notify callbacks
    notifySignalCallbacks(signal);
    signalCount_++;
    
    return true;
}

std::vector<TradingSignal> ImbalanceSignalGenerator::generateSignals() {
    std::lock_guard<std::mutex> lock(mutex_);
    return generatedSignals_;
}

// Enhanced signal creation
TradingSignal ImbalanceSignalGenerator::createSignalFromImbalance(const ImbalanceEvent& imbalance) {
    TradingSignal signal;
    auto t0 = std::chrono::steady_clock::now();
    // Basic signal properties
    signal.id = generateSignalId();
    signal.sourceEventId = imbalance.eventId;
    signal.correlationId = imbalance.eventId;
    signal.direction = imbalance.direction;
    signal.sourceType = imbalance.type;
    signal.symbol = imbalance.symbol;
    signal.exchangeId = imbalance.exchangeId;
    
    signal.generationTime = std::chrono::system_clock::now();
    signal.confidence = trading::core::confidenceLevelToValue(imbalance.confidence);
    
    // Advanced strength calculation
    auto strengthComponents = calculateSignalStrength(imbalance);
    signal.strength = strengthComponents.finalStrength;
    signal.setMetric("strengthComponents", strengthComponents.toString());
    
    // Generate sophisticated price targets
    signal.targets = generatePriceTargets(imbalance);
    
    // Calculate adaptive TTL
    auto deltaPrice = signal.targets.empty() ? 0.001 : 
                     std::abs(signal.targets.back().price - signal.price);
    signal.ttl = calculateSignalTTL(imbalance, deltaPrice);
    signal.expirationTime = imbalance.detectionTime + signal.ttl;
    
    // Copy and enhance metrics
    for (const auto& metric : imbalance.supportingMetrics) {
        signal.setMetric(metric.first, metric.second);
    }
    
    // Apply momentum correlation analysis
    double predictedMove = 0.0;
    double correlation = 0.0;
    
    if (momentumCorrelation_) {
        predictedMove = momentumCorrelation_->predictPriceMovement(imbalance);
        std::string imbalanceTypeStr = imbalanceTypeToString(imbalance.type);
        correlation = momentumCorrelation_->getCorrelationCoefficient(imbalanceTypeStr);
        
        signal.setMetric("predictedPriceMove", predictedMove);
        signal.setMetric("momentumCorrelation", correlation);
        
        // Handle prediction alignment
        bool alignmentCorrect = checkPredictionAlignment(imbalance.direction, predictedMove);
        signal.setMetric("predictionAligned", alignmentCorrect ? 1.0 : 0.0);
        
        if (!alignmentCorrect) {
            handlePredictionConflict(signal, imbalance, predictedMove, correlation);
        }
        
        // Apply sophisticated confidence adjustment
        double absCorrelation = std::abs(correlation);
        double confidenceMultiplier = calculateConfidenceMultiplier(absCorrelation, alignmentCorrect);
        signal.confidence = std::clamp(signal.confidence * confidenceMultiplier, 0.0, 1.0);
    }
    //if direction is BUY then set price to best ask else if SELL set to best bid
    if (imbalance.direction == core::Direction::BUY) {
        signal.price = imbalance.getSupportingMetric("askPrice", 0.0);
    } else if (imbalance.direction == core::Direction::SELL) {
        signal.price = imbalance.getSupportingMetric("bidPrice", 0.0);
    } else {
        signal.price = imbalance.getSupportingMetric("midPrice", 0.0);
    }
    //if price is 0 then we should get from something else
    // Calculate comprehensive quality metrics
    auto qualityMetrics = calculateSignalQuality(signal, predictedMove, correlation);
    signal.quality = qualityMetrics.finalQuality;
    signal.setMetric("qualityMetrics", qualityMetrics.toString());
    
    // Calculate adaptive target size
    signal.targetSize = calculateTargetSize(imbalance, signal.strength, signal.confidence, signal.quality);
    
    // Apply threshold manager adjustments
    if (thresholdManager_) {
        double sensitivity = thresholdManager_->getGlobalSensitivity();
        signal.setMetric("thresholdSensitivity", sensitivity);
        
        if (sensitivity != 1.0) {
            signal.strength = std::min(1.0, signal.strength * sensitivity);
        }
    }
    
    // Add market regime context
    auto regime = getCurrentMarketRegime(imbalance);
    signal.setMetric("marketRegime", static_cast<double>(regime));
    signal.setMetric("regimeName", marketRegimeToString(regime));
    auto dur_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();

    if (dur_us > 2000) {
        LOG_DEBUG("createSignalFromImbalance: signal id={} creation took {} us", signal.id, dur_us);
    } else {
        LOG_DEBUG("createSignalFromImbalance: took {} us", dur_us);
    }
    
    return signal;
}

// Enhanced filtering with regime awareness
bool ImbalanceSignalGenerator::passesSignalFilters(const ImbalanceEvent& imbalance) const {
    // Basic strength check
    if (imbalance.strength < config_.minSignalStrength) {
        return false;
    }
    
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
    if (regime == MarketRegime::STRESS) {
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
    if (regime == MarketRegime::RANGE_BOUND || regime == MarketRegime::VOLATILE) {
        metrics.finalQuality *= 0.8; // Reduce quality during difficult conditions
    }
    
    metrics.finalQuality = std::clamp(metrics.finalQuality, 0.0, 1.0);
    
    return metrics;
}

// Enhanced target size calculation
double ImbalanceSignalGenerator::calculateTargetSize(
    const ImbalanceEvent& imbalance, double strength, double confidence, double quality, double volatility) const {
    
    // Base size calculation with quality emphasis
    double baseSize = std::pow(strength * confidence * quality, 0.9); // Slight compression
    
    // Regime-aware scaling
    auto regime = getCurrentMarketRegime(imbalance);
    double regimeScaling = 1.0;
    
    switch (regime) {
        case MarketRegime::NORMAL:
            regimeScaling = 1.0;
            break;
        case MarketRegime::VOLATILE:
            regimeScaling = 0.7; // Reduce size in volatile conditions
            break;
        case MarketRegime::TRENDING:
            regimeScaling = 1.2; // Increase size in trending markets
            break;
        case MarketRegime::RANGE_BOUND:
            regimeScaling = 0.8; // Slight reduction in range-bound markets
            break;
        case MarketRegime::STRESS:
            regimeScaling = 0.5; // Significant reduction during stress
            break;
        case MarketRegime::QUIET:
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
            typeScaling = (regime == MarketRegime::STRESS) ? 0.8 : 1.3; // Reduce during stress
            break;
        case ImbalanceType::ABNORMAL_SPREAD:
            typeScaling = 0.6;
            break;
        case ImbalanceType::TREND_CONTINUATION:
            typeScaling = (regime == MarketRegime::TRENDING) ? 1.4 : 0.9;
            break;
        case ImbalanceType::TREND_REVERSAL:
            typeScaling = (regime == MarketRegime::RANGE_BOUND) ? 1.2 : 0.8;
            break;
        default:
            typeScaling = 0.9;
            break;
    }
    
    double finalSize = baseSize * regimeScaling * typeScaling;
    
    // Apply volatility-based adjustments
    // if (volatilityEstimator_) {
        // double currentVol = volatilityEstimator_->getCurrentVolatility(imbalance.symbol);
        double currentVol = imbalance.getSupportingMetric("volatility", 0.001);
        double volAdjustment = 1.0 - std::min(0.5, currentVol * 10.0); // Reduce size with high volatility
    //     finalSize *= volAdjustment;
    // }
    
    return std::clamp(finalSize, 0.0, 1.0);
}

// Enhanced TTL calculation
std::chrono::milliseconds ImbalanceSignalGenerator::calculateSignalTTL(
    const ImbalanceEvent& imbalance, double deltaPrice) const {
    
    // Get volatility metrics
    double volatilityPerTime = imbalance.getSupportingMetric("volatilityPerUnitTime", 0.001);
    if (volatilityPerTime <= 0.0) {
        volatilityPerTime = 0.001;
    }
    
    double price = imbalance.getSupportingMetric("midPrice", 1.0);
    double relativeMove = deltaPrice / price;
    
    // Base TTL calculation with regime awareness
    auto regime = getCurrentMarketRegime(imbalance);
    double regimeFactor = 1.0;
    
    switch (regime) {
        case MarketRegime::VOLATILE:
            regimeFactor = 0.6; // Shorter TTL in volatile markets
            break;
        case MarketRegime::TRENDING:
            regimeFactor = 1.4; // Longer TTL in trending markets
            break;
        case MarketRegime::RANGE_BOUND:
            regimeFactor = 1.1; // Slightly longer in range-bound
            break;
        case MarketRegime::STRESS:
            regimeFactor = 0.5; // Much shorter during stress
            break;
        default:
            regimeFactor = 1.0;
            break;
    }
    
    // Enhanced TTL formula
    double strengthFactor = std::pow(imbalance.strength, 0.7); // Non-linear strength impact
    double baseTTL = (4.0 * relativeMove * regimeFactor) / (volatilityPerTime * strengthFactor);
    
    uint64_t ttlMs = static_cast<uint64_t>(baseTTL * 1000);
    
    // Dynamic bounds based on regime
    uint64_t minTTL = (regime == MarketRegime::VOLATILE || regime == MarketRegime::STRESS) ? 200 : 500;
    uint64_t maxTTL = (regime == MarketRegime::TRENDING) ? 90000 : 60000;
    
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
    
    switch (regime) {
        case MarketRegime::VOLATILE:
            regimeVolAdjustment = 1.5;
            break;
        case MarketRegime::TRENDING:
            regimeVolAdjustment = 1.2;
            break;
        case MarketRegime::RANGE_BOUND:
            regimeVolAdjustment = 0.8;
            break;
        case MarketRegime::STRESS:
            regimeVolAdjustment = 2.0;
            break;
        default:
            regimeVolAdjustment = 1.0;
            break;
    }
    
    volatility *= regimeVolAdjustment;
    
    // Calculate impact with enhanced methodology
    double conservativeVolatility = std::min(volatility, 0.04); // Increased cap to 4%
    double baseImpactPercent = conservativeVolatility * 0.25; // Increased base impact
    double normalizedStrength = std::min(imbalance.strength, 0.9); // Higher cap
    double strengthMultiplier = 0.6 + (std::pow(normalizedStrength, 1.2) * 0.8); // Non-linear
    double finalImpactPercent = baseImpactPercent * strengthMultiplier;
    
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
    
    if (regime == MarketRegime::TRENDING) {
        targetMultipliers = {0.15, 0.40, 0.80, 1.20}; // More aggressive targets
        targetProbabilities = {0.8, 0.6, 0.4, 0.2};
    } else if (regime == MarketRegime::VOLATILE) {
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
        
        targets.push_back(target);
    }
    
    return targets;
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
MarketRegime ImbalanceSignalGenerator::getCurrentMarketRegime(const ImbalanceEvent& imbalance) const {
    if (regimeDetector_) {
        // Use actual regime detector if available
        // return regimeDetector_->getCurrentRegime();
    }
    
    // Fallback to simple heuristics based on available metrics
    double volatility = imbalance.getSupportingMetric("volatility", 0.01);
    double trend = imbalance.getSupportingMetric("trendStrength", 0.0);
    double stress = imbalance.getSupportingMetric("stressIndicator", 0.0);
    auto regime_number = imbalance.getSupportingMetric("marketRegime", static_cast<double>(MarketRegime::UNKNOWN));
    if (regime_number >= static_cast<double>(MarketRegime::NORMAL) && regime_number <= static_cast<double>(MarketRegime::UNKNOWN)) {
        return static_cast<MarketRegime>(static_cast<int>(regime_number));
    }

    //this is bullshit
    // if (stress > 0.7) {
    //     return MarketRegime::STRESS;
    // } else if (volatility > 0.03) {
    //     return MarketRegime::VOLATILE;
    // } else if (std::abs(trend) > 0.6) {
    //     return MarketRegime::TRENDING;
    // } else if (volatility < 0.005 && std::abs(trend) < 0.2) {
    //     return MarketRegime::RANGE_BOUND;
    // }
    
    return MarketRegime::UNKNOWN;
}

double ImbalanceSignalGenerator::getImbalanceTypeScaling(ImbalanceType type, MarketRegime regime) const {
    size_t typeIndex = static_cast<size_t>(type);
    size_t regimeIndex = static_cast<size_t>(regime);
    
    if (typeIndex < REGIME_TYPE_SCALING_MATRIX.size()) {
        return REGIME_TYPE_SCALING_MATRIX[typeIndex][regimeIndex];
    }
    
    return 0.9; // Conservative default
}

double ImbalanceSignalGenerator::getRegimeConfidenceAdjustment(MarketRegime regime, double baseConfidence) const {
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
    std::ostringstream oss;
    
    auto now = std::chrono::system_clock::now();
    auto now_us = std::chrono::time_point_cast<std::chrono::microseconds>(now);
    auto epoch = now_us.time_since_epoch();
    auto us_count = std::chrono::duration_cast<std::chrono::microseconds>(epoch).count();
    
    oss << std::hex << std::setfill('0') << std::setw(12) << us_count << "-";
    oss << std::setw(8) << signalCount_.load() << "-";
    
    // Use a thread-local PRNG seeded from a time-based seed to avoid
    // per-call std::random_device usage which can block on some platforms.
    static thread_local std::mt19937 rng(
        static_cast<unsigned>(std::chrono::high_resolution_clock::now().time_since_epoch().count() ^ reinterpret_cast<uintptr_t>(&rng)));
    std::uniform_int_distribution<uint32_t> distrib(0, 0xFFFF);
    oss << std::setw(4) << distrib(rng);
    
    return core::SignalId(oss.str());
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