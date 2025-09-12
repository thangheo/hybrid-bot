/**
 * @file post_trade_monitor.cpp
 * @brief Implementation of modern C++17 Post Trade Monitor
 * 
 * @author Trading System Team
 * @date May 26, 2025
 */

#include "risk/posttrade/post_trade_monitor.hpp"
#include <algorithm>
#include <sstream>
#include "common/logger.hpp"
namespace trading::risk {

// Helper function to replace std::format (C++20) with string stream (C++17)
template<typename... Args>
std::string formatString(const std::string& format, Args&&... args) {
    std::ostringstream oss;
    
    // Simple format replacement - you can make this more sophisticated
    std::string result = format;
    std::string search = "{}";
    
    auto replaceNext = [&](auto&& arg) {
        std::ostringstream temp;
        temp << arg;
        size_t pos = result.find(search);
        if (pos != std::string::npos) {
            result.replace(pos, search.length(), temp.str());
        }
    };
    
    (replaceNext(std::forward<Args>(args)), ...);
    return result;
}

PostTradeMonitor::PostTradeMonitor(const RiskConfig& config) : config_(config) {
    // Create interfaces using factory functions
    auto dataInterface = createDataInterface();
    auto executionInterface = createExecutionInterface(config);
    
    // Convert unique_ptr to shared_ptr for sharing among components
    dataInterface_ = std::shared_ptr<DataInterface>(dataInterface.release());
    executionInterface_ = std::shared_ptr<ExecutionInterface>(executionInterface.release());
    
    // Initialize shared dependencies with actual interfaces
    positionTracker_ = std::make_shared<PositionTracker>(config, dataInterface_);
    
    // Initialize components with correct constructor arguments
    stopLossManager_ = std::make_unique<StopLossManager>(config, positionTracker_, dataInterface_, executionInterface_);
    drawdownMonitor_ = std::make_unique<DrawdownMonitor>(config);  // Only pass config
    
    // Extract post-trade monitor configuration
    monitorConfig_.healthCheckIntervalSeconds = std::chrono::seconds(5);
    monitorConfig_.metricsUpdateIntervalSeconds = std::chrono::seconds(10);
    monitorConfig_.reconciliationIntervalMinutes = config.getBalanceReconciliationConfig().reconciliationFrequency;
    monitorConfig_.autoModeTransitions = true;
    monitorConfig_.enableBackgroundMonitoring = true;
    monitorConfig_.enableRealTimeAlerts = true;
    
    // Initialize system health
    systemHealth_.mode.store(RiskManagerMode::NORMAL);
    systemHealth_.tradingAllowed.store(true);
}
PostTradeMonitor::PostTradeMonitor(const RiskConfig& config, PortfolioManager* portfolioManager) 
    : config_(config), portfolioManager_(portfolioManager) {
    
    // Create interfaces using factory functions
    auto dataInterface = createDataInterface();
    auto executionInterface = createExecutionInterface(config);
    
    // Convert unique_ptr to shared_ptr for sharing among components
    dataInterface_ = std::shared_ptr<DataInterface>(dataInterface.release());
    executionInterface_ = std::shared_ptr<ExecutionInterface>(executionInterface.release());
    
    // Initialize shared dependencies with actual interfaces
    positionTracker_ = std::make_shared<PositionTracker>(config, dataInterface_);
    
    // Initialize components with correct constructor arguments
    stopLossManager_ = std::make_unique<StopLossManager>(config, positionTracker_, dataInterface_, executionInterface_);
    drawdownMonitor_ = std::make_unique<DrawdownMonitor>(config);  
    
    // Extract post-trade monitor configuration
    monitorConfig_.healthCheckIntervalSeconds = std::chrono::seconds(5);
    monitorConfig_.metricsUpdateIntervalSeconds = std::chrono::seconds(10);
    monitorConfig_.reconciliationIntervalMinutes = config.getBalanceReconciliationConfig().reconciliationFrequency;
    monitorConfig_.autoModeTransitions = true;
    monitorConfig_.enableBackgroundMonitoring = true;
    monitorConfig_.enableRealTimeAlerts = true;
    
    // Initialize system health
    systemHealth_.mode.store(RiskManagerMode::NORMAL);
    systemHealth_.tradingAllowed.store(true);
}
PostTradeMonitor::~PostTradeMonitor() {
    shutdown();
}

bool PostTradeMonitor::isHealthy() const {
    // Check if all main components are initialized and healthy
    if (!stopLossManager_ || !drawdownMonitor_ || !circuitBreaker_ || !balanceReconciliation_) {
        return false;
    }
    
    // Check individual component health if they have isHealthy method
    bool stopLossHealthy = true;
    bool drawdownHealthy = true;
    bool circuitBreakerHealthy = true;
    bool reconciliationHealthy = true;
    
    if (auto ptr = dynamic_cast<const StopLossManager*>(stopLossManager_.get())) {
        stopLossHealthy = ptr->isHealthy();
    }
    
    if (auto ptr = dynamic_cast<const DrawdownMonitor*>(drawdownMonitor_.get())) {
        drawdownHealthy = ptr->isHealthy();
    }
    
    if (auto ptr = dynamic_cast<const CircuitBreaker*>(circuitBreaker_.get())) {
        circuitBreakerHealthy = ptr->isHealthy();
    }
    
    if (auto ptr = dynamic_cast<const BalanceReconciliation*>(balanceReconciliation_.get())) {
        reconciliationHealthy = ptr->isHealthy();
    }
    
    return stopLossHealthy && drawdownHealthy && circuitBreakerHealthy && reconciliationHealthy;
}

bool PostTradeMonitor::initialize() {
    if (!initializeComponents()) {
        return false;
    }
    
    if (!validateConfiguration(config_)) {
        LOG_ERROR(std::format("{} {}", "PostTradeMonitor", "Invalid configuration"));
        return false;
    }
    
    connectComponentCallbacks();
    
    // Start background monitoring if enabled
    if (monitorConfig_.enableBackgroundMonitoring) {
        backgroundThread_ = std::make_unique<std::thread>([this]() {
            backgroundMonitoringLoop();
        });
        
        healthCheckThread_ = std::make_unique<std::thread>([this]() {
            healthCheckLoop();
        });
    }
    
    return true;
}

void PostTradeMonitor::shutdown() {
    shutdownRequested_.store(true);
    
    // Stop background threads
    if (backgroundThread_) {
        if (backgroundThread_->joinable()) {
            backgroundThread_->join();
        }
    }
    
    if (healthCheckThread_) {
        if (healthCheckThread_->joinable()) {
            healthCheckThread_->join();
        }
    }
    
    shutdownComponents();
}

void PostTradeMonitor::processExecutionReport(const execution::models::ExecutionReport& executionReport) {
    totalExecutionReports_.fetch_add(1);
    
    // Check if trading is allowed
    if (!isTradingAllowed()) {
        RiskEvent event(
            RiskEventType::EMERGENCY_ACTION,
            formatString("Execution blocked by circuit breaker: {}:{}", 
                       executionReport.exchangeId().toString(), 
                       executionReport.symbol().toString()),
            "critical"
        );
        
        notifyRiskEvent(event);
        return;
    }
    
    // Forward to components (non-const version)
    safeComponentOperation(stopLossManager_, [&](auto& component) {
        component.processExecutionReport(executionReport);
        return true;
    });
    
    // Update position tracker
    if (positionTracker_) {
        positionTracker_->processExecutionReport(executionReport);
    }
    
    // Perform health checks after processing
    auto healthResult = performHealthCheck();
    (void)healthResult; // Suppress unused variable warning
}

void PostTradeMonitor::updateMarketData(const core::MarketData& marketData) {
    totalMarketDataUpdates_.fetch_add(1);
    
    // Forward to stop loss manager for trailing stops (non-const version)
    safeComponentOperation(stopLossManager_, [&](auto& component) {
        component.updateMarketData(marketData);
        return true;
    });
    
    // Update position tracker
    if (positionTracker_) {
        positionTracker_->updateMarketData(marketData);
    }
    
    // Check circuit breaker metrics if available (non-const version)
    safeComponentOperation(circuitBreaker_, [&](auto& component) {
        // Update market-related metrics - ignore return values for fire-and-forget updates
        auto result1 = component.updateMetric("price_volatility", 
            marketData.extra.count("volatility") ? marketData.extra.at("volatility") : 0.0);
        auto result2 = component.updateMetric("spread_percent",
            (marketData.ask - marketData.bid) / ((marketData.ask + marketData.bid) / 2.0) * 100.0);
        (void)result1; (void)result2; // Suppress unused variable warnings
        return true;
    });
}

void PostTradeMonitor::updatePerformanceMetrics(
    const strategy::PerformanceMetrics& portfolioMetrics,
    const std::map<std::string, strategy::PerformanceMetrics>& strategyMetrics) {
    
    // Forward to drawdown monitor (non-const version)
    auto drawdownEvents = safeComponentOperation(drawdownMonitor_, [&](auto& component) {
        return component.updatePerformanceMetrics(portfolioMetrics, strategyMetrics);
    });
    
    if (drawdownEvents) {
        for (const auto& event : *drawdownEvents) {
            handleDrawdownEvent(event);
        }
    }
    
    // Update circuit breaker with performance metrics (non-const version)
    safeComponentOperation(circuitBreaker_, [&](auto& component) {
        auto result1 = component.updateMetric("portfolio_pnl", portfolioMetrics.pnl);
        auto result2 = component.updateMetric("max_drawdown", portfolioMetrics.maxDrawdown);
        auto result3 = component.updateMetric("sharpe_ratio", portfolioMetrics.sharpeRatio);
        auto result4 = component.updateMetric("win_rate", portfolioMetrics.winRate);
        (void)result1; (void)result2; (void)result3; (void)result4; // Suppress unused warnings
        return true;
    });
}

SystemHealthStatus PostTradeMonitor::getSystemHealth() {
    updateComponentHealth();
    
    // Create a snapshot by manually copying the atomic values
    SystemHealthStatus snapshot;
    snapshot.mode.store(systemHealth_.mode.load());
    snapshot.circuitBreakerState.store(systemHealth_.circuitBreakerState.load());
    snapshot.stopLossesActive.store(systemHealth_.stopLossesActive.load());
    snapshot.reconciliationActive.store(systemHealth_.reconciliationActive.load());
    snapshot.tradingAllowed.store(systemHealth_.tradingAllowed.load());
    snapshot.totalRiskEvents.store(systemHealth_.totalRiskEvents.load());
    snapshot.criticalEvents.store(systemHealth_.criticalEvents.load());
    snapshot.lastEventTimestampNanos.store(systemHealth_.lastEventTimestampNanos.load());
    snapshot.stopLossManagerHealthy.store(systemHealth_.stopLossManagerHealthy.load());
    snapshot.drawdownMonitorHealthy.store(systemHealth_.drawdownMonitorHealthy.load());
    snapshot.circuitBreakerHealthy.store(systemHealth_.circuitBreakerHealthy.load());
    snapshot.reconciliationHealthy.store(systemHealth_.reconciliationHealthy.load());
    
    return snapshot;
}

void PostTradeMonitor::setMode(RiskManagerMode mode, std::string_view reason) {
    RiskManagerMode oldMode = systemHealth_.mode.exchange(mode);
    
    if (oldMode != mode) {
        recordModeTransition(mode);
        
        // Update trading allowed status based on mode
        bool tradingAllowed = (mode != RiskManagerMode::EMERGENCY && 
                              mode != RiskManagerMode::MAINTENANCE);
        systemHealth_.tradingAllowed.store(tradingAllowed);
        
        // Notify callbacks
        notifyModeChange(oldMode, mode, reason);
        
        // Create risk event
        RiskEvent event(
            RiskEventType::RISK_MODE_CHANGE,
            formatString("Risk manager mode changed: {} -> {} ({})",
                       modeToString(oldMode), modeToString(mode), reason),
            mode == RiskManagerMode::EMERGENCY ? "critical" : "warning"
        );
        
        notifyRiskEvent(event);
    }
}

// CircuitBreakerState PostTradeMonitor::getCircuitBreakerState() {
//     return safeComponentOperation(circuitBreaker_, [](auto& component) {
//         return component.getState();
//     }).value_or(CircuitBreakerState::OFF);
// }

// strategy::PerformanceMetrics PostTradeMonitor::getPerformanceMetrics() const {
//     return safeComponentOperation(drawdownMonitor_, [](const auto& component) {
//         return component.getPerformanceMetrics();
//     }).value_or(strategy::PerformanceMetrics{});
// }
CircuitBreakerState PostTradeMonitor::getCircuitBreakerState() {
    return safeComponentOperation(circuitBreaker_, [](CircuitBreaker& component) {
        return component.getState();
    }).value_or(CircuitBreakerState::OFF);
}

strategy::PerformanceMetrics PostTradeMonitor::getPerformanceMetrics() const {
    return safeComponentOperation(drawdownMonitor_, [](const DrawdownMonitor& component) {
        return component.getPerformanceMetrics();  // This method doesn't exist!
    }).value_or(strategy::PerformanceMetrics{});
}

std::future<ReconciliationResult> PostTradeMonitor::reconcilePositions(
    std::optional<core::ExchangeId> exchangeId) {
    
    return executeAsync([this, exchangeId]() -> ReconciliationResult {
        if (!balanceReconciliation_) {
            ReconciliationResult result;
            result.success = false;
            return result;
        }
        
        try {
            if (exchangeId) {
                return balanceReconciliation_->reconcileExchangePositions(*exchangeId);
            } else {
                return balanceReconciliation_->reconcileAllPositions();
            }
        } catch (...) {
            ReconciliationResult result;
            result.success = false;
            return result;
        }
    });
}

bool PostTradeMonitor::updateConfiguration(const RiskConfig& config) {
    if (!validateConfiguration(config)) {
        return false;
    }
    
    std::unique_lock<std::shared_mutex> lock(configMutex_);
    config_ = config;
    lock.unlock();
    
    // Update component configurations (non-const versions)
    bool success = true;
    
    success &= safeComponentOperation(stopLossManager_, [&](auto& component) {
        EnhancedStopLossConfig stopLossConfig;
        stopLossConfig.enableDefaultStopLoss = config.getStopLossConfig().enableDefaultStopLoss;
        stopLossConfig.defaultStopLossPercent = config.getStopLossConfig().maxLossPercentage;
        stopLossConfig.autoLiquidateOnTrigger = config.getStopLossConfig().forceLiquidation;
        return component.updateConfiguration(stopLossConfig);
    }).value_or(false);
    
    success &= safeComponentOperation(circuitBreaker_, [&](auto& component) {
        return component.updateConfiguration(config.getCircuitBreakerConfig());
    }).value_or(false);
    
    success &= safeComponentOperation(drawdownMonitor_, [&](auto& component) {
        EnhancedDrawdownConfig drawdownConfig;
        drawdownConfig.portfolioWarningThreshold = config.getDrawdownConfig().warningThreshold;
        drawdownConfig.portfolioCriticalThreshold = config.getDrawdownConfig().criticalThreshold;
        drawdownConfig.enableAutoRiskReduction = config.getDrawdownConfig().enableAutoRiskReduction;
        return component.updateConfiguration(drawdownConfig);
    }).value_or(false);
    
    success &= safeComponentOperation(balanceReconciliation_, [&](auto& component) {
        return component.updateConfiguration(config.getBalanceReconciliationConfig());
    }).value_or(false);
    
    return success;
}

void PostTradeMonitor::registerRiskEventCallback(const PostTradeRiskEventCallback& callback) {
    std::unique_lock<std::shared_mutex> lock(callbackMutex_);
    riskEventCallbacks_.push_back(callback);
}

void PostTradeMonitor::registerModeChangeCallback(const ModeChangeCallback& callback) {
    std::unique_lock<std::shared_mutex> lock(callbackMutex_);
    modeChangeCallbacks_.push_back(callback);
}

void PostTradeMonitor::registerHealthStatusCallback(const HealthStatusCallback& callback) {
    std::unique_lock<std::shared_mutex> lock(callbackMutex_);
    healthStatusCallbacks_.push_back(callback);
}

std::map<std::string, double> PostTradeMonitor::getMonitoringStatistics() {
    std::map<std::string, double> stats;
    
    stats["total_execution_reports"] = static_cast<double>(totalExecutionReports_.load());
    stats["total_market_data_updates"] = static_cast<double>(totalMarketDataUpdates_.load());
    stats["system_health_score"] = systemHealth_.getHealthScore();
    stats["total_risk_events"] = static_cast<double>(systemHealth_.totalRiskEvents.load());
    stats["critical_events"] = static_cast<double>(systemHealth_.criticalEvents.load());
    
    // Add component statistics
    auto stopLossStats = safeComponentOperation(stopLossManager_, [](const auto& component) {
        return component.getStatistics();
    });
    
    if (stopLossStats) {
        for (const auto& [key, value] : *stopLossStats) {
            stats["stop_loss_" + key] = value;
        }
    }
    
    auto drawdownStats = safeComponentOperation(drawdownMonitor_, [](const auto& component) {
        return component.getPerformanceStatistics();
    });
    
    if (drawdownStats) {
        for (const auto& [key, value] : *drawdownStats) {
            stats["drawdown_" + key] = value;
        }
    }
    
    return stats;
}

std::vector<RiskEvent> PostTradeMonitor::getRecentEvents(
    std::optional<RiskEventType> typeFilter,
    std::optional<std::string> severityFilter,
    std::chrono::minutes lookbackMinutes) const {
    
    return filterEventsByPredicate([&](const RiskEvent& event) {
        bool typeMatch = !typeFilter || event.type == *typeFilter;
        bool severityMatch = !severityFilter || event.severity == *severityFilter;
        return typeMatch && severityMatch;
    }, lookbackMinutes);
}

bool PostTradeMonitor::emergencyStop(std::string_view reason) {
    setMode(RiskManagerMode::EMERGENCY, reason);
    
    // Stop all trading activities
    systemHealth_.tradingAllowed.store(false);
    
    // Trigger circuit breaker (non-const version)
    safeComponentOperation(circuitBreaker_, [&](auto& component) {
        return component.manualTrigger(reason);
    });
    
    // Create emergency event
    RiskEvent event(
        RiskEventType::EMERGENCY_ACTION,
        formatString("Emergency stop triggered: {}", reason),
        "critical"
    );
    
    notifyRiskEvent(event);
    return true;
}

bool PostTradeMonitor::resumeTrading(std::string_view reason) {
    // Reset circuit breaker (non-const version)
    bool resetSuccess = safeComponentOperation(circuitBreaker_, [](auto& component) {
        return component.reset();
    }).value_or(false);
    
    if (resetSuccess) {
        setMode(RiskManagerMode::NORMAL, reason);
        systemHealth_.tradingAllowed.store(true);
        
        RiskEvent event(
            RiskEventType::RISK_MODE_CHANGE,
            formatString("Trading resumed: {}", reason),
            "info"
        );
        
        notifyRiskEvent(event);
        return true;
    }
    
    return false;
}

bool PostTradeMonitor::performHealthCheck() {
    updateComponentHealth();
    
    auto currentTime = core::now();
    lastHealthCheckNanos_.store(currentTime.time_since_epoch().count());
    
    // Check if mode transition is needed
    if (monitorConfig_.autoModeTransitions) {
        RiskManagerMode currentMode = systemHealth_.mode.load();
        RiskManagerMode requiredMode = calculateRequiredMode();
        
        if (currentMode != requiredMode) {
            setMode(requiredMode, "Automatic mode transition based on system state");
        }
    }
    
    // Notify health status callbacks with a snapshot
    SystemHealthStatus snapshot;
    snapshot.mode.store(systemHealth_.mode.load());
    snapshot.circuitBreakerState.store(systemHealth_.circuitBreakerState.load());
    snapshot.stopLossesActive.store(systemHealth_.stopLossesActive.load());
    snapshot.reconciliationActive.store(systemHealth_.reconciliationActive.load());
    snapshot.tradingAllowed.store(systemHealth_.tradingAllowed.load());
    snapshot.totalRiskEvents.store(systemHealth_.totalRiskEvents.load());
    snapshot.criticalEvents.store(systemHealth_.criticalEvents.load());
    snapshot.lastEventTimestampNanos.store(systemHealth_.lastEventTimestampNanos.load());
    snapshot.stopLossManagerHealthy.store(systemHealth_.stopLossManagerHealthy.load());
    snapshot.drawdownMonitorHealthy.store(systemHealth_.drawdownMonitorHealthy.load());
    snapshot.circuitBreakerHealthy.store(systemHealth_.circuitBreakerHealthy.load());
    snapshot.reconciliationHealthy.store(systemHealth_.reconciliationHealthy.load());
    
    notifyHealthStatus(snapshot);
    
    return systemHealth_.isSystemHealthy();
}

std::map<std::string, std::string> PostTradeMonitor::getDiagnostics() {
    std::map<std::string, std::string> diagnostics;
    
    diagnostics["component_name"] = "PostTradeMonitor";
    diagnostics["current_mode"] = modeToString(systemHealth_.mode.load());
    diagnostics["circuit_breaker_state"] = circuitBreakerStateToString(getCircuitBreakerState());
    diagnostics["trading_allowed"] = systemHealth_.tradingAllowed.load() ? "true" : "false";
    diagnostics["system_health_score"] = std::to_string(systemHealth_.getHealthScore());
    
    // Component diagnostics
    auto stopLossDiag = safeComponentOperation(stopLossManager_, [](const auto& component) {
        return component.getDiagnostics();
    });
    
    if (stopLossDiag) {
        for (const auto& [key, value] : *stopLossDiag) {
            diagnostics["stop_loss_" + key] = value;
        }
    }
    
    auto drawdownDiag = safeComponentOperation(drawdownMonitor_, [](const auto& component) {
        return component.getDiagnostics();
    });
    
    if (drawdownDiag) {
        for (const auto& [key, value] : *drawdownDiag) {
            diagnostics["drawdown_" + key] = value;
        }
    }
    
    return diagnostics;
}

// Private method implementations

bool PostTradeMonitor::initializeComponents() {
    try {
        // Initialize stop loss manager
        stopLossManager_ = std::make_unique<StopLossManager>(
            config_, positionTracker_, dataInterface_, executionInterface_
        );
        
        if (!stopLossManager_->initialize()) {
            return false;
        }
        
        // Initialize drawdown monitor
        drawdownMonitor_ = std::make_unique<DrawdownMonitor>(config_);
        if (!drawdownMonitor_->initialize()) {
            return false;
        }
        
        // Initialize circuit breaker
        circuitBreaker_ = std::make_unique<CircuitBreaker>(config_);
        if (!circuitBreaker_->initialize()) {
            return false;
        }
        
        // Initialize balance reconciliation
        balanceReconciliation_ = std::make_unique<BalanceReconciliation>(
            config_, positionTracker_, dataInterface_, executionInterface_
        );
        
        if (!balanceReconciliation_->initialize()) {
            return false;
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

void PostTradeMonitor::shutdownComponents() {
    if (balanceReconciliation_) {
        balanceReconciliation_->shutdown();
    }
    
    if (drawdownMonitor_) {
        drawdownMonitor_->shutdown();
    }
    
    // Circuit breaker and stop loss manager don't have explicit shutdown methods
    // but their destructors will handle cleanup
}

void PostTradeMonitor::connectComponentCallbacks() {
    // Connect stop loss callbacks
    if (stopLossManager_) {
        stopLossManager_->registerStopLossCallback([this](const EnhancedStopLossEvent& event) {
            handleStopLossEvent(event);
        });
    }
    
    // Connect circuit breaker callbacks
    if (circuitBreaker_) {
        circuitBreaker_->registerStateChangeCallback([this](CircuitBreakerState oldState, 
                                                           CircuitBreakerState newState, 
                                                           std::string_view reason) {
            handleCircuitBreakerEvent(oldState, newState, reason);
        });
    }
    
    // Connect drawdown callbacks
    if (drawdownMonitor_) {
        drawdownMonitor_->registerDrawdownCallback([this](const DrawdownEvent& event) {
            handleDrawdownEvent(event);
        });
    }
    
    // Connect reconciliation callbacks
    if (balanceReconciliation_) {
        balanceReconciliation_->registerReconciliationCallback([this](const PositionReconciliationEvent& event) {
            handleReconciliationEvent(event);
        });
    }
}

void PostTradeMonitor::handleStopLossEvent(const EnhancedStopLossEvent& event) {
    RiskEvent riskEvent(
        RiskEventType::STOP_LOSS_TRIGGERED,
        formatString("Stop loss triggered for {}:{} at price {}", 
                   event.exchangeId.toString(), event.tradingPair.toString(), event.triggerPrice),
        event.isCriticalLoss() ? "critical" : "warning"
    );
    
    notifyRiskEvent(riskEvent);
    
    // Update mode if critical loss
    if (event.isCriticalLoss(config_.getStopLossConfig().maxLossPercentage * 0.8)) {
        setMode(RiskManagerMode::HEIGHTENED_RISK, "Critical stop loss triggered");
    }
}

void PostTradeMonitor::handleCircuitBreakerEvent(CircuitBreakerState oldState, 
                                                CircuitBreakerState newState, 
                                                std::string_view reason) {
    CircuitBreakerEvent cbEvent(oldState, newState, std::string(reason));
    
    RiskEvent riskEvent(
        RiskEventType::CIRCUIT_BREAKER_TRIGGERED,
        formatString("Circuit breaker state changed: {} -> {} ({})",
                   circuitBreakerStateToString(oldState),
                   circuitBreakerStateToString(newState),
                   reason),
        newState == CircuitBreakerState::TRIGGERED ? "critical" : "warning"
    );
    
    notifyRiskEvent(riskEvent);
    
    // Update mode based on circuit breaker state
    if (newState == CircuitBreakerState::TRIGGERED) {
        setMode(RiskManagerMode::EMERGENCY, "Circuit breaker triggered");
    } else if (newState == CircuitBreakerState::OFF && oldState != CircuitBreakerState::WARNING) {
        setMode(RiskManagerMode::NORMAL, "Circuit breaker reset");
    }
}

void PostTradeMonitor::handleDrawdownEvent(const DrawdownEvent& event) {
    RiskEvent riskEvent(
        RiskEventType::DRAWDOWN_ALERT,
        formatString("Drawdown alert: {} drawdown of {:.2f}%",
                   event.strategyId.id.empty() ? "Portfolio" : ("Strategy " + event.strategyId.id),
                   event.drawdownPercentage * 100.0),
        event.thresholdBreached ? "critical" : "warning"
    );
    
    notifyRiskEvent(riskEvent);
    
    // Update mode if critical drawdown
    if (event.thresholdBreached) {
        if (event.strategyId.id.empty()) { // Portfolio drawdown
            setMode(RiskManagerMode::EMERGENCY, "Critical portfolio drawdown");
        } else {
            setMode(RiskManagerMode::HEIGHTENED_RISK, "Critical strategy drawdown");
        }
    }
}

void PostTradeMonitor::handleReconciliationEvent(const PositionReconciliationEvent& event) {
    RiskEvent riskEvent(
        RiskEventType::POSITION_RECONCILIATION_MISMATCH,
        formatString("Position reconciliation: {}:{} discrepancy of {:.4f}{}",
                   event.exchangeId.toString(), event.tradingPair.toString(),
                   event.discrepancyPercentage,
                   event.autoCorrect ? " (auto-corrected)" : ""),
        std::abs(event.discrepancyPercentage) > 5.0 ? "critical" : "warning"
    );
    
    notifyRiskEvent(riskEvent);
    
    // Update mode if significant discrepancy
    if (std::abs(event.discrepancyPercentage) > config_.getBalanceReconciliationConfig().maxDiscrepancyPercent * 2) {
        setMode(RiskManagerMode::HEIGHTENED_RISK, "Significant position reconciliation discrepancy");
    }
}

void PostTradeMonitor::notifyRiskEvent(const RiskEvent& event) {
    // Update system health statistics
    systemHealth_.totalRiskEvents.fetch_add(1);
    if (event.isCritical()) {
        systemHealth_.criticalEvents.fetch_add(1);
    }
    systemHealth_.setLastEventTime(event.timestamp);
    
    // Add to event history
    addToEventHistory(const_cast<RiskEvent&>(event));
    
    // Notify callbacks
    std::shared_lock<std::shared_mutex> lock(callbackMutex_);
    for (const auto& callback : riskEventCallbacks_) {
        try {
            callback(event);
        } catch (...) {
            // Log error but continue
        }
    }
}

void PostTradeMonitor::notifyModeChange(RiskManagerMode oldMode, RiskManagerMode newMode, std::string_view reason) {
    std::shared_lock<std::shared_mutex> lock(callbackMutex_);
    for (const auto& callback : modeChangeCallbacks_) {
        try {
            callback(oldMode, newMode, reason);
        } catch (...) {
            // Log error but continue
        }
    }
}

void PostTradeMonitor::notifyHealthStatus(const SystemHealthStatus& status) {
    std::shared_lock<std::shared_mutex> lock(callbackMutex_);
    for (const auto& callback : healthStatusCallbacks_) {
        try {
            callback(status);
        } catch (...) {
            // Log error but continue
        }
    }
}

void PostTradeMonitor::backgroundMonitoringLoop() {
    while (!shutdownRequested_.load()) {
        try {
            performScheduledTasks();
            
            std::this_thread::sleep_for(monitorConfig_.metricsUpdateIntervalSeconds);
        } catch (...) {
            // Log error and continue
        }
    }
}

void PostTradeMonitor::healthCheckLoop() {
    while (!shutdownRequested_.load()) {
        try {
            auto healthResult = performHealthCheck();
            (void)healthResult; // Suppress unused variable warning
            
            std::this_thread::sleep_for(monitorConfig_.healthCheckIntervalSeconds);
        } catch (...) {
            // Log error and continue
        }
    }
}

void PostTradeMonitor::performScheduledTasks() {
    // Check stop losses (non-const version)
    safeComponentOperation(stopLossManager_, [](auto& component) {
        auto events = component.checkStopLosses();
        (void)events; // Suppress unused variable warning
        return true;
    });
    
    // Perform maintenance tasks
    static auto lastMaintenance = core::now();
    auto now = core::now();
    
    if (now - lastMaintenance >= std::chrono::minutes(10)) {
        performMaintenanceTasks();
        lastMaintenance = now;
    }
}

void PostTradeMonitor::performMaintenanceTasks() {
    // Prune old events
    pruneEventHistory();
    
    // Recalculate trailing stops (non-const version)
    safeComponentOperation(stopLossManager_, [](auto& component) {
        component.recalculateTrailingStops();
        return true;
    });
    
    // Check for automatic mode transitions
    if (monitorConfig_.autoModeTransitions) {
        checkModeTimeouts();
    }
}

RiskManagerMode PostTradeMonitor::calculateRequiredMode() {
    // Check component health
    if (!systemHealth_.isSystemHealthy()) {
        return RiskManagerMode::EMERGENCY;
    }
    
    // Check circuit breaker state
    CircuitBreakerState cbState = getCircuitBreakerState();
    if (cbState == CircuitBreakerState::TRIGGERED) {
        return RiskManagerMode::EMERGENCY;
    }
    
    // Check recent critical events
    auto recentCriticalEvents = getRecentEvents(std::nullopt, "critical", std::chrono::minutes(30));
    if (recentCriticalEvents.size() >= 3) {
        return RiskManagerMode::HEIGHTENED_RISK;
    }
    
    return RiskManagerMode::NORMAL;
}

void PostTradeMonitor::recordModeTransition(RiskManagerMode mode) {
    std::unique_lock<std::shared_mutex> lock(modeTransitionMutex_);
    modeTransitionTimes_[mode] = core::now();
}

void PostTradeMonitor::addToEventHistory(RiskEvent event) {
    std::unique_lock<std::shared_mutex> lock(eventMutex_);
    
    eventHistory_.push_back(std::move(event));
    
    // Limit history size
    while (eventHistory_.size() > monitorConfig_.maxEventHistorySize) {
        eventHistory_.pop_front();
    }
}

void PostTradeMonitor::pruneEventHistory() {
    std::unique_lock<std::shared_mutex> lock(eventMutex_);
    
    auto cutoffTime = core::now() - monitorConfig_.eventRetentionHours;
    
    auto newEnd = std::remove_if(eventHistory_.begin(), eventHistory_.end(),
        [cutoffTime](const RiskEvent& event) {
            return event.timestamp < cutoffTime;
        });
    
    eventHistory_.erase(newEnd, eventHistory_.end());
}

bool PostTradeMonitor::validateConfiguration(const RiskConfig& config) const {
    return config.validate() && monitorConfig_.isValid();
}

void PostTradeMonitor::updateComponentHealth() {
    systemHealth_.stopLossManagerHealthy.store(
        stopLossManager_ && stopLossManager_->isHealthy()
    );
    
    systemHealth_.drawdownMonitorHealthy.store(
        drawdownMonitor_ && drawdownMonitor_->isHealthy()
    );
    
    systemHealth_.circuitBreakerHealthy.store(
        circuitBreaker_ != nullptr
    );
    
    systemHealth_.reconciliationHealthy.store(
        balanceReconciliation_ != nullptr
    );
    
    systemHealth_.circuitBreakerState.store(getCircuitBreakerState());
}

std::string PostTradeMonitor::modeToString(RiskManagerMode mode) const {
    switch (mode) {
        case RiskManagerMode::NORMAL: return "NORMAL";
        case RiskManagerMode::HEIGHTENED_RISK: return "HEIGHTENED_RISK";
        case RiskManagerMode::EMERGENCY: return "EMERGENCY";
        case RiskManagerMode::MAINTENANCE: return "MAINTENANCE";
        default: return "UNKNOWN";
    }
}

std::string PostTradeMonitor::circuitBreakerStateToString(CircuitBreakerState state) const {
    switch (state) {
        case CircuitBreakerState::OFF: return "OFF";
        case CircuitBreakerState::WARNING: return "WARNING";
        case CircuitBreakerState::TRIGGERED: return "TRIGGERED";
        case CircuitBreakerState::COOLING: return "COOLING";
        default: return "UNKNOWN";
    }
}

void PostTradeMonitor::checkModeTimeouts() {
    std::shared_lock<std::shared_mutex> lock(modeTransitionMutex_);
    auto currentMode = systemHealth_.mode.load();
    auto now = core::now();
    
    auto it = modeTransitionTimes_.find(currentMode);
    if (it != modeTransitionTimes_.end()) {
        auto timeInMode = now - it->second;
        
        if (currentMode == RiskManagerMode::EMERGENCY && 
            timeInMode >= monitorConfig_.emergencyModeTimeoutMinutes) {
            lock.unlock();
            setMode(RiskManagerMode::HEIGHTENED_RISK, "Emergency mode timeout");
        } else if (currentMode == RiskManagerMode::HEIGHTENED_RISK && 
                  timeInMode >= monitorConfig_.heightenedRiskTimeoutMinutes) {
            lock.unlock();
            setMode(RiskManagerMode::NORMAL, "Heightened risk mode timeout");
        }
    }
}

std::vector<RiskEvent> PostTradeMonitor::filterEventsByPredicate(
    std::function<bool(const RiskEvent&)> predicate, 
    std::chrono::minutes lookback) const {
    
    std::shared_lock<std::shared_mutex> lock(eventMutex_);
    auto cutoffTime = core::now() - lookback;
    
    std::vector<RiskEvent> result;
    for (const auto& event : eventHistory_) {
        if (event.timestamp >= cutoffTime && predicate(event)) {
            result.push_back(event);
        }
    }
    
    return result;
}

// createPostTradeMonitor
// std::shared_ptr<PostTradeMonitor> createPostTradeMonitor(
//     const RiskConfig& config,
//     const PostTradeMonitorConfig& monitorConfig,
//     std::shared_ptr<strategy::PositionTracker> positionTracker,
//     std::shared_ptr<core::DataInterface> dataInterface,
//     std::shared_ptr<core::ExecutionInterface> executionInterface) {
    
//     auto monitor = std::make_shared<PostTradeMonitor>(
//         config, monitorConfig, positionTracker, dataInterface, executionInterface
//     );
    
//     if (!monitor->initialize()) {
//         return nullptr;
//     }
    
//     return monitor;
// }

} // namespace trading::risk
