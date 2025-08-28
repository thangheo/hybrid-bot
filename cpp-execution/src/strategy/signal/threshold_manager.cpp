/**
 * @file threshold_manager.cpp
 * @brief Implementation of dynamic threshold management
 */

 #include "strategy/signal/threshold_manager.hpp"
 #include "core/market_types.hpp"
 #include <algorithm>
 #include <chrono>
 #include <cmath>
 #include <fstream>
 #include <iostream>
 #include <nlohmann/json.hpp>
 
 using namespace trading::data_ingestion;
 namespace trading {
 namespace strategy {
 
 ThresholdManager::ThresholdManager() {
     // Initialize with default base thresholds
     base_thresholds_.global_sensitivity = 1.0;
     
     // Default imbalance type thresholds
     ImbalanceThreshold volume_imbalance;
     volume_imbalance.min_threshold = 0.2;
     volume_imbalance.max_threshold = 0.8;
     volume_imbalance.activation_threshold = 0.3;
     base_thresholds_.type_thresholds[ImbalanceType::VOLUME_IMBALANCE] = volume_imbalance;
     
     ImbalanceThreshold price_level_imbalance;
     price_level_imbalance.min_threshold = 0.15;
     price_level_imbalance.max_threshold = 0.7;
     price_level_imbalance.activation_threshold = 0.25;
     base_thresholds_.type_thresholds[ImbalanceType::PRICE_LEVEL_IMBALANCE] = price_level_imbalance;
     
     ImbalanceThreshold liquidity_imbalance;
     liquidity_imbalance.min_threshold = 0.25;
     liquidity_imbalance.max_threshold = 0.9;
     liquidity_imbalance.activation_threshold = 0.35;
     base_thresholds_.type_thresholds[ImbalanceType::LIQUIDITY_IMBALANCE] = liquidity_imbalance;
     
     ImbalanceThreshold spread_anomaly;
     spread_anomaly.min_threshold = 0.3;
     spread_anomaly.max_threshold = 0.8;
     spread_anomaly.activation_threshold = 0.4;
     base_thresholds_.type_thresholds[ImbalanceType::ABNORMAL_SPREAD] = spread_anomaly;
     
     ImbalanceThreshold trend_continuation;
     trend_continuation.min_threshold = 0.15;
     trend_continuation.max_threshold = 0.7;
     trend_continuation.activation_threshold = 0.25;
     base_thresholds_.type_thresholds[ImbalanceType::TREND_CONTINUATION] = trend_continuation;
     
     ImbalanceThreshold trend_reversal;
     trend_reversal.min_threshold = 0.25;
     trend_reversal.max_threshold = 0.85;
     trend_reversal.activation_threshold = 0.35;
     base_thresholds_.type_thresholds[ImbalanceType::TREND_REVERSAL] = trend_reversal;
     
     // Initialize default volatility adjustment factors
     volatility_factors_[VolatilityLevel::VERY_LOW] = 0.7;    // Lower thresholds in very low volatility
     volatility_factors_[VolatilityLevel::LOW] = 0.85;        // Slightly lower thresholds in low volatility
     volatility_factors_[VolatilityLevel::NORMAL] = 1.0;      // No adjustment for normal volatility
     volatility_factors_[VolatilityLevel::HIGH] = 1.3;        // Higher thresholds in high volatility
     volatility_factors_[VolatilityLevel::VERY_HIGH] = 1.6;   // Much higher thresholds in very high volatility
     
     // Initialize default time-of-day factors
     // Hours in local time (assuming exchange timezone)
     for (int hour = 0; hour < 24; ++hour) {
         // Default is no adjustment
         time_of_day_factors_[hour] = 1.0;
     }
     
     // Example: Reduce thresholds during major market hours
     // Assumes major market hours are 8:00-16:00 local time
     for (int hour = 8; hour < 16; ++hour) {
         time_of_day_factors_[hour] = 0.9;  // 10% lower thresholds during market hours
     }
     
     // Example: Increase thresholds during off-hours
     for (int hour = 20; hour < 24; ++hour) {
         time_of_day_factors_[hour] = 1.2;  // 20% higher thresholds during off-hours
     }
     for (int hour = 0; hour < 4; ++hour) {
         time_of_day_factors_[hour] = 1.2;  // 20% higher thresholds during off-hours
     }
     
     // Initialize performance adjustment factors
     performance_adjustment_factors_["accuracy_below_30"] = 1.4;  // Increase thresholds for low accuracy
     performance_adjustment_factors_["accuracy_30_50"] = 1.2;     // Slightly increase thresholds
     performance_adjustment_factors_["accuracy_50_70"] = 1.0;     // No adjustment
     performance_adjustment_factors_["accuracy_70_90"] = 0.9;     // Slightly decrease thresholds
     performance_adjustment_factors_["accuracy_above_90"] = 0.8;  // Decrease thresholds for high accuracy
 }
 
 void ThresholdManager::updateMarketConditions(const MarketState& state) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (thresholds_locked_) {
         return;  // Don't update if thresholds are locked
     }
     
     current_market_state_ = state;
 }
 
 ThresholdSet ThresholdManager::getCurrentThresholds() {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Start with base thresholds
     ThresholdSet current = base_thresholds_;
     
     // Apply adjustments unless thresholds are locked
     if (!thresholds_locked_) {
         current = applyVolatilityAdjustment(current);
         current = applyTimeOfDayAdjustment(current);
         current = applyPerformanceAdjustment(current);
     }
     
     // Ensure thresholds stay within safe bounds
     current = enforceSafeBounds(current);
     
     return current;
 }
 
 void ThresholdManager::setBaseThresholds(const ThresholdSet& base_thresholds) {
     std::lock_guard<std::mutex> lock(mutex_);
     base_thresholds_ = base_thresholds;
 }
 
 ThresholdSet ThresholdManager::getExchangeThresholds(const ExchangeId& exchangeId) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Check if we have exchange-specific thresholds
     auto it = exchange_thresholds_.find(exchangeId);
     if (it != exchange_thresholds_.end()) {
         // Start with exchange-specific base thresholds
         ThresholdSet current = it->second;
         
         // Apply adjustments unless thresholds are locked
         if (!thresholds_locked_) {
             current = applyVolatilityAdjustment(current);
             current = applyTimeOfDayAdjustment(current);
             current = applyPerformanceAdjustment(current);
         }
         
         // Ensure thresholds stay within safe bounds
         return enforceSafeBounds(current);
     }
     
     // Fall back to standard thresholds if no exchange-specific ones exist
     return getCurrentThresholds();
 }
 
 void ThresholdManager::setExchangeThresholds(const ExchangeId& exchangeId, const ThresholdSet& thresholds) {
     std::lock_guard<std::mutex> lock(mutex_);
     exchange_thresholds_[exchangeId] = thresholds;
 }
 
 void ThresholdManager::setVolatilityFactors(const std::map<VolatilityLevel, double>& factors) {
     std::lock_guard<std::mutex> lock(mutex_);
     volatility_factors_ = factors;
 }
 
 void ThresholdManager::setTimeOfDayFactors(const std::map<int, double>& factors) {
     std::lock_guard<std::mutex> lock(mutex_);
     time_of_day_factors_ = factors;
 }
 
 void ThresholdManager::adjustFromPerformance(const SignalPerformance& performance) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (thresholds_locked_) {
         return;  // Don't adjust if thresholds are locked
     }
     
     // Store performance metrics for each imbalance type
     for (const auto& [type, metrics] : performance.typePerformance) {
         // Calculate adjustment factor based on accuracy
         std::string accuracy_key;
         
         if (metrics.accuracy < 0.3) {
             accuracy_key = "accuracy_below_30";
         } else if (metrics.accuracy < 0.5) {
             accuracy_key = "accuracy_30_50";
         } else if (metrics.accuracy < 0.7) {
             accuracy_key = "accuracy_50_70";
         } else if (metrics.accuracy < 0.9) {
             accuracy_key = "accuracy_70_90";
         } else {
             accuracy_key = "accuracy_above_90";
         }
         
         double factor = performance_adjustment_factors_[accuracy_key];
         
         // Apply adjustment to base thresholds for this type
         auto it = base_thresholds_.type_thresholds.find(type);
         if (it != base_thresholds_.type_thresholds.end()) {
             // Gradually adjust thresholds (20% new value, 80% existing)
             it->second.activation_threshold = 0.8 * it->second.activation_threshold + 
                                              0.2 * it->second.activation_threshold * factor;
             
             // Also adjust min/max thresholds proportionally
             double ratio = it->second.activation_threshold / 
                           (base_thresholds_.type_thresholds[type].activation_threshold);
             
             it->second.min_threshold = base_thresholds_.type_thresholds[type].min_threshold * ratio;
             it->second.max_threshold = base_thresholds_.type_thresholds[type].max_threshold * ratio;
         }
     }
     
     // Ensure thresholds stay within safe bounds
     base_thresholds_ = enforceSafeBounds(base_thresholds_);
 }
 
 void ThresholdManager::lockThresholds(bool lock) {
     std::lock_guard<std::mutex> lock_guard(mutex_);
     thresholds_locked_ = lock;
 }
 
 bool ThresholdManager::areThresholdsLocked() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return thresholds_locked_;
 }
 
 bool ThresholdManager::saveThresholds(const std::string& identifier) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     try {
         // Create JSON object
         nlohmann::json json_data;
         
         // Add base thresholds
         json_data["base_thresholds"]["global_sensitivity"] = base_thresholds_.global_sensitivity;
         
         // Add type thresholds
         for (const auto& [type, threshold] : base_thresholds_.type_thresholds) {
             std::string type_name = ImbalanceTypeToString(type);
             
             json_data["base_thresholds"]["type_thresholds"][type_name]["min_threshold"] = threshold.min_threshold;
             json_data["base_thresholds"]["type_thresholds"][type_name]["max_threshold"] = threshold.max_threshold;
             json_data["base_thresholds"]["type_thresholds"][type_name]["activation_threshold"] = threshold.activation_threshold;
         }
         
         // Add exchange-specific thresholds
         for (const auto& [exchangeId, thresholds] : exchange_thresholds_) {
             std::string exchange_name = exchangeId.toString();
             
             json_data["exchange_thresholds"][exchange_name]["global_sensitivity"] = thresholds.global_sensitivity;
             
             for (const auto& [type, threshold] : thresholds.type_thresholds) {
                 std::string type_name = ImbalanceTypeToString(type);
                 
                 json_data["exchange_thresholds"][exchange_name]["type_thresholds"][type_name]["min_threshold"] = threshold.min_threshold;
                 json_data["exchange_thresholds"][exchange_name]["type_thresholds"][type_name]["max_threshold"] = threshold.max_threshold;
                 json_data["exchange_thresholds"][exchange_name]["type_thresholds"][type_name]["activation_threshold"] = threshold.activation_threshold;
             }
         }
         
         // Add metadata
         json_data["metadata"]["timestamp"] = std::chrono::system_clock::now().time_since_epoch().count();
         json_data["metadata"]["identifier"] = identifier.empty() ? "default" : identifier;
         
         // Write to file
         std::string filename = "thresholds_" + (identifier.empty() ? "default" : identifier) + ".json";
         std::ofstream file(filename);
         
         if (!file.is_open()) {
             return false;
         }
         
         file << json_data.dump(4);  // Pretty-print JSON with 4-space indentation
         file.close();
         
         return true;
     } catch (const std::exception& e) {
         // Log error
         std::cerr << "Error saving thresholds: " << e.what() << std::endl;
         return false;
     }
 }
//  //getGlobalSensitivity
 double ThresholdManager::getGlobalSensitivity() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return base_thresholds_.global_sensitivity;
 }
 void ThresholdManager::updateParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update parameters as needed
    // For example, update thresholds or other configurations
    
    // Use getNumericParam instead of non-existent has/get methods
    double globalSensitivity = params.getNumericParam("global_sensitivity", base_thresholds_.global_sensitivity);
    base_thresholds_.global_sensitivity = globalSensitivity;
    
    // For complex types like maps, we need a different approach since ParameterSet 
    // doesn't directly support map types in the getters
    auto volatilityArray = params.getArrayParam("volatility_factors");
    if (!volatilityArray.empty() && volatilityArray.size() % 2 == 0) {
        // Assuming the array is stored as [level1, factor1, level2, factor2, ...]
        volatility_factors_.clear();
        for (size_t i = 0; i < volatilityArray.size(); i += 2) {
            VolatilityLevel level = static_cast<VolatilityLevel>(static_cast<int>(volatilityArray[i]));
            double factor = volatilityArray[i + 1];
            volatility_factors_[level] = factor;
        }
    }
    
    // Similar approach for time_of_day_factors
    auto timeFactorsArray = params.getArrayParam("time_of_day_factors");
    if (!timeFactorsArray.empty() && timeFactorsArray.size() % 2 == 0) {
        // Assuming the array is stored as [hour1, factor1, hour2, factor2, ...]
        time_of_day_factors_.clear();
        for (size_t i = 0; i < timeFactorsArray.size(); i += 2) {
            int hour = static_cast<int>(timeFactorsArray[i]);
            double factor = timeFactorsArray[i + 1];
            time_of_day_factors_[hour] = factor;
        }
    }
}

 bool ThresholdManager::loadThresholds(const std::string& identifier) {
     try {
         // Read from file
         std::string filename = "thresholds_" + (identifier.empty() ? "default" : identifier) + ".json";
         std::ifstream file(filename);
         
         if (!file.is_open()) {
             return false;
         }
         
         nlohmann::json json_data;
         file >> json_data;
         file.close();
         
         std::lock_guard<std::mutex> lock(mutex_);
         
         // Load base thresholds
         if (json_data.contains("base_thresholds")) {
             const auto& base = json_data["base_thresholds"];
             
             if (base.contains("global_sensitivity")) {
                 base_thresholds_.global_sensitivity = base["global_sensitivity"];
             }
             
             if (base.contains("type_thresholds")) {
                 for (const auto& [type_name, threshold_json] : base["type_thresholds"].items()) {
                     ImbalanceType type = imbalanceTypeFromString(type_name);
                     
                     ImbalanceThreshold threshold;
                     threshold.min_threshold = threshold_json["min_threshold"];
                     threshold.max_threshold = threshold_json["max_threshold"];
                     threshold.activation_threshold = threshold_json["activation_threshold"];
                     
                     base_thresholds_.type_thresholds[type] = threshold;
                 }
             }
         }
         
         // Load exchange-specific thresholds
         if (json_data.contains("exchange_thresholds")) {
             for (const auto& [exchange_name, exchange_json] : json_data["exchange_thresholds"].items()) {
                 // Convert string to ExchangeId
                 core::ExchangeId exchangeId = core::ExchangeId::fromString(exchange_name);
                 
                 ThresholdSet exchange_thresholds;
                 
                 if (exchange_json.contains("global_sensitivity")) {
                     exchange_thresholds.global_sensitivity = exchange_json["global_sensitivity"];
                 }
                 
                 if (exchange_json.contains("type_thresholds")) {
                     for (const auto& [type_name, threshold_json] : exchange_json["type_thresholds"].items()) {
                         ImbalanceType type = imbalanceTypeFromString(type_name);
                         
                         ImbalanceThreshold threshold;
                        //  threshold.min_threshold = threshold_json["min_threshold"];
                        threshold.min_threshold = threshold_json["min_threshold"];
                        threshold.max_threshold = threshold_json["max_threshold"];
                        threshold.activation_threshold = threshold_json["activation_threshold"];
                        
                        exchange_thresholds.type_thresholds[type] = threshold;
                    }
                }
                
                exchange_thresholds_[exchangeId] = exchange_thresholds;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        // Log error
        std::cerr << "Error loading thresholds: " << e.what() << std::endl;
        return false;
    }
}

ThresholdSet ThresholdManager::applyVolatilityAdjustment(const ThresholdSet& thresholds) {
    ThresholdSet adjusted = thresholds;
    
    // Get current volatility level
    VolatilityLevel volatility = getCurrentVolatilityLevel();
    
    // Get adjustment factor for this volatility level
    auto it = volatility_factors_.find(volatility);
    if (it == volatility_factors_.end()) {
        // No adjustment if level not found
        return adjusted;
    }
    
    double factor = it->second;
    
    // Apply adjustment to global sensitivity
    adjusted.global_sensitivity *= factor;
    
    // Apply to each imbalance type threshold
    for (auto& [type, threshold] : adjusted.type_thresholds) {
        threshold.activation_threshold *= factor;
        
        // Adjust min/max thresholds proportionally, but ensure they stay in order
        threshold.min_threshold *= factor;
        threshold.max_threshold *= factor;
    }
    
    return adjusted;
}

ThresholdSet ThresholdManager::applyTimeOfDayAdjustment(const ThresholdSet& thresholds) {
    ThresholdSet adjusted = thresholds;
    
    // Get current time factor
    double factor = getCurrentTimeFactor();
    
    // Apply adjustment to global sensitivity
    adjusted.global_sensitivity *= factor;
    
    // Apply to each imbalance type threshold
    for (auto& [type, threshold] : adjusted.type_thresholds) {
        threshold.activation_threshold *= factor;
        
        // Adjust min/max thresholds proportionally
        threshold.min_threshold *= factor;
        threshold.max_threshold *= factor;
    }
    
    return adjusted;
}

ThresholdSet ThresholdManager::applyPerformanceAdjustment(const ThresholdSet& thresholds) {
    // No current performance data to apply
    return thresholds;
}

ThresholdSet ThresholdManager::enforceSafeBounds(const ThresholdSet& thresholds) {
    ThresholdSet safe = thresholds;
    
    // Enforce bounds on global sensitivity
    safe.global_sensitivity = std::max(0.5, std::min(2.0, safe.global_sensitivity));
    
    // Enforce bounds on each threshold
    for (auto& [type, threshold] : safe.type_thresholds) {
        // Ensure min_threshold is between 0.05 and 0.5
        threshold.min_threshold = std::max(0.05, std::min(0.5, threshold.min_threshold));
        
        // Ensure max_threshold is between 0.5 and 1.0
        threshold.max_threshold = std::max(0.5, std::min(1.0, threshold.max_threshold));
        
        // Ensure activation_threshold is between min and max
        threshold.activation_threshold = std::max(
            threshold.min_threshold,
            std::min(threshold.max_threshold, threshold.activation_threshold)
        );
        
        // Ensure thresholds stay in proper order
        if (threshold.min_threshold > threshold.activation_threshold) {
            threshold.min_threshold = threshold.activation_threshold;
        }
        
        if (threshold.max_threshold < threshold.activation_threshold) {
            threshold.max_threshold = threshold.activation_threshold;
        }
    }
    
    return safe;
}

VolatilityLevel ThresholdManager::getCurrentVolatilityLevel() const {
    // Extract volatility from market state
    double volatility = current_market_state_.volatility;
    
    // Determine volatility level based on thresholds
    if (volatility < 0.001) {
        return VolatilityLevel::VERY_LOW;
    } else if (volatility < 0.005) {
        return VolatilityLevel::LOW;
    } else if (volatility < 0.02) {
        return VolatilityLevel::NORMAL;
    } else if (volatility < 0.05) {
        return VolatilityLevel::HIGH;
    } else {
        return VolatilityLevel::VERY_HIGH;
    }
}

double ThresholdManager::getCurrentTimeFactor() const {
    // Get current hour
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm* local_time = std::localtime(&time);
    int hour = local_time->tm_hour;
    
    // Get time factor for current hour
    auto it = time_of_day_factors_.find(hour);
    if (it != time_of_day_factors_.end()) {
        return it->second;
    }
    
    // Default to no adjustment if hour not found
    return 1.0;
}

} // namespace strategy
} // namespace trading