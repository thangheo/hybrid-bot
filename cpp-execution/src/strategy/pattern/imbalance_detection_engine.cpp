/**
 * @file imbalance_detection_engine.cpp
 * @brief Implementation of the imbalance detection engine
 * 
 * @details
 * Implements algorithms for detecting significant order book imbalances
 * that may predict short-term price movements.
 * 
 * Created on: May 5, 2025
 */

 #include "strategy/pattern/imbalance_detection_engine.hpp"
 #include <common/event_bus.hpp>
 #include <common/trading_events.hpp>
 #include <algorithm>
 #include <cmath>
 #include <iostream>
 #include <chrono>
 #include <sstream>
#include <logger.hpp>
 namespace trading {
 namespace strategy {
 
 // Constructor
 ImbalanceDetectionEngine::ImbalanceDetectionEngine() : nextCallbackId_(0) {
     // Initialize imbalance state map for all imbalance types
     imbalanceState_[ImbalanceType::VOLUME_IMBALANCE] = false;
     imbalanceState_[ImbalanceType::PRICE_LEVEL_IMBALANCE] = false;
     imbalanceState_[ImbalanceType::LIQUIDITY_FLOW_IMBALANCE] = false;
     imbalanceState_[ImbalanceType::ABNORMAL_SPREAD] = false;
 }
 ImbalanceDetectionEngine::ImbalanceDetectionEngine(const ImbalanceDetectionConfig& config){
    
     config_ = config;
 }

 // Initialize the detection engine with configuration
 bool ImbalanceDetectionEngine::initialize(const ImbalanceDetectionConfig& config) {
     std::lock_guard<std::mutex> lock(mutex_);
     config_ = config;
     
     // Initialize thresholds with defaults if not specified
     if (thresholds_.empty()) {
         // Set default thresholds if none provided
         thresholds_.setThreshold("volume_imbalance", 2.0); // Volume imbalance threshold
         thresholds_.setThreshold("liquidity_removal", 1.5); // Liquidity removal threshold
         thresholds_.setThreshold("price_level_imbalance", 1.5); // Price level imbalance threshold
         thresholds_.setThreshold("abnormal_spread", 2.5); // Abnormal spread threshold
        thresholds_.setThreshold("liquidity_flow_imbalance", 3.0); // Liquidity flow imbalance threshold
     }
     
     // Clear any existing state
     clearImbalances();
     
     return true;
 }
 
 // Analyze order book metrics to detect imbalances
 bool ImbalanceDetectionEngine::analyzeMetrics(const OrderBookMetrics& metrics) {
     std::lock_guard<std::mutex> lock(mutex_);
     // Store metrics history for time-series analysis
     for (const auto& metric : metrics.metrics) {
         metricHistory_[metric.first] = metric.second;
     }
     // Apply different detection algorithms
     bool volumeImbalance = detectVolumeImbalance(metrics);
     bool priceLevelImbalance = detectPriceLevelImbalance(metrics);
     bool liquidityFlowImbalance = detectLiquidityFlowImbalance(metrics);
     bool abnormalSpread = detectAbnormalSpread(metrics);
     //data looks correct
     // Return true if any imbalance was detected
     return volumeImbalance || priceLevelImbalance || liquidityFlowImbalance || abnormalSpread;
 }
 
 // Get all detected imbalance events
 std::vector<ImbalanceEvent> ImbalanceDetectionEngine::getDetectedImbalances() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return detectedImbalances_;
 }
 
 // Check if a specific type of imbalance has been detected
 bool ImbalanceDetectionEngine::hasSignificantImbalance(ImbalanceType type) const {
     std::lock_guard<std::mutex> lock(mutex_);
     auto it = imbalanceState_.find(type);
     return (it != imbalanceState_.end()) ? it->second : false;
 }
 
 // Set detection thresholds
 void ImbalanceDetectionEngine::setThresholds(const ThresholdSet& thresholds) {
     std::lock_guard<std::mutex> lock(mutex_);
     thresholds_ = thresholds;
 }
 
 // Get current detection thresholds
 ThresholdSet ImbalanceDetectionEngine::getThresholds() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return thresholds_;
 }
 
 // Register callback for imbalance detection events
 size_t ImbalanceDetectionEngine::registerImbalanceCallback(const ImbalanceCallback& callback) {
     std::lock_guard<std::mutex> lock(mutex_);
     size_t id = nextCallbackId_++;
     callbacks_[id] = callback;
     return id;
 }
 
 // Unregister a previously registered callback
 bool ImbalanceDetectionEngine::unregisterImbalanceCallback(size_t callbackId) {
     std::lock_guard<std::mutex> lock(mutex_);
     auto it = callbacks_.find(callbackId);
     if (it != callbacks_.end()) {
         callbacks_.erase(it);
         return true;
     }
     return false;
 }
 
 // Clear all detected imbalances
 void ImbalanceDetectionEngine::clearImbalances() {
     detectedImbalances_.clear();
     
     // Reset imbalance state for all types
     for (auto& pair : imbalanceState_) {
         pair.second = false;
     }
 }
 
 // Get the engine status
 std::string ImbalanceDetectionEngine::getStatus() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Build a simple status string
     std::string status = "ImbalanceDetectionEngine Status:\n";
     status += "  Active imbalances: " + std::to_string(detectedImbalances_.size()) + "\n";
     
     // Add status for each imbalance type
     for (const auto& pair : imbalanceState_) {
         std::string typeName;
         switch (pair.first) {
             case ImbalanceType::VOLUME_IMBALANCE:
                 typeName = "VOLUME_IMBALANCE";
                 break;
             case ImbalanceType::PRICE_LEVEL_IMBALANCE:
                 typeName = "PRICE_LEVEL_IMBALANCE";
                 break;
             case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE:
                 typeName = "LIQUIDITY_FLOW_IMBALANCE";
                 break;
             case ImbalanceType::ABNORMAL_SPREAD:
                 typeName = "ABNORMAL_SPREAD";
                 break;
             default:
                 typeName = "UNKNOWN";
                 break;
         }
         status += "  " + typeName + ": " + (pair.second ? "ACTIVE" : "INACTIVE") + "\n";
     }
     
     return status;
 }
   
 void ImbalanceDetectionEngine::updateParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Define default values for each threshold
    const double DEFAULT_VOLUME_IMBALANCE = 2.0;
    const double DEFAULT_PRICE_LEVEL_IMBALANCE = 1.5;
    const double DEFAULT_LIQUIDITY_FLOW_IMBALANCE = 3.0;
    const double DEFAULT_ABNORMAL_SPREAD = 2.5;
    
    // Update volume imbalance threshold
    if (params.numeric_params.find("volume_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("volume_imbalance_threshold", DEFAULT_VOLUME_IMBALANCE);
        thresholds_.setThreshold("volume_imbalance", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::VOLUME_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::VOLUME_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::VOLUME_IMBALANCE, imbalanceThreshold);
        }
    }
    
    // Update price level imbalance threshold
    if (params.numeric_params.find("price_level_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("price_level_imbalance_threshold", DEFAULT_PRICE_LEVEL_IMBALANCE);
        thresholds_.setThreshold("price_level_imbalance", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::PRICE_LEVEL_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::PRICE_LEVEL_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::PRICE_LEVEL_IMBALANCE, imbalanceThreshold);
        }
    }
    
    // Update liquidity flow imbalance threshold
    if (params.numeric_params.find("liquidity_flow_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("liquidity_flow_imbalance_threshold", DEFAULT_LIQUIDITY_FLOW_IMBALANCE);
        thresholds_.setThreshold("liquidity_flow_imbalance", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE, imbalanceThreshold);
        }
    }
    
    // Update abnormal spread threshold
    if (params.numeric_params.find("abnormal_spread_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("abnormal_spread_threshold", DEFAULT_ABNORMAL_SPREAD);
        thresholds_.setThreshold("abnormal_spread", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::ABNORMAL_SPREAD) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::ABNORMAL_SPREAD);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::ABNORMAL_SPREAD, imbalanceThreshold);
        }
    }
    
    // Update global sensitivity if provided
    if (params.numeric_params.find("global_sensitivity") != params.numeric_params.end()) {
        thresholds_.global_sensitivity = params.getDouble("global_sensitivity", 1.0);
    }
}
 // PRIVATE METHODS


 //strength normalizer helper
    double ImbalanceDetectionEngine::calculateImbalanceConfidence(ImbalanceType type, double strength, const OrderBookMetrics& metrics) {
        // Basic normalization based on type-specific thresholds
        ImbalanceThreshold threshold = thresholds_.getTypeThreshold(type);
        double normalizedStrength = strength;
        
        if (threshold.max_threshold > threshold.min_threshold) {
            normalizedStrength = (strength - threshold.min_threshold) / (threshold.max_threshold - threshold.min_threshold);
            normalizedStrength = std::clamp(normalizedStrength, 0.0, 1.0);
        } else {
            normalizedStrength = std::clamp(strength, 0.0, 1.0);
        }
        
        // Further adjust confidence based on market conditions
        // For example, higher confidence during high liquidity or low volatility
        double liquidityFactor = (metrics.total_bid_volume + metrics.total_ask_volume) / 2.0;
        double volatilityFactor = metrics.volatility_expectation; // Assuming this is a value between 0 and 1
        
        // Simple heuristic: increase confidence with liquidity, decrease with volatility
        double liquidityWeight = 0.5; // Weight for liquidity impact
        double volatilityWeight = 0.5; // Weight for volatility impact
        
        double liquidityAdjustment = (liquidityFactor > 1000.0) ? liquidityWeight : (liquidityFactor / 1000.0) * liquidityWeight;
        double volatilityAdjustment = (volatilityFactor < 0.5) ? volatilityWeight : (1.0 - volatilityFactor) * volatilityWeight;
        
        double adjustedConfidence = normalizedStrength + liquidityAdjustment - volatilityAdjustment;
        adjustedConfidence = std::clamp(adjustedConfidence, 0.0, 1.0);
        
        return adjustedConfidence;
    }
 // Detect volume imbalance
 bool ImbalanceDetectionEngine::detectVolumeImbalance(const OrderBookMetrics& metrics) {
     // Check if required metrics are available
     auto bidVolIt = metrics.metrics.find("bid_volume");
     auto askVolIt = metrics.metrics.find("ask_volume");
     
     if (bidVolIt == metrics.metrics.end() || askVolIt == metrics.metrics.end()) {
         return false; // Required metrics not available
     }
     
     double bidVolume = bidVolIt->second;
     double askVolume = askVolIt->second;
     //print bid and ask volume
     // Avoid division by zero
     if (askVolume < 1e-10) {
         askVolume = 1e-10;
     }
     
     // Calculate bid/ask volume ratio
     double volumeRatio = bidVolume / askVolume;
     
     // Get threshold for volume imbalance
     double threshold = thresholds_.getTypeThreshold(ImbalanceType::VOLUME_IMBALANCE).activation_threshold;
     
     // Detect imbalance if ratio exceeds threshold or is below its inverse
     bool isImbalance = (volumeRatio >= threshold || volumeRatio <= 1.0 / threshold);
     
     // Update imbalance state
     imbalanceState_[ImbalanceType::VOLUME_IMBALANCE] = isImbalance;
     
     // If imbalance detected, create an event
     if (isImbalance) {
            double strength;                       
            //simplified strength calculation
            if (volumeRatio > threshold) {
                strength = (volumeRatio - threshold) / threshold; // Normalize above threshold
            } else if (volumeRatio < 1.0 / threshold) {
                strength = (1.0/threshold - volumeRatio) / (1.0/threshold); // Normalize below inverse threshold
            } else {
                strength = 0.0; // Should not happen as isImbalance is true
            }
            strength = std::min(1.0, strength); // Cap at 1.0
            // Ensure strength is never negative or invalid
            if (strength < 0.0 || !std::isfinite(strength)) {
                strength = 0.0;
            }
         Direction direction = (volumeRatio > 1.0) ? Direction::BUY : Direction::SELL;
         createImbalanceEvent(ImbalanceType::VOLUME_IMBALANCE, strength, direction, metrics);
        //  std::cout<<"[TradingApp]ImbalanceDetectionEngine::detectVolumeImbalance:strength="<<strength<<", volumeRatio="<<volumeRatio<<", threshold="<<threshold<<"\n";

     }
     
     return isImbalance;
 }
 
 // Detect price level imbalance
 bool ImbalanceDetectionEngine::detectPriceLevelImbalance(const OrderBookMetrics& metrics) {
     // Check if required metrics are available
     auto bidLevelsIt = metrics.metrics.find("bid_level_count");
     auto askLevelsIt = metrics.metrics.find("ask_level_count");
     
     if (bidLevelsIt == metrics.metrics.end() || askLevelsIt == metrics.metrics.end()) {
         return false; // Required metrics not available
     }
     
     double bidLevels = bidLevelsIt->second;
     double askLevels = askLevelsIt->second;
     
     // Avoid division by zero
     if (askLevels < 1e-10) {
         askLevels = 1e-10;
     }
     
     // Calculate price level ratio
     double levelRatio = bidLevels / askLevels;
     
     // Get threshold for price level imbalance
     double threshold = thresholds_.getTypeThreshold(ImbalanceType::PRICE_LEVEL_IMBALANCE).activation_threshold;
     
     // Detect imbalance if ratio exceeds threshold or is below its inverse
    //  bool isImbalance = (levelRatio >= threshold || levelRatio <= 1.0 / threshold);
     // Detect imbalance if ratio exceeds threshold or is below its inverse
    bool isImbalance = (levelRatio > threshold || levelRatio < 1.0 / threshold);
     // Update imbalance state
     imbalanceState_[ImbalanceType::PRICE_LEVEL_IMBALANCE] = isImbalance;
     
     // If imbalance detected, create an event
     if (isImbalance) {
        //  double strength = std::abs(levelRatio - 1.0);
        double strength;
        if (levelRatio > threshold) {
            strength = (levelRatio - threshold) / threshold; // Normalize above threshold
        } else if (levelRatio < 1.0 / threshold) {
            strength = (1.0/threshold - levelRatio) / (1.0/threshold); // Normalize below inverse threshold
        } else {
            strength = 0.0; // Should not happen as isImbalance is true
        }
        strength = std::min(1.0, strength); // Cap at 1.0
        
        // Ensure strength is never negative or invalid
        if (strength < 0.0 || !std::isfinite(strength)) {
            strength = 0.0;
        }
         Direction direction = (levelRatio > 1.0) ? Direction::BUY : Direction::SELL;
         
        //  std::cout<<"[TradingApp] ImbalanceDetectionEngine::detectPriceLevelImbalance: strength="<<strength<<", levelRatio="<<levelRatio<<", threshold="<<threshold<<"\n";
         createImbalanceEvent(ImbalanceType::PRICE_LEVEL_IMBALANCE, strength, direction, metrics);
     }
     
     return isImbalance;
 }
 
bool ImbalanceDetectionEngine::detectLiquidityFlowImbalance(const OrderBookMetrics& metrics) {
    // Check if required metrics are available
    auto bidFlowIt = metrics.metrics.find("bid_flow");
    auto askFlowIt = metrics.metrics.find("ask_flow");
    if (bidFlowIt == metrics.metrics.end() || askFlowIt == metrics.metrics.end()) {
        return false; // Required metrics not available
    }

    double bidFlow = bidFlowIt->second;
    double askFlow = askFlowIt->second;

    // Calculate net and total flow
    double netFlow   = bidFlow - askFlow;
    double totalFlow = std::abs(bidFlow) + std::abs(askFlow);

    // If no meaningful flow, no imbalance
    if (totalFlow < 1e-10) {
        imbalanceState_[ImbalanceType::LIQUIDITY_FLOW_IMBALANCE] = false;
        return false;
    }

    // Flow imbalance ratio: 0 = balanced, 1 = one-sided
    double flowImbalance = std::abs(netFlow) / totalFlow;

    // Threshold should be configured directly in [0,1]
    double threshold = thresholds_
        .getTypeThreshold(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE)
        .activation_threshold;
    threshold = std::clamp(threshold, 0.0, 1.0);

    bool isImbalance = (flowImbalance >= threshold);

    // Update imbalance state
    imbalanceState_[ImbalanceType::LIQUIDITY_FLOW_IMBALANCE] = isImbalance;

    if (isImbalance) {
        double strength = flowImbalance;
        // Normalize strength to [0,1]
        strength = std::min(1.0, strength / threshold);
        // Ensure strength is valid
        if (strength < 0.0 || !std::isfinite(strength)) {
            strength = 0.0;
        }
        strength = std::min(1.0, strength); // Cap at 1.0
        Direction direction = (netFlow > 0) ? Direction::BUY : Direction::SELL;

        std::cout << "[TradingApp] detectLiquidityFlowImbalance: "
                  << "flowImbalance=" << flowImbalance
                  << ", threshold=" << threshold << "\n";

        createImbalanceEvent(
            ImbalanceType::LIQUIDITY_FLOW_IMBALANCE,
            strength,
            direction,
            metrics
        );
    }

    return isImbalance;
}
 
 // Detect abnormal spread
 bool ImbalanceDetectionEngine::detectAbnormalSpread(const OrderBookMetrics& metrics) {
     // Check if required metrics are available
     auto spreadIt = metrics.metrics.find("spread_percent");
     auto avgSpreadIt = metrics.metrics.find("average_spread_percent");
     
     if (spreadIt == metrics.metrics.end() || avgSpreadIt == metrics.metrics.end()) {
         return false; // Required metrics not available
     }
     
     double currentSpread = spreadIt->second;
     double averageSpread = avgSpreadIt->second;
     
     // Avoid division by zero
     if (averageSpread < 1e-10) {
         averageSpread = 1e-10;
     }
     
     // Calculate spread ratio
     double spreadRatio = currentSpread / averageSpread;
     
     // Get threshold for abnormal spread
     double threshold = thresholds_.getTypeThreshold(ImbalanceType::ABNORMAL_SPREAD).activation_threshold;
     
     // Detect imbalance if spread ratio exceeds threshold
     bool isImbalance = (spreadRatio >= threshold);
     
     // Update imbalance state
     imbalanceState_[ImbalanceType::ABNORMAL_SPREAD] = isImbalance;
     
     // If imbalance detected, create an event
     if (isImbalance) {
         double strength = spreadRatio - 1.0;
         //normalize strength to [0,1]
         strength = std::min(1.0, strength / (threshold - 1.0));
         // Ensure strength is never negative or invalid
         if (strength < 0.0 || !std::isfinite(strength)) {
             strength = 0.0;
         }
         strength = std::min(1.0, strength); // Cap at 1.0

         // Widening spread often indicates uncertainty or selling pressure
         // This is a simplified heuristic - in real systems, you'd use additional data
         Direction direction = Direction::SELL;
         
         // Check if there's additional data to determine direction more accurately
         auto bidVolumeIt = metrics.metrics.find("bid_volume");
         auto askVolumeIt = metrics.metrics.find("ask_volume");
         
         if (bidVolumeIt != metrics.metrics.end() && askVolumeIt != metrics.metrics.end()) {
             double bidVolume = bidVolumeIt->second;
             double askVolume = askVolumeIt->second;
             
             // If bid volume is significantly higher, it might indicate buying pressure despite the wide spread
             if (bidVolume > askVolume * 1.5) {
                 direction = Direction::BUY;
             }
         }
        //  std::cout<<"[TradingApp] ImbalanceDetectionEngine::detectAbnormalSpread: spreadRatio="<<spreadRatio<<", threshold="<<threshold<<"\n";
         createImbalanceEvent(ImbalanceType::ABNORMAL_SPREAD, strength, direction, metrics);
     }
     
     return isImbalance;
 }
 
 // Calculate imbalance strength based on type and metrics
 double ImbalanceDetectionEngine::calculateImbalanceStrength(ImbalanceType type, const OrderBookMetrics& metrics) {
     double strength = 0.0;
     
     switch (type) {
         case ImbalanceType::VOLUME_IMBALANCE: {
             auto bidVolIt = metrics.metrics.find("bid_volume");
             auto askVolIt = metrics.metrics.find("ask_volume");
             
             if (bidVolIt != metrics.metrics.end() && askVolIt != metrics.metrics.end()) {
                 double bidVol = bidVolIt->second;
                 double askVol = askVolIt->second;
                 
                 if (askVol < 1e-10) askVol = 1e-10;
                 double ratio = bidVol / askVol;
                 
                 strength = std::abs(ratio - 1.0);
             }
             break;
         }
         case ImbalanceType::PRICE_LEVEL_IMBALANCE: {
             auto bidLevelsIt = metrics.metrics.find("bid_level_count");
             auto askLevelsIt = metrics.metrics.find("ask_level_count");
             
             if (bidLevelsIt != metrics.metrics.end() && askLevelsIt != metrics.metrics.end()) {
                 double bidLevels = bidLevelsIt->second;
                 double askLevels = askLevelsIt->second;
                 
                 if (askLevels < 1e-10) askLevels = 1e-10;
                 double ratio = bidLevels / askLevels;
                 
                 strength = std::abs(ratio - 1.0);
             }
             break;
         }
         case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE: {
             auto bidFlowIt = metrics.metrics.find("bid_flow");
             auto askFlowIt = metrics.metrics.find("ask_flow");
             
             if (bidFlowIt != metrics.metrics.end() && askFlowIt != metrics.metrics.end()) {
                 double bidFlow = bidFlowIt->second;
                 double askFlow = askFlowIt->second;
                 
                 double netFlow = bidFlow - askFlow;
                 double totalFlow = std::abs(bidFlow) + std::abs(askFlow);
                 
                 if (totalFlow < 1e-10) totalFlow = 1e-10;
                 strength = std::abs(netFlow) / totalFlow;
             }
             break;
         }
         case ImbalanceType::ABNORMAL_SPREAD: {
             auto spreadIt = metrics.metrics.find("spread_percent");
             auto avgSpreadIt = metrics.metrics.find("average_spread_percent");
             
             if (spreadIt != metrics.metrics.end() && avgSpreadIt != metrics.metrics.end()) {
                 double currentSpread = spreadIt->second;
                 double averageSpread = avgSpreadIt->second;
                 
                 if (averageSpread < 1e-10) averageSpread = 1e-10;
                 strength = (currentSpread / averageSpread) - 1.0;
                 
                 // Clamp negative values to zero
                 if (strength < 0) strength = 0;
             }
             break;
         }
         default:
             break;
     }
     
     return strength;
 }
 
 // Determine imbalance direction based on type and metrics
 Direction ImbalanceDetectionEngine::determineImbalanceDirection(ImbalanceType type, const OrderBookMetrics& metrics) {
     Direction direction = Direction::NEUTRAL;
     
     switch (type) {
         case ImbalanceType::VOLUME_IMBALANCE: {
             auto bidVolIt = metrics.metrics.find("bid_volume");
             auto askVolIt = metrics.metrics.find("ask_volume");
             
             if (bidVolIt != metrics.metrics.end() && askVolIt != metrics.metrics.end()) {
                 double bidVol = bidVolIt->second;
                 double askVol = askVolIt->second;
                 
                 direction = (bidVol > askVol) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::PRICE_LEVEL_IMBALANCE: {
             auto bidLevelsIt = metrics.metrics.find("bid_level_count");
             auto askLevelsIt = metrics.metrics.find("ask_level_count");
             
             if (bidLevelsIt != metrics.metrics.end() && askLevelsIt != metrics.metrics.end()) {
                 double bidLevels = bidLevelsIt->second;
                 double askLevels = askLevelsIt->second;
                 
                 direction = (bidLevels > askLevels) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE: {
             auto bidFlowIt = metrics.metrics.find("bid_flow");
             auto askFlowIt = metrics.metrics.find("ask_flow");
             
             if (bidFlowIt != metrics.metrics.end() && askFlowIt != metrics.metrics.end()) {
                 double bidFlow = bidFlowIt->second;
                 double askFlow = askFlowIt->second;
                 
                 direction = (bidFlow > askFlow) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::ABNORMAL_SPREAD: {
             // For abnormal spread, direction determination is more complex
             // Default to SELL as widening spread often indicates selling pressure
             direction = Direction::SELL;
             
             // Check if there's additional data to determine direction more accurately
             auto bidVolumeIt = metrics.metrics.find("bid_volume");
             auto askVolumeIt = metrics.metrics.find("ask_volume");
             
             if (bidVolumeIt != metrics.metrics.end() && askVolumeIt != metrics.metrics.end()) {
                 double bidVolume = bidVolumeIt->second;
                 double askVolume = askVolumeIt->second;
                 
                 // If bid volume is significantly higher, it might indicate buying pressure despite the wide spread
                 if (bidVolume > askVolume * 1.5) {
                     direction = Direction::BUY;
                 }
             }
             break;
         }
         default:
             break;
     }
     
     return direction;
 }
//  valueToConfidenceLevel
    trading::core::ConfidenceLevel ImbalanceDetectionEngine::valueToConfidenceLevel(double value) {
        //  enum class ConfidenceLevel {
        //     VERY_LOW,
        //     LOW,
        //     MEDIUM,
        //     HIGH,
        //     VERY_HIGH
        // };
        if (value < 0.2) {
            return trading::core::ConfidenceLevel::VERY_LOW;
        } else if (value < 0.4) {
            return trading::core::ConfidenceLevel::LOW;
        } else if (value < 0.6) {
            return trading::core::ConfidenceLevel::MEDIUM;
        } else if (value < 0.8) {
            return trading::core::ConfidenceLevel::HIGH;
        } else {
            return trading::core::ConfidenceLevel::VERY_HIGH;
        }
    }

 void ImbalanceDetectionEngine::createImbalanceEvent(ImbalanceType type, double strength, Direction direction, const OrderBookMetrics& metrics) {
    // Validate inputs
    if (strength < 0.0 || strength > 1.0) {
        LOG_WARN("Invalid strength value: {}", strength);
        return;
    }
    
    if (direction == Direction::NEUTRAL) {
        LOG_WARN("Cannot create imbalance event with NEUTRAL direction");
        return;
    }
    
    ImbalanceEvent event;
    event.type = type;
    event.strength = std::clamp(strength, 0.0, 1.0);
    event.direction = direction;
    event.detectionTime = std::chrono::system_clock::now();
    event.exchangeId = metrics.exchangeId;
    event.symbol = metrics.symbol;
    event.confidence = valueToConfidenceLevel(calculateImbalanceConfidence(type, strength, metrics)); 
    event.eventId = generateEventId();
    
    // Copy relevant metrics
    for (const auto& metric : metrics.metrics) {
        event.supportingMetrics[metric.first] = metric.second;
    }
    
    // Choose ONE method: either local storage OR event bus, not both
    // Option 1: Local storage only (current caller pattern)
    // detectedImbalances_.push_back(event);
    
    // Option 2: Event bus only (better for decoupling)
    common::EventBus::getInstance().publish(events::ImbalanceDetectedEvent{event});
}
 void ImbalanceDetectionEngine::notifyImbalanceCallbacks(const ImbalanceEvent& event) {
    // Simply publish to event bus - no locks, no deadlocks
    common::EventBus::getInstance().publish(events::ImbalanceDetectedEvent{event});
}
 // Generate a unique event ID
 uint64_t ImbalanceDetectionEngine::generateEventId() {
     static uint64_t nextId = 0;
     return ++nextId;
 }
//  double ImbalanceDetectionEngine::calculateImbalanceConfidence(ImbalanceType type, double strength, const OrderBookMetrics& metrics) {
//     // Base confidence directly on normalized strength (0-1)
//     double baseConfidence = 0.5 + (strength * 0.5);  // Maps 0-1 strength to 0.5-1.0 confidence
    
//     // Adjust confidence based on market conditions if available
//     auto volatilityIt = metrics.metrics.find("volatility");
//     if (volatilityIt != metrics.metrics.end()) {
//         double volatility = volatilityIt->second;
        
//         // High volatility reduces confidence (but less aggressively)
//         if (volatility > 0.01) {  // 1% volatility threshold
//             double volatilityPenalty = std::min(0.3, volatility * 2.0);  // Cap penalty at 30%
//             baseConfidence *= (1.0 - volatilityPenalty);
            
//             // Ensure confidence stays in valid range
//             baseConfidence = std::max(0.5, baseConfidence);
//         }
//     }
    
//     // Round to 2 decimal places
//     return std::round(baseConfidence * 100.0) / 100.0;
// }
 
 } // namespace strategy
 } // namespace trading