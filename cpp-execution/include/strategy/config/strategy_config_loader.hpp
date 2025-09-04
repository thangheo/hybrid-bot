/**
 * @file strategy_config_loader.hpp
 * @brief Header for strategy configuration loading utilities
 * 
 * Created on: May 26, 2025
 */
#pragma once

#include "strategy/types.hpp"
#include "core/market_types.hpp"
#include <string>
#include <map>

namespace trading {
namespace strategy {

/**
 * @brief Load strategy configuration from a JSON file
 * 
 * @param filePath Path to the JSON configuration file
 * @return StrategyConfig Configuration structure
 * @throws std::runtime_error if file cannot be read or has invalid format
 */
StrategyConfig loadStrategyConfigFromFile(const std::string& filePath);

/**
 * @brief Save strategy configuration to a JSON file
 * 
 * @param config Configuration to save
 * @param filePath Path to the output JSON file
 * @return bool True if successful, false otherwise
 */
bool saveStrategyConfigToFile(const StrategyConfig& config, const std::string& filePath);

/**
 * @brief Load exchange-specific parameter sets
 * 
 * @param filePath Path to the JSON file containing exchange-specific parameters
 * @return std::map<core::ExchangeId, ParameterSet> Map of exchange ID to parameter set
 */
std::map<core::ExchangeId, ParameterSet> loadExchangeSpecificParameters(const std::string& filePath);

} // namespace strategy
} // namespace trading