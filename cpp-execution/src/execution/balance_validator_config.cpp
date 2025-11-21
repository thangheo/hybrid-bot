#include "execution/balance_validator_config.hpp"
#include <fstream>
#include <stdexcept>
#include <iostream>

namespace trading {
namespace execution {

BalanceValidatorConfig BalanceValidatorConfigLoader::loadFromFile(const std::string& configPath) {
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            std::cerr << "Warning: Cannot open balance validator config file: " << configPath 
                      << ". Using default configuration." << std::endl;
            return createDefault();
        }
        
        nlohmann::json j;
        file >> j;
        return parseJson(j);
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading balance validator config from " << configPath 
                  << ": " << e.what() << ". Using default configuration." << std::endl;
        return createDefault();
    }
}

BalanceValidatorConfig BalanceValidatorConfigLoader::loadFromString(const std::string& jsonString) {
    try {
        nlohmann::json j = nlohmann::json::parse(jsonString);
        return parseJson(j);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse balance validator configuration: " + std::string(e.what()));
    }
}

BalanceValidatorConfig BalanceValidatorConfigLoader::createDefault() {
    BalanceValidatorConfig config;
    
    config.defaultTradingFee = 0.001; // 0.1%
    config.exchangeTradingFees = {
        {"binance", 0.001},
        {"ftx", 0.0007},
        {"coinbase", 0.005},
        {"kraken", 0.0026}
    };
    config.balanceBufferPercent = 0.01; // 1%
    config.enabled = true;
    config.balanceCacheExpiry = std::chrono::milliseconds(20); // 20 milliseconds
    config.enableRealTimeValidation = true;
    config.skipBalanceForExits = false;
    config.minNotionalValue = 10.0; // $10 minimum
    config.safetyMarginPercent = 0.001; // 0.1% default safety margin
    
    return config;
}

void BalanceValidatorConfigLoader::saveToFile(const BalanceValidatorConfig& config, const std::string& configPath) {
    try {
        nlohmann::json j = configToJson(config);
        
        std::ofstream file(configPath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open file for writing: " + configPath);
        }
        
        file << j.dump(2); // Pretty print with 2 spaces
        
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to save balance validator config to " + configPath + ": " + e.what());
    }
}

BalanceValidatorConfig BalanceValidatorConfigLoader::parseJson(const nlohmann::json& j) {
    BalanceValidatorConfig config = createDefault();
    
    try {
        if (j.contains("balanceValidation")) {
            const auto& bv = j["balanceValidation"];
            
            if (bv.contains("defaultTradingFee")) {
                config.defaultTradingFee = bv["defaultTradingFee"].get<double>();
            }
            
            if (bv.contains("exchangeTradingFees")) {
                config.exchangeTradingFees.clear();
                for (const auto& [exchange, fee] : bv["exchangeTradingFees"].items()) {
                    config.exchangeTradingFees[exchange] = fee.get<double>();
                }
            }
            
            if (bv.contains("balanceBufferPercent")) {
                config.balanceBufferPercent = bv["balanceBufferPercent"].get<double>();
            }

            if (bv.contains("safetyMarginPercent")) {
                config.safetyMarginPercent = bv["safetyMarginPercent"].get<double>();
            }
            
            if (bv.contains("balanceCacheExpiryMs")) {
                int expiry = bv["balanceCacheExpiryMs"].get<int>();
                config.balanceCacheExpiry = std::chrono::milliseconds(expiry);
            }
            
            if (bv.contains("enableRealTimeValidation")) {
                config.enableRealTimeValidation = bv["enableRealTimeValidation"].get<bool>();
            }

            if (bv.contains("skipBalanceForExits")) {
                config.skipBalanceForExits = bv["skipBalanceForExits"].get<bool>();
            }

            if (bv.contains("enabled")) {
                config.enabled = bv["enabled"].get<bool>();
            }
            
            if (bv.contains("minNotionalValue")) {
                config.minNotionalValue = bv["minNotionalValue"].get<double>();
            }
        }
        
        // Validate configuration values
        if (config.defaultTradingFee < 0.0 || config.defaultTradingFee > 0.1) {
            std::cerr << "Warning: Invalid defaultTradingFee (" << config.defaultTradingFee 
                      << "). Using 0.001" << std::endl;
            config.defaultTradingFee = 0.001;
        }
        
        if (config.balanceBufferPercent < 0.0 || config.balanceBufferPercent > 0.1) {
            std::cerr << "Warning: Invalid balanceBufferPercent (" << config.balanceBufferPercent 
                      << "). Using 0.01" << std::endl;
            config.balanceBufferPercent = 0.01;
        }

        if (config.safetyMarginPercent < 0.0 || config.safetyMarginPercent > 0.01) {
            std::cerr << "Warning: Invalid safetyMarginPercent (" << config.safetyMarginPercent 
                      << "). Using 0.001" << std::endl;
            config.safetyMarginPercent = 0.001;
        }
        
        if (config.minNotionalValue < 0.0) {
            std::cerr << "Warning: Invalid minNotionalValue (" << config.minNotionalValue 
                      << "). Using 10.0" << std::endl;
            config.minNotionalValue = 10.0;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error parsing balance validator configuration: " << e.what() 
                  << ". Using defaults for invalid fields." << std::endl;
    }
    
    return config;
}

nlohmann::json BalanceValidatorConfigLoader::configToJson(const BalanceValidatorConfig& config) {
    nlohmann::json j;
    
    j["balanceValidation"]["defaultTradingFee"] = config.defaultTradingFee;
    j["balanceValidation"]["exchangeTradingFees"] = config.exchangeTradingFees;
    j["balanceValidation"]["balanceBufferPercent"] = config.balanceBufferPercent;
    j["balanceValidation"]["safetyMarginPercent"] = config.safetyMarginPercent;
    j["balanceValidation"]["enabled"] = config.enabled;
    j["balanceValidation"]["balanceCacheExpiryMs"] = config.balanceCacheExpiry.count();
    j["balanceValidation"]["enableRealTimeValidation"] = config.enableRealTimeValidation;
    j["balanceValidation"]["skipBalanceForExits"] = config.skipBalanceForExits;
    j["balanceValidation"]["minNotionalValue"] = config.minNotionalValue;
    
    return j;
}

} // namespace execution
} // namespace trading