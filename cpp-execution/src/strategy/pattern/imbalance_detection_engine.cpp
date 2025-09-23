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

    //  VOLUME_IMBALANCE,              // Significant difference between bid and ask volume
    //  PRICE_LEVEL_IMBALANCE,         // Imbalance across multiple price levels
    //  LIQUIDITY_REMOVAL,             // Sudden removal of liquidity from one side
    //  ORDER_FLOW_IMBALANCE,          // Directional bias in order submissions
    //  LARGE_ORDER_APPEARANCE,        // Large orders appearing in the book
    //  LARGE_ORDER_DISAPPEARANCE,     // Large orders being removed from the book
    //  PRICE_LEVEL_CONCENTRATION,     // Concentration of volume at specific price levels
    //  BID_ASK_SPREAD_CHANGE,         // Significant change in the bid-ask spread
    //  MARKET_DEPTH_CHANGE,           // Significant change in market depth
    //  LIQUIDITY_FLOW_IMBALANCE,      // Imbalance in liquidity flow
    //  ABNORMAL_SPREAD,               // Abnormal spread between bid and ask prices
    //  LIQUIDITY_IMBALANCE,           // General liquidity imbalance
    //  TREND_CONTINUATION,            // Continuation of a price trend
    //  TREND_REVERSAL,                // Reversal of a price trend
     // Initialize imbalance state map for all imbalance types
    imbalanceState_[ImbalanceType::VOLUME_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::PRICE_LEVEL_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::LIQUIDITY_FLOW_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::ABNORMAL_SPREAD] = false;
        // Add other imbalance types as needed
    imbalanceState_[ImbalanceType::LIQUIDITY_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::LIQUIDITY_REMOVAL] = false;
    imbalanceState_[ImbalanceType::ORDER_FLOW_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::LARGE_ORDER_APPEARANCE] = false;
    imbalanceState_[ImbalanceType::LARGE_ORDER_DISAPPEARANCE] = false;
    imbalanceState_[ImbalanceType::PRICE_LEVEL_CONCENTRATION] = false;
    imbalanceState_[ImbalanceType::BID_ASK_SPREAD_CHANGE] = false;
    imbalanceState_[ImbalanceType::MARKET_DEPTH_CHANGE] = false;
    imbalanceState_[ImbalanceType::TREND_CONTINUATION] = false;
    imbalanceState_[ImbalanceType::TREND_REVERSAL] = false;
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
         thresholds_.setThreshold("volumeImbalance ", 2.0); // Volume imbalance threshold
         thresholds_.setThreshold("emaVolumeImbalance", 1.5); // EMA volume imbalance threshold
         thresholds_.setThreshold("priceLevelImbalance", 1.5); // Price level imbalance threshold
         thresholds_.setThreshold("liquidity_flow_imbalance", 3.0); // Liquidity flow imbalance threshold
         thresholds_.setThreshold("liquidity_removal", 1.5); // Liquidity removal threshold
         thresholds_.setThreshold("priceLevelImbalance", 1.5); // Price level imbalance threshold
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
    // Initialize lastMetrics_ on first call and return early - detectors need a previous snapshot
    if (!lastMetrics_) {
        lastMetrics_ = metrics;
        return false;
    }
    //chekc if metrics is valid
    if (metrics.bidVolumesByLevel.empty() || metrics.askVolumesByLevel.empty()) {
        // std::cerr << "Invalid metrics: empty bid or ask volumes." << std::endl;
        return false;
    }
     // Apply different detection algorithms
     bool volumeImbalance = detectVolumeImbalance(metrics);
    //  std::cout << "[analyzeMetrics] volumeImbalance =" << volumeImbalance << std::endl;
     bool priceLevelImbalance = detectPriceLevelImbalance(metrics);
    //  std::cout << "[analyzeMetrics] priceLevelImbalance =" << priceLevelImbalance << std::endl;
     bool liquidityFlowImbalance = detectLiquidityFlowImbalance(metrics);
    //  std::cout << "[analyzeMetrics] liquidityFlowImbalance =" << liquidityFlowImbalance << std::endl;
     bool abnormalSpread = detectAbnormalSpread(metrics);
    //  std::cout << "[analyzeMetrics] abnormalSpread =" << abnormalSpread << std::endl;
     bool marketDepthChange = detectMarketDepthChange(metrics);
    //  std::cout << "[analyzeMetrics] marketDepthChange =" << marketDepthChange << std::endl;
     //data looks correct
    // Update last snapshot after all detectors ran (keeps detectors comparing to the same previous snapshot)
    lastMetrics_ = metrics;

    // Return true if any imbalance was detected
    return volumeImbalance || priceLevelImbalance || liquidityFlowImbalance || abnormalSpread || marketDepthChange;
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
            case ImbalanceType::LIQUIDITY_IMBALANCE:
                 typeName = "LIQUIDITY_IMBALANCE";
                 break;
            case ImbalanceType::LIQUIDITY_REMOVAL:
                 typeName = "LIQUIDITY_REMOVAL";
                 break;
            case ImbalanceType::ORDER_FLOW_IMBALANCE:
                 typeName = "ORDER_FLOW_IMBALANCE";
                 break;
            case ImbalanceType::LARGE_ORDER_APPEARANCE:
                 typeName = "LARGE_ORDER_APPEARANCE";
                 break;
            case ImbalanceType::LARGE_ORDER_DISAPPEARANCE:
                 typeName = "LARGE_ORDER_DISAPPEARANCE";
                 break;
            case ImbalanceType::PRICE_LEVEL_CONCENTRATION:
                 typeName = "PRICE_LEVEL_CONCENTRATION";
                 break;
            case ImbalanceType::BID_ASK_SPREAD_CHANGE:
                 typeName = "BID_ASK_SPREAD_CHANGE";
                 break;
            case ImbalanceType::MARKET_DEPTH_CHANGE:
                 typeName = "MARKET_DEPTH_CHANGE";
                 break;
            case ImbalanceType::TREND_CONTINUATION:
                 typeName = "TREND_CONTINUATION";
                 break;
            case ImbalanceType::TREND_REVERSAL:
                 typeName = "TREND_REVERSAL";
                 break;
             case ImbalanceType::CUSTOM:
                 typeName = "CUSTOM";
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
        thresholds_.setThreshold("volumeImbalance ", threshold);
        
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
        thresholds_.setThreshold("priceLevelImbalance", threshold);
        
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

double ImbalanceDetectionEngine::calculateImbalanceStrength(double volumeRatio, double threshold) {
    double strength;
    if (volumeRatio > threshold) {
        strength = (volumeRatio - threshold) / threshold;
    } else if (volumeRatio < 1.0 / threshold) {
        strength = (1.0/threshold - volumeRatio) / (1.0/threshold);
    } else {
        strength = 0.0;
    }
    
    strength = std::min(1.0, std::max(0.0, strength)); // Clamp [0,1]
    return strength;
}


double ImbalanceDetectionEngine::calculateImbalanceConfidence(ImbalanceType type, double strength, const OrderBookMetrics& metrics) {
    // Strength is already normalized (0-1), so use it directly
    double baseConfidence = std::clamp(strength, 0.0, 1.0);
    
    // Adjust confidence based on market conditions
    double liquidityFactor = (metrics.totalBidVolume  + metrics.totalAskVolume) / 2.0;
    double volatilityFactor = metrics.volatilityExpectation;
    
    // Simple heuristic adjustments
    double liquidityWeight = 0.1; // Reduced impact
    double volatilityWeight = 0.1; // Reduced impact
    
    double liquidityAdjustment = (liquidityFactor > 1000.0) ? liquidityWeight : (liquidityFactor / 1000.0) * liquidityWeight;
    double volatilityAdjustment = (volatilityFactor < 0.5) ? volatilityWeight : (1.0 - volatilityFactor) * volatilityWeight;
    
    double adjustedConfidence = baseConfidence + liquidityAdjustment - volatilityAdjustment;
    return std::clamp(adjustedConfidence, 0.0, 1.0);
}
//  // Detect volume imbalance
//  bool ImbalanceDetectionEngine::detectVolumeImbalance(const OrderBookMetrics& metrics) {
//      // Check if required metrics are available
//      auto bidVolIt = metrics.metrics.find("bidVolume");
//      auto askVolIt = metrics.metrics.find("askVolume");
     
//      if (bidVolIt == metrics.metrics.end() || askVolIt == metrics.metrics.end()) {
//          return false; // Required metrics not available
//      }
     
//      double bidVolume = bidVolIt->second;
//      double askVolume = askVolIt->second;
//      //print bid and ask volume
//      // Avoid division by zero
//      if (askVolume < 1e-10) {
//          askVolume = 1e-10;
//      }
     
//      // Calculate bid/ask volume ratio
//      double volumeRatio = bidVolume / askVolume;
     
//      // Get threshold for volume imbalance
//      double threshold = thresholds_.getTypeThreshold(ImbalanceType::VOLUME_IMBALANCE).activation_threshold;
     
//      // Detect imbalance if ratio exceeds threshold or is below its inverse
//      bool isImbalance = (volumeRatio >= threshold || volumeRatio <= 1.0 / threshold);
     
//      // Update imbalance state
//      imbalanceState_[ImbalanceType::VOLUME_IMBALANCE] = isImbalance;
     
//      // If imbalance detected, create an event
//      if (isImbalance) {
//             double strength;                       
//             //simplified strength calculation
//             if (volumeRatio > threshold) {
//                 strength = (volumeRatio - threshold) / threshold; // Normalize above threshold
//             } else if (volumeRatio < 1.0 / threshold) {
//                 strength = (1.0/threshold - volumeRatio) / (1.0/threshold); // Normalize below inverse threshold
//             } else {
//                 strength = 0.0; // Should not happen as isImbalance is true
//             }
//             strength = std::min(1.0, strength); // Cap at 1.0
//             // Ensure strength is never negative or invalid
//             if (strength < 0.0 || !std::isfinite(strength)) {
//                 strength = 0.0;
//             }
//          Direction direction = (volumeRatio > 1.0) ? Direction::BUY : Direction::SELL;
//          createImbalanceEvent(ImbalanceType::VOLUME_IMBALANCE, strength, direction, metrics);
//         //  std::cout<<"[TradingApp]ImbalanceDetectionEngine::detectVolumeImbalance:strength="<<strength<<", volumeRatio="<<volumeRatio<<", threshold="<<threshold<<"\n";

//      }
     
//      return isImbalance;
//  }
 bool ImbalanceDetectionEngine::detectVolumeImbalance(const OrderBookMetrics& metrics) {
    // Check if required metrics are available
    auto bidVolIt = metrics.metrics.find("bidVolume");
    auto askVolIt = metrics.metrics.find("askVolume");
    if (bidVolIt == metrics.metrics.end() || askVolIt == metrics.metrics.end()) {
        return false;
    }

    double bidVolume = bidVolIt->second;
    double askVolume = askVolIt->second;

    // Avoid division by zero
    if (askVolume < 1e-10) {
        askVolume = 1e-10;
    }

    double volumeRatio = bidVolume / askVolume;
    //try emaVolumeImbalance
    auto emaIt = metrics.metrics.find("emaVolumeImbalance");
    if (emaIt != metrics.metrics.end()) {
        double emaVolumeImbalance = emaIt->second;
        // Combine raw ratio and EMA for a smoothed ratio
        // volumeRatio = (volumeRatio + emaVolumeImbalance) / 2.0;
        volumeRatio = emaVolumeImbalance; // Use EMA directly for detection
    }
    
    // Get current threshold
    double baseThreshold = thresholds_.getTypeThreshold(ImbalanceType::VOLUME_IMBALANCE).activation_threshold;
    
    // HYSTERESIS: Use different thresholds based on current state
    bool currentlyImbalanced = imbalanceState_[ImbalanceType::VOLUME_IMBALANCE];
    
    double enterThreshold = baseThreshold;           // e.g., 1.5
    double exitThreshold = baseThreshold * 0.8;     // e.g., 1.2 (20% lower)
    
    double effectiveThreshold = currentlyImbalanced ? exitThreshold : enterThreshold;
    
    // Detect imbalance with hysteresis
    bool isImbalance = (volumeRatio >= effectiveThreshold || volumeRatio <= 1.0 / effectiveThreshold);
    
    // Add minimum change requirement
    static double lastVolumeRatio = 1.0;
    double ratioChange = std::abs(volumeRatio - lastVolumeRatio) / lastVolumeRatio;
    double minChangeThreshold = 0.05; // Require 5% change
    std::cout << "[detectVolumeImbalance] volumeRatio=" << volumeRatio 
              << ", lastVolumeRatio=" << lastVolumeRatio 
              << ", ratioChange=" << (ratioChange * 100) << "%" 
              << ", effectiveThreshold=" << effectiveThreshold 
              << ", isImbalance=" << isImbalance 
              << ", currentlyImbalanced=" << currentlyImbalanced 
              << std::endl;
    if (ratioChange < minChangeThreshold && currentlyImbalanced) {
        // Not enough change, keep current state
        lastVolumeRatio = volumeRatio;
        return currentlyImbalanced;
    }
    
    lastVolumeRatio = volumeRatio;
    
    // Update state
    imbalanceState_[ImbalanceType::VOLUME_IMBALANCE] = isImbalance;
    
    // Create event only if state changed
    if (isImbalance && (!currentlyImbalanced || ratioChange >= minChangeThreshold)) {
        double strength = calculateImbalanceStrength(volumeRatio, effectiveThreshold);
        Direction direction = (volumeRatio > 1.0) ? Direction::BUY : Direction::SELL;
        
        std::cout << "[VolumeImbalance] Ratio=" << volumeRatio 
                  << ", Threshold=" << effectiveThreshold 
                  << ", Strength=" << strength 
                  << ", Direction=" << (direction == Direction::BUY ? "BUY" : "SELL")
                  << ", Change=" << (ratioChange * 100) << "%" << std::endl;
        
        createImbalanceEvent(ImbalanceType::VOLUME_IMBALANCE, strength, direction, metrics);
    }
    
    return isImbalance;
}


 // Detect price level imbalance
 bool ImbalanceDetectionEngine::detectPriceLevelImbalance(const OrderBookMetrics& metrics) {
     // Check if required metrics are available
     auto bidLevelsIt = metrics.metrics.find("bidLevelCount");
     auto askLevelsIt = metrics.metrics.find("askLevelCount");
     
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
    auto bidFlowIt = metrics.metrics.find("bidFlow");
    auto askFlowIt = metrics.metrics.find("askFlow");
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
     auto spreadIt = metrics.metrics.find("spreadPercent");
     auto avgSpreadIt = metrics.metrics.find("averageSpreadPercent");
     
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
         auto bidVolumeIt = metrics.metrics.find("bidVolume");
         auto askVolumeIt = metrics.metrics.find("askVolume");
         
         if (bidVolumeIt != metrics.metrics.end() && askVolumeIt != metrics.metrics.end()) {
             double bidVolume = bidVolumeIt->second;
             double askVolume = askVolumeIt->second;
             
             // If bid volume is significantly higher, it might indicate buying pressure despite the wide spread
             if (bidVolume > askVolume * 1.5) {
                 direction = Direction::BUY;
             }
         }
         createImbalanceEvent(ImbalanceType::ABNORMAL_SPREAD, strength, direction, metrics);
     }
     
     return isImbalance;
 }
// bool ImbalanceDetectionEngine::detectMarketDepthChange(const OrderBookMetrics& metrics){
//     // Placeholder implementation

//     return false;
// }
bool ImbalanceDetectionEngine::detectMarketDepthChange(const OrderBookMetrics& metrics) {
    // Expect analyzeMetrics to provide a previous snapshot in lastMetrics_
    if (!lastMetrics_) {
        return false; // no previous snapshot to compare
    }

    const auto& prev = *lastMetrics_;
    bool depthChanged = false;

    // --- 1. Spread widening/narrowing ---
    double spreadDiff = std::abs(metrics.spread - prev.spread);
    if (spreadDiff > config_.spreadChangeThreshold) {
        depthChanged = true;
    }

    // --- 2. Volume imbalance shift ---
    double imbalanceDiff = std::abs(metrics.volumeImbalance - prev.volumeImbalance);
    if (imbalanceDiff > config_.volumeImbalanceThreshold) {
        depthChanged = true;
    }

    // --- 3. Depth distribution change ---
    auto checkVectorChange = [&](const std::vector<double>& cur,
                                 const std::vector<double>& old,
                                 double threshold) -> bool {
        size_t n = std::min(cur.size(), old.size());
        for (size_t i = 0; i < n; i++) {
            if (std::abs(cur[i] - old[i]) > threshold) {
                return true;
            }
        }
        return false;
    };

    if (checkVectorChange(metrics.bidVolumesByLevel, prev.bidVolumesByLevel, config_.levelVolumeChangeThreshold) ||
        checkVectorChange(metrics.askVolumesByLevel, prev.askVolumesByLevel, config_.levelVolumeChangeThreshold)) {
        depthChanged = true;
    }

    // --- 4. Liquidity pressure or density shifts ---
    double pressureDiff = std::abs(metrics.liquidityPressure - prev.liquidityPressure);
    if (pressureDiff > config_.liquidityPressureThreshold) {
        depthChanged = true;
    }

    return depthChanged;
}

bool ImbalanceDetectionEngine::detectLargeOrderAppearance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectLargeOrderDisappearance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectOrderFlowImbalance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectPriceLevelConcentration(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectLiquidityImbalance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectTrendContinuation(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectTrendReversal(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectTradeImbalance(const OrderBookMetrics& metrics)   {
    return false;
}
bool ImbalanceDetectionEngine::detectCancellationImbalance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectPriceVolatilityImbalance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectHiddenLiquidityImbalance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectCustomImbalance(const OrderBookMetrics& metrics){
    return false;
}

// Calculate imbalance strength based on type and metrics
 double ImbalanceDetectionEngine::calculateImbalanceStrength(ImbalanceType type, const OrderBookMetrics& metrics) {
     double strength = 0.0;
     
     switch (type) {
         case ImbalanceType::VOLUME_IMBALANCE: {
             auto bidVolIt = metrics.metrics.find("bidVolume");
             auto askVolIt = metrics.metrics.find("askVolume");
             
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
             auto bidLevelsIt = metrics.metrics.find("bidLevelCount");
             auto askLevelsIt = metrics.metrics.find("askLevelCount");
             
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
             auto bidFlowIt = metrics.metrics.find("bidFlow");
             auto askFlowIt = metrics.metrics.find("askFlow");
             
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
             auto spreadIt = metrics.metrics.find("spreadPercent");
             auto avgSpreadIt = metrics.metrics.find("averageSpreadPercent");
             
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
 
 //this function need to be rewrite carefully and follow below logic
 /**

function determineDirection(metrics):

    scoreBuy = 0
    scoreSell = 0

    # --- Volume imbalance ---
    if has(bidVolume, askVolume):
        ratio = (bidVolume - askVolume) / (bidVolume + askVolume)
        if ratio > +0.2: scoreBuy += 1
        if ratio < -0.2: scoreSell += 1

    # --- Price level imbalance ---
    if has(bidLevels, askLevels):
        ratio = (bidLevels - askLevels) / (bidLevels + askLevels)
        if ratio > +0.3: scoreBuy += 1
        if ratio < -0.3: scoreSell += 1

    # --- Liquidity removal ---
    if has(liquidityRemovalSide):
        if liquidityRemovalSide == "bid": scoreSell += 2
        if liquidityRemovalSide == "ask": scoreBuy += 2

    # --- Order flow imbalance (submissions) ---
    if has(bidOrdersSubmitted, askOrdersSubmitted):
        ratio = (bidOrdersSubmitted - askOrdersSubmitted) / (total)
        if ratio > +0.2: scoreBuy += 1
        if ratio < -0.2: scoreSell += 1

    # --- Large order appearance/disappearance ---
    if has(largeBidOrderAppeared): scoreBuy += 1
    if has(largeAskOrderAppeared): scoreSell += 1
    if has(largeBidOrderRemoved):  scoreSell += 1
    if has(largeAskOrderRemoved):  scoreBuy += 1

    # --- Price level concentration ---
    if has(concentratedBidLevels): scoreBuy += 1
    if has(concentratedAskLevels): scoreSell += 1

    # --- Spread changes ---
    if has(spreadChange):
        if spreadChange < 0: scoreBuy += 1   # tighter spread bullish
        if spreadChange > 0: scoreSell += 1  # wider spread bearish

    if has(spread) and spread > ABNORMAL_THRESHOLD:
        if bidVolume > askVolume * 1.5: scoreBuy += 1
        else: scoreSell += 1

    # --- Market depth change ---
    if has(bidDepthChange, askDepthChange):
        diff = bidDepthChange - askDepthChange
        if diff > +THRESH: scoreBuy += 1
        if diff < -THRESH: scoreSell += 1

    # --- Liquidity flow imbalance ---
    if has(bidFlow, askFlow):
        ratio = (bidFlow - askFlow) / (bidFlow + askFlow)
        if ratio > +0.1: scoreBuy += 2
        if ratio < -0.1: scoreSell += 2

    # --- Trend signals ---
    if has(trendStrength):
        if trendStrength > TREND_THRESHOLD: scoreBuy += 1
        if trendStrength < -TREND_THRESHOLD: scoreSell += 1

    if has(prevTrendStrength, trendStrength):
        if prevTrendStrength > 0 and trendStrength < -TREND_THRESHOLD:
            scoreSell += 2
        if prevTrendStrength < 0 and trendStrength > +TREND_THRESHOLD:
            scoreBuy += 2

    # --- Trade imbalance (executed trades) ---
    if has(marketBuyVolume, marketSellVolume):
        ratio = (marketBuyVolume - marketSellVolume) / (total)
        if ratio > +0.2: scoreBuy += 2
        if ratio < -0.2: scoreSell += 2

    # --- Cancellation imbalance ---
    if has(bidCancellations, askCancellations):
        ratio = (bidCancels - askCancels) / (total)
        if ratio > +0.2: scoreSell += 1    # buyers pulling liquidity
        if ratio < -0.2: scoreBuy += 1     # sellers pulling liquidity

    # --- Price volatility imbalance ---
    if has(volatility):
        if volatility > VOLATILITY_THRESHOLD:
            if lastMoveUp: scoreBuy += 1
            else: scoreSell += 1

    # --- Hidden liquidity imbalance (icebergs) ---
    if has(icebergOnBid): scoreBuy += 2
    if has(icebergOnAsk): scoreSell += 2

    # --- Custom ---
    if has(customSignal):
        if customSignal > 0: scoreBuy += 1
        if customSignal < 0: scoreSell += 1

    # --- Final decision ---
    if scoreBuy > scoreSell and scoreBuy >= MIN_CONFIDENCE:
        return BUY
    if scoreSell > scoreBuy and scoreSell >= MIN_CONFIDENCE:
        return SELL
    return NEUTRAL

  * 
  */

 // Determine imbalance direction based on type and metrics
 Direction ImbalanceDetectionEngine::determineImbalanceDirection(ImbalanceType type, const OrderBookMetrics& metrics) {
     Direction direction = Direction::NEUTRAL;
     
     switch (type) {
         case ImbalanceType::VOLUME_IMBALANCE: {
             auto bidVolIt = metrics.metrics.find("bidVolume");
             auto askVolIt = metrics.metrics.find("askVolume");
             
             if (bidVolIt != metrics.metrics.end() && askVolIt != metrics.metrics.end()) {
                 double bidVol = bidVolIt->second;
                 double askVol = askVolIt->second;
                 
                 direction = (bidVol > askVol) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::PRICE_LEVEL_IMBALANCE: {
             auto bidLevelsIt = metrics.metrics.find("bidLevelCount");
             auto askLevelsIt = metrics.metrics.find("askLevelCount");
             
             if (bidLevelsIt != metrics.metrics.end() && askLevelsIt != metrics.metrics.end()) {
                 double bidLevels = bidLevelsIt->second;
                 double askLevels = askLevelsIt->second;
                 
                 direction = (bidLevels > askLevels) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE: {
             auto bidFlowIt = metrics.metrics.find("bidFlow");
             auto askFlowIt = metrics.metrics.find("askFlow");
             
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
             auto bidVolumeIt = metrics.metrics.find("bidVolume");
             auto askVolumeIt = metrics.metrics.find("askVolume");
             
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
         case ImbalanceType::MARKET_DEPTH_CHANGE:
         //is this correct?
             direction = (metrics.metrics.at("depthChange") > 0) ? Direction::BUY : Direction::SELL;
             break;
         case ImbalanceType::TREND_CONTINUATION:
             direction = (metrics.metrics.at("trendStrength") > 0) ? Direction::BUY : Direction::SELL;
             break;
         case ImbalanceType::TREND_REVERSAL:
             direction = (metrics.metrics.at("trendStrength") < 0) ? Direction::BUY : Direction::SELL;
             break;
         case ImbalanceType::CUSTOM:
             direction = (metrics.metrics.at("customSignal") > 0) ? Direction::BUY : Direction::SELL;
             break;
         default:
             break;
    }
     return direction;
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
    event.confidence = trading::core::valueToConfidenceLevel(calculateImbalanceConfidence(type, strength, metrics)); 
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