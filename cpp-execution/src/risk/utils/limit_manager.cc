/**
 * Implementation of limit manager for risk limits
 *
 * File: limit_manager.cpp
 * Created: 2025-04-29
 */

 #include "risk/utils/limit_manager.hpp"
 #include "risk/utils/risk_logger.hpp"
 #include <algorithm>
 #include <stdexcept>
 
 namespace risk {
 
 LimitManager::LimitManager(const RiskConfig& config)
     : m_config(config)
     , m_defaultAssetLimit(1000000.0)  // Default values
     , m_defaultStrategyLimit(500000.0)
     , m_defaultExchangeLimit(2000000.0)
     , m_defaultExposureLimit(5000000.0) {
 }
 
 bool LimitManager::initialize() {
     try {
         // Load initial limits from configuration
         loadLimitsFromConfig();
         
         // Log initialization
         RISK_LOG_INFO("LimitManager", "Limit manager initialized", {
             {"default_asset_limit", std::to_string(m_defaultAssetLimit)},
             {"default_strategy_limit", std::to_string(m_defaultStrategyLimit)},
             {"default_exchange_limit", std::to_string(m_defaultExchangeLimit)},
             {"default_exposure_limit", std::to_string(m_defaultExposureLimit)}
         });
         
         return true;
     }
     catch (const std::exception& ex) {
         RISK_LOG_ERROR("LimitManager", "Failed to initialize limit manager", {
             {"error", ex.what()}
         });
         return false;
     }
 }
 
 double LimitManager::getAssetLimit(const std::string& assetId) const {
     // Get asset-specific limit or default
     RiskLimit limit = getLimit("ASSET", assetId);
     return limit.limitId.empty() ? m_defaultAssetLimit : limit.value;
 }
 
 double LimitManager::getStrategyLimit(const StrategyId& strategyId) const {
     // Get strategy-specific limit or default
     RiskLimit limit = getLimit("STRATEGY", strategyId);
     return limit.limitId.empty() ? m_defaultStrategyLimit : limit.value;
 }
 
 double LimitManager::getExchangeLimit(const ExchangeId& exchangeId) const {
     // Get exchange-specific limit or default
     RiskLimit limit = getLimit("EXCHANGE", exchangeId);
     return limit.limitId.empty() ? m_defaultExchangeLimit : limit.value;
 }
 
 double LimitManager::getExposureLimit(const std::optional<std::string>& assetId,
                                      const std::optional<StrategyId>& strategyId,
                                      const std::optional<ExchangeId>& exchangeId) const {
     // Try to get the most specific limit based on provided parameters
     if (assetId && strategyId && exchangeId) {
         // Combined asset-strategy-exchange limit
         std::string key = *assetId + "_" + *strategyId + "_" + *exchangeId;
         RiskLimit limit = getLimit("EXPOSURE_COMBINED", key);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     if (assetId && strategyId) {
         // Asset-strategy specific limit
         std::string key = *assetId + "_" + *strategyId;
         RiskLimit limit = getLimit("EXPOSURE_ASSET_STRATEGY", key);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     if (assetId && exchangeId) {
         // Asset-exchange specific limit
         std::string key = *assetId + "_" + *exchangeId;
         RiskLimit limit = getLimit("EXPOSURE_ASSET_EXCHANGE", key);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     if (strategyId && exchangeId) {
         // Strategy-exchange specific limit
         std::string key = *strategyId + "_" + *exchangeId;
         RiskLimit limit = getLimit("EXPOSURE_STRATEGY_EXCHANGE", key);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     // Individual limits
     if (assetId) {
         RiskLimit limit = getLimit("EXPOSURE_ASSET", *assetId);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     if (strategyId) {
         RiskLimit limit = getLimit("EXPOSURE_STRATEGY", *strategyId);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     if (exchangeId) {
         RiskLimit limit = getLimit("EXPOSURE_EXCHANGE", *exchangeId);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     // Default global exposure limit
     return m_defaultExposureLimit;
 }
 
 bool LimitManager::exceedsLimit(double value, const std::string& limitType, const std::string& limitId) const {
     // Get the applicable limit
     RiskLimit limit = getLimit(limitType, limitId);
     
     // If limit doesn't exist, check for default values
     double limitValue;
     
     if (limit.limitId.empty()) {
         if (limitType == "ASSET") {
             limitValue = m_defaultAssetLimit;
         } else if (limitType == "STRATEGY") {
             limitValue = m_defaultStrategyLimit;
         } else if (limitType == "EXCHANGE") {
             limitValue = m_defaultExchangeLimit;
         } else if (limitType.find("EXPOSURE") == 0) {
             limitValue = m_defaultExposureLimit;
         } else {
             RISK_LOG_WARN("LimitManager", "Unknown limit type", {
                 {"limit_type", limitType}, 
                 {"limit_id", limitId}
             });
             return false;  // Unknown limit type doesn't exceed
         }
     } else {
         limitValue = limit.value;
     }
     
     // Check if value exceeds limit
     bool exceeds = value > limitValue;
     
     if (exceeds) {
         RISK_LOG_WARN("LimitManager", "Limit exceeded", {
             {"limit_type", limitType},
             {"limit_id", limitId},
             {"limit_value", std::to_string(limitValue)},
             {"actual_value", std::to_string(value)}
         });
     }
     
     return exceeds;
 }
 
 bool LimitManager::updateLimit(const std::string& limitType, const std::string& limitId, double value, bool enforceStrictly) {
     if (value <= 0.0) {
         RISK_LOG_ERROR("LimitManager", "Invalid limit value", {
             {"limit_type", limitType},
             {"limit_id", limitId},
             {"value", std::to_string(value)}
         });
         return false;
     }
     
     std::lock_guard<std::mutex> lock(m_limitsMutex);
     
     // Create or update limit
     RiskLimit limit;
     limit.limitId = limitId;
     limit.limitType = limitType;
     limit.value = value;
     limit.enforceStrictly = enforceStrictly;
     limit.lastUpdated = std::chrono::system_clock::now();
     
     m_limits[limitType][limitId] = limit;
     
     RISK_LOG_INFO("LimitManager", "Limit updated", {
         {"limit_type", limitType},
         {"limit_id", limitId},
         {"value", std::to_string(value)},
         {"enforce_strictly", enforceStrictly ? "true" : "false"}
     });
     
     return true;
 }
 
 std::vector<RiskLimit> LimitManager::getAllLimits() const {
     std::lock_guard<std::mutex> lock(m_limitsMutex);
     
     std::vector<RiskLimit> allLimits;
     
     for (const auto& [type, limitsMap] : m_limits) {
         for (const auto& [id, limit] : limitsMap) {
             allLimits.push_back(limit);
         }
     }
     
     return allLimits;
 }
 
 bool LimitManager::resetLimits() {
     std::lock_guard<std::mutex> lock(m_limitsMutex);
     
     try {
         m_limits.clear();
         loadLimitsFromConfig();
         
         RISK_LOG_INFO("LimitManager", "Limits reset to defaults");
         
         return true;
     }
     catch (const std::exception& ex) {
         RISK_LOG_ERROR("LimitManager", "Failed to reset limits", {
             {"error", ex.what()}
         });
         return false;
     }
 }
 
 void LimitManager::loadLimitsFromConfig() {
     // Load default values
     m_defaultAssetLimit = m_config.getDefaultAssetLimit();
     m_defaultStrategyLimit = m_config.getDefaultStrategyLimit();
     m_defaultExchangeLimit = m_config.getDefaultExchangeLimit();
     m_defaultExposureLimit = m_config.getDefaultExposureLimit();
     
     // Load asset limits
     for (const auto& [assetId, limitValue] : m_config.getAssetLimits()) {
         updateLimit("ASSET", assetId, limitValue, true);
     }
     
     // Load strategy limits
     for (const auto& [strategyId, limitValue] : m_config.getStrategyLimits()) {
         updateLimit("STRATEGY", strategyId, limitValue, true);
     }
     
     // Load exchange limits
     for (const auto& [exchangeId, limitValue] : m_config.getExchangeLimits()) {
         updateLimit("EXCHANGE", exchangeId, limitValue, true);
     }
     
     // Load exposure limits
     for (const auto& [exposureKey, limitValue] : m_config.getExposureLimits()) {
         updateLimit("EXPOSURE", exposureKey, limitValue, true);
     }
 }
 
 std::string LimitManager::getLimitKey(const std::string& limitType, const std::string& limitId) const {
     return limitType + ":" + limitId;
 }
 
 RiskLimit LimitManager::getLimit(const std::string& limitType, const std::string& limitId) const {
     std::lock_guard<std::mutex> lock(m_limitsMutex);
     
     // Check if limit type exists
     auto typeIt = m_limits.find(limitType);
     if (typeIt == m_limits.end()) {
         return RiskLimit(); // Empty limit (not found)
     }
     
     // Check if limit ID exists
     auto idIt = typeIt->second.find(limitId);
     if (idIt == typeIt->second.end()) {
         return RiskLimit(); // Empty limit (not found)
     }
     
     return idIt->second;
 }
 
 } // namespace risk