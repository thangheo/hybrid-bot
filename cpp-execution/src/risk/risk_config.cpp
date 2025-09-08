/**
 * @file risk_config.cpp
 * @brief Implementation of Risk configuration with enhanced CircuitBreaker support
 * 
 * Modern C++20 implementation with robust JSON handling and validation.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#include "risk_config.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <format>
#include <chrono>

// Use a fully qualified namespace to avoid ambiguity
using json = nlohmann::json;

namespace trading::risk {
    
// Default constructor with reasonable default values
RiskConfig::RiskConfig() {
    // Initialize position limit config
    m_positionLimitConfig.defaultLimit = 1000.0;
    m_positionLimitConfig.enforceStrictly = true;

    // Initialize exposure limit config
    m_exposureLimitConfig.maxTotalExposure = 10000.0;
    m_exposureLimitConfig.maxAssetExposure = 2000.0;
    m_exposureLimitConfig.maxStrategyExposure = 5000.0;
    m_exposureLimitConfig.maxExchangeExposure = 8000.0;
    m_exposureLimitConfig.volatilityMultiplier = 1.5;
    m_exposureLimitConfig.includeUnrealizedPnl = true;

    // Initialize enhanced circuit breaker config
    m_circuitBreakerConfig = CircuitBreakerConfig{}; // Uses default constructor
    
    // Add additional custom thresholds if needed
    m_circuitBreakerConfig.metricThresholds["pnl_drawdown"] = MetricThreshold(0.03, 0.05, true);  // 3% warning, 5% critical
    m_circuitBreakerConfig.metricThresholds["order_rejection_rate"] = MetricThreshold(0.02, 0.05, false); // 2% warning, 5% critical

    // Initialize stop loss config  
    m_stopLossConfig.maxLossPercentage = 0.05;
    m_stopLossConfig.trailingStopDistance = 0.02;
    m_stopLossConfig.enableTrailingStop = true;
    m_stopLossConfig.forceLiquidation = true;
    m_stopLossConfig.liquidationDelay = std::chrono::seconds(5);

    // Initialize drawdown config
    m_drawdownConfig.warningThreshold = 0.10;
    m_drawdownConfig.criticalThreshold = 0.20;
    m_drawdownConfig.lookbackPeriod = std::chrono::hours(24);
    m_drawdownConfig.enableAutoRiskReduction = true;
    m_drawdownConfig.riskReductionFactor = 0.5;

    // Initialize fat finger config
    m_fatFingerConfig.maxPriceDeviation = 0.05;
    m_fatFingerConfig.maxOrderSize = 10.0;
    m_fatFingerConfig.typicalOrderSize = 1.0;
    m_fatFingerConfig.notionalValueLimit = 10000.0;
    m_fatFingerConfig.enforceAdditionalVerification = true;

    // Initialize order validation config
    m_orderValidationConfig.validateMinOrderSize = true;
    m_orderValidationConfig.validatePriceRange = true;
    m_orderValidationConfig.maxPriceDeviationPercent = 0.1;
    m_orderValidationConfig.enforceSymbolWhitelist = false;

    // Initialize balance reconciliation config with defaults
    m_balanceReconciliationConfig = BalanceReconciliationConfig{}; // Uses default constructor

    // Initialize rebalancing config
    m_rebalancingConfig.autoRebalancingEnabled = true;
    m_rebalancingConfig.autoExecuteRebalancing = true;
    m_rebalancingConfig.parallelExecution = false;
    m_rebalancingConfig.rebalancingIntervalMinutes = 60;
    m_rebalancingConfig.allocationDeviationThreshold = 0.05; // 5%
    m_rebalancingConfig.minimumOrderSize = 0.01;

    // Initialize collateral config
    m_collateralConfig.fundingSources = {"main_wallet", "backup_wallet"};
    m_collateralConfig.emergencyFundingSource = "main_wallet";
    m_collateralConfig.targetFreeCollateralRatio = 0.2;
    m_collateralConfig.enableEmergencyFunding = false;
    m_collateralConfig.maxFundingAmount = 10000.0;

    // Initialize portfolio config
    m_portfolioConfig.autoRebalancingEnabled = false;
    m_portfolioConfig.enableRealTimeMetrics = true;
    m_portfolioConfig.minimumPortfolioValue = 1000.0;
    m_portfolioConfig.baseCurrency = "USD";

    // Initialize logging config
    m_loggingConfig.logLevel = "INFO";
    m_loggingConfig.logDirectory = "/var/log/riskmanagement";
    m_loggingConfig.enableDetailedLogs = true;
    m_loggingConfig.logToConsole = true;
    m_loggingConfig.logToFile = true;
    m_loggingConfig.logRetentionPeriod = std::chrono::days(30);
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
            std::cerr << std::format("Failed to open configuration file: {}\n", configFilePath);
            return false;
        }
        
        std::string jsonStr((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
        file.close();
        
        return loadJsonConfiguration(jsonStr);
    } catch (const std::exception& e) {
        std::cerr << std::format("Exception while loading configuration: {}\n", e.what());
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
        std::cerr << std::format("Exception while parsing JSON configuration: {}\n", e.what());
        return false;
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
                m_circuitBreakerConfig.metricThresholds[metricName] = threshold;
            }
        }
    }
    
    // Load timing configuration
    if (cb.contains("coolingPeriodSeconds")) {
        int seconds = cb["coolingPeriodSeconds"].get<int>();
        m_circuitBreakerConfig.coolingPeriod = std::chrono::seconds(seconds);
        m_circuitBreakerConfig.coolingPeriodSeconds = seconds;
    }
    
    if (cb.contains("warmupPeriodSeconds")) {
        int seconds = cb["warmupPeriodSeconds"].get<int>();
        m_circuitBreakerConfig.warmupPeriod = std::chrono::seconds(seconds);
    }
    
    if (cb.contains("mandatoryHaltPeriodSeconds")) {
        m_circuitBreakerConfig.mandatoryHaltPeriodSeconds = cb["mandatoryHaltPeriodSeconds"].get<int>();
    }
    
    // Load trigger and reset behavior
    if (cb.contains("autoResume")) {
        m_circuitBreakerConfig.autoResume = cb["autoResume"].get<bool>();
    }
    
    if (cb.contains("maxTriggersPerDay")) {
        m_circuitBreakerConfig.maxTriggersPerDay = cb["maxTriggersPerDay"].get<int>();
    }
    
    if (cb.contains("maxTriggersBeforeExtendedHalt")) {
        m_circuitBreakerConfig.maxTriggersBeforeExtendedHalt = cb["maxTriggersBeforeExtendedHalt"].get<int>();
    }
    
    if (cb.contains("extendedHaltMultiplier")) {
        m_circuitBreakerConfig.extendedHaltMultiplier = cb["extendedHaltMultiplier"].get<int>();
    }
    
    if (cb.contains("triggerCountResetMinutes")) {
        m_circuitBreakerConfig.triggerCountResetMinutes = cb["triggerCountResetMinutes"].get<int>();
    }
    
    // Load history and monitoring
    if (cb.contains("maxHistorySize")) {
        m_circuitBreakerConfig.maxHistorySize = cb["maxHistorySize"].get<size_t>();
    }
    
    // Load gradual recovery
    if (cb.contains("gradualTradingResumption")) {
        m_circuitBreakerConfig.gradualTradingResumption = cb["gradualTradingResumption"].get<bool>();
    }
}

// Helper methods for loading other configs
void RiskConfig::loadPositionLimitConfig(const json& pl) {
    if (pl.contains("assetLimits") && pl["assetLimits"].is_object()) {
        for (const auto& [asset, limit] : pl["assetLimits"].items()) {
            m_positionLimitConfig.assetLimits[asset] = limit.get<double>();
        }
    }
    if (pl.contains("strategyLimits") && pl["strategyLimits"].is_object()) {
        for (const auto& [strategy, limit] : pl["strategyLimits"].items()) {
            m_positionLimitConfig.strategyLimits[strategy] = limit.get<double>();
        }
    }
    if (pl.contains("exchangeLimits") && pl["exchangeLimits"].is_object()) {
        for (const auto& [exchange, limit] : pl["exchangeLimits"].items()) {
            m_positionLimitConfig.exchangeLimits[exchange] = limit.get<double>();
        }
    }
    if (pl.contains("defaultLimit")) {
        m_positionLimitConfig.defaultLimit = pl["defaultLimit"].get<double>();
    }
    if (pl.contains("enforceStrictly")) {
        m_positionLimitConfig.enforceStrictly = pl["enforceStrictly"].get<bool>();
    }
}

void RiskConfig::loadExposureLimitConfig(const json& el) {
    if (el.contains("maxTotalExposure")) {
        m_exposureLimitConfig.maxTotalExposure = el["maxTotalExposure"].get<double>();
    }
    if (el.contains("maxAssetExposure")) {
        m_exposureLimitConfig.maxAssetExposure = el["maxAssetExposure"].get<double>();
    }
    if (el.contains("maxStrategyExposure")) {
        m_exposureLimitConfig.maxStrategyExposure = el["maxStrategyExposure"].get<double>();
    }
    if (el.contains("maxExchangeExposure")) {
        m_exposureLimitConfig.maxExchangeExposure = el["maxExchangeExposure"].get<double>();
    }
    if (el.contains("volatilityMultiplier")) {
        m_exposureLimitConfig.volatilityMultiplier = el["volatilityMultiplier"].get<double>();
    }
    if (el.contains("includeUnrealizedPnl")) {
        m_exposureLimitConfig.includeUnrealizedPnl = el["includeUnrealizedPnl"].get<bool>();
    }
}

void RiskConfig::loadStopLossConfig(const json& sl) {
    if (sl.contains("maxLossPercentage")) {
        m_stopLossConfig.maxLossPercentage = sl["maxLossPercentage"].get<double>();
    }
    if (sl.contains("trailingStopDistance")) {
        m_stopLossConfig.trailingStopDistance = sl["trailingStopDistance"].get<double>();
    }
    if (sl.contains("enableTrailingStop")) {
        m_stopLossConfig.enableTrailingStop = sl["enableTrailingStop"].get<bool>();
    }
    if (sl.contains("forceLiquidation")) {
        m_stopLossConfig.forceLiquidation = sl["forceLiquidation"].get<bool>();
    }
    if (sl.contains("liquidationDelaySeconds")) {
        m_stopLossConfig.liquidationDelay = std::chrono::seconds(sl["liquidationDelaySeconds"].get<int>());
    }
}

void RiskConfig::loadDrawdownConfig(const json& dd) {
    if (dd.contains("warningThreshold")) {
        m_drawdownConfig.warningThreshold = dd["warningThreshold"].get<double>();
    }
    if (dd.contains("criticalThreshold")) {
        m_drawdownConfig.criticalThreshold = dd["criticalThreshold"].get<double>();
    }
    if (dd.contains("lookbackPeriodHours")) {
        m_drawdownConfig.lookbackPeriod = std::chrono::hours(dd["lookbackPeriodHours"].get<int>());
    }
    if (dd.contains("enableAutoRiskReduction")) {
        m_drawdownConfig.enableAutoRiskReduction = dd["enableAutoRiskReduction"].get<bool>();
    }
    if (dd.contains("riskReductionFactor")) {
        m_drawdownConfig.riskReductionFactor = dd["riskReductionFactor"].get<double>();
    }
}

void RiskConfig::loadFatFingerConfig(const json& ff) {
    if (ff.contains("maxPriceDeviation")) {
        m_fatFingerConfig.maxPriceDeviation = ff["maxPriceDeviation"].get<double>();
    }
    if (ff.contains("maxOrderSize")) {
        m_fatFingerConfig.maxOrderSize = ff["maxOrderSize"].get<double>();
    }
    if (ff.contains("typicalOrderSize")) {
        m_fatFingerConfig.typicalOrderSize = ff["typicalOrderSize"].get<double>();
    }
    if (ff.contains("notionalValueLimit")) {
        m_fatFingerConfig.notionalValueLimit = ff["notionalValueLimit"].get<double>();
    }
    if (ff.contains("enforceAdditionalVerification")) {
        m_fatFingerConfig.enforceAdditionalVerification = ff["enforceAdditionalVerification"].get<bool>();
    }
}

void RiskConfig::loadOrderValidationConfig(const json& ov) {
    if (ov.contains("validateMinOrderSize")) {
        m_orderValidationConfig.validateMinOrderSize = ov["validateMinOrderSize"].get<bool>();
    }
    if (ov.contains("validatePriceRange")) {
        m_orderValidationConfig.validatePriceRange = ov["validatePriceRange"].get<bool>();
    }
    if (ov.contains("minOrderSizes") && ov["minOrderSizes"].is_object()) {
        for (const auto& [exchangeIdStr, pairs] : ov["minOrderSizes"].items()) {
            core::ExchangeId exchangeId = core::ExchangeId::fromString(exchangeIdStr);
            if (pairs.is_object()) {
                for (const auto& [symbolStr, minSize] : pairs.items()) {
                    TradingPair pair;
                    pair.symbol = symbolStr;
                    // Parse base/quote from symbol if needed
                    auto separatorPos = symbolStr.find("/");
                    if (separatorPos != std::string::npos) {
                        pair.base = symbolStr.substr(0, separatorPos);
                        pair.quote = symbolStr.substr(separatorPos + 1);
                    }
                    m_orderValidationConfig.minOrderSizes[exchangeId][pair] = minSize.get<double>();
                }
            }
        }
    }
    if (ov.contains("maxPriceDeviationPercent")) {
        m_orderValidationConfig.maxPriceDeviationPercent = ov["maxPriceDeviationPercent"].get<double>();
    }
    if (ov.contains("enforceSymbolWhitelist")) {
        m_orderValidationConfig.enforceSymbolWhitelist = ov["enforceSymbolWhitelist"].get<bool>();
    }
    if (ov.contains("symbolWhitelist") && ov["symbolWhitelist"].is_array()) {
        m_orderValidationConfig.symbolWhitelist.clear();
        for (const auto& symbol : ov["symbolWhitelist"]) {
            m_orderValidationConfig.symbolWhitelist.push_back(symbol.get<std::string>());
        }
    }
}

void RiskConfig::loadBalanceReconciliationConfig(const json& br) {
    if (br.contains("reconciliationFrequencyMinutes")) {
        m_balanceReconciliationConfig.reconciliationFrequency = 
            std::chrono::minutes(br["reconciliationFrequencyMinutes"].get<int>());
    }
    if (br.contains("maxDiscrepancyPercent")) {
        m_balanceReconciliationConfig.maxDiscrepancyPercent = br["maxDiscrepancyPercent"].get<double>();
    }
    if (br.contains("autoCorrectDiscrepancies")) {
        m_balanceReconciliationConfig.autoCorrectDiscrepancies = br["autoCorrectDiscrepancies"].get<bool>();
    }
    if (br.contains("alertOnDiscrepancy")) {
        m_balanceReconciliationConfig.alertOnDiscrepancy = br["alertOnDiscrepancy"].get<bool>();
    }
    if (br.contains("maxConsecutiveFailures")) {
        m_balanceReconciliationConfig.maxConsecutiveFailures = br["maxConsecutiveFailures"].get<int>();
    }
    
    // Load extended settings
    if (br.contains("minimumPositionSize")) {
        m_balanceReconciliationConfig.minimumPositionSize = br["minimumPositionSize"].get<double>();
    }
    if (br.contains("exchangeTimeoutSeconds")) {
        m_balanceReconciliationConfig.exchangeTimeoutSeconds = 
            std::chrono::seconds(br["exchangeTimeoutSeconds"].get<int>());
    }
    if (br.contains("enableDetailedLogging")) {
        m_balanceReconciliationConfig.enableDetailedLogging = br["enableDetailedLogging"].get<bool>();
    }
    // Add other extended settings as needed...
}

void RiskConfig::loadRebalancingConfig(const json& rb) {
    if (rb.contains("autoRebalancingEnabled")) {
        m_rebalancingConfig.autoRebalancingEnabled = rb["autoRebalancingEnabled"].get<bool>();
    }
    if (rb.contains("autoExecuteRebalancing")) {
        m_rebalancingConfig.autoExecuteRebalancing = rb["autoExecuteRebalancing"].get<bool>();
    }
    if (rb.contains("allocationDeviationThreshold")) {
        m_rebalancingConfig.allocationDeviationThreshold = rb["allocationDeviationThreshold"].get<double>();
    }
    if (rb.contains("minimumOrderSize")) {
        m_rebalancingConfig.minimumOrderSize = rb["minimumOrderSize"].get<double>();
    }
    if (rb.contains("targetAllocations") && rb["targetAllocations"].is_object()) {
        for (const auto& [asset, allocation] : rb["targetAllocations"].items()) {
            m_rebalancingConfig.targetAllocations[asset] = allocation.get<double>();
        }
    }
    // Add other rebalancing settings...
}

void RiskConfig::loadCollateralConfig(const json& co) {
    if (co.contains("targetFreeCollateralRatio")) {
        m_collateralConfig.targetFreeCollateralRatio = co["targetFreeCollateralRatio"].get<double>();
    }
    if (co.contains("enableEmergencyFunding")) {
        m_collateralConfig.enableEmergencyFunding = co["enableEmergencyFunding"].get<bool>();
    }
    if (co.contains("fundingSources") && co["fundingSources"].is_array()) {
        m_collateralConfig.fundingSources.clear();
        for (const auto& source : co["fundingSources"]) {
            m_collateralConfig.fundingSources.push_back(source.get<std::string>());
        }
    }
    if (co.contains("maxFundingAmount")) {
        m_collateralConfig.maxFundingAmount = co["maxFundingAmount"].get<double>();
    }
    // Add other collateral settings...
}

void RiskConfig::loadPortfolioConfig(const json& po) {
    if (po.contains("enableRealTimeMetrics")) {
        m_portfolioConfig.enableRealTimeMetrics = po["enableRealTimeMetrics"].get<bool>();
    }
    if (po.contains("minimumPortfolioValue")) {
        m_portfolioConfig.minimumPortfolioValue = po["minimumPortfolioValue"].get<double>();
    }
    if (po.contains("baseCurrency")) {
        m_portfolioConfig.baseCurrency = po["baseCurrency"].get<std::string>();
    }
    // Add other portfolio settings...
}

void RiskConfig::loadLoggingConfig(const json& lg) {
    if (lg.contains("logLevel")) {
        m_loggingConfig.logLevel = lg["logLevel"].get<std::string>();
    }
    if (lg.contains("logDirectory")) {
        m_loggingConfig.logDirectory = lg["logDirectory"].get<std::string>();
    }
    if (lg.contains("enableDetailedLogs")) {
        m_loggingConfig.enableDetailedLogs = lg["enableDetailedLogs"].get<bool>();
    }
    if (lg.contains("logToConsole")) {
        m_loggingConfig.logToConsole = lg["logToConsole"].get<bool>();
    }
    if (lg.contains("logToFile")) {
        m_loggingConfig.logToFile = lg["logToFile"].get<bool>();
    }
    if (lg.contains("logRetentionDays")) {
        m_loggingConfig.logRetentionPeriod = std::chrono::days(lg["logRetentionDays"].get<int>());
    }
}

// Enhanced configuration saving with proper MetricThreshold serialization
bool RiskConfig::saveToFile(const std::string& configFilePath) const {
    try {
        json j;
        
        // Position limit config
        j["positionLimit"] = {
            {"assetLimits", m_positionLimitConfig.assetLimits},
            {"strategyLimits", m_positionLimitConfig.strategyLimits},
            {"exchangeLimits", m_positionLimitConfig.exchangeLimits},
            {"defaultLimit", m_positionLimitConfig.defaultLimit},
            {"enforceStrictly", m_positionLimitConfig.enforceStrictly}
        };
        
        // Exposure limit config
        j["exposureLimit"] = {
            {"maxTotalExposure", m_exposureLimitConfig.maxTotalExposure},
            {"maxAssetExposure", m_exposureLimitConfig.maxAssetExposure},
            {"maxStrategyExposure", m_exposureLimitConfig.maxStrategyExposure},
            {"maxExchangeExposure", m_exposureLimitConfig.maxExchangeExposure},
            {"volatilityMultiplier", m_exposureLimitConfig.volatilityMultiplier},
            {"includeUnrealizedPnl", m_exposureLimitConfig.includeUnrealizedPnl}
        };
        
        // Enhanced circuit breaker config
        json metricThresholds;
        for (const auto& [metricName, threshold] : m_circuitBreakerConfig.metricThresholds) {
            metricThresholds[metricName] = {
                {"warning", threshold.warningThreshold},
                {"critical", threshold.criticalThreshold},
                {"isHigherBetter", threshold.isHigherBetter}
            };
        }
        
        j["circuitBreaker"] = {
            {"metricThresholds", metricThresholds},
            {"coolingPeriodSeconds", m_circuitBreakerConfig.coolingPeriod.count()},
            {"warmupPeriodSeconds", m_circuitBreakerConfig.warmupPeriod.count()},
            {"mandatoryHaltPeriodSeconds", m_circuitBreakerConfig.mandatoryHaltPeriodSeconds},
            {"autoResume", m_circuitBreakerConfig.autoResume},
            {"maxTriggersPerDay", m_circuitBreakerConfig.maxTriggersPerDay},
            {"maxTriggersBeforeExtendedHalt", m_circuitBreakerConfig.maxTriggersBeforeExtendedHalt},
            {"extendedHaltMultiplier", m_circuitBreakerConfig.extendedHaltMultiplier},
            {"triggerCountResetMinutes", m_circuitBreakerConfig.triggerCountResetMinutes},
            {"maxHistorySize", m_circuitBreakerConfig.maxHistorySize},
            {"gradualTradingResumption", m_circuitBreakerConfig.gradualTradingResumption}
        };
        
        // Stop loss config
        j["stopLoss"] = {
            {"maxLossPercentage", m_stopLossConfig.maxLossPercentage},
            {"trailingStopDistance", m_stopLossConfig.trailingStopDistance},
            {"enableTrailingStop", m_stopLossConfig.enableTrailingStop},
            {"forceLiquidation", m_stopLossConfig.forceLiquidation},
            {"liquidationDelaySeconds", m_stopLossConfig.liquidationDelay.count()}
        };
        
        // Add other configuration sections...
        saveOtherConfigs(j);
        
        // Write to file with pretty formatting
        std::ofstream file(configFilePath);
        if (!file.is_open()) {
            std::cerr << std::format("Failed to open file for writing: {}\n", configFilePath);
            return false;
        }
        
        file << j.dump(4); // Pretty print with 4-space indentation
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << std::format("Exception while saving configuration: {}\n", e.what());
        return false;
    }
}

void RiskConfig::saveOtherConfigs(json& j) const {
    // Drawdown config
    j["drawdown"] = {
        {"warningThreshold", m_drawdownConfig.warningThreshold},
        {"criticalThreshold", m_drawdownConfig.criticalThreshold},
        {"lookbackPeriodHours", m_drawdownConfig.lookbackPeriod.count()},
        {"enableAutoRiskReduction", m_drawdownConfig.enableAutoRiskReduction},
        {"riskReductionFactor", m_drawdownConfig.riskReductionFactor}
    };
    
    // Add other configs as needed...
}

// Enhanced validation with proper error reporting
bool RiskConfig::validate() const {
    try {
        // Validate circuit breaker config using its own validation
        if (!m_circuitBreakerConfig.isValid()) {
            std::cerr << "Circuit breaker configuration is invalid\n";
            return false;
        }
        
        // Validate balance reconciliation config
        if (!m_balanceReconciliationConfig.isValid()) {
            std::cerr << "Balance reconciliation configuration is invalid\n";
            return false;
        }
        
        // Validate position limits
        if (m_positionLimitConfig.defaultLimit <= 0) {
            std::cerr << "Invalid default position limit: must be positive\n";
            return false;
        }
        
        // Validate exposure limits
        if (m_exposureLimitConfig.maxTotalExposure <= 0 ||
            m_exposureLimitConfig.maxAssetExposure <= 0 ||
            m_exposureLimitConfig.maxStrategyExposure <= 0 ||
            m_exposureLimitConfig.maxExchangeExposure <= 0) {
            std::cerr << "Invalid exposure limits: all limits must be positive\n";
            return false;
        }
        
        // Validate metric thresholds consistency
        for (const auto& [metricName, threshold] : m_circuitBreakerConfig.metricThresholds) {
            if (threshold.isHigherBetter) {
                // For "higher is better" metrics, warning should be higher than critical
                if (threshold.warningThreshold <= threshold.criticalThreshold) {
                    std::cerr << std::format("Invalid thresholds for {}: warning ({}) should be higher than critical ({})\n",
                        metricName, threshold.warningThreshold, threshold.criticalThreshold);
                    return false;
                }
            } else {
                // For "lower is better" metrics, critical should be higher than warning
                if (threshold.criticalThreshold <= threshold.warningThreshold) {
                    std::cerr << std::format("Invalid thresholds for {}: critical ({}) should be higher than warning ({})\n",
                        metricName, threshold.criticalThreshold, threshold.warningThreshold);
                    return false;
                }
            }
        }
        
        // Add other validation logic as needed...
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << std::format("Exception during configuration validation: {}\n", e.what());
        return false;
    }
}

// Getter implementations (unchanged from original)
const PositionLimitConfig& RiskConfig::getPositionLimitConfig() const {
    return m_positionLimitConfig;
}

const ExposureLimitConfig& RiskConfig::getExposureLimitConfig() const {
    return m_exposureLimitConfig;
}

const CircuitBreakerConfig& RiskConfig::getCircuitBreakerConfig() const {
    return m_circuitBreakerConfig;
}

const StopLossConfig& RiskConfig::getStopLossConfig() const {
    return m_stopLossConfig;
}

const DrawdownConfig& RiskConfig::getDrawdownConfig() const {
    return m_drawdownConfig;
}

const FatFingerConfig& RiskConfig::getFatFingerConfig() const {
    return m_fatFingerConfig;
}

const OrderValidationConfig& RiskConfig::getOrderValidationConfig() const {
    return m_orderValidationConfig;
}

const BalanceReconciliationConfig& RiskConfig::getBalanceReconciliationConfig() const {
    return m_balanceReconciliationConfig;
}

const RebalancingConfig& RiskConfig::getRebalancingConfig() const {
    return m_rebalancingConfig;
}

const CollateralConfig& RiskConfig::getCollateralConfig() const {
    return m_collateralConfig;
}

const PortfolioConfig& RiskConfig::getPortfolioConfig() const {
    return m_portfolioConfig;
}

const RiskLoggingConfig& RiskConfig::getLoggingConfig() const {
    return m_loggingConfig;
}

// Setter implementations (unchanged from original)
void RiskConfig::setPositionLimitConfig(const PositionLimitConfig& config) {
    m_positionLimitConfig = config;
}

void RiskConfig::setExposureLimitConfig(const ExposureLimitConfig& config) {
    m_exposureLimitConfig = config;
}

void RiskConfig::setCircuitBreakerConfig(const CircuitBreakerConfig& config) {
    m_circuitBreakerConfig = config;
}

void RiskConfig::setStopLossConfig(const StopLossConfig& config) {
    m_stopLossConfig = config;
}

void RiskConfig::setDrawdownConfig(const DrawdownConfig& config) {
    m_drawdownConfig = config;
}

void RiskConfig::setFatFingerConfig(const FatFingerConfig& config) {
    m_fatFingerConfig = config;
}

void RiskConfig::setOrderValidationConfig(const OrderValidationConfig& config) {
    m_orderValidationConfig = config;
}

void RiskConfig::setBalanceReconciliationConfig(const BalanceReconciliationConfig& config) {
    m_balanceReconciliationConfig = config;
}

void RiskConfig::setRebalancingConfig(const RebalancingConfig& config) {
    m_rebalancingConfig = config;
}

void RiskConfig::setCollateralConfig(const CollateralConfig& config) {
    m_collateralConfig = config;
}

void RiskConfig::setPortfolioConfig(const PortfolioConfig& config) {
    m_portfolioConfig = config;
}

void RiskConfig::setLoggingConfig(const RiskLoggingConfig& config) {
    m_loggingConfig = config;
}

} // namespace trading::risk