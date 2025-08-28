/**
 * @file component_config.hpp
 * @brief Definition of component configuration class
 * 
 * @details
 * Part of the Strategy Engine for the Order Imbalance Trading System.
 * Stores configuration parameters for individual strategy components.
 * 
 * Created on: May 12, 2025
 */
#pragma once

#include <string>
#include <map>
#include <vector>
#include <unordered_map>

namespace trading {
namespace strategy {

/**
 * @class ComponentConfig
 * @brief Configuration for individual strategy components
 * 
 * Stores component-specific configuration parameters including
 * numeric, string, and boolean values, along with component state.
 */
class ComponentConfig {
public:
    /**
     * @brief Default constructor
     */
    ComponentConfig() : enabled(true) {}
    
    /**
     * @brief Check if the component is enabled
     * @return true if component is enabled, false otherwise
     */
    bool isEnabled() const { return enabled; }
    
    /**
     * @brief Set component enabled state
     * @param isEnabled Whether the component should be enabled
     */
    void setEnabled(bool isEnabled) { enabled = isEnabled; }
    
    /**
     * @brief Get a numeric parameter value
     * @param name Parameter name
     * @param defaultValue Default value to return if parameter not found
     * @return Parameter value or default if not found
     */
    double getParameter(const std::string& name, double defaultValue = 0.0) const {
        auto it = parameters.find(name);
        return (it != parameters.end()) ? it->second : defaultValue;
    }
    
    /**
     * @brief Set a numeric parameter value
     * @param name Parameter name
     * @param value Parameter value
     */
    void setParameter(const std::string& name, double value) {
        parameters[name] = value;
    }
    
    /**
     * @brief Get a string parameter value
     * @param name Parameter name
     * @param defaultValue Default value to return if parameter not found
     * @return Parameter value or default if not found
     */
    std::string getStringParameter(const std::string& name, const std::string& defaultValue = "") const {
        auto it = stringParameters.find(name);
        return (it != stringParameters.end()) ? it->second : defaultValue;
    }
    
    /**
     * @brief Set a string parameter value
     * @param name Parameter name
     * @param value Parameter value
     */
    void setStringParameter(const std::string& name, const std::string& value) {
        stringParameters[name] = value;
    }
    
    /**
     * @brief Get a boolean parameter value
     * @param name Parameter name
     * @param defaultValue Default value to return if parameter not found
     * @return Parameter value or default if not found
     */
    bool getBoolParameter(const std::string& name, bool defaultValue = false) const {
        auto it = boolParameters.find(name);
        return (it != boolParameters.end()) ? it->second : defaultValue;
    }
    
    /**
     * @brief Set a boolean parameter value
     * @param name Parameter name
     * @param value Parameter value
     */
    void setBoolParameter(const std::string& name, bool value) {
        boolParameters[name] = value;
    }
    
    /**
     * @brief Check if a parameter exists
     * @param name Parameter name
     * @return true if parameter exists, false otherwise
     */
    bool hasParameter(const std::string& name) const {
        return parameters.find(name) != parameters.end();
    }
    
    /**
     * @brief Check if a string parameter exists
     * @param name Parameter name
     * @return true if parameter exists, false otherwise
     */
    bool hasStringParameter(const std::string& name) const {
        return stringParameters.find(name) != stringParameters.end();
    }
    
    /**
     * @brief Check if a boolean parameter exists
     * @param name Parameter name
     * @return true if parameter exists, false otherwise
     */
    bool hasBoolParameter(const std::string& name) const {
        return boolParameters.find(name) != boolParameters.end();
    }
    
    /**
     * @brief Get all numeric parameters
     * @return Map of parameter names to values
     */
    const std::unordered_map<std::string, double>& getParameters() const {
        return parameters;
    }
    
    /**
     * @brief Get all string parameters
     * @return Map of parameter names to values
     */
    const std::unordered_map<std::string, std::string>& getStringParameters() const {
        return stringParameters;
    }
    
    /**
     * @brief Get all boolean parameters
     * @return Map of parameter names to values
     */
    const std::unordered_map<std::string, bool>& getBoolParameters() const {
        return boolParameters;
    }
    
    /**
     * @brief Clear all parameters
     */
    void clear() {
        parameters.clear();
        stringParameters.clear();
        boolParameters.clear();
    }
    
    /**
     * @brief Merge another ComponentConfig into this one
     * @param other The other ComponentConfig to merge from
     * @param overwrite Whether to overwrite existing parameters
     */
    void merge(const ComponentConfig& other, bool overwrite = true) {
        // Merge numeric parameters
        for (const auto& [key, value] : other.parameters) {
            if (overwrite || !hasParameter(key)) {
                parameters[key] = value;
            }
        }
        
        // Merge string parameters
        for (const auto& [key, value] : other.stringParameters) {
            if (overwrite || !hasStringParameter(key)) {
                stringParameters[key] = value;
            }
        }
        
        // Merge boolean parameters
        for (const auto& [key, value] : other.boolParameters) {
            if (overwrite || !hasBoolParameter(key)) {
                boolParameters[key] = value;
            }
        }
    }

    // Component enabled/disabled state
    bool enabled;
    
    // Parameter maps
    std::unordered_map<std::string, double> parameters;
    std::unordered_map<std::string, std::string> stringParameters;
    std::unordered_map<std::string, bool> boolParameters;
};

} // namespace strategy
} // namespace trading