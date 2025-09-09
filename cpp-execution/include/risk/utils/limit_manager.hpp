/**
 * Utilities for managing and applying risk limits
 *
 * File: limit_manager.hpp
 * Created: 2025-04-29
 */
#pragma once

#include "../risk_types.hpp"
#include "../risk_config.hpp"

#include <string>
#include <map>
#include <mutex>
#include <optional>
#include <vector>
namespace trading {
namespace risk {

/**
 * Structure to represent a risk limit
 */
struct RiskLimit {
    std::string limitId;
    std::string limitType;
    double value;
    bool enforceStrictly;
    Timestamp lastUpdated;
};

/**
 * LimitManager manages and applies risk limits.
 * It handles limit hierarchies and provides limit checking utilities.
 */
class LimitManager {
public:
    /**
     * Constructor
     * @param config Risk configuration
     */
    explicit LimitManager(const RiskConfig& config);
    
    /**
     * Initialize the limit manager
     * @return true if initialization was successful, false otherwise
     */
    bool initialize();
    
    /**
     * Get limit for a specific asset
     * @param assetId The asset identifier
     * @return The limit value
     */
    double getAssetLimit(const std::string& assetId) const;
    
    /**
     * Get limit for a specific strategy
     * @param strategyId The strategy identifier
     * @return The limit value
     */
    double getStrategyLimit(const StrategyId& strategyId) const;
    
    /**
     * Get limit for a specific exchange
     * @param exchangeId The exchange identifier
     * @return The limit value
     */
    double getExchangeLimit(const ExchangeId& exchangeId) const;
    
    /**
     * Get exposure limit
     * @param assetId Optional asset ID to get specific exposure limit
     * @param strategyId Optional strategy ID to get specific exposure limit
     * @param exchangeId Optional exchange ID to get specific exposure limit
     * @return The exposure limit value
     */
    double getExposureLimit(const std::optional<std::string>& assetId = std::nullopt,
                          const std::optional<StrategyId>& strategyId = std::nullopt,
                          const std::optional<ExchangeId>& exchangeId = std::nullopt) const;
    
    /**
     * Check if a value exceeds a limit
     * @param value The value to check
     * @param limitType The limit type
     * @param limitId The limit identifier
     * @return true if value exceeds limit, false otherwise
     */
    bool exceedsLimit(double value, const std::string& limitType, const std::string& limitId) const;
    
    /**
     * Update a limit
     * @param limitType The limit type
     * @param limitId The limit identifier
     * @param value The new limit value
     * @param enforceStrictly Whether to enforce the limit strictly
     * @return true if update was successful, false otherwise
     */
    bool updateLimit(const std::string& limitType, const std::string& limitId, double value, bool enforceStrictly);
    
    /**
     * Get all limits
     * @return Vector of all risk limits
     */
    std::vector<RiskLimit> getAllLimits() const;
    
    /**
     * Reset limits to default values
     * @return true if reset was successful, false otherwise
     */
    bool resetLimits();
    double getDefaultAssetLimit() const { return defaultAssetLimit_; }
    double getDefaultStrategyLimit() const { return defaultStrategyLimit_; }
    double getDefaultExchangeLimit() const { return defaultExchangeLimit_; }
    double getDefaultExposureLimit() const { return defaultExposureLimit_; }

    const std::unordered_map<std::string, double>& getAssetLimits() const { return assetLimits_; }
    const std::unordered_map<std::string, double>& getStrategyLimits() const { return strategyLimits_; }
    const std::unordered_map<std::string, double>& getExchangeLimits() const { return exchangeLimits_; }
    const std::unordered_map<std::string, double>& getExposureLimits() const { return exposureLimits_; }


private:
    RiskConfig m_config;
    
    // Limits by type and ID
    mutable std::mutex m_limitsMutex;
    std::map<std::string, std::map<std::string, RiskLimit>> m_limits;
    
    double defaultAssetLimit_{0.0};
    double defaultStrategyLimit_{0.0};
    double defaultExchangeLimit_{0.0};
    double defaultExposureLimit_{0.0};

    std::unordered_map<std::string, double> assetLimits_;
    std::unordered_map<std::string, double> strategyLimits_;
    std::unordered_map<std::string, double> exchangeLimits_;
    std::unordered_map<std::string, double> exposureLimits_;
    
    // Helper methods
    void loadLimitsFromConfig();
    std::string getLimitKey(const std::string& limitType, const std::string& limitId) const;
    RiskLimit getLimit(const std::string& limitType, const std::string& limitId) const;
};

} // namespace risk
} // namespace trading