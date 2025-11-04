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
#include <unordered_set>
// Persistence and alerting
#include "risk/storage/event_store.hpp"
#include "risk/alerting/webhook_client.hpp"
namespace trading::risk {

// Temporary debug flag for verbose TTL logging. Set to 'false' to disable.
static bool kPostTradeTtlDebug = true;

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
    // For backwards compatibility we will attempt to create interfaces if none
    // are injected. This ctor should be used sparingly in favor of the
    // dependency-injecting overload below to avoid duplicate MarketDataInterface
    // instances.
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
    // Backwards compatible ctor - prefer the DI ctor below.
    auto dataInterface = createDataInterface();
    auto executionInterface = createExecutionInterface(config);

    dataInterface_ = std::shared_ptr<DataInterface>(dataInterface.release());
    executionInterface_ = std::shared_ptr<ExecutionInterface>(executionInterface.release());

    positionTracker_ = std::make_shared<PositionTracker>(config, dataInterface_);
    stopLossManager_ = std::make_unique<StopLossManager>(config, positionTracker_, dataInterface_, executionInterface_);
    drawdownMonitor_ = std::make_unique<DrawdownMonitor>(config);

    monitorConfig_.healthCheckIntervalSeconds = std::chrono::seconds(5);
    monitorConfig_.metricsUpdateIntervalSeconds = std::chrono::seconds(10);
    monitorConfig_.reconciliationIntervalMinutes = config.getBalanceReconciliationConfig().reconciliationFrequency;
    monitorConfig_.autoModeTransitions = true;
    monitorConfig_.enableBackgroundMonitoring = true;
    monitorConfig_.enableRealTimeAlerts = true;

    systemHealth_.mode.store(RiskManagerMode::NORMAL);
    systemHealth_.tradingAllowed.store(true);
}

// New constructor that accepts injected shared interfaces to avoid creating
// duplicate DataInterface/MarketDataInterface instances. If injected pointers
// are null, the constructor will fall back to creating instances (but log a
// warning so callers can be corrected).
PostTradeMonitor::PostTradeMonitor(const RiskConfig& config,
                                   PortfolioManager* portfolioManager,
                                   std::shared_ptr<DataInterface> dataInterface,
                                   std::shared_ptr<ExecutionInterface> executionInterface,
                                   std::shared_ptr<PositionTracker> positionTracker)
    : config_(config), portfolioManager_(portfolioManager), dataInterface_(std::move(dataInterface)), executionInterface_(std::move(executionInterface)), injectedPositionTracker_(std::move(positionTracker)) {

    if (!dataInterface_) {
        LOG_WARN("PostTradeMonitor: no DataInterface provided, creating one internally (this may cause duplicate MarketDataInterface instances). Provide a shared DataInterface to avoid this.");
        auto ai = createDataInterface();
        dataInterface_ = std::shared_ptr<DataInterface>(ai.release());
    }

    if (!executionInterface_) {
        LOG_WARN("PostTradeMonitor: no ExecutionInterface provided, creating one internally");
        auto ei = createExecutionInterface(config_);
        executionInterface_ = std::shared_ptr<ExecutionInterface>(ei.release());
    }

    // Initialize shared dependencies with actual interfaces
    // Use injected PositionTracker if provided (to avoid duplicate instances)
    if (injectedPositionTracker_) {
        positionTracker_ = injectedPositionTracker_;
    } else {
        positionTracker_ = std::make_shared<PositionTracker>(config_, dataInterface_);
    }
    stopLossManager_ = std::make_unique<StopLossManager>(config_, positionTracker_, dataInterface_, executionInterface_);
    drawdownMonitor_ = std::make_unique<DrawdownMonitor>(config_);

    monitorConfig_.healthCheckIntervalSeconds = std::chrono::seconds(5);
    monitorConfig_.metricsUpdateIntervalSeconds = std::chrono::seconds(10);
    monitorConfig_.reconciliationIntervalMinutes = config_.getBalanceReconciliationConfig().reconciliationFrequency;
    monitorConfig_.autoModeTransitions = true;
    monitorConfig_.enableBackgroundMonitoring = true;
    monitorConfig_.enableRealTimeAlerts = true;

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

    // Emit startup log for easier diagnostics in deployed logs
    if (kPostTradeTtlDebug) {
        LOG_INFO(formatString("[TTL_DEBUG] PostTradeMonitor initialized (enableBackgroundMonitoring={} enableTtlExtension={})", monitorConfig_.enableBackgroundMonitoring, monitorConfig_.enableTtlExtension));
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
    
    // Debug log to verify execution reports are received
    LOG_INFO(formatString("[TTL_DEBUG] processExecutionReport: symbol={} orderId={} status={}", 
             executionReport.symbol().toString(), 
             executionReport.orderId(),
             static_cast<int>(executionReport.status())));
    
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
    stats["ttl_exits"] = static_cast<double>(ttlExits_.load());
    
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

        // Initialize persistence (EventStore) if enabled in config
        try {
            const auto& pconf = config_.getPersistenceConfig();
            if (pconf.enabled && pconf.type == "sqlite") {
                // Map backpressure mode string to enum
                trading::risk::storage::EventStore::BackpressureMode mode = trading::risk::storage::EventStore::BackpressureMode::BLOCK;
                if (pconf.backpressureMode == "DROP_OLDEST") mode = trading::risk::storage::EventStore::BackpressureMode::DROP_OLDEST;
                else if (pconf.backpressureMode == "FAIL") mode = trading::risk::storage::EventStore::BackpressureMode::FAIL;

                // Create EventStore with configured queue capacity and mode (0 == unbounded)
                eventStore_ = std::make_unique<trading::risk::storage::EventStore>(pconf.sqlitePath, pconf.queueCapacity, mode);
                if (!eventStore_->initSchema()) {
                    LOG_WARN("PostTradeMonitor: EventStore schema initialization failed");
                }
            }
        } catch (...) {
            LOG_WARN("PostTradeMonitor: failed to initialize EventStore");
        }

        // Initialize webhook client if alerting configured
        try {
            const auto& aconf = config_.getAlertingConfig();
            if (aconf.enabled && !aconf.webhookEndpoints.empty()) {
                webhookClient_ = std::make_unique<trading::risk::alerting::WebhookClient>(aconf.webhookEndpoints);
            }
        } catch (...) {
            LOG_WARN("PostTradeMonitor: failed to initialize WebhookClient");
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

    // Persist reconciliation event if persistence configured
    try {
        if (eventStore_) {
            // Enqueue for background persistence to avoid blocking
            bool ok = eventStore_->enqueueReconciliationEvent(event);
            if (!ok) {
                LOG_WARN("PostTradeMonitor: EventStore queue full or enqueue failed");
                // fallback: try synchronous save (best-effort)
                try {
                    auto id = eventStore_->saveReconciliationEvent(event);
                    if (!id) LOG_WARN("PostTradeMonitor: fallback synchronous save failed");
                } catch (...) {
                    LOG_WARN("PostTradeMonitor: exception during fallback synchronous save");
                }
            }
        }
    } catch (...) {
        LOG_WARN("PostTradeMonitor: exception while enqueuing reconciliation event");
    }

    // Send alert via webhook if configured and threshold exceeded
    try {
        const auto& br = config_.getBalanceReconciliationConfig();
        const auto& aconf = config_.getAlertingConfig();
        if (webhookClient_ && aconf.enabled) {
            bool critical = std::abs(event.discrepancyPercentage) >= br.criticalDiscrepancyThreshold;
            if (monitorConfig_.enableRealTimeAlerts && (critical || br.alertOnDiscrepancy)) {
                // Build a compact JSON payload
                std::string payload = "{\"type\":\"reconciliation\",\"exchange\":\"" + event.exchangeId.toString() + "\",\"trading_pair\":\"" + event.tradingPair.toString() + "\",\"discrepancy_percent\":" + std::to_string(event.discrepancyPercentage) + ",\"critical\":" + (critical ? "true" : "false") + ",\"message\":\"" + event.message + "\"}";
                webhookClient_->send(payload);
            }
        }
    } catch (...) {
        LOG_WARN("PostTradeMonitor: exception while sending webhook alert");
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

    // Enforce position TTLs more frequently than other maintenance tasks
    try {
        if (kPostTradeTtlDebug) {
            LOG_INFO("[TTL_DEBUG] performScheduledTasks invoking enforcePositionTTL");
        }
        enforcePositionTTL();
    } catch (...) {
        // swallow errors to avoid thread exit
    }
    
    // Perform maintenance tasks
    static auto lastMaintenance = core::now();
    auto now = core::now();
    
    if (now - lastMaintenance >= std::chrono::minutes(10)) {
        performMaintenanceTasks();
        lastMaintenance = now;
    }
}

int64_t PostTradeMonitor::calculateDynamicTTL(double currentVolatility) {
    const int64_t BASE_TTL = 5000;  // 5 seconds base
    const int64_t MAX_TTL = 300000;  // 30 seconds max

    // Low volatility → longer TTL
    // High volatility → shorter TTL
    double multiplier = 1.0 / (1.0 + currentVolatility);
    int64_t ttl = static_cast<int64_t>(BASE_TTL * (1.0 + multiplier));
    return std::min(MAX_TTL, ttl);
}

void PostTradeMonitor::enforcePositionTTL() {
    if (!positionTracker_ || !dataInterface_) {
        LOG_INFO(formatString("[TTL_DEBUG] enforcePositionTTL early return: positionTracker_={} dataInterface_={}", 
                 (positionTracker_ ? "valid" : "null"), (dataInterface_ ? "valid" : "null")));
        return;
    }

    // Get snapshot of positions
    auto positions = positionTracker_->getAllPositions();
    
    LOG_INFO(formatString("[TTL_DEBUG] enforcePositionTTL: found {} positions", positions.size()));

    // Build set of currently active position keys (only non-zero quantity positions)
    std::unordered_set<std::string> currentKeys;
    int nonZeroCount = 0;
    for (const auto& p : positions) {
        try {
            double qty = p.getQuantity();
            LOG_INFO(formatString("[TTL_DEBUG] Position: {}:{} qty={}", 
                     p.getExchangeId().toString(), p.getTradingPair().toString(), qty));
            if (std::abs(qty) < 1e-8) continue;
            currentKeys.insert(p.getExchangeId().toString() + ":" + p.getTradingPair().toString());
            nonZeroCount++;
        } catch (...) {
            // ignore malformed entries
        }
    }
    
    LOG_INFO(formatString("[TTL_DEBUG] Non-zero positions: {}", nonZeroCount));

    // Clean up stale entries from positionsBeingClosed_ that are no longer present
    {
        std::unique_lock<std::shared_mutex> ul(positionsClosingMutex_);
        for (auto it = positionsBeingClosed_.begin(); it != positionsBeingClosed_.end(); ) {
            if (currentKeys.find(*it) == currentKeys.end()) {
                it = positionsBeingClosed_.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto now = core::now();

    for (const auto& pos : positions) {
        try {
            if (std::abs(pos.getQuantity()) < 1e-8) continue;

            // create a stable key for the position
            std::string key = pos.getExchangeId().toString() + ":" + pos.getTradingPair().toString();

            // Skip if already being closed
            {
                std::shared_lock<std::shared_mutex> sl(positionsClosingMutex_);
                if (positionsBeingClosed_.find(key) != positionsBeingClosed_.end()) continue;
            }

            // Determine TTL (may be dynamic)
            int64_t ttlMs = static_cast<int64_t>(monitorConfig_.defaultPositionTTL.count());
            if (monitorConfig_.enableDynamicTtl) {
                double vol = dataInterface_->getVolatility(pos.getTradingPair(), monitorConfig_.volatilityTimeframe, pos.getExchangeId().toString());
                if (vol > 0.0) {
                    ttlMs = calculateDynamicTTL(vol);
                }
            }

            // Ensure trackedPositions_ has an entry for this position
            {
                std::unique_lock<std::shared_mutex> tlock(trackedPositionsMutex_);
                auto it = trackedPositions_.find(key);
                if (it == trackedPositions_.end()) {
                    TrackedPosition tp;
                    tp.entryTime = pos.getLastUpdated();
                    tp.deadline = tp.entryTime + std::chrono::milliseconds(ttlMs);
                    tp.lastSignalSeen = tp.entryTime; // assume signal present at entry
                    tp.ttlMs = static_cast<double>(ttlMs);
                    trackedPositions_.emplace(key, std::move(tp));
                    if (kPostTradeTtlDebug) {
                        LOG_INFO(formatString("[TTL_DEBUG] Tracked position created: {} (entry={} ttlMs={})", key, tp.entryTime.time_since_epoch().count(), ttlMs));
                    }
                }
            }

            // Now fetch tracked info
            TrackedPosition tracked;
            {
                std::shared_lock<std::shared_mutex> tlock(trackedPositionsMutex_);
                tracked = trackedPositions_.at(key);
            }

            // Update lastSignalSeen using signalChecker_ if available
            bool hasBuySignal = false;
            {
                std::shared_lock<std::shared_mutex> scl(signalCheckerMutex_);
                if (signalChecker_) {
                    try {
                        hasBuySignal = signalChecker_(pos.getExchangeId(), pos.getTradingPair());
                    } catch (...) {
                        hasBuySignal = false;
                    }
                }
            }

            if (hasBuySignal) {
                // renew lastSignalSeen and extend deadline if desired
                std::unique_lock<std::shared_mutex> tlock(trackedPositionsMutex_);
                auto it = trackedPositions_.find(key);
                if (it != trackedPositions_.end()) {
                    it->second.lastSignalSeen = now;
                    // Sliding TTL policy: renew/extend deadline from 'now'
                    auto newDeadline = now + std::chrono::milliseconds(static_cast<int64_t>(ttlMs));
                    auto oldDeadline = it->second.deadline;
                    it->second.deadline = newDeadline;
                    it->second.ttlMs = static_cast<double>(ttlMs);
                    if (kPostTradeTtlDebug) {
                        LOG_INFO(formatString("[TTL_DEBUG] Renewed TTL for {}: old_deadline={} new_deadline={} ttlMs={}", key, oldDeadline.time_since_epoch().count(), newDeadline.time_since_epoch().count(), ttlMs));
                    }
                }
                continue; // still has signal -> do not exit
            }

            // If no signal and deadline passed -> attempt exit
            auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - tracked.entryTime).count();
            if (now >= tracked.deadline) {
                // Before attempting an exit, consult external recommendation (if provided)
                bool extendedByRecommendation = false;
                {
                    std::shared_lock<std::shared_mutex> erl(externalRecommendationMutex_);
                    if (externalRecommendationCb_) {
                        try {
                            auto recOpt = externalRecommendationCb_(pos.getExchangeId(), pos.getTradingPair());
                            if (recOpt) {
                                const auto& rec = *recOpt;
                                if (rec.recommended_action == "extend" && monitorConfig_.enableTtlExtension) {
                                    double stayProb = rec.stay_prob;
                                    double pTrend = rec.p_trend_up;
                                    // check thresholds
                                    if (stayProb >= monitorConfig_.ttlExtensionMinStayProb && pTrend >= monitorConfig_.ttlExtensionMinPTrend) {
                                        // enforce per-position extension cap
                                        std::unique_lock<std::shared_mutex> tlock(trackedPositionsMutex_);
                                        auto it = trackedPositions_.find(key);
                                        if (it != trackedPositions_.end()) {
                                            if (it->second.exitAttempts < monitorConfig_.ttlExtensionMaxPerPosition) {
                                                // compute extension cap in ms
                                                int capMin = rec.suggested_time_cap_min.value_or(monitorConfig_.ttlExtensionDefaultCapMin);
                                                auto extensionMs = std::chrono::milliseconds(static_cast<int64_t>(capMin) * 60 * 1000);
                                                // extend deadline from now by extensionMs (time-capped)
                                                auto newDeadline = now + extensionMs;
                                                it->second.deadline = newDeadline;
                                                it->second.exitAttempts += 1;
                                                it->second.ttlMs = static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(extensionMs).count());
                                                extendedByRecommendation = true;
                                                if (kPostTradeTtlDebug) {
                                                    LOG_INFO(formatString("[TTL_DEBUG] Extended TTL for {} by recommendation: stay_prob={} p_trend_up={} capMin={} new_deadline={} attempts={}", key, stayProb, pTrend, capMin, newDeadline.time_since_epoch().count(), it->second.exitAttempts));
                                                }
                                            } else {
                                                if (kPostTradeTtlDebug) {
                                                    LOG_INFO(formatString("[TTL_DEBUG] Not extending {}: per-position extension cap reached (attempts={})", key, it->second.exitAttempts));
                                                }
                                            }
                                        }
                                    } else {
                                        if (kPostTradeTtlDebug) {
                                            LOG_INFO(formatString("[TTL_DEBUG] Recommendation to extend ignored for {}: thresholds not met (stay_prob={} p_trend_up={})", key, stayProb, pTrend));
                                        }
                                    }
                                } else {
                                    if (kPostTradeTtlDebug) {
                                        LOG_INFO(formatString("[TTL_DEBUG] External recommendation for {} is '{}', not extending", key, rec.recommended_action));
                                    }
                                }
                            }
                        } catch (...) {
                            // ignore callback exceptions and fall through to exit
                        }
                    }
                }

                if (extendedByRecommendation) {
                    // We extended the TTL; do not exit now
                    continue;
                }
                // mark as being closed
                {
                    std::unique_lock<std::shared_mutex> ul(positionsClosingMutex_);
                    positionsBeingClosed_.insert(key);
                }

                // Prefer using stopLossManager to liquidate so we follow same plumbing
                bool exitRequested = false;
                if (stopLossManager_) {
                    safeComponentOperation(stopLossManager_, [&](auto& mgr) {
                        auto oid = mgr.liquidatePosition(pos.getExchangeId(), pos.getTradingPair());
                        exitRequested = oid.has_value();
                        return true;
                    });
                }

                // Fallback to execution interface if needed
                if (!exitRequested && executionInterface_) {
                    OrderRequest req;
                    req.exchangeId = pos.getExchangeId();
                    req.tradingPair = pos.getTradingPair();
                    req.quantity = std::abs(pos.getQuantity());
                    req.side = pos.getQuantity() > 0 ? core::Direction::SELL : core::Direction::BUY;
                    req.type = OrderType::MARKET;
                    req.timestamp = core::now();
                    // Submit order
                    try {
                        auto orderId = executionInterface_->submitOrder(req);
                        exitRequested = !orderId.empty();
                        if (kPostTradeTtlDebug) {
                            LOG_DEBUG(formatString("[TTL_DEBUG] submitOrder called for {} result_order_id='{}'", key, orderId));
                        }
                    } catch (...) {
                        exitRequested = false;
                        if (kPostTradeTtlDebug) {
                            LOG_DEBUG(formatString("[TTL_DEBUG] submitOrder threw exception for {}", key));
                        }
                    }
                }

                // Record metric and event
                if (exitRequested) {
                    ttlExits_.fetch_add(1);

                    RiskEvent event(
                        RiskEventType::EMERGENCY_ACTION,
                        formatString("TTL exit triggered for {}:{} after {} ms (ttl={})", pos.getExchangeId().toString(), pos.getTradingPair().toString(), ageMs, ttlMs),
                        "info"
                    );
                    notifyRiskEvent(event);
                    if (kPostTradeTtlDebug) {
                        LOG_DEBUG(formatString("[TTL_DEBUG] TTL exit recorded for {} (ageMs={} ttlMs={} )", key, ageMs, ttlMs));
                    }
                }

                // Note: positionsBeingClosed_ will be cleaned up when position disappears from snapshot or on next reconcile
            }
        } catch (...) {
            // ignore per-position errors
        }
    }
}

void PostTradeMonitor::registerSignalChecker(const SignalChecker& checker) {
    std::unique_lock<std::shared_mutex> lock(signalCheckerMutex_);
    signalChecker_ = checker;
}

void PostTradeMonitor::registerExternalSignalChecker(const ExternalSignalChecker& checker) {
    // Forward to the private implementation
    registerSignalChecker(static_cast<const SignalChecker&>(checker));
}

void PostTradeMonitor::registerExternalRecommendationCallback(const ExternalRecommendationCallback& cb) {
    std::unique_lock<std::shared_mutex> lock(externalRecommendationMutex_);
    externalRecommendationCb_ = cb;
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
