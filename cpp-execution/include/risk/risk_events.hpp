/**
 * Event definitions for risk-related notifications
 *
 * File: risk_events.hpp
 * Created: 2025-04-29
 */

#pragma once

#include "risk_types.hpp"
#include "core/market_types.hpp"
#include <functional>
#include <string>
#include <variant>
#include <chrono>
#include <optional>
#include <map>
#include <atomic>

namespace trading {
namespace risk {

using namespace core;

/**
 * Enum for different types of risk events
 */
enum class RiskEventType {
    LIMIT_BREACH,
    CIRCUIT_BREAKER_TRIGGERED,
    STOP_LOSS_TRIGGERED,
    DRAWDOWN_ALERT,
    POSITION_RECONCILIATION_MISMATCH,
    BALANCE_RECONCILIATION_MISMATCH,
    RISK_MODE_CHANGE,
    PARAMETER_UPDATE,
    EMERGENCY_ACTION,
    SYSTEM_ERROR
};

/**
 * BASE RiskEvent structure - MUST BE DEFINED FIRST
 */
struct RiskEvent {
    // Core event information
    RiskEventType type;
    core::Timestamp timestamp;
    std::string description;
    std::string severity;
    std::string eventId;        // Unique identifier
    std::string eventSource;    // Source (e.g., "system", "user")
    std::string message;        // Additional message
    
    // Enhanced features
    std::map<std::string, std::string> metadata;  // Additional context

    // Constructors
    RiskEvent(RiskEventType eventType, core::Timestamp eventTime, std::string desc,
              std::string sev = "info", std::string eventData = "{}")
        : type(eventType), timestamp(eventTime), description(std::move(desc)),
          severity(std::move(sev)), eventId(generateEventId()), eventSource("system"),
          message("") {}
    
    RiskEvent(RiskEventType eventType, std::string desc, std::string sev = "info")
        : type(eventType), timestamp(core::now()), description(std::move(desc)),
          severity(std::move(sev)), eventId(generateEventId()), eventSource("system"),
          message("") {}
    
    virtual ~RiskEvent() = default;
    
    // Severity helpers
    bool isCritical() const noexcept { return severity == "critical"; }
    bool isWarning() const noexcept { return severity == "warning"; }
    bool isInfo() const noexcept { return severity == "info"; }
    bool isError() const noexcept { return severity == "error"; }
    
    // Metadata helpers
    void addMetadata(const std::string& key, const std::string& value) {
        metadata[key] = value;
    }
    
    std::string getMetadata(const std::string& key) const {
        auto it = metadata.find(key);
        return (it != metadata.end()) ? it->second : "";
    }

private:
    static std::string generateEventId() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        return "risk_" + std::to_string(timestamp) + "_" + std::to_string(counter++);
    }
};

/**
 * NOW DEFINE SPECIFIC EVENTS THAT INHERIT FROM RiskEvent
 */

/**
 * Event for when a stop loss is triggered
 */
struct StopLossEvent : public RiskEvent {
    ExchangeId exchangeId;
    TradingPair tradingPair;
    StrategyId strategyId;
    double positionSize;
    double entryPrice;
    double currentPrice;
    double lossPercentage;
    bool liquidationInitiated;
    
    StopLossEvent(
        Timestamp eventTime,
        ExchangeId exchange,
        TradingPair pair,
        StrategyId strategy,
        double size,
        double entry,
        double current,
        double lossPercent,
        bool liquidation,
        std::string desc
    ) : RiskEvent(RiskEventType::STOP_LOSS_TRIGGERED, eventTime, std::move(desc), "critical"),
        exchangeId(std::move(exchange)),
        tradingPair(std::move(pair)),
        strategyId(std::move(strategy)),
        positionSize(size),
        entryPrice(entry),
        currentPrice(current),
        lossPercentage(lossPercent),
        liquidationInitiated(liquidation) {}
};

/**
 * Enhanced CircuitBreakerEvent structure
 */
struct CircuitBreakerEvent : public RiskEvent {
    CircuitBreakerState oldState = CircuitBreakerState::OFF;
    CircuitBreakerState newState = CircuitBreakerState::OFF;
    std::chrono::system_clock::time_point triggerTime;
    std::optional<std::chrono::system_clock::time_point> resumeTime;
    std::string reason;
    std::string triggerMetric;
    double metricValue = 0.0;
    double metricThreshold = 0.0;
    int triggerCount = 0;
    
    CircuitBreakerEvent() : RiskEvent(RiskEventType::CIRCUIT_BREAKER_TRIGGERED, 
                                     std::chrono::system_clock::now(), 
                                     "Circuit breaker state change", 
                                     "info") {
        triggerTime = std::chrono::system_clock::now();
    }
    
    CircuitBreakerEvent(CircuitBreakerState oldSt, CircuitBreakerState newSt, 
                       const std::string& reasonStr, const std::string& metric = "",
                       double value = 0.0, double threshold = 0.0)
        : RiskEvent(RiskEventType::CIRCUIT_BREAKER_TRIGGERED,
                   std::chrono::system_clock::now(),
                   "Circuit breaker: " + reasonStr,
                   (newSt == CircuitBreakerState::TRIGGERED) ? "critical" : "warning"),
          oldState(oldSt), newState(newSt), reason(reasonStr),
          triggerMetric(metric), metricValue(value), metricThreshold(threshold) {
        triggerTime = std::chrono::system_clock::now();
    }
};

/**
 * Event for when a risk limit is breached
 */
struct LimitBreachEvent : public RiskEvent {
    ExchangeId exchangeId;
    TradingPair tradingPair;
    StrategyId strategyId;
    ViolationType violationType;
    double limitValue;
    double actualValue;
    
    LimitBreachEvent(
        Timestamp eventTime,
        ExchangeId exchange,
        TradingPair pair,
        StrategyId strategy,
        ViolationType type,
        double limit,
        double actual,
        std::string desc
    ) : RiskEvent(RiskEventType::LIMIT_BREACH, eventTime, std::move(desc), "warning"),
        exchangeId(std::move(exchange)),
        tradingPair(std::move(pair)),
        strategyId(std::move(strategy)),
        violationType(type),
        limitValue(limit),
        actualValue(actual) {}
};

/**
 * Event for drawdown alerts
 */
struct DrawdownEvent : public RiskEvent {
    double drawdownPercentage;
    double highWatermark;
    double currentValue;
    StrategyId strategyId;
    Timestamp highWatermarkTime;
    bool thresholdBreached;
    
    DrawdownEvent(
        Timestamp eventTime,
        double drawdown,
        double hwm,
        double current,
        StrategyId strategy,
        Timestamp hwmTime,
        bool breached,
        std::string desc
    ) : RiskEvent(RiskEventType::DRAWDOWN_ALERT, eventTime, std::move(desc), 
                  breached ? "critical" : "warning"),
        drawdownPercentage(drawdown),
        highWatermark(hwm),
        currentValue(current),
        strategyId(std::move(strategy)),
        highWatermarkTime(hwmTime),
        thresholdBreached(breached) {}
};

/**
 * Event for position reconciliation mismatches
 */
struct PositionReconciliationEvent : public RiskEvent {
    ExchangeId exchangeId;
    TradingPair tradingPair;
    double expectedQuantity;
    double actualQuantity;
    double discrepancyPercentage;
    bool autoCorrect;
    
    PositionReconciliationEvent(
        Timestamp eventTime,
        ExchangeId exchange,
        TradingPair pair,
        double expected,
        double actual,
        double discrepancy,
        bool correction,
        std::string desc
    ) : RiskEvent(RiskEventType::POSITION_RECONCILIATION_MISMATCH, eventTime, std::move(desc), "warning"),
        exchangeId(std::move(exchange)),
        tradingPair(std::move(pair)),
        expectedQuantity(expected),
        actualQuantity(actual),
        discrepancyPercentage(discrepancy),
        autoCorrect(correction) {}
};

/**
 * Event for balance reconciliation mismatches
 */
struct BalanceReconciliationEvent : public RiskEvent {
    ExchangeId exchangeId;
    std::string currency;
    double expectedBalance;
    double actualBalance;
    double discrepancyAmount;
    bool autoCorrect;
    
    BalanceReconciliationEvent(
        Timestamp eventTime,
        ExchangeId exchange,
        std::string curr,
        double expected,
        double actual,
        double discrepancy,
        bool correction,
        std::string desc
    ) : RiskEvent(RiskEventType::BALANCE_RECONCILIATION_MISMATCH, eventTime, std::move(desc), "warning"),
        exchangeId(std::move(exchange)),
        currency(std::move(curr)),
        expectedBalance(expected),
        actualBalance(actual),
        discrepancyAmount(discrepancy),
        autoCorrect(correction) {}
};

/**
 * Event for risk mode changes
 */
struct RiskModeEvent : public RiskEvent {
    RiskManagerMode oldMode;
    RiskManagerMode newMode;
    std::string changeReason;
    
    RiskModeEvent(
        Timestamp eventTime,
        RiskManagerMode oldMd,
        RiskManagerMode newMd,
        std::string reason,
        std::string desc
    ) : RiskEvent(RiskEventType::RISK_MODE_CHANGE, eventTime, std::move(desc), "info"),
        oldMode(oldMd),
        newMode(newMd),
        changeReason(std::move(reason)) {}
};

/**
 * Event for parameter updates
 */
struct ParameterUpdateEvent : public RiskEvent {
    std::string parameterName;
    std::string oldValue;
    std::string newValue;
    std::string updatedBy;
    
    ParameterUpdateEvent(
        Timestamp eventTime,
        std::string paramName,
        std::string oldVal,
        std::string newVal,
        std::string updater,
        std::string desc
    ) : RiskEvent(RiskEventType::PARAMETER_UPDATE, eventTime, std::move(desc), "info"),
        parameterName(std::move(paramName)),
        oldValue(std::move(oldVal)),
        newValue(std::move(newVal)),
        updatedBy(std::move(updater)) {}
};

/**
 * Event for emergency actions
 */
struct EmergencyActionEvent : public RiskEvent {
    std::string actionType;
    std::string trigger;
    std::map<std::string, std::string> actionDetails;
    bool actionSuccessful;
    
    EmergencyActionEvent(
        Timestamp eventTime,
        std::string action,
        std::string triggerReason,
        std::map<std::string, std::string> details,
        bool successful,
        std::string desc
    ) : RiskEvent(RiskEventType::EMERGENCY_ACTION, eventTime, std::move(desc), "critical"),
        actionType(std::move(action)),
        trigger(std::move(triggerReason)),
        actionDetails(std::move(details)),
        actionSuccessful(successful) {}
};

/**
 * Type alias for risk event callback function
 */
using RiskEventCallback = std::function<void(const RiskEvent&)>;

/**
 * Helper function to convert RiskEventType to string
 */
inline std::string riskEventTypeToString(RiskEventType type) {
    switch (type) {
        case RiskEventType::LIMIT_BREACH: return "limit_breach";
        case RiskEventType::CIRCUIT_BREAKER_TRIGGERED: return "circuit_breaker_triggered";
        case RiskEventType::STOP_LOSS_TRIGGERED: return "stop_loss_triggered";
        case RiskEventType::DRAWDOWN_ALERT: return "drawdown_alert";
        case RiskEventType::POSITION_RECONCILIATION_MISMATCH: return "position_reconciliation_mismatch";
        case RiskEventType::BALANCE_RECONCILIATION_MISMATCH: return "balance_reconciliation_mismatch";
        case RiskEventType::RISK_MODE_CHANGE: return "risk_mode_change";
        case RiskEventType::PARAMETER_UPDATE: return "parameter_update";
        case RiskEventType::EMERGENCY_ACTION: return "emergency_action";
        case RiskEventType::SYSTEM_ERROR: return "system_error";
        default: return "unknown";
    }
}

/**
 * Helper function to get severity as string
 */
inline std::string getSeverityString(const RiskEvent& event) {
    return event.severity;
}

} // namespace risk
} // namespace trading