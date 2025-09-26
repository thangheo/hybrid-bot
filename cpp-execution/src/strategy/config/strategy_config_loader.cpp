/**
 * @file strategy_config_loader.cpp
 * @brief Utility functions for loading strategy configuration from files
 * 
 * Created on: May 5, 2025
 */

#include "strategy/engine/strategy_engine.hpp"
#include "strategy/types.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <stdexcept>
#include <filesystem>
#include "common/logger.hpp"

namespace trading {
namespace strategy {

using json = nlohmann::json;

// Forward declarations of parsing functions for specific config types
OrderBookAnalyzerConfig parseOrderBookAnalyzerConfig(const json& componentJson);
ImbalanceDetectionConfig parseImbalanceDetectionConfig(const json& componentJson);
LiquidityFlowConfig parseLiquidityFlowConfig(const json& componentJson);
VolumeProfileConfig parseVolumeProfileConfig(const json& componentJson);
SignalGeneratorConfig parseSignalGeneratorConfig(const json& componentJson);
MomentumCorrelationConfig parseMomentumCorrelationConfig(const json& componentJson);
ThresholdManagerConfig parseThresholdManagerConfig(const json& componentJson);
SignalDecayConfig parseSignalDecayConfig(const json& componentJson);
ExchangeCalibrationConfig parseExchangeCalibrationConfig(const json& componentJson);
AdaptiveMechanismConfig parseAdaptiveMechanismConfig(const json& componentJson);

// Forward declarations of serialization functions for specific config types
json orderBookAnalyzerConfigToJson(const OrderBookAnalyzerConfig& config);
json imbalanceDetectionConfigToJson(const ImbalanceDetectionConfig& config);
json liquidityFlowConfigToJson(const LiquidityFlowConfig& config);
json volumeProfileConfigToJson(const VolumeProfileConfig& config);
json signalGeneratorConfigToJson(const SignalGeneratorConfig& config);
json momentumCorrelationConfigToJson(const MomentumCorrelationConfig& config);
json thresholdManagerConfigToJson(const ThresholdManagerConfig& config);
json signalDecayConfigToJson(const SignalDecayConfig& config);
json exchangeCalibrationConfigToJson(const ExchangeCalibrationConfig& config);
json adaptiveMechanismConfigToJson(const AdaptiveMechanismConfig& config);

/**
 * @brief Load strategy configuration from a JSON file
 * 
 * @param filePath Path to the JSON configuration file
 * @return StrategyConfig Configuration structure
 * @throws std::runtime_error if file cannot be read or has invalid format
 */
StrategyConfig loadStrategyConfigFromFile(const std::string& filePath) {
    LOG_INFO("Loading strategy configuration from file: {}", filePath);
    
    // Check if file exists
    if (!std::filesystem::exists(filePath)) {
        LOG_ERROR("Configuration file not found: {}", filePath);
        throw std::runtime_error("Configuration file not found: " + filePath);
    }
    
    // Open the file
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR("Failed to open configuration file: {}", filePath);
        throw std::runtime_error("Failed to open configuration file: " + filePath);
    }
    
    try {
        // Parse JSON
        json configJson;
        file >> configJson;
        
        // Create config object
        StrategyConfig config;
        
        // Parse general settings
        config.strategy_id = configJson.value("strategy_id", "default_strategy");
        config.version = configJson.value("version", "1.0.0");
        
        // Parse trading pairs
        if (configJson.contains("tradingPairs") && configJson["tradingPairs"].is_array()) {
            for (const auto& pair : configJson["tradingPairs"]) {
                
                if (!pair.contains("symbol") || !pair.contains("exchange")) {
                    LOG_ERROR("Trading pair entry missing 'symbol' or 'exchange': {}", pair.dump());
                    throw std::runtime_error("Trading pair entry missing 'symbol' or 'exchange': " + pair.dump());
                }
                core::Symbol symbol = core::Symbol::fromString(pair["symbol"].get<std::string>());
                core::ExchangeId exchangeId = core::ExchangeId::fromString(pair["exchange"].get<std::string>());
                if (!exchangeId.isValid()) {
                    LOG_ERROR("Invalid exchange ID in trading pair: {}", pair["exchange"].get<std::string>());
                    throw std::runtime_error("Invalid exchange ID in trading pair: " + pair["exchange"].get<std::string>());
                }
                if (symbol.empty()) {
                    LOG_ERROR("Empty symbol in trading pair: {}", pair.dump());
                    throw std::runtime_error("Empty symbol in trading pair: " + pair.dump());
                }
                
                // ✅ Handle optional fields (category, description) - just log them for now
                if (pair.contains("category")) {
                    LOG_INFO("Trading pair {} has category: {}", symbol, pair["category"].get<std::string>());
                }
                if (pair.contains("description")) {
                    LOG_DEBUG("Trading pair {} description: {}", symbol, pair["description"].get<std::string>());
                }
                // std::string symbol = pair["symbol"];
                // std::string exchange = pair["exchange"];
                // config.tradingPairs[symbol] = exchange;
                config.tradingPairs[symbol] = exchangeId;
                // config.tradingPairs[core::Symbol::fromString(symbol)] = exchangeId; --- IGNORE ---
            }
        }
        
        // Parse default parameters
        if (configJson.contains("parameters") && configJson["parameters"].is_object()) {
            const auto& params = configJson["parameters"];
            
            // Parse numeric parameters
            if (params.contains("numeric") && params["numeric"].is_object()) {
                for (auto it = params["numeric"].begin(); it != params["numeric"].end(); ++it) {
                    config.defaultParameters.numeric_params[it.key()] = it.value().get<double>();
                }
            }
            
            // Parse string parameters
            if (params.contains("string") && params["string"].is_object()) {
                for (auto it = params["string"].begin(); it != params["string"].end(); ++it) {
                    config.defaultParameters.string_params[it.key()] = it.value().get<std::string>();
                }
            }
            
            // Parse boolean parameters
            if (params.contains("boolean") && params["boolean"].is_object()) {
                for (auto it = params["boolean"].begin(); it != params["boolean"].end(); ++it) {
                    config.defaultParameters.bool_params[it.key()] = it.value().get<bool>();
                }
            }
            
            // Parse array parameters
            if (params.contains("array") && params["array"].is_object()) {
                for (auto it = params["array"].begin(); it != params["array"].end(); ++it) {
                    if (it.value().is_array()) {
                        std::vector<double> values;
                        for (const auto& val : it.value()) {
                            values.push_back(val.get<double>());
                        }
                        config.defaultParameters.array_params[it.key()] = values;
                    }
                }
            }
        }
        
        // Parse component-specific configs
        if (configJson.contains("components") && configJson["components"].is_object()) {
            const auto& components = configJson["components"];
            
            // Parse OrderBookAnalyzer config
            if (components.contains("orderBookAnalyzer")) {
                config.orderBookAnalyzerConfig = parseOrderBookAnalyzerConfig(components["orderBookAnalyzer"]);
            }
            
            // Parse ImbalanceDetectionEngine config
            if (components.contains("imbalanceDetector")) {
                config.imbalanceDetectionConfig = parseImbalanceDetectionConfig(components["imbalanceDetector"]);
            }
            
            // Parse LiquidityFlowMonitor config
            if (components.contains("liquidityMonitor")) {
                config.liquidityFlowConfig = parseLiquidityFlowConfig(components["liquidityMonitor"]);
            }
            
            // Parse VolumeProfileAnalyzer config
            if (components.contains("volumeProfileAnalyzer")) {
                config.volumeProfileConfig = parseVolumeProfileConfig(components["volumeProfileAnalyzer"]);
            }
            
            // Parse ImbalanceSignalGenerator config
            if (components.contains("signalGenerator")) {
                config.signalGeneratorConfig = parseSignalGeneratorConfig(components["signalGenerator"]);
            }
            
            // Parse MomentumCorrelation config
            if (components.contains("momentumCorrelator")) {
                config.momentumCorrelationConfig = parseMomentumCorrelationConfig(components["momentumCorrelator"]);
            }
            
            // Parse ThresholdManager config
            if (components.contains("thresholdManager")) {
                config.thresholdManagerConfig = parseThresholdManagerConfig(components["thresholdManager"]);
            }
            
            // Parse SignalDecayModel config
            if (components.contains("signalDecayModel")) {
                config.signalDecayConfig = parseSignalDecayConfig(components["signalDecayModel"]);
            }
            
            // Parse ExchangeCalibration config
            if (components.contains("exchangeCalibration")) {
                config.exchangeCalibrationConfig = parseExchangeCalibrationConfig(components["exchangeCalibration"]);
            }
            
            // Parse AdaptiveMechanism config
            if (components.contains("adaptiveMechanism")) {
                config.adaptiveMechanismConfig = parseAdaptiveMechanismConfig(components["adaptiveMechanism"]);
            }
        }
        
        LOG_INFO("Successfully loaded strategy configuration: id={}, version={}, tradingPairs={}",
                       config.strategy_id.id, config.version, config.tradingPairs.size());

        return config;
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing strategy configuration file: {}", e.what());
        throw std::runtime_error("Error parsing strategy configuration file: " + std::string(e.what()));
    }
}

/**
 * @brief Parse OrderBookAnalyzer configuration from JSON
 * 
 * @param componentJson JSON object containing component configuration
 * @return OrderBookAnalyzerConfig Configuration for OrderBookAnalyzer
 */
OrderBookAnalyzerConfig parseOrderBookAnalyzerConfig(const json& componentJson) {
    OrderBookAnalyzerConfig config;
    
    // Parse parameters if present
    if (componentJson.contains("parameters") && componentJson["parameters"].is_object()) {
        const auto& params = componentJson["parameters"];
        
        // Parse specific fields
        if (params.contains("depth")) config.depth = params["depth"].get<int>();
        if (params.contains("historySize")) config.historySize = params["historySize"].get<int>();
        if (params.contains("priceWeightFactor")) config.priceWeightFactor = params["priceWeightFactor"].get<double>();
        if (params.contains("volumeWeightFactor")) config.volumeWeightFactor = params["volumeWeightFactor"].get<double>();
        if (params.contains("imbalanceThreshold")) config.imbalanceThreshold = params["imbalanceThreshold"].get<double>();
        if (params.contains("liquidityPressureThreshold")) config.liquidityPressureThreshold = params["liquidityPressureThreshold"].get<double>();
        if (params.contains("orderFlowThreshold")) config.orderFlowThreshold = params["orderFlowThreshold"].get<double>();
    }
    
    return config;
}

/**
 * @brief Parse ImbalanceDetection configuration from JSON
 * 
 * @param componentJson JSON object containing component configuration
 * @return ImbalanceDetectionConfig Configuration for ImbalanceDetection
 */
ImbalanceDetectionConfig parseImbalanceDetectionConfig(const json& componentJson) {
    ImbalanceDetectionConfig config;
    
    // Parse basic properties
    if (componentJson.contains("enabled")) {
        // Assuming there's an 'enabled' field in ImbalanceDetectionConfig (add it if needed)
        // config.enabled = componentJson["enabled"].get<bool>();
    }
    
    // Parse parameters if present
    if (componentJson.contains("parameters") && componentJson["parameters"].is_object()) {
        const auto& params = componentJson["parameters"];
        
        // Parse specific fields
        if (params.contains("strategy_id")) config.strategy_id = params["strategy_id"].get<std::string>(), config.version = "1.0";
        if (params.contains("version")) config.version = params["version"].get<std::string>();
        if (params.contains("volumeImbalanceThreshold")) config.volumeImbalanceThreshold = params["volumeImbalanceThreshold"].get<double>();
        if (params.contains("priceLevelImbalanceThreshold")) config.priceLevelImbalanceThreshold = params["priceLevelImbalanceThreshold"].get<double>();
        if (params.contains("liquidityRemovalThreshold")) config.liquidityRemovalThreshold = params["liquidityRemovalThreshold"].get<double>();
        if (params.contains("detectionWindowMs")) config.detectionWindowMs = params["detectionWindowMs"].get<int>();
        if (params.contains("maxHistorySize")) config.maxHistorySize = params["maxHistorySize"].get<int>();
        if (params.contains("global_sensitivity")) config.global_sensitivity = params["global_sensitivity"].get<double>();
        //Bayesian model path
        if (params.contains("bayesModelPath")) config.bayesModelPath = params["bayesModelPath"].get<std::string>();
    // Optional save interval in milliseconds (overrides default 1h if provided)
    if (params.contains("bayesSaveIntervalMs")) config.bayesSaveIntervalMs = params["bayesSaveIntervalMs"].get<int>();
    // Optional save-after-N-updates policy (0 disables)
    if (params.contains("bayesSaveAfterUpdates")) config.bayesSaveAfterUpdates = params["bayesSaveAfterUpdates"].get<int>();
        // Labeling delay in milliseconds for online training buffer
        if (params.contains("labelingDelayMs")) config.labelingDelayMs = params["labelingDelayMs"].get<int>();
        // Parse thresholds map
        if (params.contains("thresholds") && params["thresholds"].is_object()) {
            for (auto it = params["thresholds"].begin(); it != params["thresholds"].end(); ++it) {
                config.thresholds[it.key()] = it.value().get<double>();
            }
        }
        
        // Parse parameters and string_parameters maps
        for (auto it = params.begin(); it != params.end(); ++it) {
            // Skip fields we've already handled
            if (it.key() == "strategy_id" || it.key() == "version" || 
                it.key() == "volumeImbalanceThreshold" || it.key() == "priceLevelImbalanceThreshold" ||
                it.key() == "liquidityRemovalThreshold" || it.key() == "detectionWindowMs" ||
                it.key() == "maxHistorySize" || it.key() == "global_sensitivity" ||
                it.key() == "thresholds") {
                continue;
            }
            
            // Store as double or string parameter based on type
            if (it.value().is_number()) {
                config.parameters[it.key()] = it.value().get<double>();
            } else if (it.value().is_string()) {
                config.string_parameters[it.key()] = it.value().get<std::string>();
            }
        }
    }
    
    return config;
}

/**
 * @brief Parse LiquidityFlow configuration from JSON
 * 
 * @param componentJson JSON object containing component configuration
 * @return LiquidityFlowConfig Configuration for LiquidityFlow
 */
LiquidityFlowConfig parseLiquidityFlowConfig(const json& componentJson) {
    LiquidityFlowConfig config;
    
    // Parse parameters if present
    if (componentJson.contains("parameters") && componentJson["parameters"].is_object()) {
        const auto& params = componentJson["parameters"];
        
        // Parse specific fields
        if (params.contains("historySize")) config.historySize = params["historySize"].get<int>();
        if (params.contains("flowImbalanceThreshold")) config.flowImbalanceThreshold = params["flowImbalanceThreshold"].get<double>();
        if (params.contains("exchangeId")) config.exchangeId = ExchangeId::fromString(params["exchangeId"].get<std::string>());
        if (params.contains("tradingPair")) config.tradingPair = core::Symbol(params["tradingPair"].get<std::string>());
        if (params.contains("largeOrderThreshold")) config.largeOrderThreshold = params["largeOrderThreshold"].get<double>();
        if (params.contains("historyWindowSeconds")) config.historyWindowSeconds = params["historyWindowSeconds"].get<int>();
        if (params.contains("largeOrderVolume")) config.largeOrderVolume = params["largeOrderVolume"].get<double>();
        if (params.contains("cancellationRatio")) config.cancellationRatio = params["cancellationRatio"].get<double>();
        if (params.contains("pressureIndex")) config.pressureIndex = params["pressureIndex"].get<double>();
        if (params.contains("flowImbalance")) config.flowImbalance = params["flowImbalance"].get<double>();
        if (params.contains("typicalOrderSize")) config.typicalOrderSize = params["typicalOrderSize"].get<double>();
        
        // Parse params if present
        if (params.contains("params") && params["params"].is_object()) {
            // Parse numeric parameters
            if (params["params"].contains("numeric") && params["params"]["numeric"].is_object()) {
                for (auto it = params["params"]["numeric"].begin(); it != params["params"]["numeric"].end(); ++it) {
                    config.params.numeric_params[it.key()] = it.value().get<double>();
                }
            }
            
            // Parse string parameters
            if (params["params"].contains("string") && params["params"]["string"].is_object()) {
                for (auto it = params["params"]["string"].begin(); it != params["params"]["string"].end(); ++it) {
                    config.params.string_params[it.key()] = it.value().get<std::string>();
                }
            }
            
            // Parse boolean parameters
            if (params["params"].contains("boolean") && params["params"]["boolean"].is_object()) {
                for (auto it = params["params"]["boolean"].begin(); it != params["params"]["boolean"].end(); ++it) {
                    config.params.bool_params[it.key()] = it.value().get<bool>();
                }
            }
            
            // Parse array parameters
            if (params["params"].contains("array") && params["params"]["array"].is_object()) {
                for (auto it = params["params"]["array"].begin(); it != params["params"]["array"].end(); ++it) {
                    if (it.value().is_array()) {
                        std::vector<double> values;
                        for (const auto& val : it.value()) {
                            values.push_back(val.get<double>());
                        }
                        config.params.array_params[it.key()] = values;
                    }
                }
            }
        }
    }
    
    return config;
}

/**
 * @brief Parse VolumeProfile configuration from JSON
 * 
 * @param componentJson JSON object containing component configuration
 * @return VolumeProfileConfig Configuration for VolumeProfile
 */
VolumeProfileConfig parseVolumeProfileConfig(const json& componentJson) {
    VolumeProfileConfig config;
    
    // Parse parameters if present
    if (componentJson.contains("parameters") && componentJson["parameters"].is_object()) {
        const auto& params = componentJson["parameters"];
        
        // Parse specific fields
        if (params.contains("timeWindowMs")) config.timeWindowMs = params["timeWindowMs"].get<int>();
        if (params.contains("priceLevels")) config.priceLevels = params["priceLevels"].get<int>();
        if (params.contains("exchangeId")) config.exchangeId = ExchangeId::fromString(params["exchangeId"].get<std::string>());
        if (params.contains("tradingPair")) config.tradingPair = core::Symbol(params["tradingPair"].get<std::string>());
        if (params.contains("rangeSizePercent")) config.rangeSizePercent = params["rangeSizePercent"].get<double>();
        if (params.contains("timeFrameMinutes")) config.timeFrameMinutes = params["timeFrameMinutes"].get<int>();
        if (params.contains("numPriceRanges")) config.numPriceRanges = params["numPriceRanges"].get<int>();
        
        // Parse params if present
        if (params.contains("params") && params["params"].is_object()) {
            // Parse numeric parameters
            if (params["params"].contains("numeric") && params["params"]["numeric"].is_object()) {
                for (auto it = params["params"]["numeric"].begin(); it != params["params"]["numeric"].end(); ++it) {
                    config.params.numeric_params[it.key()] = it.value().get<double>();
                }
            }
            
            // Parse string parameters
            if (params["params"].contains("string") && params["params"]["string"].is_object()) {
                for (auto it = params["params"]["string"].begin(); it != params["params"]["string"].end(); ++it) {
                    config.params.string_params[it.key()] = it.value().get<std::string>();
                }
            }
            
            // Parse boolean parameters
            if (params["params"].contains("boolean") && params["params"]["boolean"].is_object()) {
                for (auto it = params["params"]["boolean"].begin(); it != params["params"]["boolean"].end(); ++it) {
                    config.params.bool_params[it.key()] = it.value().get<bool>();
                }
            }
            
            // Parse array parameters
            if (params["params"].contains("array") && params["params"]["array"].is_object()) {
                for (auto it = params["params"]["array"].begin(); it != params["params"]["array"].end(); ++it) {
                    if (it.value().is_array()) {
                        std::vector<double> values;
                        for (const auto& val : it.value()) {
                            values.push_back(val.get<double>());
                        }
                        config.params.array_params[it.key()] = values;
                    }
                }
            }
        }
    }
    
    return config;
}

/**
 * @brief Parse SignalGenerator configuration from JSON
 * 
 * @param componentJson JSON object containing component configuration
 * @return SignalGeneratorConfig Configuration for SignalGenerator
 */
SignalGeneratorConfig parseSignalGeneratorConfig(const json& componentJson) {
    SignalGeneratorConfig config;
    
    // Parse parameters if present
    if (componentJson.contains("parameters") && componentJson["parameters"].is_object()) {
        const auto& params = componentJson["parameters"];
        
        // Parse specific fields
        if (params.contains("minSignalStrength")) config.minSignalStrength = params["minSignalStrength"].get<double>();
        if (params.contains("confidenceThreshold")) config.confidenceThreshold = params["confidenceThreshold"].get<double>();
        if (params.contains("signalTtlMs")) config.signalTtlMs = params["signalTtlMs"].get<uint64_t>();
    }
    
    return config;
}

/**
 * @brief Parse MomentumCorrelation configuration from JSON
 * 
 * @param componentJson JSON object containing component configuration
 * @return MomentumCorrelationConfig Configuration for MomentumCorrelation
 */
MomentumCorrelationConfig parseMomentumCorrelationConfig(const json& componentJson) {
    MomentumCorrelationConfig config;
    
    // Parse parameters if present
    if (componentJson.contains("parameters") && componentJson["parameters"].is_object()) {
        const auto& params = componentJson["parameters"];
        
        // Parse specific fields
        if (params.contains("correlationWindowMs")) config.correlationWindowMs = params["correlationWindowMs"].get<int>();
        if (params.contains("predictionHorizonMs")) config.predictionHorizonMs = params["predictionHorizonMs"].get<int>();
    }
    
    return config;
}

/**
 * @brief Parse ThresholdManager configuration from JSON
 * 
 * @param componentJson JSON object containing component configuration
 * @return ThresholdManagerConfig Configuration for ThresholdManager
 */
ThresholdManagerConfig parseThresholdManagerConfig(const json& componentJson) {
    ThresholdManagerConfig config;
    
    // Parse parameters if present
    if (componentJson.contains("parameters") && componentJson["parameters"].is_object()) {
        const auto& params = componentJson["parameters"];
        
        // Parse specific fields
        if (params.contains("globalSensitivity")) config.globalSensitivity = params["globalSensitivity"].get<double>();
        if (params.contains("volatilityAdjustmentFactor")) config.volatilityAdjustmentFactor = params["volatilityAdjustmentFactor"].get<double>();
    }
    
    return config;
}

/**
 * @brief Parse SignalDecay configuration from JSON
 * 
 * @param componentJson JSON object containing component configuration
 * @return SignalDecayConfig Configuration for SignalDecay
 */
SignalDecayConfig parseSignalDecayConfig(const json& componentJson) {
    SignalDecayConfig config;
    
    // Parse parameters if present
    if (componentJson.contains("parameters") && componentJson["parameters"].is_object()) {
        const auto& params = componentJson["parameters"];
        
        // Parse specific fields
        if (params.contains("defaultDecayRate")) config.defaultDecayRate = params["defaultDecayRate"].get<double>();
        if (params.contains("minSignalStrength")) config.minSignalStrength = params["minSignalStrength"].get<double>();
    }
    
    return config;
}

/**
 * @brief Parse ExchangeCalibration configuration from JSON
 * 
 * @param componentJson JSON object containing component configuration
 * @return ExchangeCalibrationConfig Configuration for ExchangeCalibration
 */
ExchangeCalibrationConfig parseExchangeCalibrationConfig(const json& componentJson) {
    ExchangeCalibrationConfig config;
    
    // Parse parameters if present
    if (componentJson.contains("parameters") && componentJson["parameters"].is_object()) {
        const auto& params = componentJson["parameters"];
        
        // Parse specific fields
        if (params.contains("enableAutoCalibration")) config.enableAutoCalibration = params["enableAutoCalibration"].get<bool>();
        if (params.contains("calibrationIntervalMs")) config.calibrationIntervalMs = params["calibrationIntervalMs"].get<int>();
        if (params.contains("calibrationThreshold")) config.calibrationThreshold = params["calibrationThreshold"].get<double>();
        
        // Parse calibration parameters
        if (params.contains("calibrationParameters") && params["calibrationParameters"].is_object()) {
            for (auto it = params["calibrationParameters"].begin(); it != params["calibrationParameters"].end(); ++it) {
                config.calibrationParameters[it.key()] = it.value().get<double>();
            }
        }
        
        // Parse base parameters if present
        if (params.contains("baseParameters") && params["baseParameters"].is_object()) {
            // Parse numeric parameters
            if (params["baseParameters"].contains("numeric") && params["baseParameters"]["numeric"].is_object()) {
                for (auto it = params["baseParameters"]["numeric"].begin(); it != params["baseParameters"]["numeric"].end(); ++it) {
                    config.baseParameters.numeric_params[it.key()] = it.value().get<double>();
                }
            }
            
            // Parse string parameters
            if (params["baseParameters"].contains("string") && params["baseParameters"]["string"].is_object()) {
                for (auto it = params["baseParameters"]["string"].begin(); it != params["baseParameters"]["string"].end(); ++it) {
                    config.baseParameters.string_params[it.key()] = it.value().get<std::string>();
                }
            }
            
            // Parse boolean parameters
            if (params["baseParameters"].contains("boolean") && params["baseParameters"]["boolean"].is_object()) {
                for (auto it = params["baseParameters"]["boolean"].begin(); it != params["baseParameters"]["boolean"].end(); ++it) {
                    config.baseParameters.bool_params[it.key()] = it.value().get<bool>();
                }
            }
            
            // Parse array parameters
            if (params["baseParameters"].contains("array") && params["baseParameters"]["array"].is_object()) {
                for (auto it = params["baseParameters"]["array"].begin(); it != params["baseParameters"]["array"].end(); ++it) {
                    if (it.value().is_array()) {
                        std::vector<double> values;
                        for (const auto& val : it.value()) {
                            values.push_back(val.get<double>());
                        }
                        config.baseParameters.array_params[it.key()] = values;
                    }
                }
            }
        }
        
        // Parse exchanges
        if (params.contains("exchanges") && params["exchanges"].is_array()) {
            for (const auto& exchange : params["exchanges"]) {
                config.exchanges.push_back(ExchangeId::fromString(exchange.get<std::string>()));
            }
        }
    }
    
    return config;
}

/**
 * @brief Parse AdaptiveMechanism configuration from JSON
 * 
 * @param componentJson JSON object containing component configuration
 * @return AdaptiveMechanismConfig Configuration for AdaptiveMechanism
 */
AdaptiveMechanismConfig parseAdaptiveMechanismConfig(const json& componentJson) {
    AdaptiveMechanismConfig config;
    
    // Parse parameters if present
    if (componentJson.contains("parameters") && componentJson["parameters"].is_object()) {
        const auto& params = componentJson["parameters"];
        
        // Parse specific fields
        if (params.contains("enableAdaptation")) config.enableAdaptation = params["enableAdaptation"].get<bool>();
        if (params.contains("adaptationRate")) config.adaptationRate = params["adaptationRate"].get<double>();
        if (params.contains("adaptationIntervalMs")) config.adaptationIntervalMs = params["adaptationIntervalMs"].get<int>();
    }
    
    return config;
}

/**
 * @brief Save strategy configuration to a JSON file
 * 
 * @param config Configuration to save
 * @param filePath Path to the output JSON file
 * @return bool True if successful, false otherwise
 */
bool saveStrategyConfigToFile(const StrategyConfig& config, const std::string& filePath) {
    try {
        // Create JSON object
        json configJson;
        
        // General settings
        configJson["strategy_id"] = config.strategy_id.id;
        configJson["version"] = config.version;
        
        // Trading pairs
        json pairsJson = json::array();
        for (const auto& [symbol, exchangeId] : config.tradingPairs) {
            json pairJson;
            pairJson["symbol"] = symbol.toString();
            pairJson["exchange"] = exchangeId.toString();
            pairsJson.push_back(pairJson);
        }
        configJson["tradingPairs"] = pairsJson;
        
        // Parameters
        json paramsJson;
        
        // Numeric parameters
        json numericJson;
        for (const auto& [key, value] : config.defaultParameters.numeric_params) {
            numericJson[key] = value;
        }
        paramsJson["numeric"] = numericJson;
        
        // String parameters
        json stringJson;
        for (const auto& [key, value] : config.defaultParameters.string_params) {
            stringJson[key] = value;
        }
        paramsJson["string"] = stringJson;
        
        // Boolean parameters
        json boolJson;
        for (const auto& [key, value] : config.defaultParameters.bool_params) {
            boolJson[key] = value;
        }
        paramsJson["boolean"] = boolJson;
        
        // Array parameters
        json arrayJson;
        for (const auto& [key, value] : config.defaultParameters.array_params) {
            arrayJson[key] = value;
        }
        paramsJson["array"] = arrayJson;
        
        configJson["parameters"] = paramsJson;
        
        // Component configurations
        json componentsJson;
        
        // OrderBookAnalyzer
        componentsJson["orderBookAnalyzer"] = orderBookAnalyzerConfigToJson(config.orderBookAnalyzerConfig);
        
        // ImbalanceDetectionEngine
        componentsJson["imbalanceDetector"] = imbalanceDetectionConfigToJson(config.imbalanceDetectionConfig);
        
        // LiquidityFlowMonitor
        componentsJson["liquidityMonitor"] = liquidityFlowConfigToJson(config.liquidityFlowConfig);
        
        // VolumeProfileAnalyzer
        componentsJson["volumeProfileAnalyzer"] = volumeProfileConfigToJson(config.volumeProfileConfig);
        
        // ImbalanceSignalGenerator
        componentsJson["signalGenerator"] = signalGeneratorConfigToJson(config.signalGeneratorConfig);
        
        // MomentumCorrelation
        componentsJson["momentumCorrelator"] = momentumCorrelationConfigToJson(config.momentumCorrelationConfig);
        
        // ThresholdManager
        componentsJson["thresholdManager"] = thresholdManagerConfigToJson(config.thresholdManagerConfig);
        
        // SignalDecayModel
        componentsJson["signalDecayModel"] = signalDecayConfigToJson(config.signalDecayConfig);
        
        // ExchangeCalibration
        componentsJson["exchangeCalibration"] = exchangeCalibrationConfigToJson(config.exchangeCalibrationConfig);
        
        // AdaptiveMechanism
        componentsJson["adaptiveMechanism"] = adaptiveMechanismConfigToJson(config.adaptiveMechanismConfig);
        
        configJson["components"] = componentsJson;
        
        // Write to file
        std::ofstream file(filePath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open file for writing: {}", filePath);
            return false;
        }
        
        file << configJson.dump(4);  // Pretty print with 4 spaces
        file.close();
        
        LOG_INFO("Successfully saved strategy configuration to file: {}", filePath);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error saving strategy configuration: {}", e.what());
        return false;
    }
}

/**
 * @brief Convert OrderBookAnalyzerConfig to JSON
 * 
 * @param config Configuration for OrderBookAnalyzer
 * @return json JSON representation of the configuration
 */
json orderBookAnalyzerConfigToJson(const OrderBookAnalyzerConfig& config) {
    json componentJson;
    
    // Create parameters object
    json params;
    params["depth"] = config.depth;
    params["historySize"] = config.historySize;
    params["priceWeightFactor"] = config.priceWeightFactor;
    params["volumeWeightFactor"] = config.volumeWeightFactor;
    params["imbalanceThreshold"] = config.imbalanceThreshold;
    params["liquidityPressureThreshold"] = config.liquidityPressureThreshold;
    params["orderFlowThreshold"] = config.orderFlowThreshold;
    
    componentJson["parameters"] = params;
    
    return componentJson;
}

/**
 * @brief Convert ImbalanceDetectionConfig to JSON
 * 
 * @param config Configuration for ImbalanceDetection
 * @return json JSON representation of the configuration
 */
json imbalanceDetectionConfigToJson(const ImbalanceDetectionConfig& config) {
    json componentJson;
    
    // Create parameters object
    json params;
    params["strategy_id"] = config.strategy_id;
    params["version"] = config.version;
    params["volumeImbalanceThreshold"] = config.volumeImbalanceThreshold;
    params["priceLevelImbalanceThreshold"] = config.priceLevelImbalanceThreshold;
    params["liquidityRemovalThreshold"] = config.liquidityRemovalThreshold;
    params["detectionWindowMs"] = config.detectionWindowMs;
    params["maxHistorySize"] = config.maxHistorySize;
    params["global_sensitivity"] = config.global_sensitivity;
    
    // Add thresholds as a nested object
    json thresholdsJson;
    for (const auto& [key, value] : config.thresholds) {
        thresholdsJson[key] = value;
    }
    params["thresholds"] = thresholdsJson;
    
    // Add other parameters
    for (const auto& [key, value] : config.parameters) {
        params[key] = value;
    }
    
    // Add string parameters
    for (const auto& [key, value] : config.string_parameters) {
        params[key] = value;
    }
    
    componentJson["parameters"] = params;
    
    return componentJson;
}

/**
 * @brief Convert LiquidityFlowConfig to JSON
 * 
 * @param config Configuration for LiquidityFlow
 * @return json JSON representation of the configuration
 */
json liquidityFlowConfigToJson(const LiquidityFlowConfig& config) {
    json componentJson;
    
    // Create parameters object
    json params;
    params["historySize"] = config.historySize;
    params["flowImbalanceThreshold"] = config.flowImbalanceThreshold;
    params["exchangeId"] = config.exchangeId.toString();
    params["tradingPair"] = config.tradingPair.toString();
    params["largeOrderThreshold"] = config.largeOrderThreshold;
    params["historyWindowSeconds"] = config.historyWindowSeconds;
    params["largeOrderVolume"] = config.largeOrderVolume;
    params["cancellationRatio"] = config.cancellationRatio;
    params["pressureIndex"] = config.pressureIndex;
    params["flowImbalance"] = config.flowImbalance;
    params["typicalOrderSize"] = config.typicalOrderSize;
    
    // Add params as a nested object
    json paramsJson;
    
    // Numeric parameters
    json numericJson;
    for (const auto& [key, value] : config.params.numeric_params) {
        numericJson[key] = value;
    }
    paramsJson["numeric"] = numericJson;
    
    // String parameters
    json stringJson;
    for (const auto& [key, value] : config.params.string_params) {
        stringJson[key] = value;
    }
    paramsJson["string"] = stringJson;
    
    // Boolean parameters
    json boolJson;
    for (const auto& [key, value] : config.params.bool_params) {
        boolJson[key] = value;
    }
    paramsJson["boolean"] = boolJson;
    
    // Array parameters
    json arrayJson;
    for (const auto& [key, value] : config.params.array_params) {
        arrayJson[key] = value;
    }
    paramsJson["array"] = arrayJson;
    
    params["params"] = paramsJson;
    
    componentJson["parameters"] = params;
    
    return componentJson;
}

/**
 * @brief Convert VolumeProfileConfig to JSON
 * 
 * @param config Configuration for VolumeProfile
 * @return json JSON representation of the configuration
 */
json volumeProfileConfigToJson(const VolumeProfileConfig& config) {
    json componentJson;
    
    // Create parameters object
    json params;
    params["timeWindowMs"] = config.timeWindowMs;
    params["priceLevels"] = config.priceLevels;
    params["exchangeId"] = config.exchangeId.toString();
    params["tradingPair"] = config.tradingPair.toString();
    params["rangeSizePercent"] = config.rangeSizePercent;
    params["timeFrameMinutes"] = config.timeFrameMinutes;
    params["numPriceRanges"] = config.numPriceRanges;
    
    // Add params as a nested object
    json paramsJson;
    
    // Numeric parameters
    json numericJson;
    for (const auto& [key, value] : config.params.numeric_params) {
        numericJson[key] = value;
    }
    paramsJson["numeric"] = numericJson;
    
    // String parameters
    json stringJson;
    for (const auto& [key, value] : config.params.string_params) {
        stringJson[key] = value;
    }
    paramsJson["string"] = stringJson;
    
    // Boolean parameters
    json boolJson;
    for (const auto& [key, value] : config.params.bool_params) {
        boolJson[key] = value;
    }
    paramsJson["boolean"] = boolJson;
    
    // Array parameters
    json arrayJson;
    for (const auto& [key, value] : config.params.array_params) {
        arrayJson[key] = value;
    }
    paramsJson["array"] = arrayJson;
    
    params["params"] = paramsJson;
    
    componentJson["parameters"] = params;
    
    return componentJson;
}

/**
 * @brief Convert SignalGeneratorConfig to JSON
 * 
 * @param config Configuration for SignalGenerator
 * @return json JSON representation of the configuration
 */
json signalGeneratorConfigToJson(const SignalGeneratorConfig& config) {
    json componentJson;
    
    // Create parameters object
    json params;
    params["minSignalStrength"] = config.minSignalStrength;
    params["confidenceThreshold"] = config.confidenceThreshold;
    params["signalTtlMs"] = config.signalTtlMs;
    
    componentJson["parameters"] = params;
    
    return componentJson;
}

/**
 * @brief Convert MomentumCorrelationConfig to JSON
 * 
 * @param config Configuration for MomentumCorrelation
 * @return json JSON representation of the configuration
 */
json momentumCorrelationConfigToJson(const MomentumCorrelationConfig& config) {
    json componentJson;
    
    // Create parameters object
    json params;
    params["correlationWindowMs"] = config.correlationWindowMs;
    params["predictionHorizonMs"] = config.predictionHorizonMs;
    
    componentJson["parameters"] = params;
    
    return componentJson;
}

/**
 * @brief Convert ThresholdManagerConfig to JSON
 * 
 * @param config Configuration for ThresholdManager
 * @return json JSON representation of the configuration
 */
json thresholdManagerConfigToJson(const ThresholdManagerConfig& config) {
    json componentJson;
    
    // Create parameters object
    json params;
    params["globalSensitivity"] = config.globalSensitivity;
    params["volatilityAdjustmentFactor"] = config.volatilityAdjustmentFactor;
    
    componentJson["parameters"] = params;
    
    return componentJson;
}

/**
 * @brief Convert SignalDecayConfig to JSON
 * 
 * @param config Configuration for SignalDecay
 * @return json JSON representation of the configuration
 */
json signalDecayConfigToJson(const SignalDecayConfig& config) {
    json componentJson;
    
    // Create parameters object
    json params;
    params["defaultDecayRate"] = config.defaultDecayRate;
    params["minSignalStrength"] = config.minSignalStrength;
    
    componentJson["parameters"] = params;
    
    return componentJson;
}

/**
 * @brief Convert ExchangeCalibrationConfig to JSON
 * 
 * @param config Configuration for ExchangeCalibration
 * @return json JSON representation of the configuration
 */
json exchangeCalibrationConfigToJson(const ExchangeCalibrationConfig& config) {
    json componentJson;
    
    // Create parameters object
    json params;
    params["enableAutoCalibration"] = config.enableAutoCalibration;
    params["calibrationIntervalMs"] = config.calibrationIntervalMs;
    params["calibrationThreshold"] = config.calibrationThreshold;
    
    // Add calibration parameters
    json calibrationParamsJson;
    for (const auto& [key, value] : config.calibrationParameters) {
        calibrationParamsJson[key] = value;
    }
    params["calibrationParameters"] = calibrationParamsJson;
    
    // Add base parameters
    json baseParamsJson;
    
    // Numeric parameters
    json numericJson;
    for (const auto& [key, value] : config.baseParameters.numeric_params) {
        numericJson[key] = value;
    }
    baseParamsJson["numeric"] = numericJson;
    
    // String parameters
    json stringJson;
    for (const auto& [key, value] : config.baseParameters.string_params) {
        stringJson[key] = value;
    }
    baseParamsJson["string"] = stringJson;
    
    // Boolean parameters
    json boolJson;
    for (const auto& [key, value] : config.baseParameters.bool_params) {
        boolJson[key] = value;
    }
    baseParamsJson["boolean"] = boolJson;
    
    // Array parameters
    json arrayJson;
    for (const auto& [key, value] : config.baseParameters.array_params) {
        arrayJson[key] = value;
    }
    baseParamsJson["array"] = arrayJson;
    
    params["baseParameters"] = baseParamsJson;
    
    // Add exchanges
    json exchangesJson = json::array();
    for (const auto& exchange : config.exchanges) {
        exchangesJson.push_back(exchange.toString());
    }
    params["exchanges"] = exchangesJson;
    
    componentJson["parameters"] = params;
    
    return componentJson;
}

/**
 * @brief Convert AdaptiveMechanismConfig to JSON
 * 
 * @param config Configuration for AdaptiveMechanism
 * @return json JSON representation of the configuration
 */
json adaptiveMechanismConfigToJson(const AdaptiveMechanismConfig& config) {
    json componentJson;
    
    // Create parameters object
    json params;
    params["enableAdaptation"] = config.enableAdaptation;
    params["adaptationRate"] = config.adaptationRate;
    params["adaptationIntervalMs"] = config.adaptationIntervalMs;
    
    componentJson["parameters"] = params;
    
    return componentJson;
}

/**
 * @brief Load exchange-specific parameter sets
 * 
 * @param filePath Path to the JSON file containing exchange-specific parameters
 * @return std::map<ExchangeId, ParameterSet> Map of exchange ID to parameter set
 */
std::map<ExchangeId, ParameterSet> loadExchangeSpecificParameters(const std::string& filePath) {
    std::map<ExchangeId, ParameterSet> result;
    
    try {
        // Check if file exists
        if (!std::filesystem::exists(filePath)) {
            LOG_WARN("Exchange-specific parameters file not found: {}", filePath);
            return result;
        }
        
        // Open the file
        std::ifstream file(filePath);
        if (!file.is_open()) {
            LOG_ERROR("Failed to open exchange-specific parameters file: {}", filePath);
            return result;
        }
        
        // Parse JSON
        json paramsJson;
        file >> paramsJson;
        
        // Iterate through exchanges
        for (auto it = paramsJson.begin(); it != paramsJson.end(); ++it) {
            // Parse exchange ID
            ExchangeId exchangeId = ExchangeId::fromString(it.key());
            
            // Parse parameter set
            ParameterSet params;
            
            const auto& exchangeParams = it.value();
            
            // Parse numeric parameters
            if (exchangeParams.contains("numeric") && exchangeParams["numeric"].is_object()) {
                for (auto paramIt = exchangeParams["numeric"].begin(); paramIt != exchangeParams["numeric"].end(); ++paramIt) {
                    params.numeric_params[paramIt.key()] = paramIt.value().get<double>();
                }
            }
            
            // Parse string parameters
            if (exchangeParams.contains("string") && exchangeParams["string"].is_object()) {
                for (auto paramIt = exchangeParams["string"].begin(); paramIt != exchangeParams["string"].end(); ++paramIt) {
                    params.string_params[paramIt.key()] = paramIt.value().get<std::string>();
                }
            }
            
            // Parse boolean parameters
            if (exchangeParams.contains("boolean") && exchangeParams["boolean"].is_object()) {
                for (auto paramIt = exchangeParams["boolean"].begin(); paramIt != exchangeParams["boolean"].end(); ++paramIt) {
                    params.bool_params[paramIt.key()] = paramIt.value().get<bool>();
                }
            }
            
            // Parse array parameters
            if (exchangeParams.contains("array") && exchangeParams["array"].is_object()) {
                for (auto paramIt = exchangeParams["array"].begin(); paramIt != exchangeParams["array"].end(); ++paramIt) {
                    if (paramIt.value().is_array()) {
                        std::vector<double> values;
                        for (const auto& val : paramIt.value()) {
                            values.push_back(val.get<double>());
                        }
                        params.array_params[paramIt.key()] = values;
                    }
                }
            }
            
            // Add to result map
            result[exchangeId] = params;
        }
        
        LOG_INFO("Loaded exchange-specific parameters for {} exchanges", result.size());
        return result;
    } catch (const std::exception& e) {
        LOG_ERROR("Error loading exchange-specific parameters: {}", e.what());
        return result;
    }
}

} // namespace strategy
} // namespace trading