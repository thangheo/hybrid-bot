/**
 * @file strategy/types/parameter_set.hpp
 * @brief Parameter set for strategy configuration
 * 
 * This file defines the ParameterSet structure for strategy configuration.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include <string>
 #include <vector>
 #include <map>
 #include "core/types.hpp"
 #include "core/market_types.hpp"
 namespace trading {
 namespace strategy {
 
 /**
  * Represents a set of parameters for the imbalance strategy
  */
 struct ParameterSet {
     std::string version;                              // Version identifier
     core::ExchangeId exchangeId;                     // Exchange these parameters apply to
     core::Timestamp last_updated;                     // When parameters were last updated
     std::map<std::string, double> numeric_params;     // Numeric parameters
     std::map<std::string, std::string> string_params; // String parameters
     std::map<std::string, bool> bool_params;          // Boolean parameters
     std::map<std::string, std::vector<double>> array_params; // Array parameters
     
     // Constructor
     ParameterSet() : 
         version("1.0.0"),
         exchangeId(),
         last_updated(std::chrono::system_clock::now()) {}
     // Get a double parameter with default value if not found
    double getDouble(const std::string& name, double default_value = 0.0) const {
        return getNumericParam(name, default_value);
    }
    
    // Get an integer parameter with default value if not found
    int getInt(const std::string& name, int default_value = 0) const {
        return static_cast<int>(getNumericParam(name, static_cast<double>(default_value)));
    }
     // Parameter access methods with typed getters
     double getNumericParam(const std::string& name, double default_value = 0.0) const {
         auto it = numeric_params.find(name);
         return (it != numeric_params.end()) ? it->second : default_value;
     }
     
     std::string getStringParam(const std::string& name, const std::string& default_value = "") const {
         auto it = string_params.find(name);
         return (it != string_params.end()) ? it->second : default_value;
     }
     
     bool getBoolParam(const std::string& name, bool default_value = false) const {
         auto it = bool_params.find(name);
         return (it != bool_params.end()) ? it->second : default_value;
     }
     
     std::vector<double> getArrayParam(const std::string& name, const std::vector<double>& default_value = {}) const {
         auto it = array_params.find(name);
         return (it != array_params.end()) ? it->second : default_value;
     }
     
     // Parameter setters
     void setParameter(const std::string& name, double value) {
         numeric_params[name] = value;
         updateTimestamp();
     }
     void setParameter(const std::string& name, int value) {
         numeric_params[name] = value;
         updateTimestamp();
     }
     
     void setParameter(const std::string& name, const std::string& value) {
         string_params[name] = value;
         updateTimestamp();
     }
     
     void setParameter(const std::string& name, bool value) {
         bool_params[name] = value;
         updateTimestamp();
     }
     
     void setParameter(const std::string& name, const std::vector<double>& value) {
         array_params[name] = value;
         updateTimestamp();
     }
     
     // Helper to update the timestamp
     void updateTimestamp() {
         last_updated = std::chrono::system_clock::now();
     }
 };
 
 /**
  * Strategy configuration combining parameters and thresholds
  */
//  struct StrategyConfig {
//      core::StrategyId strategy_id;                      // Unique identifier for the strategy
//      std::string version;                               // Version of the strategy
//      ParameterSet parameters;                           // Strategy parameters
//      int detection_window_ms = 1000;                    // Detection window in milliseconds
//      int max_history_size = 1000;                       // Maximum history size for metrics
     
//      StrategyConfig() : strategy_id(""), version("1.0.0") {}
     
//      StrategyConfig(const core::StrategyId& id, const std::string& ver = "1.0.0")
//          : strategy_id(id), version(ver) {}
//  };
 
 } // namespace strategy
 } // namespace trading