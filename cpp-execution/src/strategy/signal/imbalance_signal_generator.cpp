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
#include "strategy/types/constants.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <random>
 
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
    std::cout<<"ImbalanceSignalGenerator initialized with config: "
             <<"minSignalStrength="<<config_.minSignalStrength
             <<", confidenceThreshold="<<config_.confidenceThreshold
             <<", signalTtlMs="<<config_.signalTtlMs<<"\n";
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
     //add exchange and symbol to the debug print
     std::cout<<"Processing imbalance event: Type="<<imbalanceTypeToString(imbalance.type)
              <<", Direction="<<(imbalance.direction == core::Direction::BUY ? "BUY" : (imbalance.direction == core::Direction::SELL ? "SELL" : "NEUTRAL"))
              <<", Strength="<<imbalance.strength<<", Confidence="<<confidenceLevelToValue(imbalance.confidence)<<"\n";
     std::cout<<"Exchange="<<imbalance.exchangeId.toString()<<", Symbol="<<imbalance.symbol.toString()<<"\n";

     // Store the imbalance for signal generation
     pendingImbalances_.push_back(imbalance);
     
     // Create and emit signal immediately
     TradingSignal signal = createSignalFromImbalance(imbalance);
     generatedSignals_.push_back(signal);
     
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
 void ImbalanceSignalGenerator::setMomentumCorrelation(MomentumCorrelationPtr momentum_correlation) {
     std::lock_guard<std::mutex> lock(mutex_);
     momentumCorrelation_ = momentum_correlation;
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
 
 // Create a trading signal from an imbalance event
 TradingSignal ImbalanceSignalGenerator::createSignalFromImbalance(const ImbalanceEvent& imbalance) {
     TradingSignal signal;
     
     // Basic signal properties
     signal.id = generateSignalId();
    signal.sourceEventId = imbalance.eventId;  // ← Add this line
    signal.correlationId = imbalance.eventId;  // ← Add this line
     signal.direction = imbalance.direction;
     signal.strength = calculateSignalStrength(imbalance);
    //  signal.confidence = static_cast<double>(imbalance.confidence) / 100.0; // Convert from enum to 0-1 range
    signal.confidence = confidenceLevelToValue(imbalance.confidence); // Convert from enum to 0-1 range
     signal.generation_time = std::chrono::system_clock::now();
     signal.ttl = std::chrono::milliseconds(config_.signalTtlMs);
     signal.source_type = imbalance.type;
     signal.symbol = imbalance.symbol;
     signal.exchangeId = imbalance.exchangeId;
     
     // Generate price targets
     signal.targets = generatePriceTargets(imbalance);
     
     // Copy relevant metrics
     for (const auto& metric : imbalance.supportingMetrics) {
         signal.setMetric(metric.first, metric.second);
     }
    //  std::cout<<"Creating signal from imbalance: ID="<<signal.id<<", Direction="<< (signal.direction == core::Direction::BUY ? "BUY" : "SELL") 
    //           <<", Strength="<<signal.strength<<", Confidence="<<signal.confidence<<"\n";
     // Apply momentum correlation if available
     if (momentumCorrelation_) {
         double predictedMove = momentumCorrelation_->predictPriceMovement(imbalance);
         std::string imbalanceTypeStr = imbalanceTypeToString(imbalance.type);
         double correlation = momentumCorrelation_->getCorrelationCoefficient(imbalanceTypeStr);
         
         signal.setMetric("predicted_price_move", predictedMove);
         signal.setMetric("momentum_correlation", correlation);
         
         // Adjust confidence based on correlation
         if (correlation > 0.5) {
             signal.confidence = std::min(1.0, signal.confidence * (1.0 + (correlation - 0.5)));
         } else if (correlation < 0.3) {
             signal.confidence = signal.confidence * (correlation / 0.3);
         }
     }
     else{
         std::cout<<"No momentum correlation component set.\n";
     }
     
     // Apply threshold adjustments if available
     if (thresholdManager_) {
         double sensitivity = thresholdManager_->getGlobalSensitivity();
         signal.setMetric("threshold_sensitivity", sensitivity);
         
         // Adjust strength based on sensitivity
         if (sensitivity != 1.0) {
             signal.strength = std::min(1.0, signal.strength * sensitivity);
         }
     }
     std::cout <<"Generated signal: ID="<<signal.id<<", Direction="<< (signal.direction == core::Direction::BUY ? "BUY" : "SELL") 
              <<", Strength="<<signal.strength<<", Confidence="<<signal.confidence<<"\n";
     return signal;
 }
trading::core::ConfidenceLevel ImbalanceSignalGenerator::valueToConfidenceLevel(double value)const {
    if (value >= 0.9) {
        return trading::core::ConfidenceLevel::HIGH;
    } else if (value >= 0.7) {
        return trading::core::ConfidenceLevel::MEDIUM;
    } else {
        return trading::core::ConfidenceLevel::LOW;
    }
}
double ImbalanceSignalGenerator::confidenceLevelToValue(trading::core::ConfidenceLevel level) const {
    switch (level) {
        case trading::core::ConfidenceLevel::VERY_HIGH: return 1.0;
        case trading::core::ConfidenceLevel::HIGH: return 0.9;
        case trading::core::ConfidenceLevel::MEDIUM: return 0.7;
        case trading::core::ConfidenceLevel::LOW: return 0.4;
        case trading::core::ConfidenceLevel::VERY_LOW: return 0.1;
        default: return 0.5;
    }
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
     if (confidenceLevelToValue(imbalance.confidence) < config_.confidenceThreshold) {
         return false;
     }
     
     // Check if direction is neutral
     if (imbalance.direction == core::Direction::NEUTRAL) {
         return false;
     }
     
     // Additional filters can be added here
     
     return true;
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
     auto midPriceIt = imbalance.supportingMetrics.find("mid_price");
     if (midPriceIt == imbalance.supportingMetrics.end()) {
         return targets; // Can't generate targets without price
     }
     
     double midPrice = midPriceIt->second;
     
     // Get volatility if available
     double volatility = 0.001; // Default 0.1% volatility
     auto volatilityIt = imbalance.supportingMetrics.find("volatility");
     if (volatilityIt != imbalance.supportingMetrics.end()) {
         volatility = volatilityIt->second;
     }
     
     // Calculate expected price impact based on imbalance strength and type
     double basePriceImpact = volatility * 5.0; // Base impact is 5x volatility
     double strengthScaling = 0.5 + (imbalance.strength * 1.5); // Scale from 0.5x to 2.0x
     
     double priceImpact = basePriceImpact * strengthScaling;
     
     // Adjust for imbalance type
     switch (imbalance.type) {
         case ImbalanceType::VOLUME_IMBALANCE:
             priceImpact *= 1.0;
             break;
         case ImbalanceType::PRICE_LEVEL_IMBALANCE:
             priceImpact *= 0.8;
             break;
         case ImbalanceType::LIQUIDITY_REMOVAL:
             priceImpact *= 1.2;
             break;
         case ImbalanceType::ABNORMAL_SPREAD:
             priceImpact *= 0.7;
             break;
         default:
             break;
     }
     
     // Adjust direction
     if (imbalance.direction == core::Direction::SELL) {
         priceImpact = -priceImpact;
     }
     
     // Create three targets with different probabilities and time horizons
     
     // Short-term target (high probability)
     PriceTarget shortTarget;
     shortTarget.price = midPrice * (1.0 + (priceImpact * 0.3));
     shortTarget.probability = 0.7;
     shortTarget.expected_time = std::chrono::milliseconds(5000); // 5 seconds
     targets.push_back(shortTarget);
     
     // Medium-term target (medium probability)
     PriceTarget mediumTarget;
     mediumTarget.price = midPrice * (1.0 + (priceImpact * 0.7));
     mediumTarget.probability = 0.5;
     mediumTarget.expected_time = std::chrono::milliseconds(15000); // 15 seconds
     targets.push_back(mediumTarget);
     
     // Long-term target (low probability)
     PriceTarget longTarget;
     longTarget.price = midPrice * (1.0 + priceImpact);
     longTarget.probability = 0.3;
     longTarget.expected_time = std::chrono::milliseconds(30000); // 30 seconds
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
     // Copy callbacks to prevent deadlocks if callback registers/unregisters another callback
    //  std::map<size_t, SignalCallback> callbacksCopy;
    //  {
    //      std::lock_guard<std::mutex> lock(mutex_);
    //      callbacksCopy = callbacks_;
    //  }
     
    //  // Notify all callbacks
    //  for (const auto& cb : callbacksCopy) {
    //      try {
    //          cb.second(signal);
    //      } catch (const std::exception& e) {
    //          std::cerr << "Exception in signal callback: " << e.what() << std::endl;
    //      }
    //  }
    // std::cout << "Publishing SignalGeneratedEvent for signal ID: " << signal.id << std::endl;
    common::EventBus::getInstance().publish(events::SignalGeneratedEvent{signal});
 }
 
 } // namespace strategy
 } // namespace trading