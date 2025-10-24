#pragma once

#include "execution/balance_validator.hpp"
#include <string>
#include <nlohmann/json.hpp>

namespace trading {
namespace execution {

/**
 * @brief Configuration loader for balance validator
 */
class BalanceValidatorConfigLoader {
public:
    /**
     * @brief Load configuration from JSON file
     * @param configPath Path to configuration file
     * @return Loaded configuration
     */
    static BalanceValidatorConfig loadFromFile(const std::string& configPath);
    
    /**
     * @brief Load configuration from JSON string
     * @param jsonString JSON configuration string
     * @return Loaded configuration
     */
    static BalanceValidatorConfig loadFromString(const std::string& jsonString);
    
    /**
     * @brief Create default configuration
     * @return Default configuration
     */
    static BalanceValidatorConfig createDefault();
    
    /**
     * @brief Save configuration to file
     * @param config Configuration to save
     * @param configPath Output file path
     */
    static void saveToFile(const BalanceValidatorConfig& config, const std::string& configPath);

private:
    static BalanceValidatorConfig parseJson(const nlohmann::json& j);
    static nlohmann::json configToJson(const BalanceValidatorConfig& config);
};

} // namespace execution
} // namespace trading