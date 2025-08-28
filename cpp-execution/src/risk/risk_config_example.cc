/**
 * Example usage of the Risk Configuration
 *
 * File: risk_config_example.cpp
 * Created: 2025-05-05
 */

 #include "risk_config.hpp"
 #include <iostream>
 
 int main() {
     // Create a default configuration
     risk::RiskConfig config;
     
     // Modify some settings
     risk::PositionLimitConfig posLimits = config.getPositionLimitConfig();
     posLimits.defaultLimit = 2000.0;
     posLimits.assetLimits["BTC"] = 5.0;
     posLimits.assetLimits["ETH"] = 50.0;
     config.setPositionLimitConfig(posLimits);
     
     risk::StopLossConfig stopLoss = config.getStopLossConfig();
     stopLoss.maxLossPercentage = 0.03;  // 3% max loss
     stopLoss.enableTrailingStop = true;
     config.setStopLossConfig(stopLoss);
     
     // Save configuration to file
     if (config.saveToFile("risk_config.json")) {
         std::cout << "Configuration saved successfully" << std::endl;
     } else {
         std::cerr << "Failed to save configuration" << std::endl;
         return 1;
     }
     
     // Load configuration from file
     risk::RiskConfig loadedConfig;
     if (loadedConfig.loadFromFile("risk_config.json")) {
         std::cout << "Configuration loaded successfully" << std::endl;
     } else {
         std::cerr << "Failed to load configuration" << std::endl;
         return 1;
     }
     
     // Check loaded values
     const auto& loadedPosLimits = loadedConfig.getPositionLimitConfig();
     std::cout << "Default position limit: " << loadedPosLimits.defaultLimit << std::endl;
     std::cout << "BTC position limit: " << loadedPosLimits.assetLimits.at("BTC") << std::endl;
     std::cout << "ETH position limit: " << loadedPosLimits.assetLimits.at("ETH") << std::endl;
     
     const auto& loadedStopLoss = loadedConfig.getStopLossConfig();
     std::cout << "Max loss percentage: " << loadedStopLoss.maxLossPercentage << std::endl;
     std::cout << "Trailing stop enabled: " << (loadedStopLoss.enableTrailingStop ? "Yes" : "No") << std::endl;
     
     // Try to load a non-existent file
     risk::RiskConfig invalidConfig;
     if (!invalidConfig.loadFromFile("non_existent_file.json")) {
         std::cout << "Correctly handled non-existent file" << std::endl;
     }
     
     // Try loading with constructor
     try {
         risk::RiskConfig configFromFile("risk_config.json");
         std::cout << "Successfully loaded configuration in constructor" << std::endl;
     } catch (const std::exception& e) {
         std::cerr << "Exception while loading: " << e.what() << std::endl;
         return 1;
     }
     
     // Try loading invalid configuration
     try {
         risk::RiskConfig configFromFile("non_existent_file.json");
         std::cerr << "Should have thrown an exception for non-existent file" << std::endl;
         return 1;
     } catch (const risk::ConfigurationException& e) {
         std::cout << "Correctly caught configuration exception: " << e.what() << std::endl;
     }
     
     return 0;
 }