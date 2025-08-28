// strategy_variant_utils.hpp
#pragma once

#include "strategy/types.hpp"
// #include "strategy/parameter_set.hpp"
#include <map>
#include <set>
#include <string>

namespace trading {
namespace strategy {

/**
 * Apply a strategy variant to a parameter set
 * 
 * @param parameterSet Base parameter set to modify
 * @param variant Strategy variant with parameter overrides
 * @return Modified parameter set
 */
ParameterSet applyVariant(const ParameterSet& parameterSet, const StrategyVariant& variant);

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
                             const std::map<std::string, double>& parameterChanges);

/**
 * Create a default variant with no parameter changes
 * 
 * @param variantId Unique variant identifier
 * @return Default strategy variant
 */
StrategyVariant createDefaultVariant(const std::string& variantId);

/**
 * Get a string representation of a variant for logging
 * 
 * @param variant Strategy variant to represent
 * @return String representation
 */
std::string variantToString(const StrategyVariant& variant);

/**
 * Calculate the difference between two variants
 * 
 * @param baseVariant Base variant
 * @param compareVariant Variant to compare against base
 * @return Map of parameter name to percentage difference
 */
std::map<std::string, double> calculateVariantDifference(
    const StrategyVariant& baseVariant,
    const StrategyVariant& compareVariant);

/**
 * Calculate statistical significance between two variants
 * 
 * @param variant1 First variant
 * @param variant2 Second variant
 * @param metricName Name of the metric to compare
 * @return p-value of the comparison (lower is more significant)
 */
double calculateStatisticalSignificance(
    const VariantResult& variant1,
    const VariantResult& variant2,
    const std::string& metricName = "sharpeRatio");

/**
 * Generate a detailed report for experiment results
 * 
 * @param results Experiment results
 * @return String containing the detailed report
 */
std::string generateExperimentReport(const ExperimentResults& results);

/**
 * Create a JSON representation of experiment results for API responses
 * 
 * @param results Experiment results
 * @return JSON string representation
 */
std::string resultsToJson(const ExperimentResults& results);

} // namespace strategy
} // namespace trading