/**
 * @file risk/posttrade/drawdown_monitor.hpp
 * @brief Enhanced drawdown monitoring with modern C++20 features
 * 
 * Tracks performance against risk thresholds at multiple levels with
 * comprehensive monitoring, event handling, and thread-safe operations.
 * 
 * @author Trading System Team
 * @date May 26, 2025
 */

#pragma once

#include "../risk_types.hpp"
#include "../risk_events.hpp" 
#include "../risk_config.hpp"
#include "strategy/types/performance_metrics.hpp"
#include "core/time.hpp"

#include <atomic>
#include <chrono>
#include <concepts>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <condition_variable>

namespace trading::risk {

/**
 * High water mark information with thread-safe access
 */
struct HighWaterMark {
    std::atomic<double> value{0.0};
    std::atomic<uint64_t> timestampNanos{0};
    
    // Convenience methods for timestamp handling
    void setTimestamp(const core::Timestamp& timestamp) {
        timestampNanos.store(timestamp.time_since_epoch().count());
    }
    
    core::Timestamp getTimestamp() const {
        return core::Timestamp(std::chrono::nanoseconds(timestampNanos.load()));
    }
    
    // Atomic update both value and timestamp
    void update(double newValue, const core::Timestamp& timestamp) {
        value.store(newValue);
        setTimestamp(timestamp);
    }
};

/**
 * Comprehensive drawdown information with performance metrics
 */
struct DrawdownInfo {
    std::atomic<double> currentValue{0.0};
    HighWaterMark highWaterMark;
    std::atomic<double> drawdownPercent{0.0};
    std::atomic<double> maxDrawdownPercent{0.0};
    std::atomic<bool> warningThresholdBreached{false};
    std::atomic<bool> criticalThresholdBreached{false};
    std::atomic<uint64_t> lastUpdatedNanos{0};
    std::atomic<uint32_t> breachCount{0};
    
    // Performance tracking
    std::atomic<double> averageDrawdown{0.0};
    std::atomic<uint32_t> updateCount{0};
    
    // Convenience methods
    void setLastUpdated(const core::Timestamp& timestamp) {
        lastUpdatedNanos.store(timestamp.time_since_epoch().count());
    }
    
    core::Timestamp getLastUpdated() const {
        return core::Timestamp(std::chrono::nanoseconds(lastUpdatedNanos.load()));
    }
    
    // Calculate running average of drawdowns
    void updateAverageDrawdown(double newDrawdown) {
        uint32_t count = updateCount.fetch_add(1) + 1;
        double currentAvg = averageDrawdown.load();
        averageDrawdown.store(currentAvg + (newDrawdown - currentAvg) / count);
    }
};

/**
 * Enhanced drawdown configuration with additional monitoring parameters
 */
struct EnhancedDrawdownConfig {
    // Threshold configurations
    double portfolioWarningThreshold{0.05};     // 5% portfolio warning
    double portfolioCriticalThreshold{0.10};    // 10% portfolio critical
    double strategyWarningThreshold{0.08};      // 8% strategy warning
    double strategyCriticalThreshold{0.15};     // 15% strategy critical
    
    // Time-based configurations
    std::chrono::minutes lookbackPeriodMinutes{1440}; // 24 hours default
    std::chrono::minutes historyRetentionMinutes{10080}; // 7 days
    std::chrono::seconds updateIntervalSeconds{5};     // Update frequency
    
    // Risk management behaviors
    bool enableAutoRiskReduction{true};
    double riskReductionFactor{0.5};
    bool enableEmergencyStop{true};
    double emergencyStopThreshold{0.25};     // 25% emergency stop
    
    // Performance and monitoring
    bool enableDetailedMetrics{true};
    bool enableHistoricalTracking{true};
    size_t maxHistorySize{10000};
    bool enablePerformanceLogging{true};
    
    // Notification settings
    bool enableRealTimeAlerts{true};
    std::chrono::seconds alertCooldownSeconds{300}; // 5 minutes between alerts
    
    // Validation
    [[nodiscard]] bool isValid() const noexcept {
        return portfolioWarningThreshold > 0.0 &&
               portfolioCriticalThreshold > portfolioWarningThreshold &&
               strategyWarningThreshold > 0.0 &&
               strategyCriticalThreshold > strategyWarningThreshold &&
               lookbackPeriodMinutes.count() > 0 &&
               updateIntervalSeconds.count() > 0 &&
               riskReductionFactor > 0.0 && riskReductionFactor <= 1.0 &&
               maxHistorySize > 0;
    }
};

/**
 * Historical value entry with metadata
 */
struct HistoricalValue {
    core::Timestamp timestamp;
    double value;
    double drawdownAtTime;
    std::string metadata;
    
    HistoricalValue(const core::Timestamp& ts, double val, double dd = 0.0, std::string meta = "")
        : timestamp(ts), value(val), drawdownAtTime(dd), metadata(std::move(meta)) {}
};

/**
 * Strategy performance tracker with comprehensive metrics
 */
struct StrategyPerformanceTracker {
    std::string strategyId;
    DrawdownInfo drawdownInfo;
    std::deque<HistoricalValue> valueHistory;
    strategy::PerformanceMetrics latestMetrics;
    std::atomic<bool> isActive{true};
    std::atomic<uint64_t> lastActivityNanos{0};
    
    explicit StrategyPerformanceTracker(std::string id) 
        : strategyId(std::move(id)) {
        setLastActivity(core::now());
    }
    
    void setLastActivity(const core::Timestamp& timestamp) {
        lastActivityNanos.store(timestamp.time_since_epoch().count());
    }
    
    core::Timestamp getLastActivity() const {
        return core::Timestamp(std::chrono::nanoseconds(lastActivityNanos.load()));
    }
};

/**
 * Event callback types for different scenarios
 */
using DrawdownEventCallback = std::function<void(const DrawdownEvent&)>;
using EmergencyCallback = std::function<void(const std::string& strategyId, double drawdown)>;
using MetricsUpdateCallback = std::function<void(const std::string& strategyId, const DrawdownInfo&)>;

/**
 * Modern C++20 drawdown monitor with enhanced features
 */
class DrawdownMonitor {
public:
    /**
     * Constructor with enhanced configuration
     */
    explicit DrawdownMonitor(const RiskConfig& config);
    
    /**
     * Destructor ensures proper cleanup
     */
    ~DrawdownMonitor();
    
    // Disable copy operations, enable move
    DrawdownMonitor(const DrawdownMonitor&) = delete;
    DrawdownMonitor& operator=(const DrawdownMonitor&) = delete;
    DrawdownMonitor(DrawdownMonitor&&) = default;
    DrawdownMonitor& operator=(DrawdownMonitor&&) = default;
    
    /**
     * Initialize the monitor with validation
     */
    [[nodiscard]] bool initialize();
    
    /**
     * Shutdown the monitor gracefully
     */
    void shutdown();
    
    /**
     * Update performance metrics with comprehensive validation
     */
    [[nodiscard]] std::vector<DrawdownEvent> updatePerformanceMetrics(
        const strategy::PerformanceMetrics& portfolioMetrics,
        const std::map<std::string, strategy::PerformanceMetrics>& strategyMetrics);
    
    /**
     * Update individual strategy value with validation
     */
    [[nodiscard]] std::optional<DrawdownEvent> updateStrategyValue(
        const std::string& strategyId, 
        double value,
        const std::optional<strategy::PerformanceMetrics>& metrics = std::nullopt);
    
    /**
     * Update portfolio value with comprehensive tracking
     */
    [[nodiscard]] std::optional<DrawdownEvent> updatePortfolioValue(
        double value,
        const std::optional<strategy::PerformanceMetrics>& metrics = std::nullopt);
    
    /**
     * Get current drawdown information for strategy
     */
    // [[nodiscard]] std::optional<DrawdownInfo> getStrategyDrawdown(const std::string& strategyId) const;
    [[nodiscard]] const DrawdownInfo* getStrategyDrawdown(const std::string& strategyId) const;
    
    /**
     * Get portfolio drawdown information
     */
    // [[nodiscard]] DrawdownInfo getPortfolioDrawdown() const;
    [[nodiscard]] const DrawdownInfo& getPortfolioDrawdown() const;
    
    /**
     * Get comprehensive strategy performance data
     */
    [[nodiscard]] std::optional<StrategyPerformanceTracker> getStrategyPerformance(
        const std::string& strategyId) const;
    
    /**
     * Update configuration with validation
     */
    [[nodiscard]] bool updateConfiguration(const EnhancedDrawdownConfig& config);
    
    /**
     * Get current configuration
     */
    [[nodiscard]] EnhancedDrawdownConfig getConfiguration() const;
    
    /**
     * Register event callbacks with type safety
     */
    void registerDrawdownCallback(const DrawdownEventCallback& callback);
    void registerEmergencyCallback(const EmergencyCallback& callback);
    void registerMetricsCallback(const MetricsUpdateCallback& callback);
    
    /**
     * Reset high water marks with validation
     */
    [[nodiscard]] bool resetStrategyHighWaterMark(const std::string& strategyId, 
                                                  std::optional<double> value = std::nullopt);
    [[nodiscard]] bool resetPortfolioHighWaterMark(std::optional<double> value = std::nullopt);
    
    /**
     * Get comprehensive statistics
     */
    [[nodiscard]] std::map<std::string, double> getPerformanceStatistics() const;
    
    /**
     * Get historical data for analysis
     */
    [[nodiscard]] std::vector<HistoricalValue> getStrategyHistory(
        const std::string& strategyId,
        const core::Timestamp& startTime = {},
        const core::Timestamp& endTime = {}) const;
    
    [[nodiscard]] std::vector<HistoricalValue> getPortfolioHistory(
        const core::Timestamp& startTime = {},
        const core::Timestamp& endTime = {}) const;
    
    /**
     * Health check and diagnostics
     */
    [[nodiscard]] bool isHealthy() const noexcept;
    [[nodiscard]] std::map<std::string, std::string> getDiagnostics() const;
    
    /**
     * Active strategy management
     */
    void activateStrategy(const std::string& strategyId);
    void deactivateStrategy(const std::string& strategyId);
    [[nodiscard]] std::vector<std::string> getActiveStrategies() const;
    [[nodiscard]] strategy::PerformanceMetrics getPerformanceMetrics() const;
private:
    double maxDrawdown_ = 0.0;
    double currentDrawdown_ = 0.0;
    double totalPnl_ = 0.0;
    // Configuration and state
    mutable std::shared_mutex configMutex_;
    EnhancedDrawdownConfig config_;
    std::atomic<bool> isInitialized_{false};
    std::atomic<bool> isShuttingDown_{false};
    
    // Portfolio tracking
    mutable std::shared_mutex portfolioMutex_;
    DrawdownInfo portfolioDrawdown_;
    std::deque<HistoricalValue> portfolioHistory_;
    
    // Strategy tracking with efficient access
    mutable std::shared_mutex strategyMutex_;
    std::unordered_map<std::string, std::unique_ptr<StrategyPerformanceTracker>> strategyTrackers_;
    
    // Event callbacks with thread safety
    mutable std::mutex callbackMutex_;
    std::vector<DrawdownEventCallback> drawdownCallbacks_;
    std::vector<EmergencyCallback> emergencyCallbacks_;
    std::vector<MetricsUpdateCallback> metricsCallbacks_;
    
    // Background processing
    std::unique_ptr<std::thread> maintenanceThread_;
    std::atomic<bool> maintenanceActive_{false};
    // Condition variable and mutex to allow interruptible sleep/wait for shutdown
    mutable std::mutex maintenanceMutex_;
    mutable std::condition_variable maintenanceCv_;
    
    // Performance tracking
    mutable std::atomic<uint64_t> totalUpdates_{0};
    mutable std::atomic<uint64_t> totalEvents_{0};
    std::atomic<uint64_t> lastMaintenanceNanos_{0};
    
    // Alert rate limiting
    mutable std::mutex alertMutex_;
    std::unordered_map<std::string, core::Timestamp> lastAlertTimes_;
    
    // Private methods
    [[nodiscard]] double calculateDrawdownPercent(double currentValue, double highWaterMark) const noexcept;
    
    [[nodiscard]] DrawdownEvent createDrawdownEvent(
        const std::string& strategyId,
        double drawdownPercent,
        double highWaterMark,
        double currentValue,
        const core::Timestamp& hwmTime,
        bool isCritical) const;
        // Template for thread-safe operations
       

    // void updateHighWaterMark(DrawdownInfo& drawdownInfo, double currentValue, 
    //                        const core::Timestamp& timestamp);
    
    void notifyCallbacks(const DrawdownEvent& event);
    void notifyEmergencyCallbacks(const std::string& strategyId, double drawdown);
    void notifyMetricsCallbacks(const std::string& strategyId, const DrawdownInfo& info);
    
    void performMaintenance();
    void pruneHistory();
    void validateStrategies();
    void updatePerformanceMetrics();
    double calculateSharpeRatio() const;

    [[nodiscard]] bool canSendAlert(const std::string& strategyId) const;
    void recordAlert(const std::string& strategyId);
    
    [[nodiscard]] bool validateStrategyId(const std::string& strategyId) const noexcept;
    [[nodiscard]] bool validateValue(double value) const noexcept;
    
    // Template for thread-safe operations
    template<typename Func>
    auto safeExecute(Func&& func) const noexcept -> decltype(func()) {
        try {
            return std::forward<Func>(func)();
        } catch (...) {
            // Log error in production
            return {};
        }
    }
    
    // Range-based filtering for history
    template<typename Container>
    [[nodiscard]] auto filterByTimeRange(const Container& container,
                                        const core::Timestamp& startTime,
                                        const core::Timestamp& endTime) const {
        return container | std::views::filter([startTime, endTime](const auto& item) {
            return item.timestamp >= startTime && item.timestamp <= endTime;
        });
    }
};

} // namespace trading::risk