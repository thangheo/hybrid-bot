/**
 * @file imbalance_signal_generator.cpp
 * @brief Implementation of the imbalance signal generator
 * 
 * @details
 * Implements the generation of trading signals from detected imbalance events.
 * 
 * Created on: May 13, 2025
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

// using namespace trading::core;
 namespace trading {
 namespace strategy {
 
 // Constructor
 ImbalanceSignalGenerator::ImbalanceSignalGenerator()
     : signalCount_(0), nextCallbackId_(0) {
 }
// Constr
// Constructor with configuration
ImbalanceSignalGenerator::ImbalanceSignalGenerator(const SignalGeneratorConfig& config) {  // ✅ CORRECT CLASS AND CONFIG
    initialize(config);
}

// Initialize method
bool ImbalanceSignalGenerator::initialize(const SignalGeneratorConfig& config) {  // ✅ CORRECT CONFIG TYPE
    config_ = config;
    //print configuration for debugging
    // std::cout<<"ImbalanceSignalGenerator initialized with config: "
    //          <<"minSignalStrength="<<config_.minSignalStrength
    //          <<", confidenceThreshold="<<config_.confidenceThreshold
    //          <<", signalTtlMs="<<config_.signalTtlMs<<"\n";
    signalCount_ = 0;
    nextCallbackId_ = 1;
    pendingImbalances_.clear();
    generatedSignals_.clear();
    callbacks_.clear();
    momentumCorrelation_ = nullptr;
    thresholdManager_ = nullptr;    
    // Clear any existing state
    // Initialize internal components based on config
    return true;
}


 // Process newly detected imbalances
 int ImbalanceSignalGenerator::processImbalances(const std::vector<ImbalanceEvent>& imbalances) {
     std::lock_guard<std::mutex> lock(mutex_);
     int signalsGenerated = 0;
     
     for (const auto& imbalance : imbalances) {
         if (processImbalance(imbalance)) {
             signalsGenerated++;
         }
     }
     
     return signalsGenerated;
 }
 
 // Process a single imbalance event
 bool ImbalanceSignalGenerator::processImbalance(const ImbalanceEvent& imbalance) {
     // Check if imbalance passes filtering criteria
     if (!passesSignalFilters(imbalance)) {
         return false;
     } 
     // Store the imbalance for signal generation
     pendingImbalances_.push_back(imbalance);
     
     // Create and emit signal immediately
     TradingSignal signal = createSignalFromImbalance(imbalance);
     generatedSignals_.push_back(signal);
     auto price = imbalance.getSupportingMetric("midPrice", 0.0);
    //  print price for debug.price
    // LOG_DEBUG("Generated signal from imbalance: Type={}, Direction={}, Strength={:.3f}, Confidence={:.2f}, Price={:.2f}, SignalID={}",
    //           imbalanceTypeToString(imbalance.type),
    //           (imbalance.direction == core::Direction::BUY ? "BUY" : (imbalance.direction == core::Direction::SELL ? "SELL" : "NEUTRAL")),
    //           imbalance.strength,
    //           trading::core::confidenceLevelToValue(imbalance.confidence),
    //           price,
    //           signal.id);
     
     // Notify callbacks
     notifySignalCallbacks(signal);
     
     // Update signal count
     signalCount_++;
     
     return true;
 }
 
 // Generate trading signals from processed imbalances
 std::vector<TradingSignal> ImbalanceSignalGenerator::generateSignals() {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Just return the already generated signals
     // (signals are generated immediately in processImbalance)
     return generatedSignals_;
 }
 
 // Configure signal generation parameters
 void ImbalanceSignalGenerator::configureSignalParameters(const SignalGeneratorConfig& params) {
     std::lock_guard<std::mutex> lock(mutex_);
     config_ = params;
 }
 
 // Register a callback for signal generation
 size_t ImbalanceSignalGenerator::registerSignalCallback(const SignalCallback& callback) {
     std::lock_guard<std::mutex> lock(mutex_);
     size_t id = nextCallbackId_++;
     callbacks_[id] = callback;
     return id;
 }
 
 // Unregister a previously registered callback
 bool ImbalanceSignalGenerator::unregisterSignalCallback(size_t callbackId) {
     std::lock_guard<std::mutex> lock(mutex_);
     auto it = callbacks_.find(callbackId);
     if (it != callbacks_.end()) {
         callbacks_.erase(it);
         return true;
     }
     return false;
 }
 
 // Clear existing imbalance data and pending signals
 void ImbalanceSignalGenerator::clear() {
     pendingImbalances_.clear();
     generatedSignals_.clear();
 }
 
 // Get the total count of signals generated
 uint64_t ImbalanceSignalGenerator::getSignalCount() const {
     return signalCount_.load();
 }
 
 // Set the momentum correlation component for signal enhancement
 void ImbalanceSignalGenerator::setMomentumCorrelation(MomentumCorrelationPtr momentumCorrelation) {
     std::lock_guard<std::mutex> lock(mutex_);
     momentumCorrelation_ = momentumCorrelation;
 }
 
 // Set the threshold manager for adaptive thresholding
 void ImbalanceSignalGenerator::setThresholdManager(ThresholdManagerPtr threshold_manager) {
     std::lock_guard<std::mutex> lock(mutex_);
     thresholdManager_ = threshold_manager;
 }
 //    void updateParameters(const ParameterSet& params);
    void ImbalanceSignalGenerator::updateParameters(const ParameterSet& params) {
        std::lock_guard<std::mutex> lock(mutex_);
        // Update parameters as needed
        // For example, update thresholds or other configurations
        if (thresholdManager_) {
            // thresholdManager_->updateMarketConditions(params.getMarketState());
            // thresholdManager_->updateMarketConditions(
        }
    }
    
 // PRIVATE METHODS
 double ImbalanceSignalGenerator::calculateConfidenceMultiplier(double absCorrelation, bool aligned) {
    if (!aligned) {
        return 1.0; // Conflict already handled separately
    }
    
    // Boost confidence based on correlation strength
    if (absCorrelation >= 0.7) {
        return 1.0 + 0.3 * ((absCorrelation - 0.7) / 0.3); // 1.0x to 1.3x
    } else if (absCorrelation >= 0.5) {
        return 1.0 + 0.15 * ((absCorrelation - 0.5) / 0.2); // 1.0x to 1.15x  
    } else {
        return 1.0; // No boost for weak correlations
    }
}
bool ImbalanceSignalGenerator::checkPredictionAlignment(core::Direction signalDirection, double predictedMove) {
    if (std::abs(predictedMove) < config_.minPredictionThreshold) {
        return true; // Neutral prediction - no conflict
    }
    
    return (signalDirection == core::Direction::BUY && predictedMove > 0) ||
            (signalDirection == core::Direction::SELL && predictedMove < 0);
}
void ImbalanceSignalGenerator::handlePredictionConflict(TradingSignal& signal, 
                                 const ImbalanceEvent& imbalance,
                                 double predictedMove, 
                                 double correlation) {
        
    // Only override for strong correlations with significant predictions
    if (std::abs(correlation) >= config_.minCorrelationForOverride && 
        std::abs(predictedMove) >= config_.minPredictionThreshold) {
        
        // Override signal direction based on prediction
        core::Direction newDirection = (predictedMove > 0) ? core::Direction::BUY : core::Direction::SELL;
        
        LOG_INFO("Direction override: {} -> {} (correlation={:.3f}, prediction={:.4f}%)",
                directionToString(signal.direction), directionToString(newDirection),
                correlation, predictedMove * 100);
        
        signal.direction = newDirection;
        signal.setMetric("directionOverridden", 1.0);
        
    } else {
        // Apply confidence penalty for weaker conflicts
        signal.confidence *= config_.conflictConfidencePenalty;
        signal.setMetric("predictionConflictPenalty", config_.conflictConfidencePenalty);
        
        // LOG_DEBUG("Prediction conflict penalty applied: correlation={:.3f}, prediction={:.4f}%",
        //             correlation, predictedMove * 100);
    }
}
//function to calculate signal ttl

std::chrono::milliseconds ImbalanceSignalGenerator::calculateSignalTTL(const ImbalanceEvent& imbalance, double delta_price) const {
    // Use volatility per unit time instead of regular volatility
    auto volatility_per_time = imbalance.getSupportingMetric("volatilityPerUnitTime", 0.001);
    
    if (volatility_per_time <= 0.0) {
        volatility_per_time = 0.001; // Prevent division by zero
    }
    
    auto price = imbalance.getSupportingMetric("midPrice", 1.0);
    int k = 4;
    // TTL calculation now uses time-normalized volatility
    double relative_move = delta_price / price;
    uint64_t ttl = static_cast<uint64_t>((k * relative_move) / (volatility_per_time * imbalance.strength));
    // relative_delta = delta_price / price;
    ttl = std::clamp(ttl, static_cast<uint64_t>(500), static_cast<uint64_t>(60000)); // Clamp between 0.5s and 60s
    
    //print debug info
    // std::cout << "Calculated TTL: " << ttl << " ms (Delta Price: " << delta_price
    //           << ", Volatility/Time: " << volatility_per_time << ", Price: " << price
    //           << ", Strength: " << imbalance.strength << ")\n";
    // return std::chrono::milliseconds(ttl);
    return std::chrono::milliseconds(ttl);
}

// Create a trading signal from an imbalance event
TradingSignal ImbalanceSignalGenerator::createSignalFromImbalance(const ImbalanceEvent& imbalance) {
     TradingSignal signal;
     
     // Basic signal properties
     signal.id = generateSignalId();
     signal.sourceEventId = imbalance.eventId;  // ← Add this line
     signal.correlationId = imbalance.eventId;  // ← Add this line
     signal.direction = imbalance.direction;
     signal.strength = calculateSignalStrength(imbalance);
     signal.quality = 0.0; // Will be calculated below
     signal.confidence = trading::core::confidenceLevelToValue(imbalance.confidence);
     signal.generationTime = std::chrono::system_clock::now();
    //  signal.ttl = std::chrono::milliseconds(config_.signalTtlMs);
     signal.sourceType = imbalance.type;
     signal.symbol = imbalance.symbol;
     signal.exchangeId = imbalance.exchangeId;
    //  signal.expirationTime = imbalance.detectionTime + signal.ttl; // Set expiration time

     // Generate price targets
     signal.targets = generatePriceTargets(imbalance);
     signal.ttl = calculateSignalTTL(imbalance, std::abs(signal.targets[2].price - imbalance.getSupportingMetric("midPrice", 1.0)));
    //  signal.expirationTime = imbalance.detectionTime + std::chrono::duration_cast<core::Timestamp::duration>(signal.ttl);
     signal.expirationTime = imbalance.detectionTime + signal.ttl;

     // Copy relevant metrics
     for (const auto& metric : imbalance.supportingMetrics) {
         signal.setMetric(metric.first, metric.second);
     } 
     // Apply momentum correlation if available
     if (momentumCorrelation_) {
         double predictedMove = momentumCorrelation_->predictPriceMovement(imbalance);
         std::string imbalanceTypeStr = imbalanceTypeToString(imbalance.type);
         double correlation = momentumCorrelation_->getCorrelationCoefficient(imbalanceTypeStr);
         signal.setMetric("predictedPriceMove", predictedMove);
         signal.setMetric("momentumCorrelation", correlation);

         // Adjust quality based on prediction and correlation
        signal.quality = calculateSignalQuality(signal, predictedMove, correlation);
        bool alignmentCorrect = checkPredictionAlignment(imbalance.direction, predictedMove);
        signal.setMetric("predictionAligned", alignmentCorrect ? 1.0 : 0.0);
        if (!alignmentCorrect) {
            handlePredictionConflict(signal, imbalance, predictedMove, correlation);
        }
        double absCorrelation = std::abs(correlation);
        double confidenceMultiplier = calculateConfidenceMultiplier(absCorrelation, alignmentCorrect);
        signal.confidence = std::clamp(signal.confidence * confidenceMultiplier, 0.0, 1.0);
         // Adjust confidence based on correlation
        // More balanced confidence adjustment based on correlation        
        // std::cout << "Adjusted signal confidence: " << signal.confidence << "\n";
    } else {
        std::cout << "No momentum correlation component set.\n";
    }

     // Apply threshold adjustments if available
     if (thresholdManager_) {
         double sensitivity = thresholdManager_->getGlobalSensitivity();
         signal.setMetric("thresholdSensitivity", sensitivity);
         
         // Adjust strength based on sensitivity
         if (sensitivity != 1.0) {
             signal.strength = std::min(1.0, signal.strength * sensitivity);
         }
     }
     return signal;
 }

// Check if an imbalance event passes signal generation filters
bool ImbalanceSignalGenerator::passesSignalFilters(const ImbalanceEvent& imbalance) const {

    // std::cout<<"Applying filters to imbalance: Type="<<imbalanceTypeToString(imbalance.type)
    //          <<", Direction="<<(imbalance.direction == core::Direction::BUY ? "BUY" : (imbalance.direction == core::Direction::SELL ? "SELL" : "NEUTRAL"))
    //          <<", Strength="<<imbalance.strength<<", Confidence="<<confidenceLevelToValue(imbalance.confidence)<<"\n";
     // Check minimum strength
     if (imbalance.strength < config_.minSignalStrength) {
         return false;
     }
     
     // Check minimum confidence
     if (trading::core::confidenceLevelToValue(imbalance.confidence) < config_.confidenceThreshold) {
         return false;
     }
     
     // Check if direction is neutral
     if (imbalance.direction == core::Direction::NEUTRAL) {
         return false;
     }
     
     // Additional filters can be added here
     
     return true;
 }
 double ImbalanceSignalGenerator::calculateSignalQuality(
    const TradingSignal& signal, double predictedMove, double correlation) const {
    
    // Quality components (each 0.0 to 1.0)
    double strengthComponent = signal.strength;
    double confidenceComponent = signal.confidence;
    double predictionComponent = std::min(1.0, std::abs(predictedMove) * 100.0); // Scale to 0-1
    double correlationComponent = std::abs(correlation);
    
    // Weighted average
    double quality = (strengthComponent * 0.3 + 
                     confidenceComponent * 0.3 + 
                     predictionComponent * 0.2 + 
                     correlationComponent * 0.2);
    
    return std::clamp(quality, 0.0, 1.0);
}

 // Calculate signal strength based on imbalance
 double ImbalanceSignalGenerator::calculateSignalStrength(const ImbalanceEvent& imbalance) const {
     double baseStrength = imbalance.strength;
     
     // Apply type-specific scaling
     switch (imbalance.type) {
         case ImbalanceType::VOLUME_IMBALANCE:
             // Volume imbalance is already well-normalized
             break;
             
         case ImbalanceType::PRICE_LEVEL_IMBALANCE:
             // Price level imbalance might need scaling down
             baseStrength = std::min(1.0, baseStrength * 0.8);
             break;
             
         case ImbalanceType::LIQUIDITY_REMOVAL:
             // Liquidity removal might be more significant
             baseStrength = std::min(1.0, baseStrength * 1.2);
             break;
             
         case ImbalanceType::ABNORMAL_SPREAD:
             // Abnormal spread signals might need scaling down
             baseStrength = std::min(1.0, baseStrength * 0.7);
             break;
             
         default:
             // Default scaling
             break;
     }
     
     // Apply confidence scaling
     double confidenceFactor = static_cast<double>(imbalance.confidence) / 100.0;
     baseStrength = baseStrength * (0.7 + (0.3 * confidenceFactor));
     
     // Ensure strength is in [0, 1] range
     return std::max(0.0, std::min(1.0, baseStrength));
 }
 

 // Generate price targets based on imbalance event
 std::vector<PriceTarget> ImbalanceSignalGenerator::generatePriceTargets(const ImbalanceEvent& imbalance) const {
     std::vector<PriceTarget> targets;
     // Get mid price from supporting metrics
     auto midPriceIt = imbalance.supportingMetrics.find("midPrice");
     if (midPriceIt == imbalance.supportingMetrics.end()) {
         return targets; // Can't generate targets without price
     }
     
     double midPrice = midPriceIt->second;
     
     // Get volatility if available
     double volatility = 0.001; // Default 0.1% volatility
     auto volatilityIt = imbalance.supportingMetrics.find("volatilityPerUnitTime");
     if (volatilityIt != imbalance.supportingMetrics.end()) {
         volatility = volatilityIt->second;
     }
     auto rawVolatility = volatility; // Store raw volatility for logging
    // CRITICAL DEBUG INFO
    double conservativeVolatility = std::min(volatility, 0.03); // Cap at 3%

    // Base impact should be a small fraction of volatility for short-term predictions
    double baseImpactPercent = conservativeVolatility * 0.2; // 20% of volatility as base impact

    // Conservative strength scaling (your strength = 1.0 is too aggressive)
    double normalizedStrength = std::min(imbalance.strength, 0.8); // Cap strength at 80%
    double strengthMultiplier = 0.7 + (normalizedStrength * 0.5); // 0.7x to 1.2x range

    double finalImpactPercent = baseImpactPercent * strengthMultiplier;

    // Type-specific adjustments (keep these conservative)
    //  enum class ImbalanceType {
    //     VOLUME_IMBALANCE,              // Significant difference between bid and ask volume
    //     PRICE_LEVEL_IMBALANCE,         // Imbalance across multiple price levels
    //     LIQUIDITY_REMOVAL,             // Sudden removal of liquidity from one side
    //     ORDER_FLOW_IMBALANCE,          // Directional bias in order submissions
    //     LARGE_ORDER_APPEARANCE,        // Large orders appearing in the book
    //     LARGE_ORDER_DISAPPEARANCE,     // Large orders being removed from the book
    //     PRICE_LEVEL_CONCENTRATION,     // Concentration of volume at specific price levels
    //     BID_ASK_SPREAD_CHANGE,         // Significant change in the bid-ask spread
    //     MARKET_DEPTH_CHANGE,           // Significant change in market depth
    //     LIQUIDITY_FLOW_IMBALANCE,      // Imbalance in liquidity flow
    //     ABNORMAL_SPREAD,               // Abnormal spread between bid and ask prices
    //     LIQUIDITY_IMBALANCE,           // General liquidity imbalance
    //     TREND_CONTINUATION,            // Continuation of a price trend
    //     TREND_REVERSAL,                // Reversal of a price trend
    //     CUSTOM                         // Custom imbalance type defined by strategy
    // };
    switch (imbalance.type) {
        case ImbalanceType::VOLUME_IMBALANCE:       // Type 0
            finalImpactPercent *= 1.1; // Slightly boost
            break;
        case ImbalanceType::PRICE_LEVEL_IMBALANCE:  // Type 1  
            finalImpactPercent *= 0.9; 
            break;
        case ImbalanceType::LIQUIDITY_REMOVAL:
            finalImpactPercent *= 1.2;
            break;
        case ImbalanceType::ABNORMAL_SPREAD:
            finalImpactPercent *= 0.7;
            break;
        default:
            finalImpactPercent *= 0.8;
            break;
    }
    // In high volatility periods, be more aggressive
    if (conservativeVolatility > 0.025) {  // > 2.5%
        finalImpactPercent *= 1.2;  // 20% more aggressive
    }
    // HARD CAP: Never exceed 2% price impact for 30-second targets
    finalImpactPercent = std::clamp(finalImpactPercent, 0.0005, 0.02); // 0.05% to 2.0% max

    // Apply direction
    if (imbalance.direction == core::Direction::SELL) {
        finalImpactPercent = -finalImpactPercent;
    }
    auto signal_ttl = config_.signalTtlMs;
    // Generate realistic targets with the conservative impact
    PriceTarget shortTarget;
    shortTarget.price = midPrice * (1.0 + (finalImpactPercent * 0.25)); // 25% of full impact
    shortTarget.probability = 0.7;
    // Short target: 25% of signal TTL
    // shortTarget.expected_time = std::chrono::milliseconds(static_cast<int64_t>(signal_ttl * 0.25));
    shortTarget.expected_time = calculateSignalTTL(imbalance, std::abs(shortTarget.price - midPrice));
    targets.push_back(shortTarget);

    PriceTarget mediumTarget;
    mediumTarget.price = midPrice * (1.0 + (finalImpactPercent * 0.60)); // 60% of full impact
    mediumTarget.probability = 0.5;
    // mediumTarget.expected_time = std::chrono::milliseconds(static_cast<int64_t>(signal_ttl * 0.50));
    mediumTarget.expected_time = calculateSignalTTL(imbalance, std::abs(mediumTarget.price - midPrice));
    targets.push_back(mediumTarget);

    PriceTarget longTarget;
    longTarget.price = midPrice * (1.0 + finalImpactPercent); // Full impact
    longTarget.probability = 0.3;
    // longTarget.expected_time = std::chrono::milliseconds(static_cast<int64_t>(signal_ttl * 0.75));
    longTarget.expected_time = calculateSignalTTL(imbalance, std::abs(longTarget.price - midPrice));

    targets.push_back(longTarget);
 
    return targets;
}

// Generate a unique signal ID
core::SignalId ImbalanceSignalGenerator::generateSignalId() const {
     // Create a unique ID based on timestamp and counter
     std::ostringstream oss;
     
     // Get current time with microsecond precision
     auto now = std::chrono::system_clock::now();
     auto now_us = std::chrono::time_point_cast<std::chrono::microseconds>(now);
     auto epoch = now_us.time_since_epoch();
     auto us_count = std::chrono::duration_cast<std::chrono::microseconds>(epoch).count();
     
     // Format ID as timestamp-count-random
     oss << std::hex << std::setfill('0') << std::setw(12) << us_count << "-";
     oss << std::setw(8) << signalCount_.load() << "-";
     
     // Add some randomness
     std::random_device rd;
     std::mt19937 gen(rd());
     std::uniform_int_distribution<> distrib(0, 0xFFFF);
     oss << std::setw(4) << distrib(gen);
     
     return core::SignalId(oss.str());
 }
 
 // Notify all registered callbacks about a new signal
 void ImbalanceSignalGenerator::notifySignalCallbacks(const TradingSignal& signal) {
  
    common::EventBus::getInstance().publish(events::SignalGeneratedEvent{signal});
 }
 
 } // namespace strategy
 } // namespace trading