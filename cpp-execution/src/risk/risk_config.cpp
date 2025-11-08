/**
 * @file risk_config.cpp
 * @brief Implementation of Risk configuration with enhanced CircuitBreaker support
 * 
 * Modern C++20 implementation with robust JSON handling and validation.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#include "risk/risk_config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <chrono>
#include <cmath>

// Use a fully qualified namespace to avoid ambiguity
// using json = nlohmann::json;

namespace trading::risk {
    
// Default constructor with reasonable default values
RiskConfig::RiskConfig() {
    // Initialize position limit config
    positionLimitConfig_.defaultLimit = 1000.0;
    positionLimitConfig_.enforceStrictly = true;

    // Initialize exposure limit config
    exposureLimitConfig_.maxTotalExposure = 10000.0;
    exposureLimitConfig_.maxAssetExposure = exposureLimitConfig_.maxTotalExposure/2;
    exposureLimitConfig_.maxStrategyExposure = 10000;
    exposureLimitConfig_.maxExchangeExposure = 10000.0;
    exposureLimitConfig_.volatilityMultiplier = 1.5;
    exposureLimitConfig_.includeUnrealizedPnl = true;

    // Initialize enhanced circuit breaker config
    circuitBreakerConfig_ = CircuitBreakerConfig{}; // Uses default constructor

    // Add additional custom thresholds if needed
    circuitBreakerConfig_.metricThresholds["pnl_drawdown"] = MetricThreshold(0.03, 0.05, true);  // 3% warning, 5% critical
    circuitBreakerConfig_.metricThresholds["order_rejection_rate"] = MetricThreshold(0.02, 0.05, false); // 2% warning, 5% critical

    // Initialize stop loss config
    stopLossConfig_.maxLossPercentage = 0.05;
    stopLossConfig_.trailingStopDistance = 0.02;
    stopLossConfig_.enableTrailingStop = true;
    stopLossConfig_.forceLiquidation = true;
    stopLossConfig_.liquidationDelay = std::chrono::seconds(5);

    // Initialize drawdown config
    drawdownConfig_.warningThreshold = 0.10;
    drawdownConfig_.criticalThreshold = 0.20;
    drawdownConfig_.lookbackPeriod = std::chrono::hours(24);
    drawdownConfig_.enableAutoRiskReduction = true;
    drawdownConfig_.riskReductionFactor = 0.5;

    // Initialize fat finger config
    fatFingerConfig_.maxPriceDeviation = 0.05;
    fatFingerConfig_.maxOrderSize = 10.0;
    fatFingerConfig_.typicalOrderSize = 1.0;
    fatFingerConfig_.notionalValueLimit = 10000.0;
    fatFingerConfig_.enforceAdditionalVerification = true;

    // Initialize order validation config
    orderValidationConfig_.validateMinOrderSize = true;
    orderValidationConfig_.validatePriceRange = true;
    orderValidationConfig_.maxPriceDeviationPercent = 0.1;
    orderValidationConfig_.enforceSymbolWhitelist = false;

    // Initialize balance reconciliation config with defaults
    balanceReconciliationConfig_ = BalanceReconciliationConfig{}; // Uses default constructor

    // Initialize rebalancing config
    rebalancingConfig_.autoRebalancingEnabled = true;
    rebalancingConfig_.autoExecuteRebalancing = true;
    rebalancingConfig_.parallelExecution = false;
    rebalancingConfig_.rebalancingIntervalMinutes = 60;
    rebalancingConfig_.allocationDeviationThreshold = 0.05; // 5%
    rebalancingConfig_.minimumOrderSize = 0.01;

    // Initialize collateral config
    collateralConfig_.fundingSources = {"main_wallet", "backup_wallet"};
    collateralConfig_.emergencyFundingSource = "main_wallet";
    collateralConfig_.targetFreeCollateralRatio = 0.2;
    collateralConfig_.enableEmergencyFunding = false;
    collateralConfig_.maxFundingAmount = 10000.0;

    // Initialize portfolio config
    portfolioConfig_.autoRebalancingEnabled = false;
    portfolioConfig_.enableRealTimeMetrics = true;
    portfolioConfig_.minimumPortfolioValue = 1000.0;
    portfolioConfig_.baseCurrency = "USD";

    // Initialize logging config
    loggingConfig_.logLevel = "INFO";
    loggingConfig_.logDirectory = "logs/riskmanagement";
    loggingConfig_.enableDetailedLogs = true;
    loggingConfig_.logToConsole = true;
    loggingConfig_.logToFile = true;
    loggingConfig_.logRetentionPeriod = std::chrono::hours(30 * 24); // 30 days
}

// Constructor with file path
RiskConfig::RiskConfig(const std::string& configFilePath) : RiskConfig() {
    if (!loadFromFile(configFilePath)) {
        throw ConfigurationException("Failed to load configuration from file: " + configFilePath);
    }
}

// Load configuration from file
bool RiskConfig::loadFromFile(const std::string& configFilePath) {
    try {
        std::ifstream file(configFilePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open configuration file: " << configFilePath << "\n";
            return false;
        }
        
        std::string jsonStr((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();
        
        return loadJsonConfiguration(jsonStr);
    } catch (const std::exception& e) {
        std::cerr << "Exception while loading configuration: " << e.what() << "\n";
        return false;
    }
}

// Enhanced JSON configuration parsing
bool RiskConfig::loadJsonConfiguration(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        // Load position limit config
        if (j.contains("positionLimit")) {
            loadPositionLimitConfig(j["positionLimit"]);
        }
        // Load position sizing config
        if (j.contains("positionSizing")) {
            loadPositionSizingConfig(j["positionSizing"]);
        }
        
        // Load exposure limit config
        if (j.contains("exposureLimit")) {
            loadExposureLimitConfig(j["exposureLimit"]);
        }
        
        // Load enhanced circuit breaker config
        if (j.contains("circuitBreaker")) {
            loadCircuitBreakerConfig(j["circuitBreaker"]);
        }
        
        // Load other configs...
        if (j.contains("stopLoss")) {
            loadStopLossConfig(j["stopLoss"]);
        }
        
        if (j.contains("drawdown")) {
            loadDrawdownConfig(j["drawdown"]);
        }
        
        if (j.contains("fatFinger")) {
            loadFatFingerConfig(j["fatFinger"]);
        }
        
        if (j.contains("orderValidation")) {
            loadOrderValidationConfig(j["orderValidation"]);
        }
        
        if (j.contains("balanceReconciliation")) {
            loadBalanceReconciliationConfig(j["balanceReconciliation"]);
        }

        if (j.contains("persistence")) {
            loadPersistenceConfig(j["persistence"]);
        }

        if (j.contains("alerting")) {
            loadAlertingConfig(j["alerting"]);
        }

        if (j.contains("runtime")) {
            loadRuntimeConfig(j["runtime"]);
        }

        if (j.contains("idempotency")) {
            loadIdempotencyConfig(j["idempotency"]);
        }
        
        if (j.contains("rebalancing")) {
            loadRebalancingConfig(j["rebalancing"]);
        }
        
        if (j.contains("collateral")) {
            loadCollateralConfig(j["collateral"]);
        }
        
        if (j.contains("portfolio")) {
            loadPortfolioConfig(j["portfolio"]);
        }
        
        if (j.contains("logging")) {
            loadLoggingConfig(j["logging"]);
        }
        
        // Validate the loaded configuration
        if (!validate()) {
            std::cerr << "Configuration validation failed\n";
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception while parsing JSON configuration: " << e.what() << "\n";
        return false;
    }
}

bool RiskConfig::getBoolValue(std::string key) {
    try {
        if (key == "positionLimit.enforceStrictly") {
            return positionLimitConfig_.enforceStrictly;
        } else if (key == "exposureLimit.includeUnrealizedPnl") {
            return exposureLimitConfig_.includeUnrealizedPnl;
        } else if (key == "circuitBreaker.autoResume") {
            return circuitBreakerConfig_.autoResume;
        } else if (key == "stopLoss.enableTrailingStop") {
            return stopLossConfig_.enableTrailingStop;
        } else if (key == "stopLoss.forceLiquidation") {
            return stopLossConfig_.forceLiquidation;
        } else if (key == "drawdown.enableAutoRiskReduction") {
            return drawdownConfig_.enableAutoRiskReduction;
        } else if (key == "fatFinger.enforceAdditionalVerification") {
            return fatFingerConfig_.enforceAdditionalVerification;
        } else if (key == "orderValidation.validateMinOrderSize") {
            return orderValidationConfig_.validateMinOrderSize;
        } else if (key == "orderValidation.validatePriceRange") {
            return orderValidationConfig_.validatePriceRange;
        } else if (key == "orderValidation.enforceSymbolWhitelist") {
            return orderValidationConfig_.enforceSymbolWhitelist;
        } else if (key == "rebalancing.autoRebalancingEnabled") {
            return rebalancingConfig_.autoRebalancingEnabled;
        } else if (key == "rebalancing.autoExecuteRebalancing") {
            return rebalancingConfig_.autoExecuteRebalancing;
        } else if (key == "collateral.enableEmergencyFunding") {
            return collateralConfig_.enableEmergencyFunding;
        } else if (key == "logging.enableDetailedLogs") {
            return loggingConfig_.enableDetailedLogs;
        } else if (key == "logging.logToConsole") {
            return loggingConfig_.logToConsole;
        } else if (key == "logging.logToFile") {
            return loggingConfig_.logToFile;
        } else {
            throw std::invalid_argument("Unknown boolean configuration key: " + key);
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception while retrieving boolean config value for key '" << key << "': " << e.what() << "\n";
        throw; // Rethrow to indicate failure
    }
}
bool RiskConfig::hasKey(std::string key) {
    try {
        if (key == "positionLimit.enforceStrictly" ||
            key == "exposureLimit.includeUnrealizedPnl" ||
            key == "circuitBreaker.autoResume" ||
            key == "stopLoss.enableTrailingStop" ||
            key == "stopLoss.forceLiquidation" ||
            key == "drawdown.enableAutoRiskReduction" ||
            key == "fatFinger.enforceAdditionalVerification" ||
            key == "orderValidation.validateMinOrderSize" ||
            key == "orderValidation.validatePriceRange" ||
            key == "orderValidation.enforceSymbolWhitelist" ||
            key == "rebalancing.autoRebalancingEnabled" ||
            key == "rebalancing.autoExecuteRebalancing" ||
            key == "collateral.enableEmergencyFunding" ||
            key == "logging.enableDetailedLogs" ||
            key == "logging.logToConsole" ||
            key == "logging.logToFile") {
            return true;
        } else {
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception while checking existence of config key '" << key << "': " << e.what() << "\n";
        throw; // Rethrow to indicate failure
    }
}
// getIntValue
int RiskConfig::getIntValue(std::string key) {
    try {
        if (key == "circuitBreaker.coolingPeriodSeconds") {
            return circuitBreakerConfig_.coolingPeriodSeconds;
        } else if (key == "circuitBreaker.mandatoryHaltPeriodSeconds") {
            return circuitBreakerConfig_.mandatoryHaltPeriodSeconds;
        } else if (key == "circuitBreaker.maxTriggersPerDay") {
            return circuitBreakerConfig_.maxTriggersPerDay;
        } else if (key == "circuitBreaker.maxTriggersBeforeExtendedHalt") {
            return circuitBreakerConfig_.maxTriggersBeforeExtendedHalt;
        } else if (key == "circuitBreaker.extendedHaltMultiplier") {
            return circuitBreakerConfig_.extendedHaltMultiplier;
        } else if (key == "circuitBreaker.triggerCountResetMinutes") {
            return circuitBreakerConfig_.triggerCountResetMinutes;
        } else if (key == "stopLoss.liquidationDelaySeconds") {
            return static_cast<int>(stopLossConfig_.liquidationDelay.count());
        } else if (key == "drawdown.lookbackPeriodHours") {
            return static_cast<int>(drawdownConfig_.lookbackPeriod.count() / 3600);
        } else if (key == "rebalancing.rebalancingIntervalMinutes") {
            return rebalancingConfig_.rebalancingIntervalMinutes;
        } else if (key == "logging.logRetentionPeriodHours") {
            return static_cast<int>(loggingConfig_.logRetentionPeriod.count() / 3600);
        } else {
            throw std::invalid_argument("Unknown integer configuration key: " + key);
        }
    } catch (const std::exception& e) {
        std::cerr << "Exception while retrieving integer config value for key '" << key << "': " << e.what() << "\n";
        throw; // Rethrow to indicate failure
    }
}
// Enhanced Circuit Breaker config loading
void RiskConfig::loadCircuitBreakerConfig(const json& cb) {
    // Load metric thresholds
    if (cb.contains("metricThresholds") && cb["metricThresholds"].is_object()) {
        for (const auto& [metricName, thresholdObj] : cb["metricThresholds"].items()) {
            if (thresholdObj.is_object()) {
                MetricThreshold threshold;
                threshold.warningThreshold = thresholdObj.value("warning", 0.0);
                threshold.criticalThreshold = thresholdObj.value("critical", 0.0);
                threshold.isHigherBetter = thresholdObj.value("isHigherBetter", false);
                circuitBreakerConfig_.metricThresholds[metricName] = threshold;
            }
        }
    }
    
    // Load timing configuration
    if (cb.contains("coolingPeriodSeconds")) {
        int seconds = cb["coolingPeriodSeconds"].get<int>();
        circuitBreakerConfig_.coolingPeriod = std::chrono::seconds(seconds);
        circuitBreakerConfig_.coolingPeriodSeconds = seconds;
    }
    
    if (cb.contains("warmupPeriodSeconds")) {
        int seconds = cb["warmupPeriodSeconds"].get<int>();
        circuitBreakerConfig_.warmupPeriod = std::chrono::seconds(seconds);
    }
    
    if (cb.contains("mandatoryHaltPeriodSeconds")) {
        circuitBreakerConfig_.mandatoryHaltPeriodSeconds = cb["mandatoryHaltPeriodSeconds"].get<int>();
    }
    
    // Load trigger and reset behavior
    if (cb.contains("autoResume")) {
        circuitBreakerConfig_.autoResume = cb["autoResume"].get<bool>();
    }
    
    if (cb.contains("maxTriggersPerDay")) {
        circuitBreakerConfig_.maxTriggersPerDay = cb["maxTriggersPerDay"].get<int>();
    }
    
    if (cb.contains("maxTriggersBeforeExtendedHalt")) {
        circuitBreakerConfig_.maxTriggersBeforeExtendedHalt = cb["maxTriggersBeforeExtendedHalt"].get<int>();
    }
    
    if (cb.contains("extendedHaltMultiplier")) {
        circuitBreakerConfig_.extendedHaltMultiplier = cb["extendedHaltMultiplier"].get<int>();
    }
    
    if (cb.contains("triggerCountResetMinutes")) {
        circuitBreakerConfig_.triggerCountResetMinutes = cb["triggerCountResetMinutes"].get<int>();
    }
    
    // Load history and monitoring
    if (cb.contains("maxHistorySize")) {
        circuitBreakerConfig_.maxHistorySize = cb["maxHistorySize"].get<size_t>();
    }
    
    // Load gradual recovery
    if (cb.contains("gradualTradingResumption")) {
        circuitBreakerConfig_.gradualTradingResumption = cb["gradualTradingResumption"].get<bool>();
    }
}

// Helper methods for loading other configs
void RiskConfig::loadPositionLimitConfig(const json& pl) {
    if (pl.contains("assetLimits") && pl["assetLimits"].is_object()) {
        for (const auto& [asset, limit] : pl["assetLimits"].items()) {
            positionLimitConfig_.assetLimits[asset] = limit.get<double>();
        }
    }
    if (pl.contains("strategyLimits") && pl["strategyLimits"].is_object()) {
        for (const auto& [strategy, limit] : pl["strategyLimits"].items()) {
            positionLimitConfig_.strategyLimits[strategy] = limit.get<double>();
        }
    }
    if (pl.contains("exchangeLimits") && pl["exchangeLimits"].is_object()) {
        for (const auto& [exchange, limit] : pl["exchangeLimits"].items()) {
            positionLimitConfig_.exchangeLimits[exchange] = limit.get<double>();
        }
    }
    if (pl.contains("defaultLimit")) {
        positionLimitConfig_.defaultLimit = pl["defaultLimit"].get<double>();
    }
    if (pl.contains("enforceStrictly")) {
        positionLimitConfig_.enforceStrictly = pl["enforceStrictly"].get<bool>();
    }
}

void RiskConfig::loadPositionSizingConfig(const json& ps) {
    if (ps.contains("method")) positionSizingConfig_.method = ps["method"].get<std::string>();
    if (ps.contains("base_risk_per_trade")) positionSizingConfig_.base_risk_per_trade = ps["base_risk_per_trade"].get<double>();
    if (ps.contains("max_position_size")) positionSizingConfig_.max_position_size = ps["max_position_size"].get<double>();
    if (ps.contains("kelly_safety_factor")) positionSizingConfig_.kelly_safety_factor = ps["kelly_safety_factor"].get<double>();
    if (ps.contains("volatility_lookback")) positionSizingConfig_.volatility_lookback = ps["volatility_lookback"].get<int>();
    if (ps.contains("confidence_compression")) positionSizingConfig_.confidence_compression = ps["confidence_compression"].get<double>();
}

void RiskConfig::loadExposureLimitConfig(const json& el) {
    if (el.contains("maxTotalExposure")) {
        exposureLimitConfig_.maxTotalExposure = el["maxTotalExposure"].get<double>();
    }
    if (el.contains("maxAssetExposure")) {
        exposureLimitConfig_.maxAssetExposure = el["maxAssetExposure"].get<double>();
    }
    if (el.contains("maxStrategyExposure")) {
        exposureLimitConfig_.maxStrategyExposure = el["maxStrategyExposure"].get<double>();
    }
    if (el.contains("maxExchangeExposure")) {
        exposureLimitConfig_.maxExchangeExposure = el["maxExchangeExposure"].get<double>();
    }
    if (el.contains("volatilityMultiplier")) {
        exposureLimitConfig_.volatilityMultiplier = el["volatilityMultiplier"].get<double>();
    }
    if (el.contains("includeUnrealizedPnl")) {
        exposureLimitConfig_.includeUnrealizedPnl = el["includeUnrealizedPnl"].get<bool>();
    }
}

void RiskConfig::loadStopLossConfig(const json& sl) {
    if (sl.contains("maxLossPercentage")) {
        stopLossConfig_.maxLossPercentage = sl["maxLossPercentage"].get<double>();
    }
    if (sl.contains("trailingStopDistance")) {
        stopLossConfig_.trailingStopDistance = sl["trailingStopDistance"].get<double>();
    }
    if (sl.contains("enableTrailingStop")) {
        stopLossConfig_.enableTrailingStop = sl["enableTrailingStop"].get<bool>();
    }
    if (sl.contains("forceLiquidation")) {
        stopLossConfig_.forceLiquidation = sl["forceLiquidation"].get<bool>();
    }
    if (sl.contains("liquidationDelaySeconds")) {
        stopLossConfig_.liquidationDelay = std::chrono::seconds(sl["liquidationDelaySeconds"].get<int>());
    }
}

void RiskConfig::loadDrawdownConfig(const json& dd) {
    if (dd.contains("warningThreshold")) {
        drawdownConfig_.warningThreshold = dd["warningThreshold"].get<double>();
    }
    if (dd.contains("criticalThreshold")) {
        drawdownConfig_.criticalThreshold = dd["criticalThreshold"].get<double>();
    }
    if (dd.contains("lookbackPeriodHours")) {
        drawdownConfig_.lookbackPeriod = std::chrono::hours(dd["lookbackPeriodHours"].get<int>());
    }
    if (dd.contains("enableAutoRiskReduction")) {
        drawdownConfig_.enableAutoRiskReduction = dd["enableAutoRiskReduction"].get<bool>();
    }
    if (dd.contains("riskReductionFactor")) {
        drawdownConfig_.riskReductionFactor = dd["riskReductionFactor"].get<double>();
    }
}

void RiskConfig::loadFatFingerConfig(const json& ff) {
    if (ff.contains("maxPriceDeviation")) {
        fatFingerConfig_.maxPriceDeviation = ff["maxPriceDeviation"].get<double>();
    }
    if (ff.contains("maxOrderSize")) {
        fatFingerConfig_.maxOrderSize = ff["maxOrderSize"].get<double>();
    }
    if (ff.contains("typicalOrderSize")) {
        fatFingerConfig_.typicalOrderSize = ff["typicalOrderSize"].get<double>();
    }
    if (ff.contains("notionalValueLimit")) {
        fatFingerConfig_.notionalValueLimit = ff["notionalValueLimit"].get<double>();
    }
    if (ff.contains("enforceAdditionalVerification")) {
        fatFingerConfig_.enforceAdditionalVerification = ff["enforceAdditionalVerification"].get<bool>();
    }
}

void RiskConfig::loadOrderValidationConfig(const json& ov) {
    if (ov.contains("validateMinOrderSize")) {
        orderValidationConfig_.validateMinOrderSize = ov["validateMinOrderSize"].get<bool>();
    }
    if (ov.contains("validatePriceRange")) {
        orderValidationConfig_.validatePriceRange = ov["validatePriceRange"].get<bool>();
    }
    // Prefer using a single minTradeValue (notional) instead of per-symbol min sizes
    if (ov.contains("minTradeValue")) {
        orderValidationConfig_.minTradeValue = ov["minTradeValue"].get<double>();
    } else if (ov.contains("minOrderSizes")) {
        // Keep backward compatibility: log a warning and ignore per-symbol sizes
        std::cerr << "Warning: 'minOrderSizes' is deprecated. Please migrate to 'minTradeValue' in orderValidation. Using default minTradeValue="
                  << orderValidationConfig_.minTradeValue << "\n";
    }
    if (ov.contains("maxPriceDeviationPercent")) {
        orderValidationConfig_.maxPriceDeviationPercent = ov["maxPriceDeviationPercent"].get<double>();
    }
    if (ov.contains("enforceSymbolWhitelist")) {
        orderValidationConfig_.enforceSymbolWhitelist = ov["enforceSymbolWhitelist"].get<bool>();
    }
    if (ov.contains("symbolWhitelist") && ov["symbolWhitelist"].is_array()) {
        orderValidationConfig_.symbolWhitelist.clear();
        for (const auto& symbol : ov["symbolWhitelist"]) {
            orderValidationConfig_.symbolWhitelist.push_back(symbol.get<std::string>());
        }
    }
}

void RiskConfig::loadBalanceReconciliationConfig(const json& br) {
    if (br.contains("reconciliationFrequencyMinutes")) {
        balanceReconciliationConfig_.reconciliationFrequency = 
            std::chrono::minutes(br["reconciliationFrequencyMinutes"].get<int>());
    }
    if (br.contains("maxDiscrepancyPercent")) {
        balanceReconciliationConfig_.maxDiscrepancyPercent = br["maxDiscrepancyPercent"].get<double>();
    }
    if (br.contains("autoCorrectDiscrepancies")) {
        balanceReconciliationConfig_.autoCorrectDiscrepancies = br["autoCorrectDiscrepancies"].get<bool>();
    }
    if (br.contains("alertOnDiscrepancy")) {
        balanceReconciliationConfig_.alertOnDiscrepancy = br["alertOnDiscrepancy"].get<bool>();
    }
    if (br.contains("maxConsecutiveFailures")) {
        balanceReconciliationConfig_.maxConsecutiveFailures = br["maxConsecutiveFailures"].get<int>();
    }
    
    // Load extended settings
    if (br.contains("minimumPositionSize")) {
        balanceReconciliationConfig_.minimumPositionSize = br["minimumPositionSize"].get<double>();
    }
    if (br.contains("exchangeTimeoutSeconds")) {
        balanceReconciliationConfig_.exchangeTimeoutSeconds = 
            std::chrono::seconds(br["exchangeTimeoutSeconds"].get<int>());
    }
    if (br.contains("enableDetailedLogging")) {
        balanceReconciliationConfig_.enableDetailedLogging = br["enableDetailedLogging"].get<bool>();
    }
    // Add other extended settings as needed...
}

void RiskConfig::loadRebalancingConfig(const json& rb) {
    if (rb.contains("autoRebalancingEnabled")) {
        rebalancingConfig_.autoRebalancingEnabled = rb["autoRebalancingEnabled"].get<bool>();
    }
    if (rb.contains("autoExecuteRebalancing")) {
        rebalancingConfig_.autoExecuteRebalancing = rb["autoExecuteRebalancing"].get<bool>();
    }
    if (rb.contains("allocationDeviationThreshold")) {
        rebalancingConfig_.allocationDeviationThreshold = rb["allocationDeviationThreshold"].get<double>();
    }
    if (rb.contains("minimumOrderSize")) {
        rebalancingConfig_.minimumOrderSize = rb["minimumOrderSize"].get<double>();
    }
    if (rb.contains("targetAllocations") && rb["targetAllocations"].is_object()) {
        for (const auto& [asset, allocation] : rb["targetAllocations"].items()) {
            rebalancingConfig_.targetAllocations[asset] = allocation.get<double>();
        }
    }
    // Add other rebalancing settings...
}

void RiskConfig::loadCollateralConfig(const json& co) {
    if (co.contains("targetFreeCollateralRatio")) {
        collateralConfig_.targetFreeCollateralRatio = co["targetFreeCollateralRatio"].get<double>();
    }
    if (co.contains("enableEmergencyFunding")) {
        collateralConfig_.enableEmergencyFunding = co["enableEmergencyFunding"].get<bool>();
    }
    if (co.contains("fundingSources") && co["fundingSources"].is_array()) {
        collateralConfig_.fundingSources.clear();
        for (const auto& source : co["fundingSources"]) {
            collateralConfig_.fundingSources.push_back(source.get<std::string>());
        }
    }
    if (co.contains("maxFundingAmount")) {
        collateralConfig_.maxFundingAmount = co["maxFundingAmount"].get<double>();
    }
    if (co.contains("managedExchanges") && co["managedExchanges"].is_array()) {
        std::cerr << "[RiskConfig] Loading managedExchanges from JSON..." << std::endl;
        collateralConfig_.managedExchanges.clear();
        for (const auto& exchangeStr : co["managedExchanges"]) {
            std::string exchangeName = exchangeStr.get<std::string>();
            std::cerr << "[RiskConfig] Processing exchange: " << exchangeName << std::endl;
            try {
                auto exchangeId = core::ExchangeId::fromString(exchangeName);
                collateralConfig_.managedExchanges.push_back(exchangeId);
                std::cerr << "[RiskConfig] Added exchange: " << exchangeName << " -> " << exchangeId.toString() << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[RiskConfig] Failed to parse exchange '" << exchangeName << "': " << e.what() << std::endl;
                // Fall back to binance for backward compatibility
                collateralConfig_.managedExchanges.push_back(core::ExchangeId::binance());
                std::cerr << "[RiskConfig] Using fallback binance exchange" << std::endl;
            }
        }
        std::cerr << "[RiskConfig] Final managedExchanges count: " << collateralConfig_.managedExchanges.size() << std::endl;
    }
    if (co.contains("minimumBalances") && co["minimumBalances"].is_object()) {
        collateralConfig_.minimumBalances.clear();
        for (const auto& [asset, amount] : co["minimumBalances"].items()) {
            collateralConfig_.minimumBalances[asset] = amount.get<double>();
        }
    }
    if (co.contains("emergencyFundingBuffer")) {
        collateralConfig_.emergencyFundingBuffer = co["emergencyFundingBuffer"].get<double>();
    }
    if (co.contains("emergencyFundingSource")) {
        collateralConfig_.emergencyFundingSource = co["emergencyFundingSource"].get<std::string>();
    }
    if (co.contains("minimumFreeCollateralRatio")) {
        collateralConfig_.minimumFreeCollateralRatio = co["minimumFreeCollateralRatio"].get<double>();
    }
    if (co.contains("emergencyFundingThreshold")) {
        collateralConfig_.emergencyFundingThreshold = co["emergencyFundingThreshold"].get<double>();
    }
    if (co.contains("autoInitiateEmergencyFunding")) {
        collateralConfig_.autoInitiateEmergencyFunding = co["autoInitiateEmergencyFunding"].get<bool>();
    }
    // Add other collateral settings...
}

void RiskConfig::loadPortfolioConfig(const json& po) {
    if (po.contains("enableRealTimeMetrics")) {
        portfolioConfig_.enableRealTimeMetrics = po["enableRealTimeMetrics"].get<bool>();
    }
    if (po.contains("minimumPortfolioValue")) {
        portfolioConfig_.minimumPortfolioValue = po["minimumPortfolioValue"].get<double>();
    }
    if (po.contains("baseCurrency")) {
        portfolioConfig_.baseCurrency = po["baseCurrency"].get<std::string>();
    }
    // Add other portfolio settings...
}

void RiskConfig::loadLoggingConfig(const json& lg) {
    if (lg.contains("logLevel")) {
        loggingConfig_.logLevel = lg["logLevel"].get<std::string>();
    }
    if (lg.contains("logDirectory")) {
        loggingConfig_.logDirectory = lg["logDirectory"].get<std::string>();
    }
    if (lg.contains("enableDetailedLogs")) {
        loggingConfig_.enableDetailedLogs = lg["enableDetailedLogs"].get<bool>();
    }
    if (lg.contains("logToConsole")) {
        loggingConfig_.logToConsole = lg["logToConsole"].get<bool>();
    }
    if (lg.contains("logToFile")) {
        loggingConfig_.logToFile = lg["logToFile"].get<bool>();
    }
    if (lg.contains("logRetentionDays")) {
        loggingConfig_.logRetentionPeriod = std::chrono::hours(lg["logRetentionDays"].get<int>() * 24);
    }
}

void RiskConfig::loadPersistenceConfig(const json& p) {
    if (p.contains("enabled")) {
        persistenceConfig_.enabled = p["enabled"].get<bool>();
    }
    if (p.contains("type")) {
        persistenceConfig_.type = p["type"].get<std::string>();
    }
    if (p.contains("sqlitePath")) {
        persistenceConfig_.sqlitePath = p["sqlitePath"].get<std::string>();
    }
    if (p.contains("postgresUrl")) {
        persistenceConfig_.postgresUrl = p["postgresUrl"].get<std::string>();
    }
    if (p.contains("retentionDays")) {
        persistenceConfig_.retentionDays = p["retentionDays"].get<int>();
    }
    if (p.contains("queueCapacity")) {
        persistenceConfig_.queueCapacity = p["queueCapacity"].get<size_t>();
    }
    if (p.contains("backpressureMode")) {
        persistenceConfig_.backpressureMode = p["backpressureMode"].get<std::string>();
    }
}

void RiskConfig::loadAlertingConfig(const json& a) {
    if (a.contains("enabled")) {
        alertingConfig_.enabled = a["enabled"].get<bool>();
    }
    if (a.contains("channels") && a["channels"].is_array()) {
        alertingConfig_.channels.clear();
        for (const auto& ch : a["channels"]) alertingConfig_.channels.push_back(ch.get<std::string>());
    }
    if (a.contains("webhookEndpoints") && a["webhookEndpoints"].is_array()) {
        alertingConfig_.webhookEndpoints.clear();
        for (const auto& url : a["webhookEndpoints"]) alertingConfig_.webhookEndpoints.push_back(url.get<std::string>());
    }
    if (a.contains("slackWebhookUrl")) {
        alertingConfig_.slackWebhookUrl = a["slackWebhookUrl"].get<std::string>();
    }
    if (a.contains("enableEmail")) {
        alertingConfig_.enableEmail = a["enableEmail"].get<bool>();
    }
    if (a.contains("smtpHost")) alertingConfig_.smtpHost = a["smtpHost"].get<std::string>();
    if (a.contains("smtpPort")) alertingConfig_.smtpPort = a["smtpPort"].get<int>();
    if (a.contains("smtpFrom")) alertingConfig_.smtpFrom = a["smtpFrom"].get<std::string>();
    if (a.contains("smtpTo") && a["smtpTo"].is_array()) {
        alertingConfig_.smtpTo.clear();
        for (const auto& recipient : a["smtpTo"]) alertingConfig_.smtpTo.push_back(recipient.get<std::string>());
    }
}

void RiskConfig::loadRuntimeConfig(const json& r) {
    if (r.contains("maxConcurrentReconciliations")) {
        runtimeConfig_.maxConcurrentReconciliations = r["maxConcurrentReconciliations"].get<int>();
    }
    if (r.contains("reconciliationWorkerTimeoutSeconds")) {
        runtimeConfig_.reconciliationWorkerTimeoutSeconds = r["reconciliationWorkerTimeoutSeconds"].get<int>();
    }
    if (r.contains("enableHttpApi")) runtimeConfig_.enableHttpApi = r["enableHttpApi"].get<bool>();
    if (r.contains("apiHost")) runtimeConfig_.apiHost = r["apiHost"].get<std::string>();
    if (r.contains("apiPort")) runtimeConfig_.apiPort = r["apiPort"].get<int>();
    if (r.contains("zmqEnabled")) runtimeConfig_.zmqEnabled = r["zmqEnabled"].get<bool>();
    if (r.contains("zmqContextThreads")) runtimeConfig_.zmqContextThreads = r["zmqContextThreads"].get<int>();
}

void RiskConfig::loadIdempotencyConfig(const json& i) {
    if (i.contains("enabled")) idempotencyConfig_.enabled = i["enabled"].get<bool>();
    if (i.contains("dedupWindowSeconds")) idempotencyConfig_.dedupWindowSeconds = i["dedupWindowSeconds"].get<int>();
}

// Enhanced configuration saving with proper MetricThreshold serialization
bool RiskConfig::saveToFile(const std::string& configFilePath) const {
    try {
        json j;
        
        // Position limit config
        j["positionLimit"] = {
            {"assetLimits", positionLimitConfig_.assetLimits},
            {"strategyLimits", positionLimitConfig_.strategyLimits},
            {"exchangeLimits", positionLimitConfig_.exchangeLimits},
            {"defaultLimit", positionLimitConfig_.defaultLimit},
            {"enforceStrictly", positionLimitConfig_.enforceStrictly}
        };
        
        // Exposure limit config
        j["exposureLimit"] = {
            {"maxTotalExposure", exposureLimitConfig_.maxTotalExposure},
            {"maxAssetExposure", exposureLimitConfig_.maxAssetExposure},
            {"maxStrategyExposure", exposureLimitConfig_.maxStrategyExposure},
            {"maxExchangeExposure", exposureLimitConfig_.maxExchangeExposure},
            {"volatilityMultiplier", exposureLimitConfig_.volatilityMultiplier},
            {"includeUnrealizedPnl", exposureLimitConfig_.includeUnrealizedPnl}
        };
        
        // Enhanced circuit breaker config
        json metricThresholds;
        for (const auto& [metricName, threshold] : circuitBreakerConfig_.metricThresholds) {
            metricThresholds[metricName] = {
                {"warning", threshold.warningThreshold},
                {"critical", threshold.criticalThreshold},
                {"isHigherBetter", threshold.isHigherBetter}
            };
        }
        
        j["circuitBreaker"] = {
            {"metricThresholds", metricThresholds},
            {"coolingPeriodSeconds", circuitBreakerConfig_.coolingPeriod.count()},
            {"warmupPeriodSeconds", circuitBreakerConfig_.warmupPeriod.count()},
            {"mandatoryHaltPeriodSeconds", circuitBreakerConfig_.mandatoryHaltPeriodSeconds},
            {"autoResume", circuitBreakerConfig_.autoResume},
            {"maxTriggersPerDay", circuitBreakerConfig_.maxTriggersPerDay},
            {"maxTriggersBeforeExtendedHalt", circuitBreakerConfig_.maxTriggersBeforeExtendedHalt},
            {"extendedHaltMultiplier", circuitBreakerConfig_.extendedHaltMultiplier},
            {"triggerCountResetMinutes", circuitBreakerConfig_.triggerCountResetMinutes},
            {"maxHistorySize", circuitBreakerConfig_.maxHistorySize},
            {"gradualTradingResumption", circuitBreakerConfig_.gradualTradingResumption}
        };
        
        // Stop loss config
        j["stopLoss"] = {
            {"maxLossPercentage", stopLossConfig_.maxLossPercentage},
            {"trailingStopDistance", stopLossConfig_.trailingStopDistance},
            {"enableTrailingStop", stopLossConfig_.enableTrailingStop},
            {"forceLiquidation", stopLossConfig_.forceLiquidation},
            {"liquidationDelaySeconds", stopLossConfig_.liquidationDelay.count()}
        };
        
        // Add other configuration sections...
        saveOtherConfigs(j);
        
        // Write to file with pretty formatting
        std::ofstream file(configFilePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open file for writing: " << configFilePath << "\n";
            return false;
        }
        
        file << j.dump(4); // Pretty print with 4-space indentation
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception while saving configuration: " << e.what() << "\n";
        return false;
    }
}

void RiskConfig::saveOtherConfigs(json& j) const {
    // Drawdown config
    j["drawdown"] = {
        {"warningThreshold", drawdownConfig_.warningThreshold},
        {"criticalThreshold", drawdownConfig_.criticalThreshold},
        {"lookbackPeriodHours", drawdownConfig_.lookbackPeriod.count()},
        {"enableAutoRiskReduction", drawdownConfig_.enableAutoRiskReduction},
        {"riskReductionFactor", drawdownConfig_.riskReductionFactor}
    };

    // Balance reconciliation
    j["balanceReconciliation"] = {
        {"reconciliationFrequencyMinutes", (int)balanceReconciliationConfig_.reconciliationFrequency.count()},
        {"maxDiscrepancyPercent", balanceReconciliationConfig_.maxDiscrepancyPercent},
        {"autoCorrectDiscrepancies", balanceReconciliationConfig_.autoCorrectDiscrepancies},
        {"alertOnDiscrepancy", balanceReconciliationConfig_.alertOnDiscrepancy},
        {"minimumPositionSize", balanceReconciliationConfig_.minimumPositionSize}
    };

    // Persistence config
    j["persistence"] = {
        {"enabled", persistenceConfig_.enabled},
        {"type", persistenceConfig_.type},
        {"sqlitePath", persistenceConfig_.sqlitePath},
        {"postgresUrl", persistenceConfig_.postgresUrl},
        {"retentionDays", persistenceConfig_.retentionDays},
        {"queueCapacity", persistenceConfig_.queueCapacity},
        {"backpressureMode", persistenceConfig_.backpressureMode}
    };

    // Alerting config
    j["alerting"] = {
        {"enabled", alertingConfig_.enabled},
        {"channels", alertingConfig_.channels},
        {"webhookEndpoints", alertingConfig_.webhookEndpoints},
        {"slackWebhookUrl", alertingConfig_.slackWebhookUrl},
        {"enableEmail", alertingConfig_.enableEmail},
        {"smtpHost", alertingConfig_.smtpHost},
        {"smtpPort", alertingConfig_.smtpPort},
        {"smtpFrom", alertingConfig_.smtpFrom},
        {"smtpTo", alertingConfig_.smtpTo}
    };

    // Runtime config
    j["runtime"] = {
        {"maxConcurrentReconciliations", runtimeConfig_.maxConcurrentReconciliations},
        {"reconciliationWorkerTimeoutSeconds", runtimeConfig_.reconciliationWorkerTimeoutSeconds},
        {"enableHttpApi", runtimeConfig_.enableHttpApi},
        {"apiHost", runtimeConfig_.apiHost},
        {"apiPort", runtimeConfig_.apiPort},
        {"zmqEnabled", runtimeConfig_.zmqEnabled},
        {"zmqContextThreads", runtimeConfig_.zmqContextThreads}
    };

    // Idempotency config
    j["idempotency"] = {
        {"enabled", idempotencyConfig_.enabled},
        {"dedupWindowSeconds", idempotencyConfig_.dedupWindowSeconds}
    };

    // Add other configs as needed...
}

// Enhanced validation with proper error reporting
bool RiskConfig::validate() const {
    try {
        // Validate circuit breaker config using its own validation
        if (!circuitBreakerConfig_.isValid()) {
            std::cerr << "Circuit breaker configuration is invalid\n";
            return false;
        }
        
        // Validate balance reconciliation config
        if (!balanceReconciliationConfig_.isValid()) {
            std::cerr << "Balance reconciliation configuration is invalid\n";
            return false;
        }
        
        // Validate position limits
        // std::cerr << "DEBUG: Validating position limits..." << std::endl;
        // std::cerr.flush();
        
        // std::cerr << "DEBUG: defaultLimit value = " << positionLimitConfig_.defaultLimit << std::endl;
        // std::cerr.flush();
        
        // Test if this comparison causes the SIGILL
        // std::cerr << "DEBUG: About to test defaultLimit <= 0..." << std::endl;
        // std::cerr.flush();
        
        double defaultLimit = positionLimitConfig_.defaultLimit;
        // std::cerr << "DEBUG: Copied defaultLimit = " << defaultLimit << std::endl;
        // std::cerr.flush();
        
        bool isLessOrEqual = (defaultLimit <= 0.0);
        // std::cerr << "DEBUG: Comparison result = " << isLessOrEqual << std::endl;
        // std::cerr.flush();
        
        // std::cerr << "DEBUG: About to test if condition..." << std::endl;
        // std::cerr.flush();
        
        if (isLessOrEqual) {
            std::cerr << "DEBUG: Inside if block" << std::endl;
            std::cerr.flush();
            std::cerr << "Invalid default position limit: must be positive\n";
            return false;
        }
        
        // std::cerr << "DEBUG: Position limit validation passed" << std::endl;
        // std::cerr.flush();
        
        // // Validate exposure limits
        // std::cerr << "DEBUG: About to validate exposure limits..." << std::endl;
        // std::cerr.flush();
        
        // std::cerr << "DEBUG: maxTotalExposure = " << exposureLimitConfig_.maxTotalExposure << std::endl;
        // std::cerr.flush();
        
        bool totalExposureValid = (exposureLimitConfig_.maxTotalExposure > 0);
        // std::cerr << "DEBUG: totalExposureValid = " << totalExposureValid << std::endl;
        // std::cerr.flush();
        
        // std::cerr << "DEBUG: Testing maxAssetExposure..." << std::endl;
        // std::cerr.flush();
        bool assetExposureValid = (exposureLimitConfig_.maxAssetExposure > 0);
        // std::cerr << "DEBUG: assetExposureValid = " << assetExposureValid << std::endl;
        // std::cerr.flush();
        
        // std::cerr << "DEBUG: Testing maxStrategyExposure..." << std::endl;
        // std::cerr.flush();
        bool strategyExposureValid = (exposureLimitConfig_.maxStrategyExposure > 0);
        // std::cerr << "DEBUG: strategyExposureValid = " << strategyExposureValid << std::endl;
        // std::cerr.flush();
        
        // std::cerr << "DEBUG: Testing maxExchangeExposure..." << std::endl;
        // std::cerr.flush();
        bool exchangeExposureValid = (exposureLimitConfig_.maxExchangeExposure > 0);
        // std::cerr << "DEBUG: exchangeExposureValid = " << exchangeExposureValid << std::endl;
        // std::cerr.flush();
        
        // std::cerr << "DEBUG: All individual tests complete, testing compound condition..." << std::endl;
        // std::cerr.flush();
        
        // std::cerr << "DEBUG: Testing !totalExposureValid..." << std::endl;
        // std::cerr.flush();
        bool not_total = !totalExposureValid;
        // std::cerr << "DEBUG: !totalExposureValid = " << not_total << std::endl;
        // std::cerr.flush();
        
        // std::cerr << "DEBUG: Testing !assetExposureValid..." << std::endl;
        // std::cerr.flush();
        bool not_asset = !assetExposureValid;
        // std::cerr << "DEBUG: !assetExposureValid = " << not_asset << std::endl;
        // std::cerr.flush();
        
        // std::cerr << "DEBUG: Testing logical OR of first two..." << std::endl;
        // std::cerr.flush();
        bool first_or = not_total || not_asset;
        // std::cerr << "DEBUG: first_or = " << first_or << std::endl;
        // std::cerr.flush();
        
        if (!totalExposureValid || !assetExposureValid || !strategyExposureValid || !exchangeExposureValid) {
            std::cerr << "Invalid exposure limits: all limits must be positive\n";
            return false;
        }
        
        // Validate metric thresholds consistency
        for (const auto& [metricName, threshold] : circuitBreakerConfig_.metricThresholds) {
            if (threshold.isHigherBetter) {
                // For "higher is better" metrics, warning should be higher than critical
                if (threshold.warningThreshold <= threshold.criticalThreshold) {
                    std::cerr << "Invalid thresholds for " << metricName << ": warning (" 
                              << threshold.warningThreshold << ") should be higher than critical (" 
                              << threshold.criticalThreshold << ")\n";
                    return false;
                }
            } else {
                if (threshold.criticalThreshold <= threshold.warningThreshold) {
                    std::cerr << "Invalid thresholds for " << metricName << ": critical (" 
                              << threshold.criticalThreshold << ") should be higher than warning (" 
                              << threshold.warningThreshold << ")\n";
                    return false;
                }
                }
            }
                
        // Add other validation logic as needed...
        
        // If we reach here, all validation passed
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception during configuration validation: " << e.what() << "\n";
        return false;
    }
}

// Getter implementations (unchanged from original)
const PositionLimitConfig& RiskConfig::getPositionLimitConfig() const {
    return positionLimitConfig_;
}

const PositionSizingConfig& RiskConfig::getPositionSizingConfig() const {
    return positionSizingConfig_;
}

void RiskConfig::setPositionSizingConfig(const PositionSizingConfig& config) {
    positionSizingConfig_ = config;
}

const ExposureLimitConfig& RiskConfig::getExposureLimitConfig() const {
    return exposureLimitConfig_;
}

const CircuitBreakerConfig& RiskConfig::getCircuitBreakerConfig() const {
    return circuitBreakerConfig_;
}

const StopLossConfig& RiskConfig::getStopLossConfig() const {
    return stopLossConfig_;
}

const DrawdownConfig& RiskConfig::getDrawdownConfig() const {
    return drawdownConfig_;
}

const FatFingerConfig& RiskConfig::getFatFingerConfig() const {
    return fatFingerConfig_;
}

const OrderValidationConfig& RiskConfig::getOrderValidationConfig() const {
    return orderValidationConfig_; 
}

const BalanceReconciliationConfig& RiskConfig::getBalanceReconciliationConfig() const {
    return balanceReconciliationConfig_;
}

const RebalancingConfig& RiskConfig::getRebalancingConfig() const {
    return rebalancingConfig_;
}

const CollateralConfig& RiskConfig::getCollateralConfig() const {
    return collateralConfig_;
}

const PortfolioConfig& RiskConfig::getPortfolioConfig() const {
    return portfolioConfig_;
}

const RiskLoggingConfig& RiskConfig::getLoggingConfig() const {
    return loggingConfig_;
}

// Setter implementations (unchanged from original)
void RiskConfig::setPositionLimitConfig(const PositionLimitConfig& config) {
    positionLimitConfig_ = config;
}

void RiskConfig::setExposureLimitConfig(const ExposureLimitConfig& config) {
    exposureLimitConfig_ = config;
}

void RiskConfig::setCircuitBreakerConfig(const CircuitBreakerConfig& config) {
    circuitBreakerConfig_ = config;
}

void RiskConfig::setStopLossConfig(const StopLossConfig& config) {
    stopLossConfig_ = config;
}

void RiskConfig::setDrawdownConfig(const DrawdownConfig& config) {
    drawdownConfig_ = config;
}

void RiskConfig::setFatFingerConfig(const FatFingerConfig& config) {
    fatFingerConfig_ = config;
}

void RiskConfig::setOrderValidationConfig(const OrderValidationConfig& config) {
    orderValidationConfig_ = config;
}

void RiskConfig::setBalanceReconciliationConfig(const BalanceReconciliationConfig& config) {
    balanceReconciliationConfig_ = config;
}

void RiskConfig::setRebalancingConfig(const RebalancingConfig& config) {
    rebalancingConfig_ = config;
}

void RiskConfig::setCollateralConfig(const CollateralConfig& config) {
    collateralConfig_ = config;
}

void RiskConfig::setPortfolioConfig(const PortfolioConfig& config) {
    portfolioConfig_ = config;
}

void RiskConfig::setLoggingConfig(const RiskLoggingConfig& config) {
    loggingConfig_ = config;
}

const PersistenceConfig& RiskConfig::getPersistenceConfig() const {
    return persistenceConfig_;
}

const AlertingConfig& RiskConfig::getAlertingConfig() const {
    return alertingConfig_;
}

const RuntimeConfig& RiskConfig::getRuntimeConfig() const {
    return runtimeConfig_;
}

const IdempotencyConfig& RiskConfig::getIdempotencyConfig() const {
    return idempotencyConfig_;
}

void RiskConfig::setPersistenceConfig(const PersistenceConfig& config) {
    persistenceConfig_ = config;
}

void RiskConfig::setAlertingConfig(const AlertingConfig& config) {
    alertingConfig_ = config;
}

void RiskConfig::setRuntimeConfig(const RuntimeConfig& config) {
    runtimeConfig_ = config;
}

void RiskConfig::setIdempotencyConfig(const IdempotencyConfig& config) {
    idempotencyConfig_ = config;
}

} // namespace trading::risk