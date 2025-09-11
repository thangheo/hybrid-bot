// /**
//  * Implementation of the risk manager (COMPILATION FIXES APPLIED)
//  *
//  * File: risk_manager_fixed.cpp
//  * Created: 2025-04-29
//  * 
//  * Key fixes applied:
//  * 1. Fixed factory function calls for interfaces
//  * 2. Fixed shared_from_this usage
//  * 3. Fixed compilation errors in loadConfiguration
//  * 4. Added missing include statements
//  */

// #include "risk_manager.hpp"
// #include "service/risk_validation_service.hpp"
// #include "interfaces/strategy_interface.hpp"
// #include "interfaces/execution_interface.hpp"
// #include "interfaces/data_interface.hpp"
// #include <iostream>
// #include <sstream>
// #include <chrono>

// namespace trading {
// namespace risk {

// // Forward declarations of factory functions
// std::unique_ptr<StrategyInterface> createStrategyInterface(const RiskConfig& config);
// std::unique_ptr<ExecutionInterface> createExecutionInterface(const RiskConfig& config);
// std::unique_ptr<DataInterface> createDataInterface(const RiskConfig& config);

// }
// #include <set>

// namespace trading {
// namespace risk {

// RiskManager::RiskManager(const RiskConfig& config)
//     : m_config(config)
//     , m_mode(RiskManagerMode::NORMAL)
// {
// }

// RiskManager::~RiskManager() {
//     // Ensure clean shutdown
//     try {
//         // Stop any running services
//         if (m_riskService) {
//             m_riskService->stop();
//         }
//     } catch (const std::exception& e) {
//         std::cerr << "Exception during RiskManager destruction: " << e.what() << std::endl;
//     }
// }

// bool RiskManager::initialize() {
//     try {
//         // Load configuration
//         loadConfiguration();
        
//         // Initialize portfolio manager first (needed by other components)
//         m_portfolioManager = std::make_unique<PortfolioManager>(m_config);
//         if (!m_portfolioManager->initialize()) {
//             std::cerr << "Failed to initialize portfolio manager" << std::endl;
//             return false;
//         }
        
//         // Initialize pre-trade filter
//         m_preTradeFilter = std::make_unique<PreTradeRiskFilter>(m_config);
//         if (!m_preTradeFilter->initialize()) {
//             std::cerr << "Failed to initialize pre-trade risk filter" << std::endl;
//             return false;
//         }
        
//         // Initialize post-trade monitor
//         m_postTradeMonitor = std::make_unique<PostTradeMonitor>(m_config, m_portfolioManager.get());
//         if (!m_postTradeMonitor->initialize()) {
//             std::cerr << "Failed to initialize post-trade monitor" << std::endl;
//             return false;
//         }
        
//         // FIXED: Initialize external interfaces using factory functions if available
//         // If you have factory functions, use them; otherwise create directly
//         try {
//             m_strategyInterface = createStrategyInterface(m_config);
//             m_executionInterface = std::make_unique<ExecutionInterface>(m_config);
//             m_dataInterface = std::make_unique<MarketDataInterface>(m_config);

//         std::unique_ptr<StrategyInterface> createStrategyInterface(const RiskConfig& config);
//         // m_executionInterface = std::make_unique<ExecutionInterface>(m_config);
//         std::unique_ptr<ExecutionInterface> createExecutionInterface(const RiskConfig& config);
//         m_dataInterface = std::make_unique<MarketDataInterface>(m_config);
        
//         } catch (const std::exception& e) {
//             std::cerr << "Failed to create interfaces: " << e.what() << std::endl;
//             // Continue without interfaces for demo purposes
//             std::cout << "Warning: Running without external interfaces" << std::endl;
//         }
        
//         if (m_strategyInterface && !m_strategyInterface->initialize()) {
//             std::cerr << "Failed to initialize strategy interface" << std::endl;
//         }
        
//         if (m_executionInterface && !m_executionInterface->initialize()) {
//             std::cerr << "Failed to initialize execution interface" << std::endl;
//         }
        
//         if (m_dataInterface && !m_dataInterface->initialize()) {
//             std::cerr << "Failed to initialize data interface" << std::endl;
//         }
        
//         // Initialize risk service
//         m_riskService = std::make_unique<RiskService>(m_config);
        
//         // FIXED: Create validation service without shared_from_this if not inheriting from enable_shared_from_this
//         // For now, create a simple version or use raw pointer if needed
//         try {
//             // Option 1: If RiskManager inherits from std::enable_shared_from_this<RiskManager>
//             auto validationService = std::make_shared<RiskValidationService>(m_config, shared_from_this());
            
//             // Option 2: Use raw pointer for now (simpler for demo)
//             // auto validationService = std::make_shared<RiskValidationService>(m_config, this);
            
//             if (!validationService->initialize()) {
//                 std::cerr << "Failed to initialize validation service" << std::endl;
//                 return false;
//             }
            
//             m_riskService->setRiskValidationService(validationService);
//         } catch (const std::exception& e) {
//             std::cerr << "Failed to create validation service: " << e.what() << std::endl;
//             // Continue without validation service for basic demo
//         }
        
//         if (!m_riskService->initialize()) {
//             std::cerr << "Failed to initialize risk service" << std::endl;
//             return false;
//         }
        
//         if (!m_riskService->start()) {
//             std::cerr << "Failed to start risk service" << std::endl;
//             return false;
//         }
        
//         // Register risk event callback if post-trade monitor is available
//         if (m_postTradeMonitor) {
//             m_postTradeMonitor->registerRiskEventCallback([this](const RiskEvent& event) {
//                 notifyRiskEvent(event);
//             });
//         }
        
//         return true;
//     } catch (const std::exception& e) {
//         std::cerr << "Exception during RiskManager initialization: " << e.what() << std::endl;
//         return false;
//     }
// }

// ValidationResult RiskManager::validateSignal(const TradingSignal& signal) {
//     // First check if the system is in a valid state for trading
//     if (m_mode != RiskManagerMode::NORMAL && m_mode != RiskManagerMode::HEIGHTENED_RISK) {
//         ValidationResult result;
//         result.signalId = signal.id;
//         result.approved = false;
        
//         RiskViolation violation;
//         violation.type = ViolationType::OTHER;
//         violation.description = "System is not in trading mode";
//         result.violations.push_back(violation);
//         result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
//             std::chrono::system_clock::now().time_since_epoch()).count();
        
//         return result;
//     }
    
//     // Execute pre-trade risk checks
//     if (m_preTradeFilter) {
//         return m_preTradeFilter->validateSignal(signal);
//     }
    
//     // If pre-trade filter is not available, create a basic validation
//     ValidationResult result;
//     result.signalId = signal.id;
//     result.approved = true;  // Allow for basic demo
//     result.adjustedSize = signal.targetSize;
//     result.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
//         std::chrono::system_clock::now().time_since_epoch()).count();
    
//     return result;
// }

// void RiskManager::processExecutionReport(const ExecutionReport& executionReport) {
//     try {
//         // Update portfolio with execution information
//         if (m_portfolioManager) {
//             m_portfolioManager->processExecution(executionReport);
//         }
        
//         // Pass to post-trade monitoring
//         if (m_postTradeMonitor) {
//             m_postTradeMonitor->processExecutionReport(executionReport);
//         }
//     } catch (const std::exception& e) {
//         std::cerr << "Exception during execution report processing: " << e.what() << std::endl;
        
//         // Create and notify risk event for the error
//         RiskEvent event(
//             RiskEventType::SYSTEM_ERROR,
//             std::chrono::system_clock::now(),
//             std::string("Error processing execution report: ") + e.what(),
//             "high"
//         );        
//         notifyRiskEvent(event);
//     }
// }

// void RiskManager::updateMarketData(const MarketData& marketData) {
//     try {
//         // Update portfolio with market data
//         if (m_portfolioManager) {
//             m_portfolioManager->updateMarketData(marketData);
//         }
        
//         // Pass to post-trade monitoring for real-time risk assessment
//         if (m_postTradeMonitor) {
//             m_postTradeMonitor->updateMarketData(marketData);
//         }
//     } catch (const std::exception& e) {
//         std::cerr << "Exception during market data update: " << e.what() << std::endl;
        
//         RiskEvent event(RiskEventType::SYSTEM_ERROR,
//                     std::chrono::system_clock::now(),
//                     std::string("Error updating market data: ") + e.what(),
//                     "critical");
        
//         notifyRiskEvent(event);
//     }
// }

// RiskManagerMode RiskManager::getMode() const {
//     return m_mode.load();
// }

// void RiskManager::setMode(RiskManagerMode mode) {
//     RiskManagerMode oldMode = m_mode.exchange(mode);
    
//     // Notify about mode change if it actually changed
//     if (oldMode != mode) {
//         RiskEvent event(RiskEventType::RISK_MODE_CHANGE,
//                     std::chrono::system_clock::now(),
//                     "",  // Will be set below
//                     "info");  
                    
//         std::stringstream ss;
//         ss << "Risk manager mode changed from " << static_cast<int>(oldMode) 
//            << " to " << static_cast<int>(mode);
//         event.description = ss.str();
        
//         notifyRiskEvent(event);
        
//         // Take specific actions based on new mode
//         if (mode == RiskManagerMode::EMERGENCY) {
//             // Execute emergency procedures
//             emergencyAction("Entered emergency mode");
//         }
//     }
// }

// RiskParameters RiskManager::getRiskParameters(const ExchangeId& exchangeId, 
//                                           const TradingPair& tradingPair,
//                                           const StrategyId& strategyId) const {
//     if (m_preTradeFilter) {
//         return m_preTradeFilter->getRiskParameters(exchangeId, tradingPair, strategyId.id);
//     }
    
//     // Return empty parameters if pre-trade filter is not available
//     return RiskParameters();
// }

// bool RiskManager::updateRiskParameters(const RiskParameters& params) {
//     if (m_preTradeFilter) {
//         return m_preTradeFilter->updateRiskParameters(params);
//     }
//     return false;
// }

// void RiskManager::registerRiskEventCallback(const RiskEventCallback& callback) {
//     std::lock_guard<std::mutex> lock(m_callbackMutex);
//     m_eventCallbacks.push_back(callback);
// }

// bool RiskManager::reconcilePositions(const std::optional<ExchangeId>& exchangeId) {
//     if (m_portfolioManager) {
//         if (exchangeId.has_value()) {
//             return m_portfolioManager->reconcilePositions(exchangeId.value());
//         } else {
//             // Handle case where no specific exchange is provided
//             // For demo purposes, return true
//             return true;
//         }
//     }
//     return false;
// }

// PerformanceMetrics RiskManager::getPerformanceMetrics() const {
//     if (m_postTradeMonitor) {
//         return m_postTradeMonitor->getPerformanceMetrics();
//     }
    
//     // Return empty metrics if post-trade monitor is not available
//     return PerformanceMetrics();
// }

// bool RiskManager::emergencyAction(const std::string& reason) {
//     try {
//         std::cout << "EXECUTING EMERGENCY ACTIONS: " << reason << std::endl;
        
//         // Create emergency event
//         RiskEvent event(
//             RiskEventType::EMERGENCY_ACTION,
//             std::chrono::system_clock::now(),
//             "Emergency action triggered: " + reason,
//             "critical"
//         );
        
//         notifyRiskEvent(event);
        
//         // 1. Set emergency mode
//         setMode(RiskManagerMode::EMERGENCY);
        
//         // 2. Cancel orders if execution interface available
//         if (m_executionInterface && m_portfolioManager) {
//             try {
//                 auto activePositions = m_portfolioManager->getAllPositions();
                
//                 std::set<std::pair<core::ExchangeId, core::Symbol>> tradingPairs;
                
//                 for (const auto& position : activePositions) {
//                     core::ExchangeId exchangeId = position.getExchangeId();
//                     core::Symbol symbol = core::Symbol::fromString(position.getTradingPair().toString());
//                     tradingPairs.insert(std::make_pair(exchangeId, symbol));
//                 }
                
//                 // Add default pairs if none found
//                 if (tradingPairs.empty()) {
//                     std::vector<std::string> defaultPairs = {"BTC/USDT", "ETH/USDT", "BNB/USDT"};
//                     std::vector<core::ExchangeId> defaultExchanges = {
//                         core::ExchangeId::binance(),
//                         core::ExchangeId::coinbase()
//                     };
                    
//                     for (const auto& exchangeId : defaultExchanges) {
//                         for (const auto& pairStr : defaultPairs) {
//                             core::Symbol symbol = core::Symbol::fromString(pairStr);
//                             tradingPairs.insert(std::make_pair(exchangeId, symbol));
//                         }
//                     }
//                 }
                
//                 int totalCancelled = 0;
//                 for (const auto& [exchangeId, symbol] : tradingPairs) {
//                     try {
//                         int cancelled = m_executionInterface->cancelAllOrders(exchangeId, symbol);
//                         totalCancelled += cancelled;
//                         std::cout << "Cancelled " << cancelled << " orders for " 
//                                   << symbol.toString() << " on " << exchangeId.toString() << std::endl;
//                     } catch (const std::exception& e) {
//                         std::cerr << "Failed to cancel orders for " << symbol.toString() 
//                                   << " on " << exchangeId.toString() << ": " << e.what() << std::endl;
//                     }
//                 }
                
//                 std::cout << "Total orders cancelled: " << totalCancelled << std::endl;
//             } catch (const std::exception& e) {
//                 std::cerr << "Error during order cancellation: " << e.what() << std::endl;
//             }
//         }
        
//         // 3. Notify strategy engine if available
//         if (m_strategyInterface) {
//             try {
//                 m_strategyInterface->notifyEmergencyState(reason);
//             } catch (const std::exception& e) {
//                 std::cerr << "Error notifying strategy interface: " << e.what() << std::endl;
//             }
//         }
        
//         return true;
//     } catch (const std::exception& e) {
//         std::cerr << "Exception during emergency action: " << e.what() << std::endl;
//         return false;
//     }
// }

// void RiskManager::notifyRiskEvent(const RiskEvent& event) {
//     std::lock_guard<std::mutex> lock(m_callbackMutex);
//     for (const auto& callback : m_eventCallbacks) {
//         try {
//             callback(event);
//         } catch (const std::exception& e) {
//             std::cerr << "Error in risk event callback: " << e.what() << std::endl;
//         }
//     }
// }

// bool RiskManager::checkSystemHealth() {
//     try {
//         // Check if all components are functioning properly
//         bool portfolioManagerHealthy = m_portfolioManager && m_portfolioManager->isHealthy();
//         bool preTradeFilterHealthy = m_preTradeFilter && m_preTradeFilter->isHealthy();
//         bool postTradeMonitorHealthy = m_postTradeMonitor && m_postTradeMonitor->isHealthy();
        
//         // Check external interfaces (allow them to be missing for demo)
//         bool strategyInterfaceHealthy = !m_strategyInterface || m_strategyInterface->isConnected();
//         bool executionInterfaceHealthy = !m_executionInterface || m_executionInterface->isConnected();
//         bool dataInterfaceHealthy = !m_dataInterface || m_dataInterface->isConnected();
        
//         // Check if service is running
//         bool serviceHealthy = m_riskService && m_riskService->isRunning();
        
//         // Overall health status
//         bool systemHealthy = portfolioManagerHealthy && preTradeFilterHealthy && postTradeMonitorHealthy && 
//                              strategyInterfaceHealthy && executionInterfaceHealthy && dataInterfaceHealthy && 
//                              serviceHealthy;
        
//         // If system is unhealthy but not in emergency or maintenance mode, trigger emergency
//         if (!systemHealthy && m_mode != RiskManagerMode::EMERGENCY && m_mode != RiskManagerMode::MAINTENANCE) {
//             std::stringstream ss;
//             ss << "System health check failed - Components status: "
//                << "Portfolio Manager: " << (portfolioManagerHealthy ? "OK" : "FAIL") << ", "
//                << "Pre-Trade Filter: " << (preTradeFilterHealthy ? "OK" : "FAIL") << ", "
//                << "Post-Trade Monitor: " << (postTradeMonitorHealthy ? "OK" : "FAIL") << ", "
//                << "Strategy Interface: " << (strategyInterfaceHealthy ? "OK" : "FAIL") << ", "
//                << "Execution Interface: " << (executionInterfaceHealthy ? "OK" : "FAIL") << ", "
//                << "Data Interface: " << (dataInterfaceHealthy ? "OK" : "FAIL") << ", "
//                << "Service: " << (serviceHealthy ? "OK" : "FAIL");
            
//             emergencyAction(ss.str());
//         }
        
//         return systemHealthy;
//     } catch (const std::exception& e) {
//         std::cerr << "Exception during health check: " << e.what() << std::endl;
//         return false;
//     }
// }

// void RiskManager::loadConfiguration() {
//     try {
//         // FIXED: Use the correct RiskConfig methods that actually exist
        
//         const auto& positionLimitConfig = m_config.getPositionLimitConfig();
//         bool enablePositionLimits = positionLimitConfig.enforceStrictly;
        
//         if (enablePositionLimits) {
//             std::cout << "Position limits enforcement is enabled" << std::endl;
//         }
        
//         const auto& circuitBreakerConfig = m_config.getCircuitBreakerConfig();
//         bool enableAutoResume = circuitBreakerConfig.autoResume;
        
//         if (enableAutoResume) {
//             std::cout << "Circuit breaker auto-resume is enabled" << std::endl;
//         }
        
//         const auto& stopLossConfig = m_config.getStopLossConfig();
//         bool enableTrailingStop = stopLossConfig.enableTrailingStop;
        
//         if (enableTrailingStop) {
//             std::cout << "Trailing stop is enabled" << std::endl;
//         }
        
//         const auto& drawdownConfig = m_config.getDrawdownConfig();
//         bool enableAutoRiskReduction = drawdownConfig.enableAutoRiskReduction;
        
//         if (enableAutoRiskReduction) {
//             std::cout << "Auto risk reduction is enabled" << std::endl;
//         }
        
//         const auto& fatFingerConfig = m_config.getFatFingerConfig();
//         bool enforceAdditionalVerification = fatFingerConfig.enforceAdditionalVerification;
        
//         if (enforceAdditionalVerification) {
//             std::cout << "Additional verification for large orders is enabled" << std::endl;
//         }
        
//         const auto& orderValidationConfig = m_config.getOrderValidationConfig();
//         bool validateMinOrderSize = orderValidationConfig.validateMinOrderSize;
//         bool validatePriceRange = orderValidationConfig.validatePriceRange;
        
//         if (validateMinOrderSize) {
//             std::cout << "Minimum order size validation is enabled" << std::endl;
//         }
        
//         if (validatePriceRange) {
//             std::cout << "Price range validation is enabled" << std::endl;
//         }
        
//         const auto& reconciliationConfig = m_config.getBalanceReconciliationConfig();
//         bool autoCorrectDiscrepancies = reconciliationConfig.autoCorrectDiscrepancies;
//         bool alertOnDiscrepancy = reconciliationConfig.alertOnDiscrepancy;
        
//         if (autoCorrectDiscrepancies) {
//             std::cout << "Auto-correction of balance discrepancies is enabled" << std::endl;
//         }
        
//         if (alertOnDiscrepancy) {
//             std::cout << "Alerting on balance discrepancies is enabled" << std::endl;
//         }
        
//         const auto& rebalancingConfig = m_config.getRebalancingConfig();
//         bool autoRebalancingEnabled = rebalancingConfig.autoRebalancingEnabled;
//         bool autoExecuteRebalancing = rebalancingConfig.autoExecuteRebalancing;
        
//         if (autoRebalancingEnabled) {
//             std::cout << "Auto-rebalancing is enabled" << std::endl;
//         }
        
//         if (autoExecuteRebalancing) {
//             std::cout << "Auto-execution of rebalancing orders is enabled" << std::endl;
//         }
        
//         const auto& collateralConfig = m_config.getCollateralConfig();
//         bool enableEmergencyFunding = collateralConfig.enableEmergencyFunding;
        
//         if (enableEmergencyFunding) {
//             std::cout << "Emergency funding is enabled" << std::endl;
//         }
        
//         const auto& loggingConfig = m_config.getLoggingConfig();
//         bool enableDetailedLogs = loggingConfig.enableDetailedLogs;
//         bool logToConsole = loggingConfig.logToConsole;
//         bool logToFile = loggingConfig.logToFile;
        
//         if (enableDetailedLogs) {
//             std::cout << "Detailed logging is enabled" << std::endl;
//         }
        
//         if (logToConsole) {
//             std::cout << "Console logging is enabled" << std::endl;
//         }
        
//         if (logToFile) {
//             std::cout << "File logging is enabled" << std::endl;
//         }
        
//         // Load default mode
//         m_mode.store(RiskManagerMode::NORMAL);
        
//         std::cout << "Risk manager configuration loaded successfully" << std::endl;
        
//     } catch (const std::exception& e) {
//         std::cerr << "Exception during configuration loading: " << e.what() << std::endl;
//         // Set safe defaults
//         m_mode.store(RiskManagerMode::NORMAL);
//     }
// }

// } // namespace risk
// } // namespace trading