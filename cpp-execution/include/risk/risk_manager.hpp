/**
 * Simplified Execution Risk Manager - Focus on execution-level risk only
 * Strategic risk is handled by Python layer
 */

#pragma once

#include "risk_types.hpp"
#include "risk_events.hpp"
#include "risk_config.hpp"
#include "pretrade/fat_finger_protection.hpp"
#include "pretrade/order_validator.hpp"
#include "posttrade/circuit_breaker.hpp"
#include "service/risk_validation_service.hpp"
#include "trading_messages.pb.h" // From your protobuf definitions
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace trading::risk {

/**
 * Lightweight execution risk manager for hybrid architecture
 * Only handles execution-level risks that require microsecond responses
 */
class ExecutionRiskManager {
public:
    explicit ExecutionRiskManager(const RiskConfig& config);
    ~ExecutionRiskManager() = default;

    /**
     * Initialize execution risk components only
     */
    bool initialize();
    
    /**
     * Fast validation for trading intents from Python
     * Must complete in < 100μs
     */
    ExecutionRiskResult validateTradingIntent(const TradingIntent& intent,
                                             const MarketIntelligence& market_intel);
    
    /**
     * Real-time order validation before execution
     * Must complete in < 50μs  
     */
    ExecutionRiskResult validateOrder(const OrderRequest& order,
                                     const MarketIntelligence& market_intel);
    
    /**
     * Update market impact tracking
     */
    void updateMarketImpact(const std::string& symbol, 
                           double executed_quantity,
                           double price_impact);
    
    /**
     * Emergency circuit breaker check
     */
    bool isCircuitBreakerTriggered(const std::string& symbol) const;
    
    /**
     * Get current execution limits for symbol
     */
    ExecutionLimits getExecutionLimits(const std::string& symbol) const;

private:
    // Execution-focused components only
    std::unique_ptr<FatFingerProtection> m_fatFingerProtection;
    std::unique_ptr<OrderValidator> m_orderValidator;  
    std::unique_ptr<CircuitBreaker> m_circuitBreaker;
    std::unique_ptr<RiskValidationService> m_validationService;
    
    // Lightweight configuration
    RiskConfig m_config;
    std::atomic<bool> m_emergencyStop{false};
    
    // Real-time tracking (minimal state)
    mutable std::mutex m_impactMutex;
    std::unordered_map<std::string, MarketImpactTracker> m_impactTrackers;
    
    // Fast lookup tables for limits
    std::unordered_map<std::string, ExecutionLimits> m_symbolLimits;
    
    // Emergency callback
    std::function<void(const std::string& reason)> m_emergencyCallback;
};

/**
 * Execution-specific risk result
 */
struct ExecutionRiskResult {
    bool approved = false;
    std::string rejection_reason;
    std::chrono::microseconds validation_time{0};
    
    // Execution adjustments (if approved with modifications)
    std::optional<double> adjusted_quantity;
    std::optional<double> max_price_impact;
    std::optional<int> max_execution_time_ms;
};

/**
 * Market impact tracker for real-time monitoring
 */
struct MarketImpactTracker {
    double cumulative_impact = 0.0;
    double recent_volume = 0.0;
    std::chrono::steady_clock::time_point last_update;
    
    void update(double quantity, double impact) {
        auto now = std::chrono::steady_clock::now();
        
        // Decay old impact (5-minute half-life)
        if (last_update != std::chrono::steady_clock::time_point{}) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_update).count();
            double decay_factor = std::exp(-elapsed / 300.0); // 5 minutes
            cumulative_impact *= decay_factor;
            recent_volume *= decay_factor;
        }
        
        cumulative_impact += std::abs(impact);
        recent_volume += std::abs(quantity);
        last_update = now;
    }
    
    double getImpactRate() const {
        return recent_volume > 0 ? cumulative_impact / recent_volume : 0.0;
    }
};

/**
 * Execution limits per symbol
 */
struct ExecutionLimits {
    double max_order_size = 0.0;
    double max_market_impact = 0.01; // 1%
    double max_price_deviation = 0.05; // 5%
    double max_participation_rate = 0.2; // 20% of volume
    int max_orders_per_second = 10;
    bool allow_market_orders = true;
    
    // Circuit breaker thresholds
    double cb_slippage_threshold = 0.02; // 2%
    double cb_rejection_rate_threshold = 0.1; // 10%
    int cb_max_consecutive_rejections = 5;
};

} // namespace trading::risk