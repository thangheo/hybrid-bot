/**
 * Implementation of the risk manager (FIXED VERSION)
 *
 * File: risk_manager.cpp
 * Created: 2025-04-29
 */

#include "risk_manager.hpp"
#include "service/risk_validation_service.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <set>
namespace trading {
namespace risk {

// RiskManager::RiskManager(std::shared_ptr<trading::execution::ExecutionService> execService, const RiskConfig& config)
//     : m_config(config)
//     , m_mode(RiskManagerMode::NORMAL)
// {
//     // Initialize components
//     m_strategyInterface = createStrategyInterface(m_config);
//     LOG_INFO("RiskManager", "Created StrategyInterface");
    
//     m_executionInterface = createExecutionInterface_withExecutionService(execService);
//     if (!m_executionInterface) {
//         LOG_ERROR("RiskManager", "Failed to create ExecutionInterface with provided ExecutionService");
//         throw std::runtime_error("Failed to create ExecutionInterface with provided ExecutionService");
//     }
//     LOG_INFO("RiskManager", "Created ExecutionInterface with provided ExecutionService");
    
//     // ✅ Create DataInterface ONCE and store as member
//     m_dataInterface = createDataInterface();
//     LOG_INFO("RiskManager", "Created DataInterface as member variable");
    
//     m_portfolioManager = nullptr;
//     // Don't create additional DataInterface instances here
// }
//new constructor
RiskManager::RiskManager(std::shared_ptr<trading::execution::ExecutionService> execService, 
                         const RiskConfig& config,
                         std::shared_ptr<trading::risk::MarketDataInterface> marketDataInterface)
    : m_execService(execService)
    , m_config(config)
    , m_mode(RiskManagerMode::NORMAL)
    , m_marketDataInterface(marketDataInterface) // Use the provided MarketDataInterface
{
    // Initialize components
    m_strategyInterface = createStrategyInterface(m_config);
    LOG_INFO("RiskManager", "Created StrategyInterface");
    
    m_executionInterface = createExecutionInterface_withExecutionService(execService);
    if (!m_executionInterface) {
        LOG_ERROR("RiskManager", "Failed to create ExecutionInterface with provided ExecutionService");
        throw std::runtime_error("Failed to create ExecutionInterface with provided ExecutionService");
    }
    LOG_INFO("RiskManager", "Created ExecutionInterface with provided ExecutionService");
    
    // ✅ Create DataInterface ONCE and store as member
    m_dataInterface = createDataInterface();
    LOG_INFO("RiskManager", "Created DataInterface as member variable");
    
    m_portfolioManager = nullptr;
    // Don't create additional DataInterface instances here
}
bool RiskManager::initialize() {
    try {
        m_strategyInterface = createStrategyInterface(m_config);
        LOG_INFO("RiskManager", "Created StrategyInterface");
        
        m_executionInterface = createExecutionInterface_withExecutionService(m_execService);
        if (!m_executionInterface) {
            LOG_ERROR("RiskManager", "Failed to create ExecutionInterface with provided ExecutionService");
            throw std::runtime_error("Failed to create ExecutionInterface with provided ExecutionService");
        }
        LOG_INFO("RiskManager", "Created ExecutionInterface with provided ExecutionService");
        
        // ✅ Create DataInterface ONCE and store as member
        m_dataInterface = createDataInterface();
        LOG_INFO("RiskManager", "Created DataInterface as member variable");
        
        m_portfolioManager = nullptr;
        // Don't create additional DataInterface instances here
        // Load configuration
        loadConfiguration();
        //print config all keys and values
        std::cout << "RiskManager Configuration:" << std::endl;
        std::cout << "CollateralConfig:" << std::endl;
        std::cout << "  targetFreeCollateralRatio: " << m_config.collateralConfig.targetFreeCollateralRatio << std::endl;
        std::cout << "  enableEmergencyFunding: " << m_config.collateralConfig.enableEmergencyFunding << std::endl;
        std::cout << "  fundingSources: ";
        for (const auto& source : m_config.collateralConfig.fundingSources) {
            std::cout << source << " ";
        }
        std::cout << std::endl;
        std::cout << "  maxFundingAmount: " << m_config.collateralConfig.maxFundingAmount << std::endl;
        std::cout << "  minimumFreeCollateralRatio: " << m_config.collateralConfig.minimumFreeCollateralRatio << std::endl;
        std::cout << "  emergencyFundingThreshold: " << m_config.collateralConfig.emergencyFundingThreshold << std::endl;
        std::cout << "  autoInitiateEmergencyFunding: " << m_config.collateralConfig.autoInitiateEmergencyFunding << std::endl;
        // std::cout << "  managed
        // ✅ Use existing m_dataInterface instead of creating new ones
        if (!m_dataInterface) {
            LOG_ERROR("RiskManager", "DataInterface not initialized in constructor");
            return false;
        }
        
        // Initialize the existing DataInterface
        if (!m_dataInterface->initialize()) {
            LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to initialize DataInterface"));
            return false;
        }
        
        // ✅ Give DataInterface time to establish connections  
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // ✅ Check if DataInterface is connected after initialization
        if (!m_dataInterface->isConnected()) {
            LOG_WARN(std::format("{} {}", "RiskManager", "DataInterface initialized but not connected - this may cause issues"));
            // Try to establish connection if DataInterface has connect method
            // Note: Some DataInterface implementations auto-connect during initialize()
        }
        LOG_INFO(std::format("{} {}", "RiskManager", "DataInterface initialization complete"));

        // Register RiskManager to receive market data updates so updateMarketData()
        // is invoked automatically when new market data arrives from the DataInterface.
        try {
            m_dataInterface->registerMarketDataCallback(
                [this](const MarketData& marketData) {
                    try {
                        this->updateMarketData(marketData);
                    } catch (const std::exception& e) {
                        LOG_ERROR("RiskManager: exception handling market data callback: {}", e.what());
                    }
                }
            );
            LOG_INFO("RiskManager", "Registered for market data callbacks");
        } catch (const std::exception& e) {
            LOG_ERROR("RiskManager: failed to register market data callback: {}", e.what());
        }

        // Initialize portfolio manager first
        m_portfolioManager = std::make_unique<PortfolioManager>(m_config);
        if (!m_portfolioManager->initialize()) {
            LOG_ERROR("RiskManager", "Failed to initialize PortfolioManager");
            return false;
        }
        LOG_INFO(std::format("{} {}", "RiskManager", "Initialized PortfolioManager successfully"));
        // Initialize pre-trade filter
        m_preTradeFilter = std::make_unique<PreTradeRiskFilter>(m_config);
        if (!m_preTradeFilter->initialize()) {
            LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to initialize PreTradeRiskFilter"));
            return false;
        }
        LOG_INFO(std::format("{} {}", "RiskManager", "Initialized PreTradeRiskFilter successfully"));
        // Initialize post-trade monitor
        m_postTradeMonitor = std::make_unique<PostTradeMonitor>(m_config, m_portfolioManager.get());
        if (!m_postTradeMonitor->initialize()) {
            LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to initialize PostTradeMonitor"));
            return false;
        }
        LOG_INFO(std::format("{} {}", "RiskManager", "Initialized PostTradeMonitor successfully"));

        // ✅ Fix smart pointer conversion - use proper shared_ptr sharing
        std::shared_ptr<DataInterface> sharedDataInterface;
        
        // Check if m_dataInterface is actually a shared_ptr wrapped in unique_ptr
        // Or create a proper shared_ptr from unique_ptr (transfer ownership)
        if (m_dataInterface) {
            // Option A: If you want to share ownership properly
            // Release from unique_ptr and create shared_ptr (CAREFUL: transfers ownership!)
            // sharedDataInterface = std::shared_ptr<DataInterface>(m_dataInterface.release());
            
            // Option B: Keep unique_ptr ownership but create safe wrapper
            sharedDataInterface = std::shared_ptr<DataInterface>(
                m_dataInterface.get(), 
                [](DataInterface*) { 
                    // Empty deleter - unique_ptr still owns the object
                    // This is safe as long as unique_ptr outlives all shared_ptr uses
                }
            );

            LOG_INFO("{} {}", "RiskManager", "Created shared DataInterface wrapper for PositionTracker");
        } else {
            LOG_ERROR("{} {}", "RiskManager", "m_dataInterface is null, cannot create shared wrapper");
            return false;
        }
        
        std::shared_ptr<PositionTracker> positionTracker = std::make_shared<PositionTracker>(m_config, sharedDataInterface);
        
        // ✅ Add debug logging before PositionTracker initialization
        LOG_INFO(std::format("{} {}", "RiskManager", "About to initialize PositionTracker..."));
        LOG_INFO(std::format("{} {}", "RiskManager", "DataInterface state before PositionTracker init:"));

        std::string dataInterfaceStatus = m_dataInterface ? "VALID" : "NULL";
        LOG_INFO(std::format("{} {}", "RiskManager", "  - DataInterface ptr: {}", dataInterfaceStatus));

        std::string dataInterfaceConnected = (m_dataInterface && m_dataInterface->isConnected()) ? "YES" : "NO";
        LOG_INFO(std::format("{} {}", "RiskManager", "  - DataInterface connected: {}", dataInterfaceConnected));

        std::string sharedWrapperStatus = sharedDataInterface ? "VALID" : "NULL";
        LOG_INFO(std::format("{} {}", "RiskManager", "  - Shared wrapper ptr: {}", sharedWrapperStatus));

        std::string sharedWrapperConnected = (sharedDataInterface && sharedDataInterface->isConnected()) ? "YES" : "NO";
        LOG_INFO(std::format("{} {}", "RiskManager", "  - Shared wrapper connected: {}", sharedWrapperConnected));

        if (!positionTracker->initialize()) {
            LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to initialize PositionTracker"));
            return false;
        }
        LOG_INFO(std::format("{} {}", "RiskManager", "Initialized PositionTracker successfully"));

        // ✅ Pass shared wrapper to CollateralManager
        m_collateralManager = std::make_unique<CollateralManager>(
            m_config,
            positionTracker,
            m_executionInterface,
            sharedDataInterface  // ✅ Reuse same DataInterface
        );
        
        if(!m_collateralManager) {
            LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to create CollateralManager"));
            return false;
        }
        LOG_INFO(std::format("{} {}", "RiskManager", "Created CollateralManager successfully"));
        if (!m_collateralManager->initialize()) {
            LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to initialize CollateralManager"));
            return false;
        }
        // Initialize strategy interface
        m_strategyInterface = createStrategyInterface(m_config);
        
        if (!m_strategyInterface->initialize() || !m_executionInterface->initialize()) {
            std::cerr << "Failed to initialize external interfaces" << std::endl;
            return false;
        }
        
        // Initialize risk service
        m_riskService = std::make_unique<RiskService>(m_config);
        
        auto validationService = std::make_shared<RiskValidationService>(m_config, shared_from_this());
        if (!validationService->initialize()) {
            std::cerr << "Failed to initialize validation service" << std::endl;
            return false;
        }
        
        m_riskService->setRiskValidationService(validationService);
        
        if (!m_riskService->initialize() || !m_riskService->start()) {
            std::cerr << "Failed to initialize or start risk service" << std::endl;
            return false;
        }
        
        m_postTradeMonitor->registerRiskEventCallback([this](const RiskEvent& event) {
            notifyRiskEvent(event);
        });
        
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
        violation.type = ViolationType::OTHER;
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
    violation.type = ViolationType::SYSTEM_ERROR;
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
        RiskEvent event(
            RiskEventType::SYSTEM_ERROR,
            std::chrono::system_clock::now(),
            std::string("Error processing execution report: ") + e.what(),
            "high"  // Use string severity as defined in struct
        );        
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
        
        // Create risk event properly using constructor
        RiskEvent event(RiskEventType::SYSTEM_ERROR,
                    std::chrono::system_clock::now(),
                    std::string("Error updating market data: ") + e.what(),
                    "critical");  // Use string severity as defined in struct
        
        // Set additional fields if needed
        // event.eventId = generateEventId();  // You'll need this function
        // event.eventSource = "market_data_processor";
        // event.message = "Market data processing failed";
        
        notifyRiskEvent(event);  // Pass by reference, not pointer
    }
}

RiskManagerMode RiskManager::getMode() const {
    return m_mode.load();
}

void RiskManager::setMode(RiskManagerMode mode) {
    RiskManagerMode oldMode = m_mode.exchange(mode);
    
    // Notify about mode change if it actually changed
    if (oldMode != mode) {
        RiskEvent event(RiskEventType::RISK_MODE_CHANGE,
                    std::chrono::system_clock::now(),
                    std::string("Error updating market data: "),
                    "");  
        m_mode.store(mode);
        std::stringstream ss;
        ss << "Risk manager mode changed from " << static_cast<int>(oldMode) 
           << " to " << static_cast<int>(mode);
        event.description = ss.str();
        
        notifyRiskEvent(event);
        
        // Take specific actions based on new mode
        if (mode == RiskManagerMode::EMERGENCY) {
            // Execute emergency procedures
            //this cause circular calls in infinite loop
            //emergencyAction ->setMode->emergencyAction->setMode...
            // emergencyAction("Entered emergency mode");
            LOG_WARN("Risk manager set to EMERGENCY mode");
        }
    }
}

RiskParameters RiskManager::getRiskParameters(const ExchangeId& exchangeId, 
                                          const TradingPair& tradingPair,
                                          const StrategyId& strategyId) const {
    if (m_preTradeFilter) {
        return m_preTradeFilter->getRiskParameters(exchangeId, tradingPair, strategyId.id);
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
        if (exchangeId.has_value()) {
            return m_portfolioManager->reconcilePositions(exchangeId.value());
        } else {
            // Handle case where no specific exchange is provided
            // You may need to reconcile all exchanges or use a default
            return m_portfolioManager->reconcilePositions(exchangeId.value());
        }
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
        RiskEvent event(
            RiskEventType::EMERGENCY_ACTION,
            std::chrono::system_clock::now(),
            "Emergency action triggered: " + reason,
            "critical"
        );
        
        notifyRiskEvent(event);
        
        // 1. Cancel all open orders across all exchanges and trading pairs
        if (m_executionInterface && m_portfolioManager) {
            // Get all active positions/trading pairs from portfolio manager
            auto activePositions = m_portfolioManager->getAllPositions();
            
            // Create a set to track unique exchange/symbol combinations
            std::set<std::pair<core::ExchangeId, core::Symbol>> tradingPairs;
            
            // Extract unique exchange/symbol pairs from positions
            for (const auto& position : activePositions) {
                core::ExchangeId exchangeId = position.getExchangeId();
                core::Symbol symbol = core::Symbol(position.getTradingPair());
                tradingPairs.insert(std::make_pair(exchangeId, symbol));
            }
            
            // If no positions found, use default exchanges and common pairs
            if (tradingPairs.empty()) {
                // Add default trading pairs for major exchanges
                std::vector<std::string> defaultPairs = {"BTC/USDT", "ETH/USDT", "BNB/USDT"};
                std::vector<core::ExchangeId> defaultExchanges = {
                    core::ExchangeId::binance(),
                    core::ExchangeId::coinbase(),
                    core::ExchangeId::kraken()
                };
                
                for (const auto& exchangeId : defaultExchanges) {
                    for (const auto& pairStr : defaultPairs) {
                        core::Symbol symbol = core::Symbol::fromString(pairStr);
                        tradingPairs.insert(std::make_pair(exchangeId, symbol));
                    }
                }
            }
            
            // Cancel orders for each exchange/symbol combination
            int totalCancelled = 0;
            for (const auto& [exchangeId, symbol] : tradingPairs) {
                try {
                    int cancelled = m_executionInterface->cancelAllOrders(exchangeId, symbol);
                    totalCancelled += cancelled;
                    std::cout << "Cancelled " << cancelled << " orders for " 
                              << symbol.toString() << " on " << exchangeId.toString() << std::endl;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to cancel orders for " << symbol.toString() 
                              << " on " << exchangeId.toString() << ": " << e.what() << std::endl;
                }
            }
            
            std::cout << "Total orders cancelled: " << totalCancelled << std::endl;
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

// void RiskManager::notifyRiskEvent(const RiskEvent& event) {
//     std::lock_guard<std::mutex> lock(m_callbackMutex);
//     for (const auto& callback : m_eventCallbacks) {
//         callback(event);
//     }
// }
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
    // FIXED: Use the correct RiskConfig methods instead of non-existent ones
    
    // Example: Check if position limits are enabled using the actual config structure
    const auto& positionLimitConfig = m_config.getPositionLimitConfig();
    bool enablePositionLimits = positionLimitConfig.enforceStrictly;
    
    if (enablePositionLimits) {
        std::cout << "Position limits enforcement is enabled" << std::endl;
        // Use the parameter...
    }
    
    // Example: Check if circuit breaker is enabled
    const auto& circuitBreakerConfig = m_config.getCircuitBreakerConfig();
    bool enableCircuitBreaker = circuitBreakerConfig.autoResume;
    
    if (enableCircuitBreaker) {
        std::cout << "Circuit breaker auto-resume is enabled" << std::endl;
        // Use the parameter...
    }
    
    // Example: Load stop loss configuration
    const auto& stopLossConfig = m_config.getStopLossConfig();
    bool enableTrailingStop = stopLossConfig.enableTrailingStop;
    
    if (enableTrailingStop) {
        std::cout << "Trailing stop is enabled" << std::endl;
        // Use the parameter...
    }
    
    // Example: Load drawdown monitoring configuration
    const auto& drawdownConfig = m_config.getDrawdownConfig();
    bool enableAutoRiskReduction = drawdownConfig.enableAutoRiskReduction;
    
    if (enableAutoRiskReduction) {
        std::cout << "Auto risk reduction is enabled" << std::endl;
        // Use the parameter...
    }
    
    // Example: Load fat finger protection
    const auto& fatFingerConfig = m_config.getFatFingerConfig();
    bool enforceAdditionalVerification = fatFingerConfig.enforceAdditionalVerification;
    
    if (enforceAdditionalVerification) {
        std::cout << "Additional verification for large orders is enabled" << std::endl;
        // Use the parameter...
    }
    
    // Example: Load order validation settings
    const auto& orderValidationConfig = m_config.getOrderValidationConfig();
    bool validateMinOrderSize = orderValidationConfig.validateMinOrderSize;
    bool validatePriceRange = orderValidationConfig.validatePriceRange;
    
    if (validateMinOrderSize) {
        std::cout << "Minimum order size validation is enabled" << std::endl;
    }
    
    if (validatePriceRange) {
        std::cout << "Price range validation is enabled" << std::endl;
    }
    
    // Example: Load balance reconciliation settings  
    const auto& reconciliationConfig = m_config.getBalanceReconciliationConfig();
    bool autoCorrectDiscrepancies = reconciliationConfig.autoCorrectDiscrepancies;
    bool alertOnDiscrepancy = reconciliationConfig.alertOnDiscrepancy;
    
    if (autoCorrectDiscrepancies) {
        std::cout << "Auto-correction of balance discrepancies is enabled" << std::endl;
    }
    
    if (alertOnDiscrepancy) {
        std::cout << "Alerting on balance discrepancies is enabled" << std::endl;
    }
    
    // Example: Load rebalancing settings
    const auto& rebalancingConfig = m_config.getRebalancingConfig();
    bool autoRebalancingEnabled = rebalancingConfig.autoRebalancingEnabled;
    bool autoExecuteRebalancing = rebalancingConfig.autoExecuteRebalancing;
    
    if (autoRebalancingEnabled) {
        std::cout << "Auto-rebalancing is enabled" << std::endl;
    }
    
    if (autoExecuteRebalancing) {
        std::cout << "Auto-execution of rebalancing orders is enabled" << std::endl;
    }
    
    // Example: Load collateral management settings
    const auto& collateralConfig = m_config.getCollateralConfig();
    bool enableEmergencyFunding = collateralConfig.enableEmergencyFunding;
    
    if (enableEmergencyFunding) {
        std::cout << "Emergency funding is enabled" << std::endl;
    }
    
    // Example: Load logging settings
    const auto& loggingConfig = m_config.getLoggingConfig();
    bool enableDetailedLogs = loggingConfig.enableDetailedLogs;
    bool logToConsole = loggingConfig.logToConsole;
    bool logToFile = loggingConfig.logToFile;
    
    if (enableDetailedLogs) {
        std::cout << "Detailed logging is enabled" << std::endl;
    }
    
    if (logToConsole) {
        std::cout << "Console logging is enabled" << std::endl;
    }
    
    if (logToFile) {
        std::cout << "File logging is enabled" << std::endl;
    }
    
    // Load default mode from configuration
    // Since there's no direct mode setting in config, use default NORMAL mode
    m_mode.store(RiskManagerMode::NORMAL);
    
    std::cout << "Risk manager configuration loaded successfully" << std::endl;
}

RiskManager::~RiskManager() {
    // Cleanup code here if needed
    try {
        // Stop any running services
        if (m_riskService) {
            m_riskService->stop();
        }
        
        // Clear callbacks
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_eventCallbacks.clear();
        
        // Other cleanup can be done here
        // unique_ptr members will be automatically cleaned up
    } catch (const std::exception& e) {
        // Don't throw from destructor
        std::cerr << "Exception in RiskManager destructor: " << e.what() << std::endl;
    }
}

} // namespace risk
} // namespace trading