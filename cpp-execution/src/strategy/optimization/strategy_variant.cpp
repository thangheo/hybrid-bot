// strategy_variant.cpp
#include "strategy/types.hpp"
#include <sstream>
#include <iostream>
#include <iomanip>
#include <map>
#include <set>

namespace trading {
namespace strategy {

/**
 * Apply a strategy variant to a parameter set
 * 
 * @param parameterSet Base parameter set to modify
 * @param variant Strategy variant with parameter overrides
 * @return Modified parameter set
 */
ParameterSet applyVariant(const ParameterSet& parameterSet, const StrategyVariant& variant) {
    ParameterSet result = parameterSet;
    
    // Apply all parameter overrides from the variant
    for (const auto& param : variant.parameters) {
        result.setParameter(param.first, param.second);
    }
    
    return result;
}

/**
 * Create a strategy variant from an experiment configuration
 * 
 * @param variantId Unique variant identifier
 * @param description Human-readable description
 * @param parameterChanges Map of parameter changes to apply
 * @return Configured strategy variant
 */
StrategyVariant createVariant(const std::string& variantId, 
                             const std::string& description,
                             const std::map<std::string, double>& parameterChanges) {
    StrategyVariant variant(variantId, description);
    
    for (const auto& param : parameterChanges) {
        variant.setParameter(param.first, param.second);
    }
    
    return variant;
}

/**
 * Create a default variant with no parameter changes
 * 
 * @param variantId Unique variant identifier
 * @return Default strategy variant
 */
StrategyVariant createDefaultVariant(const std::string& variantId) {
    return StrategyVariant(variantId, "Default variant with no parameter changes");
}

/**
 * Get a string representation of a variant for logging
 * 
 * @param variant Strategy variant to represent
 * @return String representation
 */
std::string variantToString(const StrategyVariant& variant) {
    std::stringstream ss;
    ss << "Variant[" << variant.variantId << "]: " << variant.description << " (";
    
    bool first = true;
    for (const auto& param : variant.parameters) {
        if (!first) {
            ss << ", ";
        }
        ss << param.first << "=" << param.second;
        first = false;
    }
    
    ss << ")";
    return ss.str();
}

/**
 * Calculate the difference between two variants
 * 
 * @param baseVariant Base variant
 * @param compareVariant Variant to compare against base
 * @return Map of parameter name to percentage difference
 */
std::map<std::string, double> calculateVariantDifference(
    const StrategyVariant& baseVariant,
    const StrategyVariant& compareVariant) {
    
    std::map<std::string, double> differences;
    
    // Find all parameters in both variants
    std::set<std::string> allParams;
    for (const auto& param : baseVariant.parameters) {
        allParams.insert(param.first);
    }
    for (const auto& param : compareVariant.parameters) {
        allParams.insert(param.first);
    }
    
    // Calculate differences
    for (const auto& paramName : allParams) {
        double baseValue = baseVariant.getParameter(paramName, 0.0);
        double compareValue = compareVariant.getParameter(paramName, 0.0);
        
        if (baseValue != 0.0) {
            // Calculate percentage difference
            differences[paramName] = (compareValue - baseValue) / baseValue * 100.0;
        } else if (compareValue != 0.0) {
            // Base is zero, can't calculate percentage
            differences[paramName] = compareValue > 0 ? 100.0 : -100.0;
        } else {
            // Both are zero
            differences[paramName] = 0.0;
        }
    }
    
    return differences;
}

} // namespace strategy
} // namespace trading