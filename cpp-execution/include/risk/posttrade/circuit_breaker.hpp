/**
 * @file circuit_breaker.hpp
 * @brief Modern C++20 Circuit Breaker for Trading Risk Management
 * 
 * Implements the Circuit Breaker pattern to halt trading during extreme market conditions.
 * Uses modern C++20 features for optimal performance and maintainability.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#pragma once

#include "../risk_types.hpp"
#include "../risk_events.hpp"
#include "../risk_config.hpp"
#include "core/time.hpp"

#include <string>
#include <unordered_map>
#include <map>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <memory>
#include <optional>
#include <concepts>
#include <ranges>
#include <algorithm>
#include <shared_mutex>
#include <chrono>

namespace trading::risk {

/**
 * @concept MetricProvider
 * @brief Concept for objects that can provide metric values
 */
template<typename T>
concept MetricProvider = requires(T t, const std::string& name) {
    { t.getMetric(name) } -> std::convertible_to<double>;
};

/**
 * @class CircuitBreaker
 * @brief High-performance circuit breaker for trading systems
 * 
 * Implements the Circuit Breaker pattern with C++20 features for optimal performance.
 * Monitors trading metrics and automatically halts trading when thresholds are breached.
 */
class CircuitBreaker {
public:
    using StateChangeCallback = std::function<void(CircuitBreakerState, CircuitBreakerState, std::string_view)>;
    using MetricMap = std::unordered_map<std::string, double>;
    using TimePoint = std::chrono::system_clock::time_point;
    
    /**
     * @brief Constructor with configuration
     * @param config Risk configuration containing circuit breaker settings
     */
    explicit CircuitBreaker(const RiskConfig& config);
    
    /**
     * @brief Initialize the circuit breaker
     * @return true if initialization successful
     */
    [[nodiscard]] bool initialize() noexcept;
    
    /**
     * @brief Evaluate metrics and determine circuit breaker state
     * @param metrics Map of metric names to values
     * @return New circuit breaker state
     */
    [[nodiscard]] CircuitBreakerState evaluateMetrics(const MetricMap& metrics) noexcept;
    
    /**
     * @brief Update a specific metric value
     * @param metricName Name of the metric
     * @param value New metric value
     * @return New circuit breaker state
     */
    [[nodiscard]] CircuitBreakerState updateMetric(std::string_view metricName, double value) noexcept;
    
    /**
     * @brief Get current circuit breaker state (thread-safe)
     * @return Current state
     */
    [[nodiscard]] CircuitBreakerState getState() const noexcept {
        return state_.load(std::memory_order_acquire);
    }
    
    /**
     * @brief Check if circuit breaker is healthy
     * @return true if in OFF or WARNING state
     */
    [[nodiscard]] bool isHealthy() const noexcept {
        auto state = state_.load(std::memory_order_acquire);
        return state == CircuitBreakerState::OFF || state == CircuitBreakerState::WARNING;
    }
    
    /**
     * @brief Manually trigger the circuit breaker
     * @param reason Reason for manual triggering
     * @return true if successful
     */
    [[nodiscard]] bool manualTrigger(std::string_view reason) noexcept;
    
    /**
     * @brief Reset circuit breaker to normal state
     * @return true if successful
     */
    [[nodiscard]] bool reset() noexcept;
    
    /**
     * @brief Check if trading is currently allowed
     * @return true if trading allowed
     */
    [[nodiscard]] bool isTradingAllowed() const noexcept;
    
    /**
     * @brief Register callback for state changes
     * @param callback Function to call on state changes
     */
    void registerStateChangeCallback(StateChangeCallback callback);
    
    /**
     * @brief Update circuit breaker configuration
     * @param newConfig New configuration
     * @return true if update successful
     */
    [[nodiscard]] bool updateConfiguration(const CircuitBreakerConfig& newConfig) noexcept;
    
    /**
     * @brief Get current configuration (thread-safe copy)
     * @return Current configuration
     */
    [[nodiscard]] CircuitBreakerConfig getConfiguration() const;
    
    /**
     * @brief Get seconds until automatic reset
     * @return Seconds until reset, or 0 if not applicable
     */
    [[nodiscard]] std::chrono::seconds getTimeUntilReset() const noexcept;
    
    /**
     * @brief Get trigger history (thread-safe copy)
     * @return Vector of historical trigger events
     */
    [[nodiscard]] std::vector<CircuitBreakerEvent> getTriggerHistory() const;
    
    /**
     * @brief Get current metric values (thread-safe copy)
     * @return Map of current metrics
     */
    [[nodiscard]] MetricMap getCurrentMetrics() const;
    
    /**
     * @brief Check if a specific metric is in warning state
     * @param metricName Name of the metric to check
     * @return true if metric is in warning state
     */
    [[nodiscard]] bool isMetricInWarning(std::string_view metricName) const noexcept;
    
    /**
     * @brief Get detailed status information
     * @return Detailed status as string
     */
    [[nodiscard]] std::string getDetailedStatus() const;

private:
    // Configuration and state
    mutable std::shared_mutex configMutex_;
    CircuitBreakerConfig config_;
    
    // Atomic state for lockless reads
    std::atomic<CircuitBreakerState> state_{CircuitBreakerState::OFF};
    
    // Use non-atomic time points with mutex protection for consistency
    mutable std::mutex timeMutex_;
    TimePoint triggerTime_;
    TimePoint coolingStartTime_;
    TimePoint triggerCountResetTime_;
    
    std::atomic<int> triggerCount_{0};
    
    // Protected data with fine-grained locking
    mutable std::shared_mutex metricsMutex_;
    MetricMap currentMetrics_;
    
    mutable std::shared_mutex historyMutex_;
    std::vector<CircuitBreakerEvent> triggerHistory_;
    
    mutable std::shared_mutex callbackMutex_;
    std::vector<StateChangeCallback> stateChangeCallbacks_;
    
    std::string lastTriggerReason_;
    
    // Private helper methods
    [[nodiscard]] bool isMetricCritical(std::string_view metricName, double value, double& threshold) const noexcept;
    [[nodiscard]] bool isMetricWarning(std::string_view metricName, double value, double& threshold) const noexcept;
    
    void setState(CircuitBreakerState newState, std::string_view reason) noexcept;
    [[nodiscard]] bool shouldAutoReset() const noexcept;
    void checkTriggerCount() noexcept;
    
    [[nodiscard]] CircuitBreakerEvent createEvent(CircuitBreakerState oldState, 
                                                 CircuitBreakerState newState,
                                                 std::string_view metricName = "",
                                                 double threshold = 0.0, 
                                                 double value = 0.0) const;
    
    void addToHistory(CircuitBreakerEvent event);
    void notifyCallbacks(CircuitBreakerState oldState, CircuitBreakerState newState, std::string_view reason);
    
    // Template helper for metric evaluation
    template<typename Predicate>
    [[nodiscard]] std::optional<std::pair<std::string, double>> 
    findMetricViolation(const MetricMap& metrics, Predicate pred) const;
};

// Template implementation
template<typename Predicate>
std::optional<std::pair<std::string, double>> 
CircuitBreaker::findMetricViolation(const MetricMap& metrics, Predicate pred) const {
    std::shared_lock lock(configMutex_);
    
    for (const auto& [metricName, value] : metrics) {
        if (auto it = config_.metricThresholds.find(metricName); 
            it != config_.metricThresholds.end()) {
            double threshold = 0.0;
            if (pred(metricName, value, threshold)) {
                return std::make_pair(metricName, threshold);
            }
        }
    }
    return std::nullopt;
}

} // namespace trading::risk
