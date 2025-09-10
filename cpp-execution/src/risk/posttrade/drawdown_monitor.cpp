/**
 * @file risk/posttrade/drawdown_monitor.cpp
 * @brief Implementation of enhanced drawdown monitoring
 * 
 * @author Trading System Team
 * @date May 26, 2025
 */

#include "risk/posttrade/drawdown_monitor.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <ranges>
#include <optional> 
namespace trading::risk {

DrawdownMonitor::DrawdownMonitor(const RiskConfig& config) {
    // Extract drawdown config and create enhanced version
    const auto& baseConfig = config.getDrawdownConfig();
    
    config_.portfolioWarningThreshold = baseConfig.warningThreshold;
    config_.portfolioCriticalThreshold = baseConfig.criticalThreshold;
    config_.strategyWarningThreshold = baseConfig.warningThreshold * 1.2; // Slightly higher for strategies
    config_.strategyCriticalThreshold = baseConfig.criticalThreshold * 1.2;
    config_.lookbackPeriodMinutes = std::chrono::duration_cast<std::chrono::minutes>(baseConfig.lookbackPeriod);
    config_.enableAutoRiskReduction = baseConfig.enableAutoRiskReduction;
    config_.riskReductionFactor = baseConfig.riskReductionFactor;
    
    lastMaintenanceNanos_.store(core::now().time_since_epoch().count());
}

DrawdownMonitor::~DrawdownMonitor() {
    shutdown();
}

bool DrawdownMonitor::initialize() {
    if (isInitialized_.load()) {
        return true;
    }
    
    // Validate configuration
    if (!config_.isValid()) {
        LOG_ERROR("DrawdownMonitor: Invalid configuration provided");
        return false;
    }
    
    try {
        // Initialize portfolio drawdown
        portfolioDrawdown_.currentValue.store(0.0);
        portfolioDrawdown_.highWaterMark.update(0.0, core::now());
        portfolioDrawdown_.drawdownPercent.store(0.0);
        portfolioDrawdown_.setLastUpdated(core::now());
        
        // Start maintenance thread if background processing is enabled
        if (config_.enableDetailedMetrics) {
            maintenanceActive_.store(true);
            maintenanceThread_ = std::make_unique<std::thread>([this]() {
                while (maintenanceActive_.load()) {
                    performMaintenance();
                    std::this_thread::sleep_for(config_.updateIntervalSeconds);
                }
            });
        }
        
        isInitialized_.store(true);
        LOG_INFO("DrawdownMonitor: Successfully initialized");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::format("DrawdownMonitor: Initialization failed: {}", e.what()));
        LOG_ERROR(fmt::format("DrawdownMonitor: Initialization failed: {}", e.what()));
    }
}

void DrawdownMonitor::shutdown() {
    if (!isInitialized_.load()) {
        return;
    }
    
    isShuttingDown_.store(true);
    maintenanceActive_.store(false);
    
    if (maintenanceThread_ && maintenanceThread_->joinable()) {
        maintenanceThread_->join();
    }
    
    isInitialized_.store(false);
    LOG_INFO("DrawdownMonitor: Shutdown completed");
}

std::vector<DrawdownEvent> DrawdownMonitor::updatePerformanceMetrics(
    const strategy::PerformanceMetrics& portfolioMetrics,
    const std::map<std::string, strategy::PerformanceMetrics>& strategyMetrics) {
    
    if (!isInitialized_.load() || isShuttingDown_.load()) {
        return {};
    }
    
    std::vector<DrawdownEvent> events;
    totalUpdates_.fetch_add(1);
    
    // Update portfolio
    if (auto portfolioEvent = updatePortfolioValue(portfolioMetrics.pnl, portfolioMetrics)) {
        events.push_back(*portfolioEvent);
    }
    
    // Update strategies
    for (const auto& [strategyId, metrics] : strategyMetrics) {
        if (auto strategyEvent = updateStrategyValue(strategyId, metrics.pnl, metrics)) {
            events.push_back(*strategyEvent);
        }
    }
    
    return events;
}
std::optional<DrawdownEvent> DrawdownMonitor::updateStrategyValue(
    const std::string& strategyId,
    double value,
    const std::optional<strategy::PerformanceMetrics>& metrics) {
    
    if (!validateStrategyId(strategyId) || !validateValue(value)) {
        return std::nullopt;
    }
    
    const auto now = core::now();
    
    // Get or create strategy tracker - use raw pointer instead of unique_ptr
    StrategyPerformanceTracker* tracker = nullptr;
    {
        std::unique_lock lock(strategyMutex_);
        auto it = strategyTrackers_.find(strategyId);
        if (it == strategyTrackers_.end()) {
            // Create new tracker and store it in the map
            auto newTracker = std::make_unique<StrategyPerformanceTracker>(strategyId);
            newTracker->drawdownInfo.highWaterMark.update(value, now);
            tracker = newTracker.get(); // Get raw pointer before moving
            strategyTrackers_[strategyId] = std::move(newTracker);
        } else {
            tracker = it->second.get();
        }
    }
    
    if (!tracker || !tracker->isActive.load()) {
        return std::nullopt;
    }
    
    
    // Update performance metrics if provided
    if (metrics) {
        tracker->latestMetrics = *metrics;
    }
    
    // Store historical data
    const double previousValue = tracker->drawdownInfo.currentValue.load();
    const double previousDrawdown = tracker->drawdownInfo.drawdownPercent.load();
    
    if (config_.enableHistoricalTracking) {
        std::unique_lock lock(strategyMutex_);
        tracker->valueHistory.emplace_back(now, value, previousDrawdown, "update");
        
        // Limit history size
        if (tracker->valueHistory.size() > config_.maxHistorySize) {
            tracker->valueHistory.pop_front();
        }
    }
    
    // Update current value and timestamp
    tracker->drawdownInfo.currentValue.store(value);
    tracker->drawdownInfo.setLastUpdated(now);
    tracker->setLastActivity(now);
    
    // Update high water mark if needed
    const double currentHwm = tracker->drawdownInfo.highWaterMark.value.load();
    if (value > currentHwm) {
        tracker->drawdownInfo.highWaterMark.update(value, now);
        tracker->drawdownInfo.drawdownPercent.store(0.0);
        tracker->drawdownInfo.warningThresholdBreached.store(false);
        tracker->drawdownInfo.criticalThresholdBreached.store(false);
        
        // Reset breach count on new high water mark
        tracker->drawdownInfo.breachCount.store(0);
        return std::nullopt;
    }
    
    // Calculate drawdown
    const double drawdownPercent = calculateDrawdownPercent(value, currentHwm);
    tracker->drawdownInfo.drawdownPercent.store(drawdownPercent);
    tracker->drawdownInfo.updateAverageDrawdown(drawdownPercent);
    
    // Update max drawdown if necessary
    const double currentMaxDrawdown = tracker->drawdownInfo.maxDrawdownPercent.load();
    if (drawdownPercent > currentMaxDrawdown) {
        tracker->drawdownInfo.maxDrawdownPercent.store(drawdownPercent);
    }
    
    // Check thresholds and create events
    const bool wasWarningBreached = tracker->drawdownInfo.warningThresholdBreached.load();
    const bool wasCriticalBreached = tracker->drawdownInfo.criticalThresholdBreached.load();
    
    const bool isWarningBreached = drawdownPercent >= config_.strategyWarningThreshold;
    const bool isCriticalBreached = drawdownPercent >= config_.strategyCriticalThreshold;
    
    tracker->drawdownInfo.warningThresholdBreached.store(isWarningBreached);
    tracker->drawdownInfo.criticalThresholdBreached.store(isCriticalBreached);
    
    // Emergency stop check
    if (config_.enableEmergencyStop && drawdownPercent >= config_.emergencyStopThreshold) {
        notifyEmergencyCallbacks(strategyId, drawdownPercent);
        tracker->isActive.store(false);
    }
    
    // Create event for new threshold breaches
    std::optional<DrawdownEvent> event = std::nullopt;
    
    if (!wasCriticalBreached && isCriticalBreached) {
        // New critical breach
        tracker->drawdownInfo.breachCount.fetch_add(1);
        event = createDrawdownEvent(strategyId, drawdownPercent, currentHwm, value,
                                  tracker->drawdownInfo.highWaterMark.getTimestamp(), true);
        totalEvents_.fetch_add(1);
        
    } else if (!wasWarningBreached && isWarningBreached) {
        // New warning breach
        tracker->drawdownInfo.breachCount.fetch_add(1);
        event = createDrawdownEvent(strategyId, drawdownPercent, currentHwm, value,
                                  tracker->drawdownInfo.highWaterMark.getTimestamp(), false);
        totalEvents_.fetch_add(1);
    }
    
    // Notify callbacks if event created and alert rate limiting allows
    if (event && canSendAlert(strategyId)) {
        notifyCallbacks(*event);
        notifyMetricsCallbacks(strategyId, tracker->drawdownInfo);
        recordAlert(strategyId);
    }
    
    return event;
}

std::optional<DrawdownEvent> DrawdownMonitor::updatePortfolioValue(
    double value,
    const std::optional<strategy::PerformanceMetrics>& metrics) {
    
    if (!validateValue(value)) {
        return std::nullopt;
    }
    
    const auto now = core::now();
    
    // Store historical data
    if (config_.enableHistoricalTracking) {
        std::unique_lock lock(portfolioMutex_);
        const double previousDrawdown = portfolioDrawdown_.drawdownPercent.load();
        portfolioHistory_.emplace_back(now, value, previousDrawdown, "update");
        
        // Limit history size
        if (portfolioHistory_.size() > config_.maxHistorySize) {
            portfolioHistory_.pop_front();
        }
    }
    
    // Update current value
    const double previousValue = portfolioDrawdown_.currentValue.load();
    portfolioDrawdown_.currentValue.store(value);
    portfolioDrawdown_.setLastUpdated(now);
    
    // Initialize or update high water mark
    const double currentHwm = portfolioDrawdown_.highWaterMark.value.load();
    if (currentHwm == 0.0 || value > currentHwm) {
        portfolioDrawdown_.highWaterMark.update(value, now);
        portfolioDrawdown_.drawdownPercent.store(0.0);
        portfolioDrawdown_.warningThresholdBreached.store(false);
        portfolioDrawdown_.criticalThresholdBreached.store(false);
        portfolioDrawdown_.breachCount.store(0);
        return std::nullopt;
    }
    
    // Calculate drawdown
    const double drawdownPercent = calculateDrawdownPercent(value, currentHwm);
    portfolioDrawdown_.drawdownPercent.store(drawdownPercent);
    portfolioDrawdown_.updateAverageDrawdown(drawdownPercent);
    
    // Update max drawdown
    const double currentMaxDrawdown = portfolioDrawdown_.maxDrawdownPercent.load();
    if (drawdownPercent > currentMaxDrawdown) {
        portfolioDrawdown_.maxDrawdownPercent.store(drawdownPercent);
    }
    
    // Check thresholds
    const bool wasWarningBreached = portfolioDrawdown_.warningThresholdBreached.load();
    const bool wasCriticalBreached = portfolioDrawdown_.criticalThresholdBreached.load();
    
    const bool isWarningBreached = drawdownPercent >= config_.portfolioWarningThreshold;
    const bool isCriticalBreached = drawdownPercent >= config_.portfolioCriticalThreshold;
    
    portfolioDrawdown_.warningThresholdBreached.store(isWarningBreached);
    portfolioDrawdown_.criticalThresholdBreached.store(isCriticalBreached);
    
    // Emergency handling
    if (config_.enableEmergencyStop && drawdownPercent >= config_.emergencyStopThreshold) {
        notifyEmergencyCallbacks("PORTFOLIO", drawdownPercent);
    }
    
    // Create event for new breaches
    std::optional<DrawdownEvent> event = std::nullopt;
    
    if (!wasCriticalBreached && isCriticalBreached) {
        portfolioDrawdown_.breachCount.fetch_add(1);
        event = createDrawdownEvent("PORTFOLIO", drawdownPercent, currentHwm, value,
                                  portfolioDrawdown_.highWaterMark.getTimestamp(), true);
        totalEvents_.fetch_add(1);
        
    } else if (!wasWarningBreached && isWarningBreached) {
        portfolioDrawdown_.breachCount.fetch_add(1);
        event = createDrawdownEvent("PORTFOLIO", drawdownPercent, currentHwm, value,
                                  portfolioDrawdown_.highWaterMark.getTimestamp(), false);
        totalEvents_.fetch_add(1);
    }
    
    // Notify callbacks with rate limiting
    if (event && canSendAlert("PORTFOLIO")) {
        notifyCallbacks(*event);
        notifyMetricsCallbacks("PORTFOLIO", portfolioDrawdown_);
        recordAlert("PORTFOLIO");
    }
    
    return event;
}
 
// In drawdown_monitor.cpp - change the implementation:
const DrawdownInfo* DrawdownMonitor::getStrategyDrawdown(const std::string& strategyId) const {
    if (!validateStrategyId(strategyId)) {
        return nullptr;
    }
    
    std::shared_lock lock(strategyMutex_);
    auto it = strategyTrackers_.find(strategyId);
    if (it != strategyTrackers_.end()) {
        return &(it->second->drawdownInfo);
    }
    
    return nullptr;
}
const DrawdownInfo& DrawdownMonitor::getPortfolioDrawdown() const {
    std::shared_lock lock(portfolioMutex_);
    return portfolioDrawdown_;
}

bool DrawdownMonitor::updateConfiguration(const EnhancedDrawdownConfig& config) {
    if (!config.isValid()) {
        LOG_ERROR("DrawdownMonitor: Invalid configuration provided for update");
        return false;
    }
    
    std::unique_lock lock(configMutex_);
    config_ = config;
    
    LOG_INFO("DrawdownMonitor: Configuration updated successfully");
    return true;
}

EnhancedDrawdownConfig DrawdownMonitor::getConfiguration() const {
    std::shared_lock lock(configMutex_);
    return config_;
}

void DrawdownMonitor::registerDrawdownCallback(const DrawdownEventCallback& callback) {
    std::lock_guard lock(callbackMutex_);
    drawdownCallbacks_.push_back(callback);
}

void DrawdownMonitor::registerEmergencyCallback(const EmergencyCallback& callback) {
    std::lock_guard lock(callbackMutex_);
    emergencyCallbacks_.push_back(callback);
}

void DrawdownMonitor::registerMetricsCallback(const MetricsUpdateCallback& callback) {
    std::lock_guard lock(callbackMutex_);
    metricsCallbacks_.push_back(callback);
}

bool DrawdownMonitor::resetStrategyHighWaterMark(const std::string& strategyId, 
                                                 std::optional<double> value) {
    if (!validateStrategyId(strategyId)) {
        return false;
    }
    
    std::unique_lock lock(strategyMutex_);
    auto it = strategyTrackers_.find(strategyId);
    if (it == strategyTrackers_.end()) {
        return false;
    }
    
    const auto now = core::now();
    const double newHwm = value.value_or(it->second->drawdownInfo.currentValue.load());
    
    it->second->drawdownInfo.highWaterMark.update(newHwm, now);
    it->second->drawdownInfo.drawdownPercent.store(0.0);
    it->second->drawdownInfo.warningThresholdBreached.store(false);
    it->second->drawdownInfo.criticalThresholdBreached.store(false);
    it->second->drawdownInfo.breachCount.store(0);
    
    LOG_INFO(std::format("DrawdownMonitor: Reset high water mark for strategy {} to {}", 
    LOG_INFO(fmt::format("DrawdownMonitor: Reset high water mark for strategy {} to {}", 
                            strategyId, newHwm));
}

bool DrawdownMonitor::resetPortfolioHighWaterMark(std::optional<double> value) {
    std::unique_lock lock(portfolioMutex_);
    
    const auto now = core::now();
    const double newHwm = value.value_or(portfolioDrawdown_.currentValue.load());
    
    portfolioDrawdown_.highWaterMark.update(newHwm, now);
    portfolioDrawdown_.drawdownPercent.store(0.0);
    portfolioDrawdown_.warningThresholdBreached.store(false);
    portfolioDrawdown_.criticalThresholdBreached.store(false);
    portfolioDrawdown_.breachCount.store(0);
    
    LOG_INFO(std::format("DrawdownMonitor: Reset portfolio high water mark to {}", newHwm));
    LOG_INFO(fmt::format("DrawdownMonitor: Reset portfolio high water mark to {}", newHwm));
}

std::map<std::string, double> DrawdownMonitor::getPerformanceStatistics() const {
    std::map<std::string, double> stats;
    
    // Portfolio statistics
    {
        std::shared_lock lock(portfolioMutex_);
        stats["portfolio_current_value"] = portfolioDrawdown_.currentValue.load();
        stats["portfolio_high_water_mark"] = portfolioDrawdown_.highWaterMark.value.load();
        stats["portfolio_drawdown_percent"] = portfolioDrawdown_.drawdownPercent.load();
        stats["portfolio_max_drawdown_percent"] = portfolioDrawdown_.maxDrawdownPercent.load();
        stats["portfolio_breach_count"] = portfolioDrawdown_.breachCount.load();
        stats["portfolio_avg_drawdown"] = portfolioDrawdown_.averageDrawdown.load();
    }
    
    // Strategy statistics
    {
        std::shared_lock lock(strategyMutex_);
        stats["total_strategies"] = strategyTrackers_.size();
        
        double totalDrawdown = 0.0;
        uint32_t activeStrategies = 0;
        uint32_t strategiesInWarning = 0;
        uint32_t strategiesInCritical = 0;
        
        for (const auto& [id, tracker] : strategyTrackers_) {
            if (tracker->isActive.load()) {
                activeStrategies++;
                totalDrawdown += tracker->drawdownInfo.drawdownPercent.load();
                
                if (tracker->drawdownInfo.criticalThresholdBreached.load()) {
                    strategiesInCritical++;
                } else if (tracker->drawdownInfo.warningThresholdBreached.load()) {
                    strategiesInWarning++;
                }
            }
        }
        
        stats["active_strategies"] = activeStrategies;
        stats["strategies_in_warning"] = strategiesInWarning;
        stats["strategies_in_critical"] = strategiesInCritical;
        stats["average_strategy_drawdown"] = activeStrategies > 0 ? totalDrawdown / activeStrategies : 0.0;
    }
    
    // System statistics
    stats["total_updates"] = totalUpdates_.load();
    stats["total_events"] = totalEvents_.load();
    
    return stats;
}

bool DrawdownMonitor::isHealthy() const noexcept {
    return safeExecute([this]() {
        return isInitialized_.load() && 
               !isShuttingDown_.load() &&
               config_.isValid();
    });
}

std::map<std::string, std::string> DrawdownMonitor::getDiagnostics() const {
    return safeExecute([this]() {
        std::map<std::string, std::string> diagnostics;
        
        diagnostics["initialized"] = isInitialized_.load() ? "true" : "false";
        diagnostics["shutting_down"] = isShuttingDown_.load() ? "true" : "false";
        diagnostics["maintenance_active"] = maintenanceActive_.load() ? "true" : "false";
        diagnostics["config_valid"] = config_.isValid() ? "true" : "false";
        
        const auto lastMaintenance = core::Timestamp(
            std::chrono::nanoseconds(lastMaintenanceNanos_.load()));
        diagnostics["last_maintenance"] = core::timestampToIsoString(lastMaintenance);
        
        auto stats = getPerformanceStatistics();
        for (const auto& [key, value] : stats) {
            diagnostics[key] = std::to_string(value);
        }
        
        return diagnostics;
    });
}

void DrawdownMonitor::activateStrategy(const std::string& strategyId) {
    if (!validateStrategyId(strategyId)) {
        return;
    }
    
    std::unique_lock lock(strategyMutex_);
    auto it = strategyTrackers_.find(strategyId);
    if (it != strategyTrackers_.end()) {
        it->second->isActive.store(true);
        it->second->setLastActivity(core::now());
    }
}

void DrawdownMonitor::deactivateStrategy(const std::string& strategyId) {
    if (!validateStrategyId(strategyId)) {
        return;
    }
    
    std::unique_lock lock(strategyMutex_);
    auto it = strategyTrackers_.find(strategyId);
    if (it != strategyTrackers_.end()) {
        it->second->isActive.store(false);
    }
}

std::vector<std::string> DrawdownMonitor::getActiveStrategies() const {
    std::shared_lock lock(strategyMutex_);
    std::vector<std::string> active;
    
    for (const auto& [id, tracker] : strategyTrackers_) {
        if (tracker->isActive.load()) {
            active.push_back(id);
        }
    }
    
    return active;
}

// Private method implementations

double DrawdownMonitor::calculateDrawdownPercent(double currentValue, double highWaterMark) const noexcept {
    if (highWaterMark <= 0.0 || currentValue >= highWaterMark) {
        return 0.0;
    }
    
    return (highWaterMark - currentValue) / highWaterMark;
}

DrawdownEvent DrawdownMonitor::createDrawdownEvent(
    const std::string& strategyId,
    double drawdownPercent,
    double highWaterMark,
    double currentValue,
    const core::Timestamp& hwmTime,
    bool isCritical) const {
    
    const auto now = core::now();
    const std::string severity = isCritical ? "critical" : "warning";
    const std::string description = std::format(
    const std::string description = fmt::format(
        "Drawdown {} threshold breached for {}: {:.2f}%",
        severity, strategyId, drawdownPercent * 100.0);
    return DrawdownEvent(
        now,
        drawdownPercent,
        highWaterMark,
        currentValue,
        StrategyId{strategyId, "1.0"},
        hwmTime,
        isCritical,
        description
    );
}

void DrawdownMonitor::notifyCallbacks(const DrawdownEvent& event) {
    std::lock_guard lock(callbackMutex_);
    for (const auto& callback : drawdownCallbacks_) {
        try {
            callback(event);
        } catch (const std::exception& e) {
            LOG_ERROR(std::format("DrawdownMonitor: Callback error: {}", e.what()));
            LOG_ERROR(fmt::format("DrawdownMonitor: Callback error: {}", e.what()));
    }
}

void DrawdownMonitor::notifyEmergencyCallbacks(const std::string& strategyId, double drawdown) {
    std::lock_guard lock(callbackMutex_);
    for (const auto& callback : emergencyCallbacks_) {
        try {
            callback(strategyId, drawdown);
        } catch (const std::exception& e) {
            LOG_ERROR(std::format("DrawdownMonitor: Emergency callback error: {}", e.what()));
            LOG_ERROR(fmt::format("DrawdownMonitor: Emergency callback error: {}", e.what()));
    }
}

void DrawdownMonitor::notifyMetricsCallbacks(const std::string& strategyId, const DrawdownInfo& info) {
    std::lock_guard lock(callbackMutex_);
    for (const auto& callback : metricsCallbacks_) {
        try {
            callback(strategyId, info);
        } catch (const std::exception& e) {
            LOG_ERROR(std::format("DrawdownMonitor: Metrics callback error: {}", e.what()));
            LOG_ERROR(fmt::format("DrawdownMonitor: Metrics callback error: {}", e.what()));
    }
}

void DrawdownMonitor::performMaintenance() {
    if (!isInitialized_.load() || isShuttingDown_.load()) {
        return;
    }
    
    try {
        pruneHistory();
        validateStrategies();
        updatePerformanceMetrics();
        
        lastMaintenanceNanos_.store(core::now().time_since_epoch().count());
        
    } catch (const std::exception& e) {
        LOG_ERROR(std::format("DrawdownMonitor: Maintenance error: {}", e.what()));
        LOG_ERROR(fmt::format("DrawdownMonitor: Maintenance error: {}", e.what()));
}

void DrawdownMonitor::pruneHistory() {
    if (!config_.enableHistoricalTracking) {
        return;
    }
    
    const auto cutoffTime = core::now() - config_.historyRetentionMinutes;
    
    // Prune portfolio history
    {
        std::unique_lock lock(portfolioMutex_);
        portfolioHistory_.erase(
            std::remove_if(portfolioHistory_.begin(), portfolioHistory_.end(),
                [cutoffTime](const HistoricalValue& hv) {
                    return hv.timestamp < cutoffTime;
                }),
            portfolioHistory_.end());
    }
    
    // Prune strategy histories
    {
        std::unique_lock lock(strategyMutex_);
        for (auto& [id, tracker] : strategyTrackers_) {
            tracker->valueHistory.erase(
                std::remove_if(tracker->valueHistory.begin(), tracker->valueHistory.end(),
                    [cutoffTime](const HistoricalValue& hv) {
                        return hv.timestamp < cutoffTime;
                    }),
                tracker->valueHistory.end());
        }
    }
}

void DrawdownMonitor::validateStrategies() {
    const auto cutoffTime = core::now() - std::chrono::hours(24);
    
    std::unique_lock lock(strategyMutex_);
    for (auto it = strategyTrackers_.begin(); it != strategyTrackers_.end();) {
        if (it->second->getLastActivity() < cutoffTime) {
            LOG_INFO(std::format("DrawdownMonitor: Removing inactive strategy: {}", it->first));
            LOG_INFO(fmt::format("DrawdownMonitor: Removing inactive strategy: {}", it->first));
        } else {
            ++it;
        }
    }
}
// calculateSharpeRatio
double DrawdownMonitor::calculateSharpeRatio() const {
    // Placeholder implementation - replace with actual calculation
    // This could involve tracking returns over time and calculating mean and stddev
    return 0.0;
}
strategy::PerformanceMetrics DrawdownMonitor::getPerformanceMetrics() const {
    strategy::PerformanceMetrics metrics;
    metrics.maxDrawdown = maxDrawdown_;
    metrics.currentDrawdown = currentDrawdown_;
    metrics.pnl = totalPnl_;
    metrics.sharpeRatio = calculateSharpeRatio();
    return metrics;
}

void DrawdownMonitor::updatePerformanceMetrics() {
    // Update internal performance tracking
    // This can be extended for more sophisticated metrics
}

bool DrawdownMonitor::canSendAlert(const std::string& strategyId) const {
    if (!config_.enableRealTimeAlerts) {
        return false;
    }
    
    std::lock_guard lock(alertMutex_);
    auto it = lastAlertTimes_.find(strategyId);
    if (it == lastAlertTimes_.end()) {
        return true;
    }
    
    const auto elapsed = core::now() - it->second;
    return elapsed >= config_.alertCooldownSeconds;
}

void DrawdownMonitor::recordAlert(const std::string& strategyId) {
    std::lock_guard lock(alertMutex_);
    lastAlertTimes_[strategyId] = core::now();
}

bool DrawdownMonitor::validateStrategyId(const std::string& strategyId) const noexcept {
    return !strategyId.empty() && strategyId.length() < 256;
}

bool DrawdownMonitor::validateValue(double value) const noexcept {
    return std::isfinite(value) && !std::isnan(value);
}

} // namespace trading::risk