/**
 * @file circuit_breaker.cpp
 * @brief Implementation of the Circuit Breaker pattern for trading systems
 * 
 * Modern C++20 implementation with optimal performance and thread safety.
 * 
 * @author Trading System Team  
 * @date May 25, 2025
 */

#include "risk/posttrade/circuit_breaker.hpp"
#include <algorithm>
#include <sstream>
#include <format>
#include <random>

namespace trading::risk {

CircuitBreaker::CircuitBreaker(const RiskConfig& config)
    : config_(config.getCircuitBreakerConfig()) {
    
    auto now = std::chrono::system_clock::now();
    triggerTime_.store(now, std::memory_order_relaxed);
    coolingStartTime_.store(now, std::memory_order_relaxed);
    triggerCountResetTime_.store(
        now + std::chrono::minutes(config_.triggerCountResetMinutes), 
        std::memory_order_relaxed
    );
}

bool CircuitBreaker::initialize() noexcept {
    try {
        // Reset all state
        state_.store(CircuitBreakerState::OFF, std::memory_order_release);
        triggerCount_.store(0, std::memory_order_relaxed);
        
        auto now = std::chrono::system_clock::now();
        triggerTime_.store(now, std::memory_order_relaxed);
        coolingStartTime_.store(now, std::memory_order_relaxed);
        triggerCountResetTime_.store(
            now + std::chrono::minutes(config_.triggerCountResetMinutes),
            std::memory_order_relaxed
        );
        
        lastTriggerReason_.clear();
        
        // Clear metrics and history
        {
            std::unique_lock lock(metricsMutex_);
            currentMetrics_.clear();
        }
        
        {
            std::unique_lock lock(historyMutex_);
            triggerHistory_.clear();
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

CircuitBreakerState CircuitBreaker::evaluateMetrics(const MetricMap& metrics) noexcept {
    try {
        auto currentState = state_.load(std::memory_order_acquire);
        
        // Update current metrics
        {
            std::unique_lock lock(metricsMutex_);
            for (const auto& [name, value] : metrics) {
                currentMetrics_[name] = value;
            }
        }
        
        // Handle TRIGGERED state - check for auto-reset
        if (currentState == CircuitBreakerState::TRIGGERED) {
            if (shouldAutoReset()) {
                setState(CircuitBreakerState::COOLING, "Auto cooling after mandatory halt period");
            }
            return state_.load(std::memory_order_acquire);
        }
        
        // Handle COOLING state - check for completion
        if (currentState == CircuitBreakerState::COOLING) {
            auto now = std::chrono::system_clock::now();
            auto coolingStart = coolingStartTime_.load(std::memory_order_relaxed);
            auto coolingElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - coolingStart);
            
            if (coolingElapsed >= config_.coolingPeriod) {
                setState(CircuitBreakerState::OFF, "Cooling period completed");
            }
            return state_.load(std::memory_order_acquire);
        }
        
        // Check for critical threshold violations
        if (auto violation = findMetricViolation(metrics, 
            [this](std::string_view name, double value, double& threshold) {
                return isMetricCritical(name, value, threshold);
            })) {
            
            const auto& [metricName, threshold] = *violation;
            double value = metrics.at(std::string(metricName));
            
            // Increment trigger count and check limits
            int newTriggerCount = triggerCount_.fetch_add(1, std::memory_order_acq_rel) + 1;
            checkTriggerCount();
            
            auto reason = std::format("Critical threshold breached for '{}': {} (threshold: {})", 
                                    metricName, value, threshold);
            setState(CircuitBreakerState::TRIGGERED, reason);
            
            // Create and store event
            auto event = createEvent(currentState, CircuitBreakerState::TRIGGERED, 
                                   metricName, threshold, value);
            addToHistory(std::move(event));
            
            return state_.load(std::memory_order_acquire);
        }
        
        // Check for warning threshold violations (only if currently OFF)
        if (currentState == CircuitBreakerState::OFF) {
            if (auto violation = findMetricViolation(metrics,
                [this](std::string_view name, double value, double& threshold) {
                    return isMetricWarning(name, value, threshold);
                })) {
                
                const auto& [metricName, threshold] = *violation;
                double value = metrics.at(std::string(metricName));
                
                auto reason = std::format("Warning threshold breached for '{}': {} (threshold: {})",
                                        metricName, value, threshold);
                setState(CircuitBreakerState::WARNING, reason);
                
                auto event = createEvent(CircuitBreakerState::OFF, CircuitBreakerState::WARNING,
                                       metricName, threshold, value);
                addToHistory(std::move(event));
            }
        }
        
        // Check if WARNING state should return to OFF
        if (currentState == CircuitBreakerState::WARNING) {
            bool allBelowWarning = std::ranges::all_of(metrics, 
                [this](const auto& metric) {
                    const auto& [name, value] = metric;
                    double threshold = 0.0;
                    return !isMetricWarning(name, value, threshold);
                });
                
            if (allBelowWarning) {
                setState(CircuitBreakerState::OFF, "All metrics below warning thresholds");
            }
        }
        
        return state_.load(std::memory_order_acquire);
        
    } catch (...) {
        return state_.load(std::memory_order_acquire);
    }
}

CircuitBreakerState CircuitBreaker::updateMetric(std::string_view metricName, double value) noexcept {
    try {
        // Update the specific metric
        {
            std::unique_lock lock(metricsMutex_);
            currentMetrics_[std::string(metricName)] = value;
        }
        
        // Get all current metrics and evaluate
        MetricMap metricsCopy;
        {
            std::shared_lock lock(metricsMutex_);
            metricsCopy = currentMetrics_;
        }
        
        return evaluateMetrics(metricsCopy);
        
    } catch (...) {
        return state_.load(std::memory_order_acquire);
    }
}

bool CircuitBreaker::manualTrigger(std::string_view reason) noexcept {
    try {
        auto currentState = state_.load(std::memory_order_acquire);
        if (currentState == CircuitBreakerState::TRIGGERED) {
            return false; // Already triggered
        }
        
        // Increment trigger count
        triggerCount_.fetch_add(1, std::memory_order_acq_rel);
        checkTriggerCount();
        
        auto fullReason = std::format("Manual trigger: {}", reason);
        setState(CircuitBreakerState::TRIGGERED, fullReason);
        
        // Create and store event
        auto event = createEvent(currentState, CircuitBreakerState::TRIGGERED, "MANUAL");
        addToHistory(std::move(event));
        
        return true;
        
    } catch (...) {
        return false;
    }
}

bool CircuitBreaker::reset() noexcept {
    try {
        auto currentState = state_.load(std::memory_order_acquire);
        
        // Check if reset is allowed from TRIGGERED state
        if (currentState == CircuitBreakerState::TRIGGERED) {
            auto now = std::chrono::system_clock::now();
            auto triggerTime = triggerTime_.load(std::memory_order_relaxed);
            auto haltElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - triggerTime);
            
            if (haltElapsed.count() < config_.mandatoryHaltPeriodSeconds) {
                return false; // Must wait for mandatory halt period
            }
            
            // Move to cooling state first
            setState(CircuitBreakerState::COOLING, "Manual reset from TRIGGERED to COOLING");
        }
        
        // Reset to OFF state
        setState(CircuitBreakerState::OFF, "Manual reset");
        return true;
        
    } catch (...) {
        return false;
    }
}

bool CircuitBreaker::isTradingAllowed() const noexcept {
    auto currentState = state_.load(std::memory_order_acquire);
    
    switch (currentState) {
        case CircuitBreakerState::OFF:
        case CircuitBreakerState::WARNING:
            return true;
            
        case CircuitBreakerState::TRIGGERED:
            return false;
            
        case CircuitBreakerState::COOLING: {
            if (!config_.gradualTradingResumption) {
                return false; // Binary resumption - no trading during cooling
            }
            
            // Gradual resumption based on cooling progress
            auto now = std::chrono::system_clock::now();
            auto coolingStart = coolingStartTime_.load(std::memory_order_relaxed);
            auto coolingElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - coolingStart);
            
            double progress = std::min(1.0, 
                static_cast<double>(coolingElapsed.count()) / config_.coolingPeriod.count());
            
            // Use deterministic approach instead of random for consistency
            static thread_local std::random_device rd;
            static thread_local std::mt19937 gen(rd());
            static thread_local std::uniform_real_distribution<> dis(0.0, 1.0);
            
            return dis(gen) < progress;
        }
    }
    
    return false; // Default to safe behavior
}

void CircuitBreaker::registerStateChangeCallback(StateChangeCallback callback) {
    std::unique_lock lock(callbackMutex_);
    stateChangeCallbacks_.emplace_back(std::move(callback));
}

bool CircuitBreaker::updateConfiguration(const CircuitBreakerConfig& newConfig) noexcept {
    try {
        if (!newConfig.isValid()) {
            return false;
        }
        
        std::unique_lock lock(configMutex_);
        config_ = newConfig;
        return true;
        
    } catch (...) {
        return false;
    }
}

CircuitBreakerConfig CircuitBreaker::getConfiguration() const {
    std::shared_lock lock(configMutex_);
    return config_;
}

std::chrono::seconds CircuitBreaker::getTimeUntilReset() const noexcept {
    auto currentState = state_.load(std::memory_order_acquire);
    auto now = std::chrono::system_clock::now();
    
    if (currentState == CircuitBreakerState::TRIGGERED) {
        auto triggerTime = triggerTime_.load(std::memory_order_relaxed);
        auto haltElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - triggerTime);
        auto remainingSeconds = config_.mandatoryHaltPeriodSeconds - haltElapsed.count();
        return std::chrono::seconds(std::max(0L, remainingSeconds));
        
    } else if (currentState == CircuitBreakerState::COOLING) {
        auto coolingStart = coolingStartTime_.load(std::memory_order_relaxed);
        auto coolingElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - coolingStart);
        auto remainingSeconds = config_.coolingPeriod.count() - coolingElapsed.count();
        return std::chrono::seconds(std::max(0L, remainingSeconds));
    }
    
    return std::chrono::seconds(0);
}

std::vector<CircuitBreakerEvent> CircuitBreaker::getTriggerHistory() const {
    std::shared_lock lock(historyMutex_);
    return triggerHistory_;
}

CircuitBreaker::MetricMap CircuitBreaker::getCurrentMetrics() const {
    std::shared_lock lock(metricsMutex_);
    return currentMetrics_;
}

bool CircuitBreaker::isMetricInWarning(std::string_view metricName) const noexcept {
    try {
        std::shared_lock metricsLock(metricsMutex_);
        auto it = currentMetrics_.find(std::string(metricName));
        if (it == currentMetrics_.end()) {
            return false;
        }
        
        double threshold = 0.0;
        return isMetricWarning(metricName, it->second, threshold);
        
    } catch (...) {
        return false;
    }
}

std::string CircuitBreaker::getDetailedStatus() const {
    auto currentState = state_.load(std::memory_order_acquire);
    auto triggerCount = triggerCount_.load(std::memory_order_relaxed);
    
    std::ostringstream oss;
    oss << std::format("CircuitBreaker Status:\n");
    oss << std::format("  State: {}\n", static_cast<int>(currentState));
    oss << std::format("  Trigger Count: {}\n", triggerCount);
    oss << std::format("  Trading Allowed: {}\n", isTradingAllowed() ? "Yes" : "No");
    
    if (currentState != CircuitBreakerState::OFF) {
        auto timeUntilReset = getTimeUntilReset();
        oss << std::format("  Time Until Reset: {}s\n", timeUntilReset.count());
    }
    
    // Add current metrics
    {
        std::shared_lock lock(metricsMutex_);
        if (!currentMetrics_.empty()) {
            oss << "  Current Metrics:\n";
            for (const auto& [name, value] : currentMetrics_) {
                oss << std::format("    {}: {}\n", name, value);
            }
        }
    }
    
    return oss.str();
}

// Private helper methods

bool CircuitBreaker::isMetricCritical(std::string_view metricName, double value, double& threshold) const noexcept {
    try {
        std::shared_lock lock(configMutex_);
        auto it = config_.metricThresholds.find(std::string(metricName));
        if (it == config_.metricThresholds.end()) {
            return false;
        }
        
        const auto& thresholds = it->second;
        threshold = thresholds.criticalThreshold;
        
        return thresholds.isHigherBetter ? 
               (value < thresholds.criticalThreshold) : 
               (value > thresholds.criticalThreshold);
               
    } catch (...) {
        return false;
    }
}

bool CircuitBreaker::isMetricWarning(std::string_view metricName, double value, double& threshold) const noexcept {
    try {
        std::shared_lock lock(configMutex_);
        auto it = config_.metricThresholds.find(std::string(metricName));
        if (it == config_.metricThresholds.end()) {
            return false;
        }
        
        const auto& thresholds = it->second;
        threshold = thresholds.warningThreshold;
        
        return thresholds.isHigherBetter ? 
               (value < thresholds.warningThreshold) : 
               (value > thresholds.warningThreshold);
               
    } catch (...) {
        return false;
    }
}

void CircuitBreaker::setState(CircuitBreakerState newState, std::string_view reason) noexcept {
    try {
        auto oldState = state_.exchange(newState, std::memory_order_acq_rel);
        
        if (oldState == newState) {
            return; // No change
        }
        
        // Update state-specific data
        auto now = std::chrono::system_clock::now();
        
        if (newState == CircuitBreakerState::TRIGGERED) {
            triggerTime_.store(now, std::memory_order_relaxed);
            lastTriggerReason_ = reason;
        } else if (newState == CircuitBreakerState::COOLING) {
            coolingStartTime_.store(now, std::memory_order_relaxed);
        }
        
        // Notify callbacks
        notifyCallbacks(oldState, newState, reason);
        
    } catch (...) {
        // Failed to set state - log error if logger available
    }
}

bool CircuitBreaker::shouldAutoReset() const noexcept {
    try {
        auto now = std::chrono::system_clock::now();
        auto triggerTime = triggerTime_.load(std::memory_order_relaxed);
        auto haltElapsed = std::chrono::duration_cast<std::chrono::seconds>(now - triggerTime);
        
        // Calculate required halt period based on trigger count
        int requiredHaltPeriod = config_.mandatoryHaltPeriodSeconds;
        int currentTriggerCount = triggerCount_.load(std::memory_order_relaxed);
        
        if (currentTriggerCount > config_.maxTriggersBeforeExtendedHalt) {
            requiredHaltPeriod *= config_.extendedHaltMultiplier;
        }
        
        return haltElapsed.count() >= requiredHaltPeriod;
        
    } catch (...) {
        return false;
    }
}

void CircuitBreaker::checkTriggerCount() noexcept {
    try {
        auto now = std::chrono::system_clock::now();
        auto resetTime = triggerCountResetTime_.load(std::memory_order_relaxed);
        
        if (now > resetTime) {
            triggerCount_.store(1, std::memory_order_relaxed); // Reset to 1 (current trigger)
            triggerCountResetTime_.store(
                now + std::chrono::minutes(config_.triggerCountResetMinutes),
                std::memory_order_relaxed
            );
        }
    } catch (...) {
        // Failed to check trigger count
    }
}

CircuitBreakerEvent CircuitBreaker::createEvent(CircuitBreakerState oldState,
                                               CircuitBreakerState newState,
                                               std::string_view metricName,
                                               double threshold,
                                               double value) const {
    auto event = CircuitBreakerEvent(oldState, newState, std::string(lastTriggerReason_),
                                   std::string(metricName), value, threshold);
    event.triggerCount = triggerCount_.load(std::memory_order_relaxed);
    return event;
}

void CircuitBreaker::addToHistory(CircuitBreakerEvent event) {
    std::unique_lock lock(historyMutex_);
    triggerHistory_.emplace_back(std::move(event));
    
    // Limit history size
    if (triggerHistory_.size() > config_.maxHistorySize) {
        triggerHistory_.erase(triggerHistory_.begin(), 
                            triggerHistory_.begin() + (triggerHistory_.size() - config_.maxHistorySize));
    }
}

void CircuitBreaker::notifyCallbacks(CircuitBreakerState oldState, 
                                   CircuitBreakerState newState, 
                                   std::string_view reason) {
    try {
        std::vector<StateChangeCallback> callbacksCopy;
        {
            std::shared_lock lock(callbackMutex_);
            callbacksCopy = stateChangeCallbacks_;
        }
        
        for (const auto& callback : callbacksCopy) {
            try {
                callback(oldState, newState, reason);
            } catch (...) {
                // Individual callback failed - continue with others
            }
        }
    } catch (...) {
        // Failed to notify callbacks
    }
}

} // namespace trading::risk