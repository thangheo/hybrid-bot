/**
 * @file post_trade_monitor.hpp
 * @brief Modern C++20 Post Trade Monitor with comprehensive risk management
 * 
 * Orchestrates all post-trade monitoring components with thread-safe operations,
 * event-driven architecture, and advanced risk management capabilities.
 * 
 * @author Trading System Team
 * @date May 26, 2025
 */

#pragma once

#include "../risk_types.hpp"
#include "../risk_events.hpp"
#include "../risk_config.hpp"
#include "../portfolio/position_tracker.hpp"
#include "../interfaces/data_interface.hpp"
#include "../interfaces/execution_interface.hpp"
#include "stop_loss_manager.hpp"
#include "drawdown_monitor.hpp"
#include "circuit_breaker.hpp"
#include "balance_reconciliation.hpp"
#include "execution/models/execution_report.hpp"
#include "strategy/types/performance_metrics.hpp"
#include "core/market_types.hpp"
#include "core/time.hpp"

#include <atomic>
#include <chrono>
#include <concepts>
#include <coroutine>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <deque>

namespace trading::risk {

/**
 * Enhanced risk event with additional context and metadata
 */
struct EnhancedRiskEvent {
    RiskEventType type;
    core::Timestamp timestamp{core::now()};
    std::string details;
    std::string severity{"info"};
    std::map<std::string, std::string> metadata;
    
    // Specific event data using std::variant for type safety
    std::variant<
        std::monostate,
        CircuitBreakerEvent,
        EnhancedStopLossEvent,
        DrawdownEvent,
        PositionReconciliationEvent,
        BalanceReconciliationEvent
    > specificEvent;
    
    // Convenience constructors
    EnhancedRiskEvent(RiskEventType eventType, std::string eventDetails, std::string eventSeverity = "info")
        : type(eventType), details(std::move(eventDetails)), severity(std::move(eventSeverity)) {}
    
    template<typename EventType>
    requires (!std::is_same_v<std::decay_t<EventType>, const char*> && 
              !std::is_array_v<std::decay_t<EventType>>)
    EnhancedRiskEvent(RiskEventType eventType, std::string eventDetails, EventType&& event, std::string eventSeverity = "info")
        : type(eventType), details(std::move(eventDetails)), severity(std::move(eventSeverity)), 
          specificEvent(std::forward<EventType>(event)) {}
    
    // Type-safe getters
    template<typename T>
    [[nodiscard]] std::optional<T> getSpecificEvent() const {
        if (auto* event = std::get_if<T>(&specificEvent)) {
            return *event;
        }
        return std::nullopt;
    }
    
    // Severity helpers
    [[nodiscard]] bool isCritical() const noexcept { return severity == "critical"; }
    [[nodiscard]] bool isWarning() const noexcept { return severity == "warning"; }
    [[nodiscard]] bool isInfo() const noexcept { return severity == "info"; }
};

/**
 * System health status with detailed component information
 */
struct SystemHealthStatus {
    std::atomic<RiskManagerMode> mode{RiskManagerMode::NORMAL};
    std::atomic<CircuitBreakerState> circuitBreakerState{CircuitBreakerState::OFF};
    std::atomic<bool> stopLossesActive{true};
    std::atomic<bool> reconciliationActive{true};
    std::atomic<bool> tradingAllowed{true};
    
    // Performance metrics
    std::atomic<uint64_t> totalRiskEvents{0};
    std::atomic<uint64_t> criticalEvents{0};
    std::atomic<uint64_t> lastEventTimestampNanos{0};
    
    // Component health
    std::atomic<bool> stopLossManagerHealthy{true};
    std::atomic<bool> drawdownMonitorHealthy{true};
    std::atomic<bool> circuitBreakerHealthy{true};
    std::atomic<bool> reconciliationHealthy{true};
    
    // Disable copy constructor and assignment
    SystemHealthStatus() = default;
    SystemHealthStatus(const SystemHealthStatus&) = delete;
    SystemHealthStatus& operator=(const SystemHealthStatus&) = delete;
    
    // Move operations
    SystemHealthStatus(SystemHealthStatus&& other) noexcept {
        mode.store(other.mode.load());
        circuitBreakerState.store(other.circuitBreakerState.load());
        stopLossesActive.store(other.stopLossesActive.load());
        reconciliationActive.store(other.reconciliationActive.load());
        tradingAllowed.store(other.tradingAllowed.load());
        totalRiskEvents.store(other.totalRiskEvents.load());
        criticalEvents.store(other.criticalEvents.load());
        lastEventTimestampNanos.store(other.lastEventTimestampNanos.load());
        stopLossManagerHealthy.store(other.stopLossManagerHealthy.load());
        drawdownMonitorHealthy.store(other.drawdownMonitorHealthy.load());
        circuitBreakerHealthy.store(other.circuitBreakerHealthy.load());
        reconciliationHealthy.store(other.reconciliationHealthy.load());
    }
    
    SystemHealthStatus& operator=(SystemHealthStatus&& other) noexcept {
        if (this != &other) {
            mode.store(other.mode.load());
            circuitBreakerState.store(other.circuitBreakerState.load());
            stopLossesActive.store(other.stopLossesActive.load());
            reconciliationActive.store(other.reconciliationActive.load());
            tradingAllowed.store(other.tradingAllowed.load());
            totalRiskEvents.store(other.totalRiskEvents.load());
            criticalEvents.store(other.criticalEvents.load());
            lastEventTimestampNanos.store(other.lastEventTimestampNanos.load());
            stopLossManagerHealthy.store(other.stopLossManagerHealthy.load());
            drawdownMonitorHealthy.store(other.drawdownMonitorHealthy.load());
            circuitBreakerHealthy.store(other.circuitBreakerHealthy.load());
            reconciliationHealthy.store(other.reconciliationHealthy.load());
        }
        return *this;
    }
    
    // Convenience methods
    void setLastEventTime(const core::Timestamp& timestamp) {
        lastEventTimestampNanos.store(timestamp.time_since_epoch().count());
    }
    
    core::Timestamp getLastEventTime() const {
        return core::Timestamp(std::chrono::nanoseconds(lastEventTimestampNanos.load()));
    }
    
    [[nodiscard]] bool isSystemHealthy() const noexcept {
        return stopLossManagerHealthy.load() &&
               drawdownMonitorHealthy.load() &&
               circuitBreakerHealthy.load() &&
               reconciliationHealthy.load() &&
               mode.load() != RiskManagerMode::EMERGENCY;
    }
    
    [[nodiscard]] double getHealthScore() const noexcept {
        int healthyComponents = 0;
        int totalComponents = 4;
        
        if (stopLossManagerHealthy.load()) ++healthyComponents;
        if (drawdownMonitorHealthy.load()) ++healthyComponents;
        if (circuitBreakerHealthy.load()) ++healthyComponents;
        if (reconciliationHealthy.load()) ++healthyComponents;
        
        double baseScore = static_cast<double>(healthyComponents) / totalComponents;
        
        // Reduce score based on mode
        switch (mode.load()) {
            case RiskManagerMode::EMERGENCY: return baseScore * 0.3;
            case RiskManagerMode::HEIGHTENED_RISK: return baseScore * 0.7;
            case RiskManagerMode::MAINTENANCE: return baseScore * 0.8;
            default: return baseScore;
        }
    }
};

/**
 * Configuration for post-trade monitoring behavior
 */
struct PostTradeMonitorConfig {
    // Update intervals
    std::chrono::seconds healthCheckIntervalSeconds{5};
    std::chrono::seconds metricsUpdateIntervalSeconds{10};
    std::chrono::minutes reconciliationIntervalMinutes{30};
    
    // Mode transition settings
    bool autoModeTransitions{true};
    std::chrono::minutes emergencyModeTimeoutMinutes{60};
    std::chrono::minutes heightenedRiskTimeoutMinutes{30};
    
    // Event handling
    size_t maxEventHistorySize{10000};
    bool enableEventPersistence{true};
    std::chrono::hours eventRetentionHours{72};
    
    // Performance settings
    bool enableBackgroundMonitoring{true};
    int maxConcurrentTasks{4};
    bool enableDetailedLogging{true};
    
    // Alert settings
    bool enableRealTimeAlerts{true};
    std::chrono::seconds alertCooldownSeconds{300};
    
    [[nodiscard]] bool isValid() const noexcept {
        return healthCheckIntervalSeconds.count() > 0 &&
               metricsUpdateIntervalSeconds.count() > 0 &&
               reconciliationIntervalMinutes.count() > 0 &&
               emergencyModeTimeoutMinutes.count() > 0 &&
               maxEventHistorySize > 0 &&
               maxConcurrentTasks > 0;
    }
};

/**
 * Event callback types for different scenarios
 */
// Note: Use different name to avoid conflict with existing RiskEventCallback in risk_events.hpp
using PostTradeRiskEventCallback = std::function<void(const EnhancedRiskEvent&)>;
using ModeChangeCallback = std::function<void(RiskManagerMode, RiskManagerMode, std::string_view)>;
using HealthStatusCallback = std::function<void(const SystemHealthStatus&)>;

/**
 * @class PostTradeMonitor
 * @brief Modern C++20 post-trade monitoring system
 * 
 * Orchestrates all post-trade risk management components:
 * - Stop loss management with automatic liquidation
 * - Drawdown monitoring with emergency actions
 * - Circuit breaker for system protection
 * - Position reconciliation for data integrity
 * - Comprehensive event handling and reporting
 */
class PostTradeMonitor {
public:
    /**
     * Constructor with dependency injection
     */
    explicit PostTradeMonitor(const RiskConfig& config);
    
    /**
     * Destructor ensures proper cleanup
     */
    ~PostTradeMonitor();
    
    // Disable copy operations, enable move
    PostTradeMonitor(const PostTradeMonitor&) = delete;
    PostTradeMonitor& operator=(const PostTradeMonitor&) = delete;
    PostTradeMonitor(PostTradeMonitor&&) = default;
    PostTradeMonitor& operator=(PostTradeMonitor&&) = default;
    
    /**
     * Initialize all monitoring components
     */
    [[nodiscard]] bool initialize();
    
    /**
     * Shutdown monitoring system gracefully
     */
    void shutdown();
    
    /**
     * Process execution report through all relevant monitors
     */
    void processExecutionReport(const execution::models::ExecutionReport& executionReport);
    
    /**
     * Update market data for all price-dependent monitors
     */
    void updateMarketData(const core::MarketData& marketData);
    
    /**
     * Update performance metrics for drawdown and other monitors
     */
    void updatePerformanceMetrics(const strategy::PerformanceMetrics& portfolioMetrics,
                                const std::map<std::string, strategy::PerformanceMetrics>& strategyMetrics = {});
    
    /**
     * Get current system health status
     */
    [[nodiscard]] SystemHealthStatus getSystemHealth();
    
    /**
     * Get current risk manager mode
     */
    [[nodiscard]] RiskManagerMode getMode() const noexcept {
        return systemHealth_.mode.load();
    }
    
    /**
     * Set risk manager mode with validation and notifications
     */
    void setMode(RiskManagerMode mode, std::string_view reason = "Manual override");
    
    /**
     * Get current circuit breaker state
     */
    [[nodiscard]] CircuitBreakerState getCircuitBreakerState() ;
    
    /**
     * Check if trading is currently allowed
     */
    [[nodiscard]] bool isTradingAllowed() noexcept {
        return systemHealth_.tradingAllowed.load() && 
               getCircuitBreakerState() != CircuitBreakerState::TRIGGERED;
    }
    
    /**
     * Force reconciliation of positions
     */
    [[nodiscard]] std::future<ReconciliationResult> reconcilePositions(
        std::optional<core::ExchangeId> exchangeId = std::nullopt
    );
    
    /**
     * Update configuration for all components
     */
    [[nodiscard]] bool updateConfiguration(const RiskConfig& config);
    
    /**
     * Get component references for direct access
     */
    [[nodiscard]] StopLossManager& getStopLossManager() { return *stopLossManager_; }
    [[nodiscard]] DrawdownMonitor& getDrawdownMonitor() { return *drawdownMonitor_; }
    [[nodiscard]] CircuitBreaker& getCircuitBreaker() { return *circuitBreaker_; }
    [[nodiscard]] BalanceReconciliation& getBalanceReconciliation() { return *balanceReconciliation_; }
    
    /**
     * Register event callbacks with type safety
     */
    void registerRiskEventCallback(const PostTradeRiskEventCallback& callback);
    void registerModeChangeCallback(const ModeChangeCallback& callback);
    void registerHealthStatusCallback(const HealthStatusCallback& callback);
    
    /**
     * Get comprehensive monitoring statistics
     */
    [[nodiscard]] std::map<std::string, double> getMonitoringStatistics() ;
    
    /**
     * Get recent risk events with filtering
     */
    [[nodiscard]] std::vector<EnhancedRiskEvent> getRecentEvents(
        std::optional<RiskEventType> typeFilter = std::nullopt,
        std::optional<std::string> severityFilter = std::nullopt,
        std::chrono::minutes lookbackMinutes = std::chrono::minutes(60)
    ) const;
    
    /**
     * Emergency stop all trading activities
     */
    [[nodiscard]] bool emergencyStop(std::string_view reason);
    
    /**
     * Resume trading after emergency stop
     */
    [[nodiscard]] bool resumeTrading(std::string_view reason = "Manual resume");
    
    /**
     * Health check and diagnostics
     */
    [[nodiscard]] bool performHealthCheck();
    [[nodiscard]] std::map<std::string, std::string> getDiagnostics() ;

private:
    // Configuration
    mutable std::shared_mutex configMutex_;
    RiskConfig config_;
    PostTradeMonitorConfig monitorConfig_;
    
    // System health tracking
    mutable SystemHealthStatus systemHealth_;
    
    // Component management
    std::unique_ptr<StopLossManager> stopLossManager_;
    std::unique_ptr<DrawdownMonitor> drawdownMonitor_;
    std::unique_ptr<CircuitBreaker> circuitBreaker_;
    std::unique_ptr<BalanceReconciliation> balanceReconciliation_;
    
    // Shared dependencies
    std::shared_ptr<PositionTracker> positionTracker_;
    std::shared_ptr<DataInterface> dataInterface_;
    std::shared_ptr<ExecutionInterface> executionInterface_;
    
    // Event management
    mutable std::shared_mutex eventMutex_;
    std::deque<EnhancedRiskEvent> eventHistory_;
    
    // Callbacks
    mutable std::shared_mutex callbackMutex_;
    std::vector<PostTradeRiskEventCallback> riskEventCallbacks_;
    std::vector<ModeChangeCallback> modeChangeCallbacks_;
    std::vector<HealthStatusCallback> healthStatusCallbacks_;
    
    // Background processing
    std::atomic<bool> shutdownRequested_{false};
    std::unique_ptr<std::jthread> backgroundThread_;
    std::unique_ptr<std::jthread> healthCheckThread_;
    
    // Performance tracking
    std::atomic<uint64_t> totalExecutionReports_{0};
    std::atomic<uint64_t> totalMarketDataUpdates_{0};
    std::atomic<uint64_t> lastHealthCheckNanos_{0};
    
    // Mode transition tracking
    mutable std::shared_mutex modeTransitionMutex_;
    std::unordered_map<RiskManagerMode, core::Timestamp> modeTransitionTimes_;
    
    // Private methods for component management
    [[nodiscard]] bool initializeComponents();
    void shutdownComponents();
    void connectComponentCallbacks();
    
    // Event handling
    void handleStopLossEvent(const EnhancedStopLossEvent& event);
    void handleCircuitBreakerEvent(CircuitBreakerState oldState, CircuitBreakerState newState, std::string_view reason);
    void handleDrawdownEvent(const DrawdownEvent& event);
    void handleReconciliationEvent(const PositionReconciliationEvent& event);
    
    void notifyRiskEvent(const EnhancedRiskEvent& event);
    void notifyModeChange(RiskManagerMode oldMode, RiskManagerMode newMode, std::string_view reason);
    void notifyHealthStatus(const SystemHealthStatus& status);
    
    // Background processing
    void backgroundMonitoringLoop();
    void healthCheckLoop();
    void performScheduledTasks();
    void performMaintenanceTasks();
    
    // Mode management
    [[nodiscard]] bool shouldTransitionMode(RiskManagerMode currentMode) const;
    [[nodiscard]] RiskManagerMode calculateRequiredMode();
    void recordModeTransition(RiskManagerMode mode);
    
    // Utility methods
    void addToEventHistory(EnhancedRiskEvent event);
    void pruneEventHistory();
    
    [[nodiscard]] bool validateConfiguration(const RiskConfig& config) const;
    void updateComponentHealth() ;
    
    // String conversion helpers
    [[nodiscard]] std::string modeToString(RiskManagerMode mode) const;
    [[nodiscard]] std::string circuitBreakerStateToString(CircuitBreakerState state) const;
    
    // Template helper for async operations
    template<typename Func>
    requires std::invocable<Func>
    auto executeAsync(Func&& func) -> std::future<decltype(func())> {
        return std::async(std::launch::async, std::forward<Func>(func));
    }
    
    void checkModeTimeouts();
    
    template<typename Predicate>
    [[nodiscard]] auto filterEvents(Predicate pred, std::chrono::minutes lookback) const {
        std::shared_lock lock(eventMutex_);
        auto cutoffTime = core::now() - lookback;
        
        return eventHistory_ | std::views::filter([cutoffTime, pred](const auto& event) {
            return event.timestamp >= cutoffTime && pred(event);
        });
    }
    
    // Coroutine support for async operations
    struct OperationAwaiter {
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) const noexcept {
            std::thread([handle]() { handle.resume(); }).detach();
        }
        void await_resume() const noexcept {}
    };
    
    // Helper for safe component operations (non-const version)
    template<typename Component, typename Func>
    requires std::invocable<Func, Component&>
    auto safeComponentOperation(std::unique_ptr<Component>& component, Func&& func) 
        -> std::optional<std::invoke_result_t<Func, Component&>> {
        if (!component) return std::nullopt;
        
        try {
            return std::forward<Func>(func)(*component);
        } catch (...) {
            // Log error and mark component as unhealthy
            return std::nullopt;
        }
    }

    // // Helper for safe component operations (const version)
    // template<typename Component, typename Func>
    // requires std::invocable<Func, Component&>
    // auto safeComponentOperation( std::unique_ptr<Component>& component, Func&& func) 
    //     -> std::optional<std::invoke_result_t<Func, Component&>> {
    //     if (!component) return std::nullopt;
        
    //     try {
    //         return std::forward<Func>(func)(*component);
    //     } catch (...) {
    //         // Log error and mark component as unhealthy
    //         return std::nullopt;
    //     }
    // }
    // Helper for safe component operations (const version)
    // template<typename Component, typename Func>
    // requires std::invocable<Func, const Component&>
    // auto safeComponentOperation(const std::unique_ptr<Component>& component, Func&& func) 
    //     -> std::optional<std::invoke_result_t<Func, const Component&>> {
    //     if (!component) return std::nullopt;
        
    //     try {
    //         return std::forward<Func>(func)(*component);
    //     } catch (...) {
    //         // Log error and mark component as unhealthy
    //         return std::nullopt;
    //     }
    // }
};

} // namespace trading::risk