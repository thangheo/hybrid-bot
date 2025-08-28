// adaptation_rules.hpp
#pragma once

#include <string>
#include <vector>
#include <map>

namespace trading {
namespace strategy {

/**
 * Structure to define an adaptation rule for a specific parameter
 */
struct ParameterRule {
    std::string parameterName;  // Name of the parameter
    double minValue;            // Minimum allowed value
    double maxValue;            // Maximum allowed value
    double defaultValue;        // Default value
    double stepSize;            // Minimum step size for adjustments
    
    ParameterRule() : minValue(0.0), maxValue(0.0), defaultValue(0.0), stepSize(0.0) {}
    
    ParameterRule(const std::string& name, double min, double max, double def, double step) 
        : parameterName(name), minValue(min), maxValue(max), defaultValue(def), stepSize(step) {}
};

/**
 * Structure to define rules for adapting to a specific market regime
 */
struct RegimeRule {
    std::string regime;                              // Market regime identifier
    std::map<std::string, double> parameterAdjustments;  // Parameter name to adjustment factor
    
    RegimeRule() {}
    RegimeRule(const std::string& r) : regime(r) {}
};

/**
 * Structure for anti-fragility rules
 */
struct AntifragilityRule {
    std::string parameterName;     // Parameter that affects system fragility
    double sensitivityFactor;      // How much this parameter affects fragility (0-1)
    
    AntifragilityRule() : sensitivityFactor(0.0) {}
    AntifragilityRule(const std::string& name, double factor) 
        : parameterName(name), sensitivityFactor(factor) {}
};

/**
 * Main structure for adaptation rules
 */
struct AdaptationRules {
    std::vector<ParameterRule> parameterRules;         // Rules for individual parameters
    std::vector<RegimeRule> regimeRules;              // Rules for market regimes
    std::map<std::string, std::map<std::string, double>> blendingSources;  // Sources for parameter blending
    std::vector<AntifragilityRule> antifragilityRules;  // Rules for anti-fragility
    
    // Default adaptation frequency in hours
    int adaptationFrequencyHours = 24;
    
    // Minimum performance change to trigger adaptation (percentage)
    double minPerformanceChangePercent = 5.0;
    
    // Maximum allowed parameter change in one adaptation step (percentage)
    double maxParameterChangePercent = 10.0;
};


} // namespace strategy
} // namespace trading