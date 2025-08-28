/**
 * @file analyzer_types.hpp
 * @brief Type definitions for analyzer components
 * 
 * @details
 * Defines types used by analyzer components in the strategy system.
 * 
 * Created on: May 13, 2025
 * Author: Trading System Team
 */
#pragma once

#include <string>
#include <map>

namespace trading {
namespace strategy {

/**
 * @enum AnalyzerType
 * @brief Enumeration of analyzer types
 */
enum class AnalyzerType {
    ORDER_BOOK_ANALYZER,
    IMBALANCE_DETECTOR,
    LIQUIDITY_FLOW_MONITOR,
    VOLUME_PROFILE_ANALYZER,
    CUSTOM_ANALYZER
};

/**
 * Convert AnalyzerType to string representation
 */
inline std::string analyzerTypeToString(AnalyzerType type) {
    switch (type) {
        case AnalyzerType::ORDER_BOOK_ANALYZER: return "order_book_analyzer";
        case AnalyzerType::IMBALANCE_DETECTOR: return "imbalance_detector";
        case AnalyzerType::LIQUIDITY_FLOW_MONITOR: return "liquidity_flow_monitor";
        case AnalyzerType::VOLUME_PROFILE_ANALYZER: return "volume_profile_analyzer";
        case AnalyzerType::CUSTOM_ANALYZER: return "custom_analyzer";
        default: return "unknown";
    }
}

/**
 * @struct AnalyzerConfig
 * @brief Configuration for analyzer components
 */
struct AnalyzerConfig {
    std::string analyzer_id;                // Unique identifier for the analyzer
    std::string version = "1.0";            // Version of the analyzer
    int depth = 10;                         // Depth to analyze
    int historySize = 1000;                 // Size of history to maintain
    double priceWeightFactor = 0.5;         // Weight factor for price-weighted metrics
    
    // Additional parameters
    std::map<std::string, double> parameters;    // Additional numeric parameters
    std::map<std::string, std::string> string_parameters; // Additional string parameters
    
    AnalyzerConfig() = default;
    
    // Helper to get parameter with default value
    double getParameter(const std::string& name, double default_value) const {
        auto it = parameters.find(name);
        if (it != parameters.end()) {
            return it->second;
        }
        return default_value;
    }
    
    // Helper to get string parameter with default value
    std::string getStringParameter(const std::string& name, const std::string& default_value) const {
        auto it = string_parameters.find(name);
        if (it != string_parameters.end()) {
            return it->second;
        }
        return default_value;
    }
};

} // namespace strategy
} // namespace trading