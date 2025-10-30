/**
 * @file post_trade_monitor.hpp
 * @brief Modern C++17 Post Trade Monitor with comprehensive risk management
 * 
 * Orchestrates all post-trade monitoring components with thread-safe operations,
 * event-driven architecture, and advanced risk management capabilities.
 * 
 * @author Trading System Team
 * @date May 26, 2025
 */

#pragma once

#include "risk/risk_types.hpp"
#include "risk/risk_events.hpp"
#include "risk/risk_config.hpp"
#include "risk/portfolio/position_tracker.hpp"
#include "risk/interfaces/data_interface.hpp"
#include "risk/interfaces/execution_interface.hpp"
#include "risk/posttrade/stop_loss_manager.hpp"
#include "risk/posttrade/drawdown_monitor.hpp"
#include "risk/posttrade/circuit_breaker.hpp"
#include "risk/posttrade/balance_reconciliation.hpp"
#include "execution/models/execution_report.hpp"
#include "strategy/types/performance_metrics.hpp"
#include "core/market_types.hpp"
#include "core/time.hpp"
#include "risk/portfolio/portfolio_manager.hpp"
#include "risk/portfolio/position_tracker.hpp"
// #include "risk"
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <deque>
#include <variant>
#include <map>
#include <sstream>
#include <algorithm>
#include "risk/risk_events.hpp"

namespace trading::risk {

// Forward declarations for optional subsystems to avoid heavy includes in header
namespace storage { class EventStore; }
namespace alerting { class WebhookClient; }


/**
 * Enhanced risk event with additional context and metadata
 */
 
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
    
    bool isSystemHealthy() const noexcept {
        return stopLossManagerHealthy.load() &&
               drawdownMonitorHealthy.load() &&
               circuitBreakerHealthy.load() &&
               reconciliationHealthy.load() &&
               mode.load() != RiskManagerMode::EMERGENCY;
    }
    
    double getHealthScore() const noexcept {
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

    // Position TTL (time-to-live) settings
    std::chrono::milliseconds defaultPositionTTL{std::chrono::milliseconds(1500)}; // 1.5s default
    std::chrono::milliseconds positionTtlCheckIntervalMs{std::chrono::milliseconds(200)}; // check interval
    bool enableDynamicTtl{true};
    std::string volatilityTimeframe{"1m"};
    
    // Conservative TTL-extension (slow-gate) options
    bool enableTtlExtension{false}; // opt-in; default off (log-only rollout)
    double ttlExtensionMinStayProb{0.70}; // minimum stay probability to allow extension
    double ttlExtensionMinPTrend{0.60}; // minimum slow-drift trend probability
    size_t ttlExtensionMaxPerPosition{2}; // max number of extensions per position
    int ttlExtensionDefaultCapMin{5}; // default suggested cap in minutes if none provided by recommender
    
    // Alert settings
    bool enableRealTimeAlerts{true};
    std::chrono::seconds alertCooldownSeconds{300};
    
    bool isValid() const noexcept {
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
using PostTradeRiskEventCallback = std::function<void(const RiskEvent&)>;
using ModeChangeCallback = std::function<void(RiskManagerMode, RiskManagerMode, std::string_view)>;
using HealthStatusCallback = std::function<void(const SystemHealthStatus&)>;

/**
 * @class PostTradeMonitor
 * @brief Modern C++17 post-trade monitoring system
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
    // Update the constructor declaration
    PostTradeMonitor(const RiskConfig& config, PortfolioManager* portfolioManager = nullptr);
    // New ctor that allows dependency injection of shared interfaces to avoid
    // creating duplicate DataInterface/MarketDataInterface instances.
    PostTradeMonitor(const RiskConfig& config,
                     PortfolioManager* portfolioManager,
                     std::shared_ptr<DataInterface> dataInterface,
                     std::shared_ptr<ExecutionInterface> executionInterface = nullptr);
    /**
     * Destructor ensures proper cleanup
     */
    ~PostTradeMonitor();
    
    // Health check
    bool isHealthy() const;
    
    // Disable copy operations, enable move
    PostTradeMonitor(const PostTradeMonitor&) = delete;
    PostTradeMonitor& operator=(const PostTradeMonitor&) = delete;
    PostTradeMonitor(PostTradeMonitor&&) = default;
    PostTradeMonitor& operator=(PostTradeMonitor&&) = default;
    
    /**
     * Initialize all monitoring components
     */
    bool initialize();
    
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

    // External signal checker type (public-facing)
    using ExternalSignalChecker = std::function<bool(const core::ExchangeId&, const core::Symbol&)>;

    /**
     * Register an external signal checker callback. This is the public API
     * for other subsystems to provide a function that returns true when a
     * BUY signal is active for a given exchange+symbol. Internally this
     * forwards to the private registerSignalChecker implementation.
     */
    void registerExternalSignalChecker(const ExternalSignalChecker& checker);
    
    /**
     * Update performance metrics for drawdown and other monitors
     */
    void updatePerformanceMetrics(const strategy::PerformanceMetrics& portfolioMetrics,
                                const std::map<std::string, strategy::PerformanceMetrics>& strategyMetrics = {});
    
    /**
     * Get current system health status
     */
    SystemHealthStatus getSystemHealth();
    
    /**
     * Get current risk manager mode
     */
    RiskManagerMode getMode() const noexcept {
        return systemHealth_.mode.load();
    }
    
    /**
     * Get performance metrics
     */
    virtual strategy::PerformanceMetrics getPerformanceMetrics() const;
    
    /**
     * Set risk manager mode with validation and notifications
     */
    void setMode(RiskManagerMode mode, std::string_view reason = "Manual override");
    
    /**
     * Get current circuit breaker state
     */
    CircuitBreakerState getCircuitBreakerState();
    
    /**
     * Check if trading is currently allowed
     */
    bool isTradingAllowed() noexcept {
        return systemHealth_.tradingAllowed.load() && 
               getCircuitBreakerState() != CircuitBreakerState::TRIGGERED;
    }
    
    /**
     * Force reconciliation of positions
     */
    std::future<ReconciliationResult> reconcilePositions(
        std::optional<core::ExchangeId> exchangeId = std::nullopt
    );
    
    /**
     * Update configuration for all components
     */
    bool updateConfiguration(const RiskConfig& config);
    
    /**
     * Get component references for direct access
     */
    StopLossManager& getStopLossManager() { return *stopLossManager_; }
    DrawdownMonitor& getDrawdownMonitor() { return *drawdownMonitor_; }
    CircuitBreaker& getCircuitBreaker() { return *circuitBreaker_; }
    BalanceReconciliation& getBalanceReconciliation() { return *balanceReconciliation_; }
    
    /**
     * Register event callbacks with type safety
     */
    void registerRiskEventCallback(const PostTradeRiskEventCallback& callback);
    void registerModeChangeCallback(const ModeChangeCallback& callback);
    void registerHealthStatusCallback(const HealthStatusCallback& callback);
    
    /**
     * Get comprehensive monitoring statistics
     */
    std::map<std::string, double> getMonitoringStatistics();
    
    /**
     * Get recent risk events with filtering
     */
    std::vector<RiskEvent> getRecentEvents(
        std::optional<RiskEventType> typeFilter = std::nullopt,
        std::optional<std::string> severityFilter = std::nullopt,
        std::chrono::minutes lookbackMinutes = std::chrono::minutes(60)
    ) const;
    
    /**
     * Emergency stop all trading activities
     */
    bool emergencyStop(std::string_view reason);
    
    /**
     * Resume trading after emergency stop
     */
    bool resumeTrading(std::string_view reason = "Manual resume");
    
    /**
     * Health check and diagnostics
     */
    bool performHealthCheck();
    std::map<std::string, std::string> getDiagnostics();

private:
    // Optional portfolio manager for integrated risk management
    PortfolioManager* portfolioManager_{nullptr};
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

    // Persistence and alerting
    std::unique_ptr<trading::risk::storage::EventStore> eventStore_;
    std::unique_ptr<trading::risk::alerting::WebhookClient> webhookClient_;
    
    // Event management
    mutable std::shared_mutex eventMutex_;
    std::deque<RiskEvent> eventHistory_;
    
    // Callbacks
    mutable std::shared_mutex callbackMutex_;
    std::vector<PostTradeRiskEventCallback> riskEventCallbacks_;
    std::vector<ModeChangeCallback> modeChangeCallbacks_;
    std::vector<HealthStatusCallback> healthStatusCallbacks_;
    
    // Background processing
    std::atomic<bool> shutdownRequested_{false};
    std::unique_ptr<std::thread> backgroundThread_;
    std::unique_ptr<std::thread> healthCheckThread_;
    
    // Performance tracking
    std::atomic<uint64_t> totalExecutionReports_{0};
    std::atomic<uint64_t> totalMarketDataUpdates_{0};
    std::atomic<uint64_t> lastHealthCheckNanos_{0};
    // TTL metrics
    std::atomic<uint64_t> ttlExits_{0};

    // Positions currently being closed by TTL enforcement (keys)
    mutable std::shared_mutex positionsClosingMutex_;
    std::unordered_set<std::string> positionsBeingClosed_;

    // Tracked positions that were created after fills; used to store entry
    // timestamp, computed deadline and last signal seen time. Keyed by
    // "exchange:symbol" string.
    struct TrackedPosition {
        core::Timestamp entryTime;
        core::Timestamp deadline;
        core::Timestamp lastSignalSeen;
        double ttlMs{0.0};
        size_t exitAttempts{0};
    };

    mutable std::shared_mutex trackedPositionsMutex_;
    std::unordered_map<std::string, TrackedPosition> trackedPositions_;

    // Signal checker: client can register a callback that returns true if there
    // is an active BUY signal for a given exchange+symbol. If not registered,
    // the monitor will fall back to age-based TTL behavior.
    using SignalChecker = std::function<bool(const core::ExchangeId&, const core::Symbol&)>;
    void registerSignalChecker(const SignalChecker& checker);
    mutable std::shared_mutex signalCheckerMutex_;
    SignalChecker signalChecker_;

    // External recommendation callback type (optional). The callback should
    // return a JSON-like map (string->string) or a structured object in the
    // future; for now we use a simple struct to carry conservative fields.
    struct ExternalRecommendation {
        std::string recommended_action; // "extend" | "recommend_exit" | "none"
        double short_alpha{0.0};
        double p_trend_up{0.0};
        double stay_prob{0.0};
        double mixture_half_life_min{0.0};
        bool micro_ok{false};
        std::string reason;
        std::optional<int> suggested_time_cap_min;
    };

    using ExternalRecommendationCallback = std::function<std::optional<ExternalRecommendation>(const core::ExchangeId&, const core::Symbol&)>;
    void registerExternalRecommendationCallback(const ExternalRecommendationCallback& cb);
    mutable std::shared_mutex externalRecommendationMutex_;
    ExternalRecommendationCallback externalRecommendationCb_;

    // Enforce position TTL
    void enforcePositionTTL();

    // Calculate dynamic TTL based on volatility
    static int64_t calculateDynamicTTL(double currentVolatility);
    
    // Mode transition tracking
    mutable std::shared_mutex modeTransitionMutex_;
    std::unordered_map<RiskManagerMode, core::Timestamp> modeTransitionTimes_;
    
    // Private methods for component management
    bool initializeComponents();
    void shutdownComponents();
    void connectComponentCallbacks();
    
    // Event handling
    void handleStopLossEvent(const EnhancedStopLossEvent& event);
    void handleCircuitBreakerEvent(CircuitBreakerState oldState, CircuitBreakerState newState, std::string_view reason);
    void handleDrawdownEvent(const DrawdownEvent& event);
    void handleReconciliationEvent(const PositionReconciliationEvent& event);
    
    void notifyRiskEvent(const RiskEvent& event);
    void notifyModeChange(RiskManagerMode oldMode, RiskManagerMode newMode, std::string_view reason);
    void notifyHealthStatus(const SystemHealthStatus& status);
    
    // Background processing
    void backgroundMonitoringLoop();
    void healthCheckLoop();
    void performScheduledTasks();
    void performMaintenanceTasks();
    
    // Mode management
    bool shouldTransitionMode(RiskManagerMode currentMode) const;
    RiskManagerMode calculateRequiredMode();
    void recordModeTransition(RiskManagerMode mode);
    
    // Utility methods
    void addToEventHistory(RiskEvent event);
    void pruneEventHistory();
    
    bool validateConfiguration(const RiskConfig& config) const;
    void updateComponentHealth();
    
    // String conversion helpers
    std::string modeToString(RiskManagerMode mode) const;
    std::string circuitBreakerStateToString(CircuitBreakerState state) const;
    
    void checkModeTimeouts();
    
    // Filter events helper
    std::vector<RiskEvent> filterEventsByPredicate(
        std::function<bool(const RiskEvent&)> predicate, 
        std::chrono::minutes lookback) const;
    
    // Helper for safe component operations (non-const version)
    template<typename Component, typename Func>
    auto safeComponentOperation(std::unique_ptr<Component>& component, Func&& func) 
        -> std::optional<decltype(func(*component))> {
        if (!component) return std::nullopt;
        
        try {
            return std::forward<Func>(func)(*component);
        } catch (...) {
            // Log error and mark component as unhealthy
            return std::nullopt;
        }
    }

    // Helper for safe component operations (const version)
    template<typename Component, typename Func>
    auto safeComponentOperation(const std::unique_ptr<Component>& component, Func&& func) const
        -> std::optional<decltype(func(*component))> {
        if (!component) return std::nullopt;
        
        try {
            return std::forward<Func>(func)(*component);
        } catch (...) {
            // Log error and mark component as unhealthy
            return std::nullopt;
        }
    }

    // Template helper for async operations
    template<typename Func>
    auto executeAsync(Func&& func) -> std::future<decltype(func())> {
        return std::async(std::launch::async, std::forward<Func>(func));
    }
};

} // namespace trading::risk
