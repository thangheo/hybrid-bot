/**
 * Simplified risk configuration for execution-only risk management
 * Removed portfolio management, strategic risk, and complex configurations
 */

#pragma once

#include <string>
#include <unordered_map>
#include <chrono>
#include <nlohmann/json.hpp>

namespace trading::risk {

using json = ::nlohmann::json;

/**
 * Fat finger protection configuration
 */
struct FatFingerConfig {
    double maxPriceDeviation = 0.05;        // 5% max price deviation
    double maxOrderSize = 10.0;             // Max order size
    double typicalOrderSize = 1.0;          // Typical order size for comparison
    double notionalValueLimit = 10000.0;    // Max notional value
    bool enforceAdditionalVerification = true;
};

/**
 * Order validation configuration  
 */
struct OrderValidationConfig {
    bool validateMinOrderSize = true;
    bool validatePriceRange = true;
    double maxPriceDeviationPercent = 0.1;  // 10% max price deviation
    bool enforceSymbolWhitelist = false;
    std::vector<std::string> symbolWhitelist;
};

/**
 * Circuit breaker configuration (simplified)
 */
struct CircuitBreakerConfig {
    // Thresholds for triggering circuit breaker
    double maxSlippageThreshold = 0.02;      // 2% slippage triggers CB
    double maxRejectionRate = 0.1;           // 10% rejection rate triggers CB
    int maxConsecutiveRejections = 5;        // Max consecutive rejections
    
    // Timing
    std::chrono::seconds coolingPeriod{300}; // 5 minutes cooling period
    std::chrono::seconds warmupPeriod{60};   // 1 minute warmup
    
    // Behavior
    bool autoResume = true;
    int maxTriggersPerDay = 5;
    
    bool isValid() const {
        return maxSlippageThreshold > 0 && 
               maxRejectionRate > 0 && 
               maxConsecutiveRejections > 0 &&
               coolingPeriod.count() > 0;
    }
};

/**
 * Logging configuration
 */
struct RiskLoggingConfig {
    std::string logLevel = "INFO";
    std::string logDirectory = "/var/log/trading_risk";
    bool enableDetailedLogs = true;
    bool logToConsole = true;
    bool logToFile = true;
    std::chrono::days logRetentionPeriod{30};
};

/**
 * Execution limits configuration structure
 */
struct ExecutionLimitValues {
    double max_order_size = 0.0;
    double max_market_impact = 0.0;
    double max_price_deviation = 0.0;
    double max_participation_rate = 0.0;
    int max_orders_per_second = 0;
    bool allow_market_orders = true;
    
    // Circuit breaker thresholds
    double cb_slippage_threshold = 0.0;
    double cb_rejection_rate_threshold = 0.0;
    int cb_max_consecutive_rejections = 0;
};

/**
 * Complete execution limits configuration
 */
struct ExecutionLimitsConfig {
    ExecutionLimitValues default_limits;
    std::unordered_map<std::string, ExecutionLimitValues> symbol_overrides;
    
    // Risk scaling factors based on market conditions
    double high_volatility_scale_factor = 0.5;   // Reduce limits by 50% in high vol
    double low_liquidity_scale_factor = 0.3;     // Reduce limits by 70% in low liquidity
    double market_stress_scale_factor = 0.2;     // Reduce limits by 80% during market stress
};

/**
 * Simplified risk configuration class
 * Contains only execution-level risk parameters
 */
class RiskConfig {
public:
    // Constructors
    RiskConfig();
    explicit RiskConfig(const std::string& configFilePath);
    
    // Configuration loading/saving
    bool loadFromFile(const std::string& configFilePath);
    bool saveToFile(const std::string& configFilePath) const;
    bool loadJsonConfiguration(const std::string& jsonStr);
    
    // Validation
    bool validate() const;
    
    // Getters
    const FatFingerConfig& getFatFingerConfig() const { return m_fatFingerConfig; }
    const OrderValidationConfig& getOrderValidationConfig() const { return m_orderValidationConfig; }
    const CircuitBreakerConfig& getCircuitBreakerConfig() const { return m_circuitBreakerConfig; }
    const ExecutionLimitsConfig& getExecutionLimitsConfig() const { return m_executionLimitsConfig; }
    const RiskLoggingConfig& getLoggingConfig() const { return m_loggingConfig; }
    
    // Setters
    void setFatFingerConfig(const FatFingerConfig& config) { m_fatFingerConfig = config; }
    void setOrderValidationConfig(const OrderValidationConfig& config) { m_orderValidationConfig = config; }
    void setCircuitBreakerConfig(const CircuitBreakerConfig& config) { m_circuitBreakerConfig = config; }
    void setExecutionLimitsConfig(const ExecutionLimitsConfig& config) { m_executionLimitsConfig = config; }
    void setLoggingConfig(const RiskLoggingConfig& config) { m_loggingConfig = config; }
    
private:
    // Execution-focused configurations only
    FatFingerConfig m_fatFingerConfig;
    OrderValidationConfig m_orderValidationConfig;
    CircuitBreakerConfig m_circuitBreakerConfig;
    ExecutionLimitsConfig m_executionLimitsConfig;
    RiskLoggingConfig m_loggingConfig;
    
    // Configuration loading helpers
    void loadFatFingerConfig(const json& ff);
    void loadOrderValidationConfig(const json& ov);
    void loadCircuitBreakerConfig(const json& cb);
    void loadExecutionLimitsConfig(const json& el);
    void loadLoggingConfig(const json& lg);
};

// Implementation
inline RiskConfig::RiskConfig() {
    // Initialize with sensible defaults for execution risk
    
    // Fat finger protection
    m_fatFingerConfig.maxPriceDeviation = 0.05;      // 5%
    m_fatFingerConfig.maxOrderSize = 10.0;
    m_fatFingerConfig.typicalOrderSize = 1.0;
    m_fatFingerConfig.notionalValueLimit = 10000.0;
    m_fatFingerConfig.enforceAdditionalVerification = true;
    
    // Order validation
    m_orderValidationConfig.validateMinOrderSize = true;
    m_orderValidationConfig.validatePriceRange = true;
    m_orderValidationConfig.maxPriceDeviationPercent = 0.1;
    m_orderValidationConfig.enforceSymbolWhitelist = false;
    
    // Circuit breaker
    m_circuitBreakerConfig.maxSlippageThreshold = 0.02;
    m_circuitBreakerConfig.maxRejectionRate = 0.1;
    m_circuitBreakerConfig.maxConsecutiveRejections = 5;
    m_circuitBreakerConfig.coolingPeriod = std::chrono::seconds(300);
    m_circuitBreakerConfig.warmupPeriod = std::chrono::seconds(60);
    m_circuitBreakerConfig.autoResume = true;
    m_circuitBreakerConfig.maxTriggersPerDay = 5;
    
    // Execution limits - production defaults
    auto& default_limits = m_executionLimitsConfig.default_limits;
    default_limits.max_order_size = 1000.0;
    default_limits.max_market_impact = 0.01;         // 1%
    default_limits.max_price_deviation = 0.05;       // 5%
    default_limits.max_participation_rate = 0.20;    // 20%
    default_limits.max_orders_per_second = 10;
    default_limits.allow_market_orders = true;
    default_limits.cb_slippage_threshold = 0.02;     // 2%
    default_limits.cb_rejection_rate_threshold = 0.15; // 15%
    default_limits.cb_max_consecutive_rejections = 5;
    
    // Market condition scaling factors
    m_executionLimitsConfig.high_volatility_scale_factor = 0.5;
    m_executionLimitsConfig.low_liquidity_scale_factor = 0.3;
    m_executionLimitsConfig.market_stress_scale_factor = 0.2;
    
    // Logging
    m_loggingConfig.logLevel = "INFO";
    m_loggingConfig.logDirectory = "/var/log/trading_risk";
    m_loggingConfig.enableDetailedLogs = true;
    m_loggingConfig.logToConsole = true;
    m_loggingConfig.logToFile = true;
    m_loggingConfig.logRetentionPeriod = std::chrono::days(30);
}

inline bool RiskConfig::loadJsonConfiguration(const std::string& jsonStr) {
    try {
        json j = json::parse(jsonStr);
        
        if (j.contains("fatFinger")) {
            loadFatFingerConfig(j["fatFinger"]);
        }
        
        if (j.contains("orderValidation")) {
            loadOrderValidationConfig(j["orderValidation"]);
        }
        
        if (j.contains("circuitBreaker")) {
            loadCircuitBreakerConfig(j["circuitBreaker"]);
        }
        
        if (j.contains("executionLimits")) {
            loadExecutionLimitsConfig(j["executionLimits"]);
        }
        
        if (j.contains("logging")) {
            loadLoggingConfig(j["logging"]);
        }
        
        return validate();
    } catch (const std::exception& e) {
        std::cerr << "Error parsing JSON configuration: " << e.what() << std::endl;
        return false;
    }
}

inline void RiskConfig::loadExecutionLimitsConfig(const json& el) {
    // Load default limits
    if (el.contains("default")) {
        const auto& def = el["default"];
        auto& default_limits = m_executionLimitsConfig.default_limits;
        
        if (def.contains("maxOrderSize")) {
            default_limits.max_order_size = def["maxOrderSize"].get<double>();
        }
        if (def.contains("maxMarketImpact")) {
            default_limits.max_market_impact = def["maxMarketImpact"].get<double>();
        }
        if (def.contains("maxPriceDeviation")) {
            default_limits.max_price_deviation = def["maxPriceDeviation"].get<double>();
        }
        if (def.contains("maxParticipationRate")) {
            default_limits.max_participation_rate = def["maxParticipationRate"].get<double>();
        }
        if (def.contains("maxOrdersPerSecond")) {
            default_limits.max_orders_per_second = def["maxOrdersPerSecond"].get<int>();
        }
        if (def.contains("allowMarketOrders")) {
            default_limits.allow_market_orders = def["allowMarketOrders"].get<bool>();
        }
        if (def.contains("cbSlippageThreshold")) {
            default_limits.cb_slippage_threshold = def["cbSlippageThreshold"].get<double>();
        }
        if (def.contains("cbRejectionRateThreshold")) {
            default_limits.cb_rejection_rate_threshold = def["cbRejectionRateThreshold"].get<double>();
        }
        if (def.contains("cbMaxConsecutiveRejections")) {
            default_limits.cb_max_consecutive_rejections = def["cbMaxConsecutiveRejections"].get<int>();
        }
    }
    
    // Load symbol-specific overrides
    if (el.contains("symbols") && el["symbols"].is_object()) {
        for (const auto& [symbol, symbol_config] : el["symbols"].items()) {
            ExecutionLimitValues symbol_limits;
            
            if (symbol_config.contains("maxOrderSize")) {
                symbol_limits.max_order_size = symbol_config["maxOrderSize"].get<double>();
            }
            if (symbol_config.contains("maxMarketImpact")) {
                symbol_limits.max_market_impact = symbol_config["maxMarketImpact"].get<double>();
            }
            if (symbol_config.contains("maxPriceDeviation")) {
                symbol_limits.max_price_deviation = symbol_config["maxPriceDeviation"].get<double>();
            }
            if (symbol_config.contains("maxParticipationRate")) {
                symbol_limits.max_participation_rate = symbol_config["maxParticipationRate"].get<double>();
            }
            if (symbol_config.contains("maxOrdersPerSecond")) {
                symbol_limits.max_orders_per_second = symbol_config["maxOrdersPerSecond"].get<int>();
            }
            if (symbol_config.contains("allowMarketOrders")) {
                symbol_limits.allow_market_orders = symbol_config["allowMarketOrders"].get<bool>();
            }
            if (symbol_config.contains("cbSlippageThreshold")) {
                symbol_limits.cb_slippage_threshold = symbol_config["cbSlippageThreshold"].get<double>();
            }
            if (symbol_config.contains("cbRejectionRateThreshold")) {
                symbol_limits.cb_rejection_rate_threshold = symbol_config["cbRejectionRateThreshold"].get<double>();
            }
            if (symbol_config.contains("cbMaxConsecutiveRejections")) {
                symbol_limits.cb_max_consecutive_rejections = symbol_config["cbMaxConsecutiveRejections"].get<int>();
            }
            
            m_executionLimitsConfig.symbol_overrides[symbol] = symbol_limits;
        }
    }
    
    // Load market condition scaling factors
    if (el.contains("marketConditionScaling")) {
        const auto& scaling = el["marketConditionScaling"];
        
        if (scaling.contains("highVolatilityScaleFactor")) {
            m_executionLimitsConfig.high_volatility_scale_factor = scaling["highVolatilityScaleFactor"].get<double>();
        }
        if (scaling.contains("lowLiquidityScaleFactor")) {
            m_executionLimitsConfig.low_liquidity_scale_factor = scaling["lowLiquidityScaleFactor"].get<double>();
        }
        if (scaling.contains("marketStressScaleFactor")) {
            m_executionLimitsConfig.market_stress_scale_factor = scaling["marketStressScaleFactor"].get<double>();
        }
    }
}

inline void RiskConfig::loadFatFingerConfig(const json& ff) {
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

inline void RiskConfig::loadOrderValidationConfig(const json& ov) {
    if (ov.contains("validateMinOrderSize")) {
        m_orderValidationConfig.validateMinOrderSize = ov["validateMinOrderSize"].get<bool>();
    }
    if (ov.contains("validatePriceRange")) {
        m_orderValidationConfig.validatePriceRange = ov["validatePriceRange"].get<bool>();
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

inline void RiskConfig::loadCircuitBreakerConfig(const json& cb) {
    if (cb.contains("maxSlippageThreshold")) {
        m_circuitBreakerConfig.maxSlippageThreshold = cb["maxSlippageThreshold"].get<double>();
    }
    if (cb.contains("maxRejectionRate")) {
        m_circuitBreakerConfig.maxRejectionRate = cb["maxRejectionRate"].get<double>();
    }
    if (cb.contains("maxConsecutiveRejections")) {
        m_circuitBreakerConfig.maxConsecutiveRejections = cb["maxConsecutiveRejections"].get<int>();
    }
    if (cb.contains("coolingPeriodSeconds")) {
        m_circuitBreakerConfig.coolingPeriod = std::chrono::seconds(cb["coolingPeriodSeconds"].get<int>());
    }
    if (cb.contains("warmupPeriodSeconds")) {
        m_circuitBreakerConfig.warmupPeriod = std::chrono::seconds(cb["warmupPeriodSeconds"].get<int>());
    }
    if (cb.contains("autoResume")) {
        m_circuitBreakerConfig.autoResume = cb["autoResume"].get<bool>();
    }
    if (cb.contains("maxTriggersPerDay")) {
        m_circuitBreakerConfig.maxTriggersPerDay = cb["maxTriggersPerDay"].get<int>();
    }
}

inline void RiskConfig::loadLoggingConfig(const json& lg) {
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

inline bool RiskConfig::validate() const {
    // Validate fat finger config
    if (m_fatFingerConfig.maxPriceDeviation <= 0 || m_fatFingerConfig.maxPriceDeviation > 1.0) {
        std::cerr << "Invalid fat finger price deviation\n";
        return false;
    }
    
    if (m_fatFingerConfig.maxOrderSize <= 0) {
        std::cerr << "Invalid fat finger max order size\n";
        return false;
    }
    
    // Validate order validation config
    if (m_orderValidationConfig.maxPriceDeviationPercent <= 0 || 
        m_orderValidationConfig.maxPriceDeviationPercent > 1.0) {
        std::cerr << "Invalid order validation price deviation\n";
        return false;
    }
    
    // Validate circuit breaker config
    if (!m_circuitBreakerConfig.isValid()) {
        std::cerr << "Invalid circuit breaker configuration\n";
        return false;
    }
    
    return true;
}

} // namespace trading::risk