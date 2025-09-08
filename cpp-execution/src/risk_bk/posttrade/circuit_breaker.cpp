/**
 * @file circuit_breaker.cpp
 * @brief Implementation of clean C++17 circuit breaker system
 */

#include "risk/posttrade/circuit_breaker.hpp"
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace trading::risk {

// ============================================================================
// MarketImpactTracker Implementation
// ============================================================================

void MarketImpactTracker::update(double quantity, double impact) {
    std::lock_guard lock(mutex_);
    
    impact_history_.emplace_back(quantity, std::abs(impact));
    
    if (impact_history_.size() > MAX_HISTORY) {
        impact_history_.pop_front();
    }
}

double MarketImpactTracker::getAverageImpact() const {
    std::lock_guard lock(mutex_);
    
    if (impact_history_.empty()) {
        return 0.0;
    }
    
    double total_impact = 0.0;
    double total_quantity = 0.0;
    
    for (const auto& [quantity, impact] : impact_history_) {
        total_quantity += quantity;
        total_impact += impact * quantity;
    }
    
    return total_quantity > 0.0 ? total_impact / total_quantity : 0.0;
}

void MarketImpactTracker::reset() {
    std::lock_guard lock(mutex_);
    impact_history_.clear();
}

size_t MarketImpactTracker::size() const {
    std::lock_guard lock(mutex_);
    return impact_history_.size();
}

// ============================================================================
// SymbolRiskMonitor Implementation
// ============================================================================

SymbolRiskMonitor::SymbolRiskMonitor(std::string symbol) 
    : symbol_(std::move(symbol)) {}

bool SymbolRiskMonitor::isHalted() const {
    std::lock_guard lock(mutex_);
    return state_ == SymbolRiskState::HALTED;
}

bool SymbolRiskMonitor::halt(std::string_view reason) {
    std::lock_guard lock(mutex_);
    
    if (state_ == SymbolRiskState::HALTED) {
        return false; // Already halted
    }
    
    state_ = SymbolRiskState::HALTED;
    halt_reason_ = reason;
    halt_time_ = std::chrono::system_clock::now();
    
    return true;
}

void SymbolRiskMonitor::reset() {
    std::lock_guard lock(mutex_);
    
    state_ = SymbolRiskState::OK;
    halt_reason_.clear();
    impact_tracker_.reset();
    // halt_time_ intentionally not cleared for audit purposes
}

void SymbolRiskMonitor::updateMarketImpact(double quantity, double impact) {
    impact_tracker_.update(quantity, impact);
    
    // Update state based on impact if needed
    std::lock_guard lock(mutex_);
    if (state_ == SymbolRiskState::OK && impact_tracker_.hasSufficientHistory()) {
        const double avg_impact = impact_tracker_.getAverageImpact();
        if (avg_impact > 0.01) { // 1% threshold for warning
            state_ = SymbolRiskState::WARNING;
        }
    }
}

double SymbolRiskMonitor::getMarketImpact() const {
    return impact_tracker_.getAverageImpact();
}

SymbolRiskState SymbolRiskMonitor::getState() const {
    std::lock_guard lock(mutex_);
    return state_;
}

std::string SymbolRiskMonitor::getHaltReason() const {
    std::lock_guard lock(mutex_);
    return halt_reason_;
}

std::optional<std::chrono::system_clock::time_point> SymbolRiskMonitor::getHaltTime() const {
    std::lock_guard lock(mutex_);
    if (state_ == SymbolRiskState::HALTED) {
        return halt_time_;
    }
    return std::nullopt;
}

// ============================================================================
// CircuitBreakerManager Implementation
// ============================================================================

bool CircuitBreakerManager::initialize() {
    // Reset everything to clean state
    resetAll();
    resumeTrading();
    return true;
}

bool CircuitBreakerManager::isTriggeredForSymbol(std::string_view symbol) const {
    if (isEmergencyStop()) {
        return true;
    }
    
    auto monitor = getMonitor(symbol);
    return monitor.has_value() && monitor->get().isHalted();
}

bool CircuitBreakerManager::triggerForSymbol(std::string_view symbol, std::string_view reason) {
    auto& monitor = getOrCreateMonitor(std::string(symbol));
    return monitor.halt(reason);
}

void CircuitBreakerManager::updateMarketImpact(std::string_view symbol, double quantity, double impact) {
    auto& monitor = getOrCreateMonitor(std::string(symbol));
    monitor.updateMarketImpact(quantity, impact);
    
    // Auto-trigger if threshold exceeded
    const auto limits = getExecutionLimits(symbol);
    if (monitor.getMarketImpact() > limits.cb_slippage_threshold) {
        std::ostringstream ss;
        ss << "Market impact threshold exceeded: " << std::fixed << std::setprecision(3) 
           << monitor.getMarketImpact() * 100 << "% > " << limits.cb_slippage_threshold * 100 << "%";
        triggerForSymbol(symbol, ss.str());
    }
}

ExecutionLimits CircuitBreakerManager::getExecutionLimits(std::string_view symbol) const {
    std::lock_guard lock(limits_mutex_);
    
    const auto it = symbol_limits_.find(std::string(symbol));
    return it != symbol_limits_.end() ? it->second : default_limits_;
}

void CircuitBreakerManager::setExecutionLimits(std::string_view symbol, const ExecutionLimits& limits) {
    std::lock_guard lock(limits_mutex_);
    symbol_limits_[std::string(symbol)] = limits;
}

void CircuitBreakerManager::setDefaultLimits(const ExecutionLimits& limits) {
    std::lock_guard lock(limits_mutex_);
    default_limits_ = limits;
}

void CircuitBreakerManager::resetSymbol(std::string_view symbol) {
    std::lock_guard lock(monitors_mutex_);
    
    const auto it = symbol_monitors_.find(std::string(symbol));
    if (it != symbol_monitors_.end()) {
        it->second->reset();
    }
}

void CircuitBreakerManager::resetAll() {
    std::lock_guard lock(monitors_mutex_);
    
    for (auto& [symbol, monitor] : symbol_monitors_) {
        monitor->reset();
    }
}

void CircuitBreakerManager::emergencyStop() noexcept {
    global_emergency_stop_.store(true);
}

void CircuitBreakerManager::resumeTrading() noexcept {
    global_emergency_stop_.store(false);
}

bool CircuitBreakerManager::isEmergencyStop() const noexcept {
    return global_emergency_stop_.load();
}

std::vector<std::string> CircuitBreakerManager::getHaltedSymbols() const {
    std::vector<std::string> halted;
    std::lock_guard lock(monitors_mutex_);
    
    for (const auto& [symbol, monitor] : symbol_monitors_) {
        if (monitor->isHalted()) {
            halted.push_back(symbol);
        }
    }
    
    return halted;
}

std::vector<std::string> CircuitBreakerManager::getTrackedSymbols() const {
    std::vector<std::string> symbols;
    std::lock_guard lock(monitors_mutex_);
    
    symbols.reserve(symbol_monitors_.size());
    for (const auto& [symbol, monitor] : symbol_monitors_) {
        symbols.push_back(symbol);
    }
    
    return symbols;
}

double CircuitBreakerManager::getMarketImpactRate(std::string_view symbol) const {
    auto monitor = getMonitor(symbol);
    return monitor.has_value() ? monitor->get().getMarketImpact() : 0.0;
}

std::string CircuitBreakerManager::getStatusSummary() const {
    std::ostringstream ss;
    
    ss << "=== Circuit Breaker Status ===\n";
    ss << "Emergency Stop: " << (isEmergencyStop() ? "ACTIVE" : "INACTIVE") << "\n";
    
    const auto halted_symbols = getHaltedSymbols();
    ss << "Halted Symbols: " << halted_symbols.size() << "\n";
    
    if (!halted_symbols.empty()) {
        ss << "\nHalted Symbol Details:\n";
        for (const auto& symbol : halted_symbols) {
            auto monitor = getMonitor(symbol);
            if (monitor.has_value()) {
                const auto& mon = monitor->get();
                ss << "  " << symbol << ": " << mon.getHaltReason() << "\n";
                ss << "    Market Impact: " << std::fixed << std::setprecision(3) 
                   << mon.getMarketImpact() * 100 << "%\n";
            }
        }
    }
    
    const auto tracked = getTrackedSymbols();
    ss << "\nTotal Tracked Symbols: " << tracked.size() << "\n";
    
    return ss.str();
}

SymbolRiskState CircuitBreakerManager::getSymbolState(std::string_view symbol) const {
    auto monitor = getMonitor(symbol);
    return monitor.has_value() ? monitor->get().getState() : SymbolRiskState::OK;
}

SymbolRiskMonitor& CircuitBreakerManager::getOrCreateMonitor(const std::string& symbol) {
    std::lock_guard lock(monitors_mutex_);
    
    auto it = symbol_monitors_.find(symbol);
    if (it == symbol_monitors_.end()) {
        auto [inserted_it, success] = symbol_monitors_.emplace(
            symbol, std::make_unique<SymbolRiskMonitor>(symbol)
        );
        return *inserted_it->second;
    }
    
    return *it->second;
}

std::optional<std::reference_wrapper<const SymbolRiskMonitor>> 
CircuitBreakerManager::getMonitor(std::string_view symbol) const {
    std::lock_guard lock(monitors_mutex_);
    
    const auto it = symbol_monitors_.find(std::string(symbol));
    if (it != symbol_monitors_.end()) {
        return std::cref(*it->second);
    }
    
    return std::nullopt;
}

// ============================================================================
// ExecutionRiskManager Implementation
// ============================================================================

// ExecutionRiskManager::ExecutionRiskManager(const ExecutionLimits& default_limits) {
//     circuit_breaker_manager_.setDefaultLimits(default_limits);
//     circuit_breaker_manager_.initialize();
// }

// void ExecutionRiskManager::updateMarketImpact(std::string_view symbol, 
//                                             double executed_quantity, 
//                                             double price_impact) {
//     circuit_breaker_manager_.updateMarketImpact(symbol, executed_quantity, price_impact);
// }

// bool ExecutionRiskManager::isCircuitBreakerTriggered(std::string_view symbol) const {
//     return circuit_breaker_manager_.isTriggeredForSymbol(symbol) || 
//            risk_manager_emergency_stop_.load();
// }

// ExecutionLimits ExecutionRiskManager::getExecutionLimits(std::string_view symbol) const {
//     return circuit_breaker_manager_.getExecutionLimits(symbol);
// }

// bool ExecutionRiskManager::checkExecutionAllowed(std::string_view symbol, 
//                                                double quantity, 
//                                                double current_price) const {
//     // Check circuit breaker first
//     if (isCircuitBreakerTriggered(symbol)) {
//         return false;
//     }
    
//     // Check basic limits
//     const auto limits = getExecutionLimits(symbol);
    
//     if (quantity > limits.max_order_size) {
//         return false;
//     }
    
//     // Additional checks can be added here
//     return true;
// }

// void ExecutionRiskManager::setLimitsForSymbol(std::string_view symbol, const ExecutionLimits& limits) {
//     circuit_breaker_manager_.setExecutionLimits(symbol, limits);
// }

// void ExecutionRiskManager::resetSymbol(std::string_view symbol) {
//     circuit_breaker_manager_.resetSymbol(symbol);
// }

// void ExecutionRiskManager::emergencyStop() noexcept {
//     risk_manager_emergency_stop_.store(true);
//     circuit_breaker_manager_.emergencyStop();
// }

// void ExecutionRiskManager::resumeTrading() noexcept {
//     risk_manager_emergency_stop_.store(false);
//     circuit_breaker_manager_.resumeTrading();
// }

// std::string ExecutionRiskManager::getStatus() const {
//     std::ostringstream ss;
    
//     ss << "=== Execution Risk Manager Status ===\n";
//     ss << "Local Emergency Stop: " << (risk_manager_emergency_stop_.load() ? "ACTIVE" : "INACTIVE") << "\n";
//     ss << "\n" << circuit_breaker_manager_.getStatusSummary();
    
//     return ss.str();
// }

// double ExecutionRiskManager::getMarketImpactRate(std::string_view symbol) const {
//     return circuit_breaker_manager_.getMarketImpactRate(symbol);
// }

// std::vector<std::string> ExecutionRiskManager::getMonitoredSymbols() const {
//     return circuit_breaker_manager_.getTrackedSymbols();
// }

// bool ExecutionRiskManager::isEmergencyStopActive() const noexcept {
//     return risk_manager_emergency_stop_.load() || circuit_breaker_manager_.isEmergencyStop();
// }

} // namespace trading::risk