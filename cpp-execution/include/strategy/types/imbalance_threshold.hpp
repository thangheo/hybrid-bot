/**
 * @file strategy/types/imbalance_threshold.hpp
 * @brief Imbalance threshold definitions
 * 
 * This file defines threshold structures for imbalance detection.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include <map>
 #include <string>
 #include "strategy/types/imbalance_type.hpp"
 
 namespace trading {
 namespace strategy {
 
 /**
  * ImbalanceThreshold structure to define thresholds for different imbalance types
  */
 struct ImbalanceThreshold {
     double min_threshold;          // Minimum threshold for detection
     double max_threshold;          // Maximum threshold for detection
     double activation_threshold;   // Threshold to activate the signal  
 
     ImbalanceThreshold(double min = 0.0, double max = 1.0, double activation = 0.2)
         : min_threshold(min), max_threshold(max), activation_threshold(activation) {}
 };
 
 /**
  * ThresholdSet for storing multiple thresholds with a global sensitivity
  */
 struct ThresholdSet {
     std::map<std::string, double> thresholds;                 // Map of threshold name to value
     double global_sensitivity = 1.0;                          // Global sensitivity for all thresholds
     std::map<ImbalanceType, ImbalanceThreshold> type_thresholds; // Type-specific thresholds
     double priceVolatilityImbalance = 0.6;
     ///just set it low because we will learn it online
     double priceVolatilityImbalanceMin = 0.3;
    double priceVolatilityImbalanceMax = 0.9;   
    double spreadPersistence = 0.5;
    double depthVolumeInconsistency = 0.4;
    double volatilitySuppression = 0.5;
    double hiddenLiquidityPatterns = 0.4;
    double densityPressureMismatch = 0.5;
    double velocityAnomalies = 0.4;
    double marketImpactResistance = 0.5;
    double correlationAnomalies = 0.4;
    double quoteStuffing = 0.3;
    double flashCrashPattern = 0.3;
    double levelVolumeChange = 0.4;
    double suddenLiquidityRemoval = 0.5;
    double largeOrderAppearance = 0.7;
    double largeOrderDisappearance = 0.5;
    double orderFlowImbalance = 0.6;
    double priceLevelConcentration = 0.5;
    double liquidityImbalance = 0.6;
    double trendContinuation = 0.5;
    double trendReversal = 0.5;
    double tradeImbalance = 0.6;
    double abnormalSpread = 0.5;
    double liquidityFlowImbalance = 0.6;
    double marketDepthChange = 0.5;
    double volumeImbalance = 0.7;
    double priceLevelImbalance = 0.6;
    double liquidityRemoval = 0.5;
    double priceVolatility = 0.6;
    double volatilitySpike = 0.5;
    double orderBookImbalance = 0.6;
    double orderBookDepthChange = 0.5;
    double cancellationImbalance = 0.5;
    double hiddenLiquidityImbalance = 0.4;
     // Common threshold getters
     double getVolumeImbalanceThreshold() const {
         return getThreshold("volumeImbalance ");
     }
      
     double getLiquidityRemovalThreshold() const {
         return getThreshold("liquidity_removal");
     }
      
     double getPriceLevelImbalanceThreshold() const {
         return getThreshold("priceLevelImbalance");
     }
    //  double getPriceVolatilityImbalanceThreshold() const {
    //      return getThreshold("priceVolatilityImbalance", priceVolatilityImbalance);
    //  }
      
     // Generic threshold getter with default value
     double getThreshold(const std::string& name, double default_value = 0.0) const {
         auto it = thresholds.find(name);
         return (it != thresholds.end()) ? it->second : default_value;
     }
      
     // Threshold setter
     void setThreshold(const std::string& name, double value) {
         thresholds[name] = value;
     }
     
     // Adjust threshold by global sensitivity
     double getAdjustedThreshold(const std::string& name, double default_value = 0.0) const {
         double base = getThreshold(name, default_value);
         return base * global_sensitivity;
     }
     
     // Get ImbalanceThreshold for a specific type
     ImbalanceThreshold getTypeThreshold(ImbalanceType type) const {
         auto it = type_thresholds.find(type);
         return (it != type_thresholds.end()) ? it->second : ImbalanceThreshold();
     }
     
     // Set ImbalanceThreshold for a specific type
     void setTypeThreshold(ImbalanceType type, const ImbalanceThreshold& threshold) {
         type_thresholds[type] = threshold;
     }
     bool empty() const {
         return thresholds.empty() && type_thresholds.empty();
     }
 };
 
 } // namespace strategy
 } // namespace trading