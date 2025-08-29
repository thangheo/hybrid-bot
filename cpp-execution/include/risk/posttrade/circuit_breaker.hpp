/**
 * @file circuit_breaker.hpp
 * @brief Clean, modern C++17 circuit breaker design for execution risk management
 * 
 * Focused design with clear separation of concerns and RAII principles.
 * 
 * @author Trading System Team
 * @date August 29, 2025
 */

#pragma once

#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <deque>
#include <memory>
#include <vector>
#include <optional>
#include <string_view>

namespace trading::risk {

/**
 * @brief Risk state for individual symbols
 */
enum class SymbolRiskState : uint8_t {
    OK = 0,
    WARNING = 1,
    HALTED = 2
};

/**
 * @brief Execution limits configuration per symbol
 */
struct ExecutionLimits {
    double max_order_size = 100000.0;
    double max_market_impact = 0.002;              // 0.2%
    double max_price_deviation = 0.05;             // 5%
    double max_participation_rate = 0.2;           // 20% of volume
    
    // Circuit breaker thresholds
    double cb_slippage_threshold = 0.005;          // 0.5%
    double cb_rejection_rate_threshold = 0.1;      // 10%
    
    int max_orders_per_second = 10;
    int cb_max_consecutive_rejections = 5;
    bool allow_market_orders = true;
    bool enable_monitoring = true;
    
    // Factory methods for common configurations
    [[nodiscard]] static ExecutionLimits conservative() noexcept {
        return ExecutionLimits{
            .max_order_size = 50000.0,
            .max_market_impact = 0.001,
            .cb_slippage_threshold = 0.003
        };
    }
    
    [[nodiscard]] static ExecutionLimits aggressive() noexcept {
        return ExecutionLimits{
            .max_order_size = 200000.0,
            .max_market_impact = 0.005,
            .cb_slippage_threshold = 0.01
        };
    }
};

/**
 * @brief Thread-safe market impact tracker using RAII
 */
class MarketImpactTracker {
public:
    static constexpr size_t MAX_HISTORY = 50;
    
    /**
     * @brief Record new market impact data
     * @param quantity Executed quantity
     * @param impact Price impact as ratio (absolute value will be used)
     */
    void update(double quantity, double impact);
    
    /**
     * @brief Get volume-weighted average market impact
     * @return Average impact ratio, 0.0 if no history
     */
    [[nodiscard]] double getAverageImpact() const;
    
    /**
     * @brief Clear all impact history
     */
    void reset();
    
    /**
     * @brief Get number of impact records stored
     * @return History size
     */
    [[nodiscard]] size_t size() const;
    
    /**
     * @brief Check if tracker has sufficient data for reliable metrics
     * @return true if sufficient history exists
     */
    [[nodiscard]] bool hasSufficientHistory() const { return size() >= 5; }

private:
    mutable std::mutex mutex_;
    std::deque<std::pair<double, double>> impact_history_; // {quantity, impact}
};

/**
 * @brief Per-symbol risk monitor with automatic state management
 */
class SymbolRiskMonitor {
public:
    /**
     * @brief Construct monitor for specific symbol
     * @param symbol Trading symbol to monitor
     */
    explicit SymbolRiskMonitor(std::string symbol);
    
    // Non-copyable, movable
    SymbolRiskMonitor(const SymbolRiskMonitor&) = delete;
    SymbolRiskMonitor& operator=(const SymbolRiskMonitor&) = delete;
    SymbolRiskMonitor(SymbolRiskMonitor&&) = default;
    SymbolRiskMonitor& operator=(SymbolRiskMonitor&&) = default;
    
    /**
     * @brief Check if symbol trading is currently halted
     * @return true if halted
     */
    [[nodiscard]] bool isHalted() const;
    
    /**
     * @brief Halt trading for this symbol
     * @param reason Descriptive reason for halt
     * @return true if halt was successful (wasn't already halted)
     */
    bool halt(std::string_view reason);
    
    /**
     * @brief Reset symbol to normal trading state
     */
    void reset();
    
    /**
     * @brief Update market impact metrics
     * @param quantity Executed quantity
     * @param impact Price impact observed
     */
    void updateMarketImpact(double quantity, double impact);
    
    /**
     * @brief Get current market impact rate
     * @return Volume-weighted average market impact
     */
    [[nodiscard]] double getMarketImpact() const;
    
    /**
     * @brief Get current risk state
     * @return Current state (OK, WARNING, HALTED)
     */
    [[nodiscard]] SymbolRiskState getState() const;
    
    /**
     * @brief Get halt reason if currently halted
     * @return Halt reason, empty if not halted
     */
    [[nodiscard]] std::string getHaltReason() const;
    
    /**
     * @brief Get symbol this monitor tracks
     * @return Symbol name
     */
    [[nodiscard]] const std::string& getSymbol() const noexcept { return symbol_; }
    
    /**
     * @brief Get time when symbol was halted (if halted)
     * @return Optional halt time
     */
    [[nodiscard]] std::optional<std::chrono::system_clock::time_point> getHaltTime() const;

private:
    const std::string symbol_;
    mutable std::mutex mutex_;
    SymbolRiskState state_{SymbolRiskState::OK};
    std::string halt_reason_;
    std::chrono::system_clock::time_point halt_time_;
    MarketImpactTracker impact_tracker_;
};

/**
 * @brief Main circuit breaker manager with clean interface
 */
class CircuitBreakerManager {
public:
    CircuitBreakerManager() = default;
    
    // Non-copyable, non-movable for safety
    CircuitBreakerManager(const CircuitBreakerManager&) = delete;
    CircuitBreakerManager& operator=(const CircuitBreakerManager&) = delete;
    CircuitBreakerManager(CircuitBreakerManager&&) = delete;
    CircuitBreakerManager& operator=(CircuitBreakerManager&&) = delete;
    
    /**
     * @brief Initialize circuit breaker system
     * @return true if successful
     */
    [[nodiscard]] bool initialize();
    
    // ========================================================================
    // CORE INTERFACE - What ExecutionRiskManager needs
    // ========================================================================
    
    /**
     * @brief Check if circuit breaker is triggered for symbol
     * @param symbol Symbol to check
     * @return true if circuit breaker is active
     */
    [[nodiscard]] bool isTriggeredForSymbol(std::string_view symbol) const;
    
    /**
     * @brief Manually trigger circuit breaker for symbol
     * @param symbol Symbol to halt
     * @param reason Descriptive reason
     * @return true if successfully triggered
     */
    bool triggerForSymbol(std::string_view symbol, std::string_view reason);
    
    /**
     * @brief Update market impact and auto-check thresholds
     * @param symbol Trading symbol
     * @param quantity Executed quantity
     * @param impact Price impact observed
     */
    void updateMarketImpact(std::string_view symbol, double quantity, double impact);
    
    /**
     * @brief Get execution limits for symbol
     * @param symbol Symbol to query
     * @return ExecutionLimits (default limits if symbol not configured)
     */
    [[nodiscard]] ExecutionLimits getExecutionLimits(std::string_view symbol) const;
    
    // ========================================================================
    // CONFIGURATION INTERFACE
    // ========================================================================
    
    /**
     * @brief Set execution limits for specific symbol
     * @param symbol Symbol to configure
     * @param limits New limits
     */
    void setExecutionLimits(std::string_view symbol, const ExecutionLimits& limits);
    
    /**
     * @brief Set default limits for unconfigured symbols
     * @param limits Default limits
     */
    void setDefaultLimits(const ExecutionLimits& limits);
    
    /**
     * @brief Reset specific symbol to normal state
     * @param symbol Symbol to reset
     */
    void resetSymbol(std::string_view symbol);
    
    /**
     * @brief Reset all symbols to normal state
     */
    void resetAll();
    
    // ========================================================================
    // EMERGENCY CONTROLS
    // ========================================================================
    
    /**
     * @brief Trigger global emergency stop
     */
    void emergencyStop() noexcept;
    
    /**
     * @brief Resume all trading
     */
    void resumeTrading() noexcept;
    
    /**
     * @brief Check if global emergency stop is active
     * @return true if emergency stop active
     */
    [[nodiscard]] bool isEmergencyStop() const noexcept;
    
    // ========================================================================
    // MONITORING INTERFACE
    // ========================================================================
    
    /**
     * @brief Get list of currently halted symbols
     * @return Vector of halted symbol names
     */
    [[nodiscard]] std::vector<std::string> getHaltedSymbols() const;
    
    /**
     * @brief Get list of all tracked symbols
     * @return Vector of all monitored symbol names
     */
    [[nodiscard]] std::vector<std::string> getTrackedSymbols() const;
    
    /**
     * @brief Get current market impact rate for symbol
     * @param symbol Symbol to check
     * @return Market impact rate, 0.0 if symbol not tracked
     */
    [[nodiscard]] double getMarketImpactRate(std::string_view symbol) const;
    
    /**
     * @brief Get detailed status summary
     * @return Multi-line status string
     */
    [[nodiscard]] std::string getStatusSummary() const;
    
    /**
     * @brief Get risk state for specific symbol
     * @param symbol Symbol to check
     * @return Current SymbolRiskState
     */
    [[nodiscard]] SymbolRiskState getSymbolState(std::string_view symbol) const;

private:
    mutable std::mutex monitors_mutex_;
    std::unordered_map<std::string, std::unique_ptr<SymbolRiskMonitor>> symbol_monitors_;
    
    mutable std::mutex limits_mutex_;
    std::unordered_map<std::string, ExecutionLimits> symbol_limits_;
    ExecutionLimits default_limits_;
    
    std::atomic<bool> global_emergency_stop_{false};
    
    /**
     * @brief Get or create symbol monitor (thread-safe)
     * @param symbol Symbol name
     * @return Reference to monitor
     */
    SymbolRiskMonitor& getOrCreateMonitor(const std::string& symbol);
    
    /**
     * @brief Get existing monitor (const version)
     * @param symbol Symbol name
     * @return Optional reference to monitor
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const SymbolRiskMonitor>> 
    getMonitor(std::string_view symbol) const;
};

/**
 * @brief High-level execution risk manager
 * 
 * Simplified interface that integrates circuit breaker with execution risk management.
 * This is the main class that your execution engine should use.
 */
// class ExecutionRiskManager {
// public:
//     /**
//      * @brief Construct with configuration
//      * @param config Risk configuration (can be simple struct)
//      */
//     explicit ExecutionRiskManager(const ExecutionLimits& default_limits = {});
    
//     // ========================================================================
//     // MAIN INTERFACE - What your execution engine needs
//     // ========================================================================
    
//     /**
//      * @brief Update market impact and check all thresholds
//      * @param symbol Trading symbol
//      * @param executed_quantity Quantity that was executed
//      * @param price_impact Price impact observed (as ratio)
//      */
//     void updateMarketImpact(std::string_view symbol, double executed_quantity, double price_impact);
    
//     /**
//      * @brief Check if circuit breaker prevents trading for symbol
//      * @param symbol Symbol to check
//      * @return true if circuit breaker is triggered (halt trading)
//      */
//     [[nodiscard]] bool isCircuitBreakerTriggered(std::string_view symbol) const;
    
//     /**
//      * @brief Get execution limits for symbol
//      * @param symbol Symbol to get limits for
//      * @return ExecutionLimits configuration
//      */
//     [[nodiscard]] ExecutionLimits getExecutionLimits(std::string_view symbol) const;
    
//     /**
//      * @brief Pre-execution risk check
//      * @param symbol Trading symbol
//      * @param quantity Intended order quantity
//      * @param current_price Current market price
//      * @return true if execution is allowed
//      */
//     [[nodiscard]] bool checkExecutionAllowed(std::string_view symbol, double quantity, double current_price) const;
    
//     // ========================================================================
//     // CONFIGURATION
//     // ========================================================================
    
//     /**
//      * @brief Set limits for specific symbol
//      * @param symbol Symbol to configure
//      * @param limits New limits
//      */
//     void setLimitsForSymbol(std::string_view symbol, const ExecutionLimits& limits);
    
//     /**
//      * @brief Reset symbol to normal state
//      * @param symbol Symbol to reset
//      */
//     void resetSymbol(std::string_view symbol);
    
//     /**
//      * @brief Emergency stop all trading
//      */
//     void emergencyStop() noexcept;
    
//     /**
//      * @brief Resume normal trading operations
//      */
//     void resumeTrading() noexcept;
    
//     // ========================================================================
//     // MONITORING
//     // ========================================================================
    
//     /**
//      * @brief Get comprehensive status report
//      * @return Detailed status string
//      */
//     [[nodiscard]] std::string getStatus() const;
    
//     /**
//      * @brief Get current market impact rate for symbol
//      * @param symbol Symbol to check
//      * @return Market impact rate
//      */
//     [[nodiscard]] double getMarketImpactRate(std::string_view symbol) const;
    
//     /**
//      * @brief Get all symbols currently being monitored
//      * @return Vector of symbol names
//      */
//     [[nodiscard]] std::vector<std::string> getMonitoredSymbols() const;
    
//     /**
//      * @brief Check if emergency stop is active
//      * @return true if global emergency stop is active
//      */
//     [[nodiscard]] bool isEmergencyStopActive() const noexcept;

// private:
//     CircuitBreakerManager circuit_breaker_manager_;
//     std::atomic<bool> risk_manager_emergency_stop_{false};
// };

} // namespace trading::risk