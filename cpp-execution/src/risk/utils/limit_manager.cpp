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
 
 namespace trading::risk {
 
 LimitManager::LimitManager(const RiskConfig& config)
     : m_config(config)
     , defaultAssetLimit_(1000000.0)  // Default values
     , defaultStrategyLimit_(500000.0)
     , defaultExchangeLimit_(2000000.0)
     , defaultExposureLimit_(5000000.0) {
 }
 
 bool LimitManager::initialize() {
     try {
         // Load initial limits from configuration
         loadLimitsFromConfig();
         
         // Log initialization
         RISK_LOG_INFO("LimitManager", "Limit manager initialized", {
             {"default_asset_limit", std::to_string(defaultAssetLimit_)},
             {"default_strategy_limit", std::to_string(defaultStrategyLimit_)},
             {"default_exchange_limit", std::to_string(defaultExchangeLimit_)},
             {"default_exposure_limit", std::to_string(defaultExposureLimit_)}
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
 
//  double LimitManager::getAssetLimit(const std::string& assetId) const {
//      // Get asset-specific limit or default
//      RiskLimit limit = getLimit("ASSET", assetId);
//      return limit.limitId.empty() ? defaultAssetLimit_ : limit.value;
//  }
 
 double LimitManager::getStrategyLimit(const StrategyId& strategyId) const {
     // Get strategy-specific limit or default
     RiskLimit limit = getLimit("STRATEGY", strategyId.id);
     return limit.limitId.empty() ? defaultStrategyLimit_ : limit.value;
 }
 
 double LimitManager::getExchangeLimit(const ExchangeId& exchangeId) const {
     // Get exchange-specific limit or default
     RiskLimit limit = getLimit("EXCHANGE", exchangeId.toString());
     return limit.limitId.empty() ? defaultExchangeLimit_ : limit.value;
 }
 
 double LimitManager::getExposureLimit(const std::optional<std::string>& assetId,
                                      const std::optional<StrategyId>& strategyId,
                                      const std::optional<ExchangeId>& exchangeId) const {
     // Try to get the most specific limit based on provided parameters
     if (assetId && strategyId && exchangeId) {
         // Combined asset-strategy-exchange limit
         std::string key = *assetId + "_" + strategyId->id + "_" + exchangeId->toString();
         RiskLimit limit = getLimit("EXPOSURE_COMBINED", key);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     if (assetId && strategyId) {
         // Asset-strategy specific limit
         std::string key = *assetId + "_" + strategyId->id;
         RiskLimit limit = getLimit("EXPOSURE_ASSET_STRATEGY", key);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     if (assetId && exchangeId) {
         // Asset-exchange specific limit
         std::string key = *assetId + "_" + exchangeId->toString();
         RiskLimit limit = getLimit("EXPOSURE_ASSET_EXCHANGE", key);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     if (strategyId && exchangeId) {
         // Strategy-exchange specific limit
         std::string key = strategyId->id + "_" + exchangeId->toString();
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
         RiskLimit limit = getLimit("EXPOSURE_STRATEGY", strategyId->id);
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     if (exchangeId.has_value()) {
         RiskLimit limit = getLimit("EXPOSURE_EXCHANGE", exchangeId->toString());
         if (!limit.limitId.empty()) {
             return limit.value;
         }
     }
     
     // Default global exposure limit
     return defaultExposureLimit_;
 }
 
 bool LimitManager::exceedsLimit(double value, const std::string& limitType, const std::string& limitId) const {
     // Get the applicable limit
     RiskLimit limit = getLimit(limitType, limitId);
     
     // If limit doesn't exist, check for default values
     double limitValue;
     
     if (limit.limitId.empty()) {
         if (limitType == "ASSET") {
             limitValue = defaultAssetLimit_;
         } else if (limitType == "STRATEGY") {
             limitValue = defaultStrategyLimit_;
         } else if (limitType == "EXCHANGE") {
             limitValue = defaultExchangeLimit_;
         } else if (limitType.find("EXPOSURE") == 0) {
             limitValue = defaultExposureLimit_;
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
         
        //  RISK_LOG_INFO("LimitManager", "Limits reset to defaults");
         
         return true;
     }
     catch (const std::exception& ex) {
         RISK_LOG_ERROR("LimitManager", "Failed to reset limits", {
             {"error", ex.what()}
         });
         return false;
     }
 }
//  getAssetLimits
const std::unordered_map<std::string, double>& LimitManager::getAssetLimits() const {
    std::lock_guard<std::mutex> lock(m_limitsMutex);
    static std::unordered_map<std::string, double> assetLimits;
    assetLimits.clear();
    
    auto typeIt = m_limits.find("ASSET");
    if (typeIt != m_limits.end()) {
        for (const auto& [id, limit] : typeIt->second) {
            assetLimits[id] = limit.value;
        }
    }
    return assetLimits;
}

const std::unordered_map<std::string, double>& LimitManager::getStrategyLimits() const {
    std::lock_guard<std::mutex> lock(m_limitsMutex);
    static std::unordered_map<std::string, double> strategyLimits;
    strategyLimits.clear();
    
    auto typeIt = m_limits.find("STRATEGY");
    if (typeIt != m_limits.end()) {
        for (const auto& [id, limit] : typeIt->second) {
            strategyLimits[id] = limit.value;
        }
    }
    return strategyLimits;
}

const std::unordered_map<std::string, double>& LimitManager::getExchangeLimits() const {
    std::lock_guard<std::mutex> lock(m_limitsMutex);
    static std::unordered_map<std::string, double> exchangeLimits;
    exchangeLimits.clear();
    
    auto typeIt = m_limits.find("EXCHANGE");
    if (typeIt != m_limits.end()) {
        for (const auto& [id, limit] : typeIt->second) {
            exchangeLimits[id] = limit.value;
        }
    }
    return exchangeLimits;
}

const std::unordered_map<std::string, double>& LimitManager::getExposureLimits() const {
    std::lock_guard<std::mutex> lock(m_limitsMutex);
    static std::unordered_map<std::string, double> exposureLimits;
    exposureLimits.clear();
    
    auto typeIt = m_limits.find("EXPOSURE");
    if (typeIt != m_limits.end()) {
        for (const auto& [id, limit] : typeIt->second) {
            exposureLimits[id] = limit.value;
        }
    }
    return exposureLimits;
}

void LimitManager::loadLimitsFromConfig() {
     // TODO: Implement loading limits from config when RiskConfig provides the necessary methods
     // For now, use default limits that are already set in the constructor
     
     RISK_LOG_INFO("LimitManager", "Using default limits - config loading not implemented", {
         {"default_asset_limit", std::to_string(defaultAssetLimit_)},
         {"default_strategy_limit", std::to_string(defaultStrategyLimit_)},
         {"default_exchange_limit", std::to_string(defaultExchangeLimit_)},
         {"default_exposure_limit", std::to_string(defaultExposureLimit_)}
     });
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

 } // namespace trading::risk
