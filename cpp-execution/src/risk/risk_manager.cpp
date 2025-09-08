/**
 * Implementation of the risk manager
 *
 * File: risk_manager.cpp
 * Created: 2025-04-29
 */

 #include "risk_manager.hpp"
 #include <iostream>
 #include <sstream>
 #include <chrono>
 
 namespace trading {
 namespace risk {
 
 RiskManager::RiskManager(const RiskConfig& config)
     : m_config(config)
     , m_mode(RiskManagerMode::NORMAL)
 {
 }
 
 RiskManager::~RiskManager() {
     // Ensure clean shutdown
     try {
         // Stop any running services
         if (m_riskService) {
             m_riskService->stop();
         }
     } catch (const std::exception& e) {
         std::cerr << "Exception during RiskManager destruction: " << e.what() << std::endl;
     }
 }
 
 bool RiskManager::initialize() {
     try {
         // Load configuration
         loadConfiguration();
         
         // Initialize portfolio manager first (needed by other components)
         m_portfolioManager = std::make_unique<PortfolioManager>(m_config);
         if (!m_portfolioManager->initialize()) {
             std::cerr << "Failed to initialize portfolio manager" << std::endl;
             return false;
         }
         
         // Initialize pre-trade filter
         m_preTradeFilter = std::make_unique<PreTradeRiskFilter>(m_config, m_portfolioManager.get());
         if (!m_preTradeFilter->initialize()) {
             std::cerr << "Failed to initialize pre-trade risk filter" << std::endl;
             return false;
         }
         
         // Initialize post-trade monitor
         m_postTradeMonitor = std::make_unique<PostTradeMonitor>(m_config, m_portfolioManager.get());
         if (!m_postTradeMonitor->initialize()) {
             std::cerr << "Failed to initialize post-trade monitor" << std::endl;
             return false;
         }
         
         // Initialize external interfaces
         m_strategyInterface = std::make_unique<StrategyInterface>(m_config);
         m_executionInterface = std::make_unique<ExecutionInterface>(m_config);
         m_dataInterface = std::make_unique<DataInterface>(m_config);
         
         if (!m_strategyInterface->initialize() || 
             !m_executionInterface->initialize() || 
             !m_dataInterface->initialize()) {
             std::cerr << "Failed to initialize external interfaces" << std::endl;
             return false;
         }
         
         // Initialize risk service
         m_riskService = std::make_unique<RiskService>(m_config);
         
         // Register callbacks
         auto self = shared_from_this(); // Shared pointer to this instance
         auto validationService = std::make_shared<RiskValidationService>(m_config, self);
         
         if (!validationService->initialize()) {
             std::cerr << "Failed to initialize validation service" << std::endl;
             return false;
         }
         
         m_riskService->setRiskValidationService(validationService);
         
         if (!m_riskService->initialize() || !m_riskService->start()) {
             std::cerr << "Failed to initialize or start risk service" << std::endl;
             return false;
         }
         
         // Set up event handlers
         m_postTradeMonitor->registerRiskEventCallback([this](const RiskEvent& event) {
             notifyRiskEvent(event);
         });
         
         // Set up periodic system health check
         // This would be set up with a timer in production code
         
         return true;
     } catch (const std::exception& e) {
         std::cerr << "Exception during RiskManager initialization: " << e.what() << std::endl;
         return false;
     }
 }
 
 ValidationResult RiskManager::validateSignal(const TradingSignal& signal) {
     // First check if the system is in a valid state for trading
     if (m_mode != RiskManagerMode::NORMAL && m_mode != RiskManagerMode::HEIGHTENED_RISK) {
         ValidationResult result;
         result.signalId = signal.id;
         result.approved = false;
         
         RiskViolation violation;
         violation.violationType = "SYSTEM_MODE";
         violation.description = "System is not in trading mode";
         result.violations.push_back(violation);
         result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
         
         return result;
     }
     
     // Execute pre-trade risk checks
     if (m_preTradeFilter) {
         return m_preTradeFilter->validateSignal(signal);
     }
     
     // If pre-trade filter is not available, reject the signal
     ValidationResult result;
     result.signalId = signal.id;
     result.approved = false;
     
     RiskViolation violation;
     violation.violationType = "SYSTEM_ERROR";
     violation.description = "Pre-trade risk filter unavailable";
     result.violations.push_back(violation);
     result.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
     
     return result;
 }
 
 void RiskManager::processExecutionReport(const ExecutionReport& executionReport) {
     try {
         // Update portfolio with execution information
         if (m_portfolioManager) {
             m_portfolioManager->processExecution(executionReport);
         }
         
         // Pass to post-trade monitoring
         if (m_postTradeMonitor) {
             m_postTradeMonitor->processExecutionReport(executionReport);
         }
     } catch (const std::exception& e) {
         std::cerr << "Exception during execution report processing: " << e.what() << std::endl;
         
         // Create and notify risk event for the error
         RiskEvent event;
         event.type = RiskEventType::SYSTEM_ERROR;
         event.severity = RiskEventSeverity::HIGH;
         event.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
         event.description = std::string("Error processing execution report: ") + e.what();
         
         notifyRiskEvent(event);
     }
 }
 
 void RiskManager::updateMarketData(const MarketData& marketData) {
     try {
         // Update portfolio with market data
         if (m_portfolioManager) {
             m_portfolioManager->updateMarketData(marketData);
         }
         
         // Pass to post-trade monitoring for real-time risk assessment
         if (m_postTradeMonitor) {
             m_postTradeMonitor->updateMarketData(marketData);
         }
     } catch (const std::exception& e) {
         std::cerr << "Exception during market data update: " << e.what() << std::endl;
         
         // Create and notify risk event for the error
         RiskEvent event;
         event.type = RiskEventType::SYSTEM_ERROR;
         event.severity = RiskEventSeverity::MEDIUM;
         event.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
         event.description = std::string("Error updating market data: ") + e.what();
         
         notifyRiskEvent(event);
     }
 }
 
 RiskManagerMode RiskManager::getMode() const {
     return m_mode.load();
 }
 
 void RiskManager::setMode(RiskManagerMode mode) {
     RiskManagerMode oldMode = m_mode.exchange(mode);
     
     // Notify about mode change if it actually changed
     if (oldMode != mode) {
         RiskEvent event;
         event.type = RiskEventType::MODE_CHANGE;
         event.severity = RiskEventSeverity::MEDIUM;
         event.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
         
         std::stringstream ss;
         ss << "Risk manager mode changed from " << static_cast<int>(oldMode) 
            << " to " << static_cast<int>(mode);
         event.description = ss.str();
         
         notifyRiskEvent(event);
         
         // Take specific actions based on new mode
         if (mode == RiskManagerMode::EMERGENCY) {
             // Execute emergency procedures
             emergencyAction("Entered emergency mode");
         }
     }
 }
 
 RiskParameters RiskManager::getRiskParameters(const ExchangeId& exchangeId, 
                                           const TradingPair& tradingPair,
                                           const StrategyId& strategyId) const {
     if (m_preTradeFilter) {
         return m_preTradeFilter->getRiskParameters(exchangeId, tradingPair, strategyId);
     }
     
     // Return empty parameters if pre-trade filter is not available
     return RiskParameters();
 }
 
 bool RiskManager::updateRiskParameters(const RiskParameters& params) {
     if (m_preTradeFilter) {
         return m_preTradeFilter->updateRiskParameters(params);
     }
     return false;
 }
 
 void RiskManager::registerRiskEventCallback(const RiskEventCallback& callback) {
     std::lock_guard<std::mutex> lock(m_callbackMutex);
     m_eventCallbacks.push_back(callback);
 }
 
 bool RiskManager::reconcilePositions(const std::optional<ExchangeId>& exchangeId) {
     if (m_portfolioManager) {
         return m_portfolioManager->reconcilePositions(exchangeId);
     }
     return false;
 }
 
 PerformanceMetrics RiskManager::getPerformanceMetrics() const {
     if (m_postTradeMonitor) {
         return m_postTradeMonitor->getPerformanceMetrics();
     }
     
     // Return empty metrics if post-trade monitor is not available
     return PerformanceMetrics();
 }
 
 bool RiskManager::emergencyAction(const std::string& reason) {
     try {
         std::cout << "EXECUTING EMERGENCY ACTIONS: " << reason << std::endl;
         
         // Create emergency event
         RiskEvent event;
         event.type = RiskEventType::EMERGENCY_ACTION;
         event.severity = RiskEventSeverity::CRITICAL;
         event.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
         event.description = "Emergency action triggered: " + reason;
         
         notifyRiskEvent(event);
         
         // 1. Cancel all open orders
         if (m_executionInterface) {
             m_executionInterface->cancelAllOrders("Emergency action");
         }
         
         // 2. Disable new order submission
         setMode(RiskManagerMode::EMERGENCY);
         
         // 3. Notify strategy engine
         if (m_strategyInterface) {
             m_strategyInterface->notifyEmergencyState(reason);
         }
         
         return true;
     } catch (const std::exception& e) {
         std::cerr << "Exception during emergency action: " << e.what() << std::endl;
         return false;
     }
 }
 
 void RiskManager::notifyRiskEvent(const RiskEvent& event) {
     std::lock_guard<std::mutex> lock(m_callbackMutex);
     for (const auto& callback : m_eventCallbacks) {
         callback(event);
     }
 }
 
 bool RiskManager::checkSystemHealth() {
     // Check if all components are functioning properly
     bool portfolioManagerHealthy = m_portfolioManager && m_portfolioManager->isHealthy();
     bool preTradeFilterHealthy = m_preTradeFilter && m_preTradeFilter->isHealthy();
     bool postTradeMonitorHealthy = m_postTradeMonitor && m_postTradeMonitor->isHealthy();
     
     // Check external interfaces
     bool strategyInterfaceHealthy = m_strategyInterface && m_strategyInterface->isConnected();
     bool executionInterfaceHealthy = m_executionInterface && m_executionInterface->isConnected();
     bool dataInterfaceHealthy = m_dataInterface && m_dataInterface->isConnected();
     
     // Check if service is running
     bool serviceHealthy = m_riskService && m_riskService->isRunning();
     
     // Overall health status
     bool systemHealthy = portfolioManagerHealthy && preTradeFilterHealthy && postTradeMonitorHealthy && 
                          strategyInterfaceHealthy && executionInterfaceHealthy && dataInterfaceHealthy && 
                          serviceHealthy;
     
     // If system is unhealthy but not in emergency or maintenance mode, trigger emergency
     if (!systemHealthy && m_mode != RiskManagerMode::EMERGENCY && m_mode != RiskManagerMode::MAINTENANCE) {
         std::stringstream ss;
         ss << "System health check failed - Components status: "
            << "Portfolio Manager: " << (portfolioManagerHealthy ? "OK" : "FAIL") << ", "
            << "Pre-Trade Filter: " << (preTradeFilterHealthy ? "OK" : "FAIL") << ", "
            << "Post-Trade Monitor: " << (postTradeMonitorHealthy ? "OK" : "FAIL") << ", "
            << "Strategy Interface: " << (strategyInterfaceHealthy ? "OK" : "FAIL") << ", "
            << "Execution Interface: " << (executionInterfaceHealthy ? "OK" : "FAIL") << ", "
            << "Data Interface: " << (dataInterfaceHealthy ? "OK" : "FAIL") << ", "
            << "Service: " << (serviceHealthy ? "OK" : "FAIL");
         
         emergencyAction(ss.str());
     }
     
     return systemHealthy;
 }
 
 void RiskManager::loadConfiguration() {
     // Load specific parameters for risk components from configuration
     
     // Example of parameter loading for pre-trade risk filter
     if (m_config.hasKey("risk.position_limits.enable")) {
         bool enablePositionLimits = m_config.getBoolValue("risk.position_limits.enable");
         // Use the parameter...
     }
     
     // Example of parameter loading for post-trade monitor
     if (m_config.hasKey("risk.circuit_breaker.enable")) {
         bool enableCircuitBreaker = m_config.getBoolValue("risk.circuit_breaker.enable");
         // Use the parameter...
     }
     
     // Load default mode
     if (m_config.hasKey("risk.default_mode")) {
         int defaultMode = m_config.getIntValue("risk.default_mode");
         m_mode.store(static_cast<RiskManagerMode>(defaultMode));
     }
 }
 
 } // namespace risk
 } // namespace trading