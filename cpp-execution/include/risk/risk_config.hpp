/**
 * @file risk_config.hpp (Fixed version)
 * @brief Risk configuration structures and parameter definitions
 * 
 * Fix for namespace ambiguity with nlohmann::json
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#pragma once

#include "risk_types.hpp"
#include "risk_events.hpp"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>
#include <common/types.hpp>
#include <chrono>
#include <communication/zmq_server_client.hpp>
using namespace trading;
using namespace trading::common;
using namespace trading::communication;
using namespace trading::risk;
using namespace nlohmann;
namespace nlohmann {
    // class basic_json;
    using json = basic_json<>;
}

// REMOVE FORWARD DECLARATION - causes ambiguity with nlohmann::json
// Forward declarations for helper methods
// using json = nlohmann::json;  // Move this to implementation file only

/**
 * Configuration structure for position limits
 */
struct PositionLimitConfig {
    std::map<std::string, double> assetLimits;
    // Optional notional limits (quote currency, e.g., USD/USDT) per asset.
    // When present, these will be converted to asset units at runtime using market prices.
    std::map<std::string, double> assetNotionalLimits;
    std::map<std::string, double> strategyLimits;
    std::map<std::string, double> exchangeLimits;
    double defaultLimit = 1000.0;
    bool enforceStrictly = true;
};

/**
 * Configuration for position sizing algorithms
 */
struct PositionSizingConfig {
    std::string method = "kelly_criterion"; // kelly_criterion, volatility_adjusted, fixed_percent, risk_parity
    double base_risk_per_trade = 0.02;        // 2% default
    double max_position_size = 0.15;          // 15% default
    double kelly_safety_factor = 0.25;        // use 25% of Kelly by default
    int volatility_lookback = 14;             // lookback window for volatility

    PositionSizingConfig() = default;
};

/**
 * Configuration structure for exposure limits
 */
struct ExposureLimitConfig {
    double maxTotalExposure = 10000.0;
    double maxAssetExposure = 2000.0;
    double maxStrategyExposure = 5000.0;
    double maxExchangeExposure = 8000.0;
    double volatilityMultiplier = 1.5;
    bool includeUnrealizedPnl = true;
};

/**
 * Metric threshold structure for circuit breaker
 */
struct MetricThreshold {
    double warningThreshold = 0.0;
    double criticalThreshold = 0.0;
    bool isHigherBetter = false;  // true if higher values are better (e.g., success rate)
    
    MetricThreshold() = default;
    MetricThreshold(double warning, double critical, bool higherBetter = false)
        : warningThreshold(warning), criticalThreshold(critical), isHigherBetter(higherBetter) {}
};

/**
 * Enhanced Configuration structure for circuit breakers
 */
struct CircuitBreakerConfig {
    // Threshold definitions for different metrics
    std::unordered_map<std::string, MetricThreshold> metricThresholds;
    
    // Timing configuration
    std::chrono::seconds coolingPeriod{300};              // 5 minutes default
    std::chrono::seconds warmupPeriod{60};                // 1 minute default
    int mandatoryHaltPeriodSeconds = 60;                  // Minimum halt time in seconds
    int coolingPeriodSeconds = 300;                       // Cooling period in seconds
    
    // Trigger and reset behavior
    bool autoResume = true;
    int maxTriggersPerDay = 10;
    int maxTriggersBeforeExtendedHalt = 3;
    int extendedHaltMultiplier = 2;                       // Multiply halt period by this factor
    int triggerCountResetMinutes = 60;                    // Reset trigger count after this period
    
    // History and monitoring
    size_t maxHistorySize = 1000;                         // Maximum number of events to keep
    
    // Gradual recovery
    bool gradualTradingResumption = true;                 // Allow gradual recovery vs binary
    
    // Default constructor with reasonable defaults
    CircuitBreakerConfig() {
        // Set up default metric thresholds
        metricThresholds["error_rate"] = MetricThreshold(0.05, 0.10, false);        // 5% warning, 10% critical
        metricThresholds["latency_ms"] = MetricThreshold(100.0, 500.0, false);      // 100ms warning, 500ms critical
        metricThresholds["success_rate"] = MetricThreshold(0.95, 0.90, true);       // 95% warning, 90% critical
        metricThresholds["throughput"] = MetricThreshold(100.0, 50.0, true);        // Throughput drops
        metricThresholds["drawdown_percent"] = MetricThreshold(5.0, 10.0, false);   // 5% warning, 10% critical
    }
    
    // Validation method
    bool isValid() const {
        return coolingPeriod.count() > 0 &&
               warmupPeriod.count() > 0 &&
               mandatoryHaltPeriodSeconds > 0 &&
               coolingPeriodSeconds > 0 &&
               maxTriggersPerDay > 0 &&
               maxTriggersBeforeExtendedHalt > 0 &&
               extendedHaltMultiplier > 0 &&
               triggerCountResetMinutes > 0 &&
               maxHistorySize > 0;
    }
};

/**
 * Configuration structure for stop loss
 */
struct StopLossConfig {
    double maxLossPercentage = 0.05;
    double trailingStopDistance = 0.02;
    bool enableTrailingStop = true;
    // enableDefaultStopLoss
    bool enableDefaultStopLoss = true;
    bool forceLiquidation = true;
    std::chrono::seconds liquidationDelay{5};
};

/**
 * Configuration structure for drawdown monitoring
 */
struct DrawdownConfig {
    double warningThreshold = 0.10;
    double criticalThreshold = 0.20;
    std::chrono::hours lookbackPeriod{24};
    bool enableAutoRiskReduction = true;
    double riskReductionFactor = 0.5;
};

/**
 * Configuration structure for fat finger protection
 */
struct FatFingerConfig {
    double maxPriceDeviation = 0.05;
    double maxOrderSize = 10.0;
    double typicalOrderSize = 1.0;
    double notionalValueLimit = 10000.0;
    bool enforceAdditionalVerification = true;
};
 
/**
 * Configuration structure for balance reconciliation
 */
struct BalanceReconciliationConfig {
    // Core reconciliation settings
    std::chrono::minutes reconciliationFrequency{30};
    double maxDiscrepancyPercent{1.0}; // 1% default tolerance
    bool autoCorrectDiscrepancies{false};
    bool alertOnDiscrepancy{true};
    int maxConsecutiveFailures{3};
    
    // Extended reconciliation settings
    double minimumPositionSize{0.001};           // Minimum position size to reconcile
    std::chrono::seconds exchangeTimeoutSeconds{30}; // Exchange query timeout
    bool enableDetailedLogging{true};            // Detailed logging of operations
    bool reconcileOnStartup{true};               // Perform reconciliation on startup
    int maxDiscrepanciesPerRun{100};             // Max discrepancies to report per run
    
    // Retry and resilience settings  
    int maxRetryAttempts{3};                     // Max retries for failed exchange queries
    std::chrono::seconds retryDelaySeconds{5}; // Delay between retries
    bool enableProgressiveBackoff{true};        // Use exponential backoff for retries
    
    // Position tolerance settings
    double absoluteToleranceThreshold{0.0001};   // Absolute tolerance for very small positions
    bool ignoreZeroPositions{true};              // Skip positions with zero quantity
    bool validateCrossExchange{true};            // Validate positions across exchanges
    
    // Reconciliation scheduling
    bool enableScheduledReconciliation{true};    // Enable automatic scheduled reconciliation
    std::chrono::minutes maxReconciliationDuration{10}; // Max time per reconciliation
    bool pauseDuringMarketHours{false};         // Pause reconciliation during active trading
    
    // Notification and alerting
    bool enableSlackNotifications{false};       // Send Slack notifications for discrepancies
    bool enableEmailAlerts{true};               // Send email alerts for major discrepancies
    double criticalDiscrepancyThreshold{5.0};   // Threshold for critical alerts (%)
    
    // Performance and optimization
    bool enableBatchProcessing{true};           // Process multiple exchanges in batches
    int maxConcurrentExchangeQueries{5};       // Max concurrent exchange API calls
    bool enablePositionCaching{true};          // Cache positions for performance
    std::chrono::minutes cacheValidityPeriod{5}; // Cache validity period
    
    // Data integrity and validation
    bool validateTimestamps{true};              // Validate position timestamps
    std::chrono::minutes maxPositionAge{60}; // Max acceptable position age
    bool enableDataQualityChecks{true};        // Perform data quality validation
    bool requireConfirmationForCorrections{false}; // Require manual confirmation for auto-corrections
    
    // Configuration validation
    bool isValid() const {
        return reconciliationFrequency.count() > 0 &&
               maxDiscrepancyPercent >= 0.0 &&
               maxDiscrepancyPercent <= 100.0 &&
               maxConsecutiveFailures > 0 &&
               minimumPositionSize >= 0.0 &&
               exchangeTimeoutSeconds.count() > 0 &&
               maxDiscrepanciesPerRun > 0 &&
               maxRetryAttempts >= 0 &&
               criticalDiscrepancyThreshold >= maxDiscrepancyPercent &&
               maxConcurrentExchangeQueries > 0 &&
               cacheValidityPeriod.count() > 0 &&
               maxPositionAge.count() > 0;
    }
    
    // Helper method to get retry delay with exponential backoff
    std::chrono::seconds getRetryDelay(int attemptNumber) const {
        if (!enableProgressiveBackoff || attemptNumber <= 1) {
            return retryDelaySeconds;
        }
        
        // Exponential backoff: delay * (2^(attempt-1))
        int multiplier = 1 << (attemptNumber - 1); // 2^(attempt-1)
        return retryDelaySeconds * std::min(multiplier, 8); // Cap at 8x original delay
    }
};

/**
 * Configuration structure for rebalancing
 */
struct RebalancingConfig {
    bool autoRebalancingEnabled = false;
    bool autoExecuteRebalancing = false;
    bool parallelExecution = false;
    int rebalancingIntervalMinutes = 60;
    double allocationDeviationThreshold = 0.05; // 5%
    double minimumOrderSize = 0.0;
    double minimumRebalancingValue = 0.0;
    double defaultTransferFeeRate = 0.001; // 0.1% default fee
    std::unordered_map<std::string, double> targetAllocations;
    std::unordered_map<std::string, double> fixedPrices;
    std::unordered_map<std::string, double> assetTransferFees;
    std::unordered_map<std::string, double> exchangeTransferFees;
    std::vector<std::string> preferredExchanges;
};

/**
 * Configuration structure for collateral management
 */
struct CollateralConfig {
    double targetFreeCollateralRatio = 0.2;
    bool enableEmergencyFunding = false;
    std::vector<std::string> fundingSources = {"main_wallet", "backup_wallet"};
    double maxFundingAmount = 10000.0;

    // Minimum ratio of free collateral to total collateral (e.g., 0.2 for 20%)
    double minimumFreeCollateralRatio = 0.2;

    // If free collateral ratio drops below this, emergency funding is triggered
    double emergencyFundingThreshold = 0.1;

    // If true, emergency funding is auto-initiated
    bool autoInitiateEmergencyFunding = false;

    // List of exchanges managed by the collateral manager
    std::vector<core::ExchangeId> managedExchanges;

    // Minimum balances required for each asset (asset symbol -> minimum amount)
    std::map<std::string, double> minimumBalances;

    // Buffer to add to emergency funding (e.g., 0.1 for 10% extra)
    double emergencyFundingBuffer = 0.1;

    // Source to use for emergency funding (e.g., "main_wallet")
    std::string emergencyFundingSource = "main_wallet";

    // Margin rates per exchange and symbol (ExchangeId -> (symbol -> margin rate))
    std::map<core::ExchangeId, std::map<std::string, double>> exchangeMarginRates;

    // Base currency for collateral calculations (e.g., "USD")
    std::string baseCurrency = "USD";

    // Asset conversion rates (asset symbol -> conversion rate to base currency)
    std::map<std::string, double> assetConversionRates;

    // Minimums for critical assets (asset symbol -> minimum amount)
    std::map<std::string, double> criticalAssetMinimums;
};

/**
 * Configuration structure for portfolio management
 */
struct PortfolioConfig {
    bool autoRebalancingEnabled = false;
    bool autoExecuteRebalancing = false;
    bool enableRealTimeMetrics = true;
    bool enableAllocationTracking = true;
    std::chrono::minutes metricsUpdateInterval{5};
    double minimumPortfolioValue = 1000.0;
    std::string baseCurrency = "USD";
    std::vector<std::string> supportedAssets;
    std::unordered_map<std::string, double> targetAllocations;
    bool enableCrossExchangeAggregation = true;
    bool enablePortfolioRebalancing = true;
};

/**
 * Configuration structure for risk logging
 */
struct RiskLoggingConfig {
    std::string logLevel = "INFO";
    std::string logDirectory = "/var/log/riskmanagement";
    std::string logFileName = "risk_manager.log";
    bool enableDetailedLogs = true;
    bool logToConsole = true;
    bool logToFile = true;
    std::chrono::hours logRetentionPeriod{30 * 24};
};

/**
 * ENHANCED Configuration structure for order validation
 */

struct OrderValidationConfig {
    bool validateMinOrderSize = true;
    bool validatePriceRange = true;
    bool enforceSymbolWhitelist = false;
    
    // Core validation settings
    std::vector<std::string> symbolWhitelist;
    double maxPriceDeviationPercent = 10.0;    // 10% default max deviation
    double defaultMinOrderSize = 0.001;        // Default minimum order size
    double maxOrderSize = 1000000.0;           // Default maximum order size
    
    // Allowed order types and time in force values
    std::vector<trading::common::OrderType> allowedOrderTypes = {
        trading::common::OrderType::MARKET,
        trading::common::OrderType::LIMIT,
        trading::common::OrderType::STOP,
        trading::common::OrderType::STOP_LIMIT
    };
    
    std::vector<std::string> allowedTimeInForce = {
        "GTC",  // Good Till Canceled
        "IOC",  // Immediate Or Cancel
        "FOK"   // Fill Or Kill
    };
    
    // Exchange-specific minimum order sizes
    // ExchangeId -> (TradingPair -> minimum size)
    std::map<core::ExchangeId, std::map<TradingPair, double>> minOrderSizes;
    
    // Minimum trade value (notional) in base currency or quote currency unit
    // Prefer using a single minimum trade value instead of brittle per-pair sizes
    double minTradeValue = 10.0;
    
    // Price validation settings
    bool enablePriceValidation = true;
    bool requireMarketDataForPriceValidation = true;
    
    // Order size validation settings
    bool enableOrderSizeValidation = true;
    bool enableExchangeSpecificMinSizes = true;
    
    // Symbol whitelist settings
    bool enableSymbolWhitelistValidation = false;
    bool strictWhitelistEnforcement = false;
    
    // Additional validation features
    bool validateAgainstMarketHours = false;
    bool enableOrderRateLimit = false;
    int maxOrdersPerSecond = 10;
    int maxOrdersPerMinute = 300;
    
    // Error handling
    bool failOnFirstViolation = false;
    bool logAllViolations = true;
    bool enableDetailedErrorReporting = true;
    
    // Default constructor with reasonable defaults
    OrderValidationConfig() = default;
    
    // Validation method
    bool isValid() const {
        return maxPriceDeviationPercent > 0.0 &&
               maxPriceDeviationPercent <= 100.0 &&
               defaultMinOrderSize >= 0.0 &&
               maxOrderSize > defaultMinOrderSize &&
               maxOrdersPerSecond > 0 &&
               maxOrdersPerMinute > 0 &&
               !allowedOrderTypes.empty() &&
               !allowedTimeInForce.empty();
    }
    
    // Helper method to add exchange-specific min order size
    void setMinOrderSize(const core::ExchangeId& exchange, const core::Symbol& symbol, double minSize) {
        TradingPair pair = TradingPair(symbol);
        minOrderSizes[exchange][pair] = minSize;
    }
    
    // Helper method to get min order size for a specific exchange and symbol
    double getMinOrderSize(const core::ExchangeId& exchange, const core::Symbol& symbol) const {
        TradingPair pair = TradingPair(symbol);
        auto exchangeIt = minOrderSizes.find(exchange);
        if (exchangeIt != minOrderSizes.end()) {
            auto symbolIt = exchangeIt->second.find(pair);
            if (symbolIt != exchangeIt->second.end()) {
                return symbolIt->second;
            }
        }
        return defaultMinOrderSize;
    }
    
    // Helper method to check if order type is allowed
    bool isOrderTypeAllowed(trading::common::OrderType type) const {
        return std::find(allowedOrderTypes.begin(), allowedOrderTypes.end(), type) != allowedOrderTypes.end();
    }
    
    // Helper method to check if time in force is allowed
    bool isTimeInForceAllowed(const std::string& tif) const {
        return std::find(allowedTimeInForce.begin(), allowedTimeInForce.end(), tif) != allowedTimeInForce.end();
    }
};

namespace trading::risk {


/**
 * Main risk configuration container
 */
class RiskConfig {
public:
    /**
     * Constructor with file path
     * @param configFilePath Path to the configuration file
     */
    explicit RiskConfig(const std::string& configFilePath);
    
    /**
     * Default constructor
     */
    RiskConfig();
    
    /**
     * Load configuration from file
     * @param configFilePath Path to the configuration file
     * @return true if loading was successful, false otherwise
     */
    bool loadFromFile(const std::string& configFilePath);
    
    /**
     * Save configuration to file
     * @param configFilePath Path to save the configuration to
     * @return true if saving was successful, false otherwise
     */
    bool saveToFile(const std::string& configFilePath) const;
    
    /**
     * Validate the configuration
     * @return true if configuration is valid, false otherwise
     */
    bool validate() const;
    
    // Getters
    const PositionLimitConfig& getPositionLimitConfig() const;
    const ExposureLimitConfig& getExposureLimitConfig() const;
    const CircuitBreakerConfig& getCircuitBreakerConfig() const;  // ONLY ONE - FIXED!
    const StopLossConfig& getStopLossConfig() const;
    const DrawdownConfig& getDrawdownConfig() const;
    const FatFingerConfig& getFatFingerConfig() const;
    const OrderValidationConfig& getOrderValidationConfig() const;
    const BalanceReconciliationConfig& getBalanceReconciliationConfig() const;
    const RebalancingConfig& getRebalancingConfig() const;
    const CollateralConfig& getCollateralConfig() const;
    const PortfolioConfig& getPortfolioConfig() const;
    const RiskLoggingConfig& getLoggingConfig() const;
    bool getBoolValue(std::string key) ;
    // const double getDoubleValue(std::string key) ;
    bool hasKey(std::string key) ;
    // const std::string getStringValue(std::string key) ;
    // getIntValue
    int getIntValue(std::string key) ;

    // Setters
    void setPositionLimitConfig(const PositionLimitConfig& config);
    void setExposureLimitConfig(const ExposureLimitConfig& config);
    void setCircuitBreakerConfig(const CircuitBreakerConfig& config);
    void setStopLossConfig(const StopLossConfig& config);
    void setDrawdownConfig(const DrawdownConfig& config);
    void setFatFingerConfig(const FatFingerConfig& config);
    void setOrderValidationConfig(const OrderValidationConfig& config);
    void setBalanceReconciliationConfig(const BalanceReconciliationConfig& config);
    void setRebalancingConfig(const RebalancingConfig& config);
    void setCollateralConfig(const CollateralConfig& config);
    void setPortfolioConfig(const PortfolioConfig& config);
    void setLoggingConfig(const RiskLoggingConfig& config);
    // Public member for direct access (as shown in original code)
    CollateralConfig collateralConfig;
    PortfolioConfig portfolioConfig;
    // zmq endpoints
    std::string executionRequestEndpoint = SERVER_EXECUTION_REQUEST_ENDPOINT;
    std::string executionPublishEndpoint = SERVER_EXECUTION_PUBLISH_ENDPOINT;
    std::string executionReportsTopic = "execution_reports";
    // marketDataRequestEndpoint
    std::string marketDataRequestEndpoint = SERVER_REQUEST_ENDPOINT;
    std::string marketDataPublishEndpoint = SERVER_RISK_PUBLISH_ENDPOINT;
    std::string marketDataTopic = MARKET_DATA_TOPIC; //ORDERBOOK_UPDATES_TOPIC
    // zmqSubscriberEndpoint
    std::string zmqSubscriberEndpoint = SERVER_PUBLISHER_ENDPOINT;
    // shared context for zmq
    // std::shared_ptr<zmq::context_t> zmqContext = std::make_shared<zmq::context_t>(1);
    // zmqPublisherEndpoint
    std::string zmqPublisherEndpoint = SERVER_REQUEST_ENDPOINT;
    // cache validity duration in milliseconds (default increased to 1000ms to avoid
    // immediate expirations during fast lookups). Can be overridden via config.
    int marketDataCacheDurationMs = 1000;
    std::string defaultExchange = "binance";
    //

private:
    // m_positionLimitConfig

    PositionLimitConfig positionLimitConfig_;
    ExposureLimitConfig exposureLimitConfig_;
    CircuitBreakerConfig circuitBreakerConfig_;
    StopLossConfig stopLossConfig_;
    DrawdownConfig drawdownConfig_;
    FatFingerConfig fatFingerConfig_;
    OrderValidationConfig orderValidationConfig_;
    BalanceReconciliationConfig balanceReconciliationConfig_;
    RebalancingConfig rebalancingConfig_;
    CollateralConfig collateralConfig_;
    PortfolioConfig portfolioConfig_;
    RiskLoggingConfig loggingConfig_;
    
    // Private helper methods for JSON loading - use forward declared json
    bool loadJsonConfiguration(const std::string& jsonStr);
    void loadPositionLimitConfig(const nlohmann::json& pl);
    void loadExposureLimitConfig(const nlohmann::json& el);
    void loadCircuitBreakerConfig(const nlohmann::json& cb);
    void loadStopLossConfig(const nlohmann::json& sl);
    void loadDrawdownConfig(const nlohmann::json& dd);
    void loadFatFingerConfig(const nlohmann::json& ff);
    void loadOrderValidationConfig(const nlohmann::json& ov);
    void loadBalanceReconciliationConfig(const nlohmann::json& br);
    void loadRebalancingConfig(const nlohmann::json& rb);
    void loadCollateralConfig(const nlohmann::json& co);
    void loadPortfolioConfig(const nlohmann::json& po);
    void loadLoggingConfig(const nlohmann::json& lg);
    
    // Helper method for saving other configs
    void saveOtherConfigs(nlohmann::json& j) const;
    
};

} // namespace trading::risk