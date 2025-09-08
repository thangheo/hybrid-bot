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
 
     ImbalanceThreshold(double min = 0.0, double max = 1.0, double activation = 0.5)
         : min_threshold(min), max_threshold(max), activation_threshold(activation) {}
 };
 
 /**
  * ThresholdSet for storing multiple thresholds with a global sensitivity
  */
 struct ThresholdSet {
     std::map<std::string, double> thresholds;                 // Map of threshold name to value
     double global_sensitivity = 1.0;                          // Global sensitivity for all thresholds
     std::map<ImbalanceType, ImbalanceThreshold> type_thresholds; // Type-specific thresholds
      
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