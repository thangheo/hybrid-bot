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

// Helper to attempt to call setPositionSynchronizer on objects that support it.
namespace {
    template <typename T>
    auto trySetPositionSync(T* obj, std::shared_ptr<trading::risk::sync::PositionSynchronizer> sync)
        -> decltype(obj->setPositionSynchronizer(sync), void()) {
        obj->setPositionSynchronizer(std::move(sync));
    }

    // Fallback overload when method doesn't exist - no-op
    inline void trySetPositionSync(...) {}
}

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
//new constructor
RiskManager::RiskManager(std::shared_ptr<trading::execution::ExecutionService> execService, 
                         const RiskConfig& config,
                         std::shared_ptr<trading::risk::MarketDataInterface> marketDataInterface,
                         std::shared_ptr<zmq::context_t> zmqContext)
    : m_execService(execService)
    , m_config(config)
    , m_mode(RiskManagerMode::NORMAL)
    , m_marketDataInterface(marketDataInterface) // Use the provided MarketDataInterface
    , zmqContext_(zmqContext)
{
    // Defer creation of StrategyInterface to initialize() to avoid starting ZMQ clients
    // during construction (useful for unit tests where mocks are injected first).
    LOG_INFO("RiskManager", "StrategyInterface will be created in initialize() if not injected");
    
    // Defer creation of ExecutionInterface and DataInterface to initialize().
    // This avoids starting background threads or ZMQ clients during construction
    // (useful for unit tests where mocks will be injected before initialize()).
    LOG_INFO("RiskManager", "ExecutionInterface and DataInterface will be created in initialize() if not injected");
    
    m_portfolioManager = nullptr;
    // If a MarketDataInterface was provided, reuse it as the DataInterface so
    // initialize() doesn't try to create a second one. MarketDataInterface
    // derives from DataInterface so we can store it directly.
    if (marketDataInterface) {
        m_dataInterface = std::static_pointer_cast<DataInterface>(marketDataInterface);
    }
    // Don't create additional DataInterface instances here
}
bool RiskManager::initialize() {
    try {
        // Ensure we have a StrategyInterface. The constructor already creates one
        // so avoid recreating it here — recreating would destroy the running
        // instance and call its destructor which stops the ZMQ client and joins
        // worker threads (this can deadlock if the worker is executing callbacks
        // or blocked). Reuse the existing instance when available.
        if (!m_strategyInterface) {
            //check if zmqContext_ is null, if so create a new one
            if (!zmqContext_) {
                zmqContext_ = std::make_shared<zmq::context_t>(1);
            }
            m_strategyInterface = createStrategyInterface(m_config, zmqContext_);
            LOG_INFO("RiskManager", "Created StrategyInterface");
        } else {
            LOG_INFO("RiskManager", "Using existing StrategyInterface");
        }

        // Initialize the strategy interface if present
        if (m_strategyInterface) {
            if (!m_strategyInterface->initialize()) {
                LOG_ERROR("RiskManager", "Failed to initialize StrategyInterface");
                return false;
            }
        }
        
        // If a test has injected an ExecutionInterface, keep it. Only create one if missing.
        if (!m_executionInterface) {
            if (m_execService) {
                m_executionInterface = createExecutionInterface_withExecutionService(m_execService);
                if (!m_executionInterface) {
                    LOG_ERROR("RiskManager", "Failed to create ExecutionInterface with provided ExecutionService");
                    throw std::runtime_error("Failed to create ExecutionInterface with provided ExecutionService");
                }
                LOG_INFO("RiskManager", "Created ExecutionInterface with provided ExecutionService");
            } else {
                m_executionInterface = createExecutionInterface(m_config);
                if (!m_executionInterface) {
                    LOG_ERROR("RiskManager", "Failed to create internal ExecutionInterface in initialize()");
                    throw std::runtime_error("Failed to create internal ExecutionInterface in initialize()");
                }
                LOG_INFO("RiskManager", "Created internal ExecutionInterface in initialize()");
            }
        } else {
            LOG_INFO("RiskManager", "Using injected ExecutionInterface");
        }
        
    // m_dataInterface should already be created in constructor; ensure it's present
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
        // if (!m_dataInterface->initialize()) {
        //     LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to initialize DataInterface"));
        //     return false;
        // }
        
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

        // Create a single shared PositionTracker early so it can be injected into other components
        std::shared_ptr<PositionTracker> sharedPositionTracker = std::make_shared<PositionTracker>(m_config, m_dataInterface);
        LOG_INFO(std::format("{} {}", "RiskManager", "About to initialize shared PositionTracker..."));
        if (!sharedPositionTracker->initialize()) {
            LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to initialize shared PositionTracker"));
            return false;
        }
        LOG_INFO(std::format("{} {}", "RiskManager", "Initialized shared PositionTracker successfully"));

        // Setup Position Synchronizer if a ZMQ context is available
        try {
            if (zmqContext_) {
                LOG_INFO("RiskManager", "Attempting to create PositionSynchronizer");
                // Use default PositionSyncConfig (can be tuned via external config later)
                trading::risk::sync::PositionSyncConfig syncConfig{};
                // Create synchronizer via factory
                m_positionSync = trading::risk::sync::createPositionSynchronizer(syncConfig, zmqContext_);
                if (m_positionSync) {
                    if (!m_positionSync->initialize()) {
                        LOG_WARN("RiskManager", "PositionSynchronizer initialize() failed - continuing without sync cache");
                        m_positionSync.reset();
                    } else if (!m_positionSync->start(sharedPositionTracker)) {
                        LOG_WARN("RiskManager", "PositionSynchronizer start() failed - continuing without sync cache");
                        m_positionSync.reset();
                    } else {
                        LOG_INFO("RiskManager", "PositionSynchronizer started and linked to shared PositionTracker");
                        // Forward position events to risk event callbacks for monitoring
                        m_positionSync->registerPositionEventCallback([this](const trading::risk::sync::PositionEvent& ev){
                            // Build a simple RiskEvent for monitoring purposes
                            RiskEvent re;
                            re.type = RiskEventType::POSITION_RECONCILIATION_MISMATCH;
                            re.description = std::string("PositionSync event: ") + ev.symbol + "@" + ev.exchangeId;
                            re.severity = "info";
                            notifyRiskEvent(re);
                        });
                    }
                }
            }
        } catch (const std::exception& e) {
            LOG_WARN("RiskManager", "Exception while setting up PositionSynchronizer: {}", e.what());
            m_positionSync.reset();
        }

        // Initialize portfolio manager first (only if not injected by tests)
        if (!m_portfolioManager) {
            // Construct PortfolioManager using the shared PositionTracker to avoid duplicate trackers
            m_portfolioManager = std::make_unique<PortfolioManager>(m_config, m_dataInterface, sharedPositionTracker);
            if (!m_portfolioManager->initialize()) {
                LOG_ERROR("RiskManager", "Failed to initialize PortfolioManager");
                return false;
            }
        }
        LOG_INFO(std::format("{} {}", "RiskManager", "Initialized PortfolioManager successfully"));
        // Initialize pre-trade filter
        if (!m_preTradeFilter) {
            m_preTradeFilter = std::make_unique<PreTradeRiskFilter>(m_config, m_dataInterface);
            if (!m_preTradeFilter) {
                LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to create PreTradeRiskFilter"));
                return false;
            }
            
            // ✅ CRITICAL FIX: Inject shared PositionTracker into PreTradeFilter
            m_preTradeFilter->setPositionTracker(sharedPositionTracker);
            LOG_INFO("RiskManager", "✅ Injected shared PositionTracker into PreTradeFilter");
            
            if (!m_preTradeFilter->initialize()) {
                LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to initialize PreTradeRiskFilter"));
                return false;
            }
        }
        LOG_INFO(std::format("{} {}", "RiskManager", "Initialized PreTradeRiskFilter successfully"));
        // Initialize post-trade monitor - pass the shared DataInterface so PostTradeMonitor
        // reuses the same instance and does not create a second MarketDataInterface.
        if (!m_dataInterface) {
            LOG_ERROR("RiskManager", "m_dataInterface is null, cannot initialize PostTradeMonitor");
            return false;
        }

        if (!m_postTradeMonitor) {
            m_postTradeMonitor = std::make_unique<PostTradeMonitor>(m_config, m_portfolioManager.get(), m_dataInterface, m_executionInterface, sharedPositionTracker);
            if (!m_postTradeMonitor->initialize()) {
                LOG_ERROR(std::format("{} {}", "RiskManager", "Failed to initialize PostTradeMonitor"));
                return false;
            }
        }
        LOG_INFO(std::format("{} {}", "RiskManager", "Initialized PostTradeMonitor successfully"));
        // If we have a running PositionSynchronizer, inject it into components that can use it
        if (m_positionSync) {
            try {
                // PostTradeMonitor may expose stop-loss and reconciliation subsystems which accept a PositionSynchronizer
                // Forward the shared pointer (wrap unique_ptr raw pointer into shared_ptr temporarily)
                // Create a non-owning shared_ptr wrapper with no-op deleter so we can pass a shared_ptr view
                auto sharedSync = std::shared_ptr<trading::risk::sync::PositionSynchronizer>(m_positionSync.get(), [](auto*){});

                // Inject into PostTradeMonitor subcomponents if available
                trySetPositionSync(&m_postTradeMonitor->getStopLossManager(), sharedSync);
                trySetPositionSync(&m_postTradeMonitor->getBalanceReconciliation(), sharedSync);

                // CollateralManager - attempt to set if API exists
                trySetPositionSync(m_collateralManager.get(), sharedSync);
            } catch (const std::exception& e) {
                LOG_WARN("RiskManager", "Failed to inject PositionSynchronizer into components: {}", e.what());
            }
        }
        
    // Create a single shared PositionTracker instance and inject it into components
    // Pass shared tracker into collateral manager (and later into portfolio/post-trade components)
    m_collateralManager = std::make_unique<CollateralManager>(
        m_config,
        sharedPositionTracker,
        m_executionInterface,
        m_dataInterface
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

        // If a post-trade signal checker has been registered earlier, forward it to the PostTradeMonitor
        if (m_postTradeMonitor && postTradeSignalChecker_) {
            try {
                m_postTradeMonitor->registerExternalSignalChecker(*postTradeSignalChecker_);
            } catch (const std::exception& e) {
                LOG_WARN("RiskManager", "Failed to register post-trade signal checker: {}", e.what());
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception during RiskManager initialization: " << e.what() << std::endl;
        return false;
    }
}
bool RiskManager::shutdown() {
    try {
        // Shutdown risk service first to stop incoming validations
        if (m_riskService) {
            m_riskService->stop();
            LOG_INFO("RiskManager", "RiskService stopped successfully");
        }
        
        // Shutdown post-trade monitor
        if (m_postTradeMonitor) {
            m_postTradeMonitor->shutdown();
            LOG_INFO("RiskManager", "PostTradeMonitor shut down successfully");
        }
        
        // Shutdown portfolio manager
        if (m_portfolioManager) {
            m_portfolioManager->shutdown();
            LOG_INFO("RiskManager", "PortfolioManager shut down successfully");
        }
        
        // Shutdown collateral manager
        if (m_collateralManager) {
            m_collateralManager->shutdown();
            LOG_INFO("RiskManager", "CollateralManager shut down successfully");
        }
        
        // Shutdown pre-trade filter
        if (m_preTradeFilter) {
            m_preTradeFilter->shutdown();
            LOG_INFO("RiskManager", "PreTradeRiskFilter shut down successfully");
        }

        // Shutdown position synchronizer if present
        if (m_positionSync) {
            try {
                m_positionSync->stop();
                m_positionSync->shutdown();
                LOG_INFO("RiskManager", "PositionSynchronizer stopped and shut down");
            } catch (const std::exception& e) {
                LOG_WARN("RiskManager", "Exception during PositionSynchronizer shutdown: {}", e.what());
            }
            m_positionSync.reset();
        }
        
        // Shutdown data interface last to ensure all components finish using it
        if (m_dataInterface) {
            m_dataInterface->shutdown();
            LOG_INFO("RiskManager", "DataInterface shut down successfully");
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception during RiskManager shutdown: " << e.what() << std::endl;
        return false;
    }
}

void RiskManager::registerPostTradeSignalChecker(const RiskManager::PostTradeSignalChecker& checker) {
    postTradeSignalChecker_ = checker;
    if (m_postTradeMonitor) {
        try {
            m_postTradeMonitor->registerExternalSignalChecker(checker);
        } catch (const std::exception& e) {
            LOG_WARN("RiskManager", "Error forwarding post-trade signal checker: {}", e.what());
        }
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
        std::cout << "[TTL_DEBUG] RiskManager::processExecutionReport: " 
                  << executionReport.symbol().toString() 
                  << " status=" << static_cast<int>(executionReport.status()) << std::endl;

        // Update portfolio with execution information (guarded)
        if (m_portfolioManager) {
            try {
                m_portfolioManager->processExecution(executionReport);
            } catch (const std::exception& e) {
                std::cerr << "Exception in PortfolioManager::processExecution: " << e.what() << std::endl;
                RiskEvent event(
                    RiskEventType::SYSTEM_ERROR,
                    std::chrono::system_clock::now(),
                    std::string("Error in PortfolioManager::processExecution: ") + e.what(),
                    "high"
                );
                notifyRiskEvent(event);
            } catch (...) {
                std::cerr << "Unknown exception in PortfolioManager::processExecution" << std::endl;
                RiskEvent event(
                    RiskEventType::SYSTEM_ERROR,
                    std::chrono::system_clock::now(),
                    std::string("Unknown error in PortfolioManager::processExecution"),
                    "high"
                );
                notifyRiskEvent(event);
            }
        } else {
            std::cout << "[TTL_DEBUG] ERROR: m_portfolioManager is NULL!" << std::endl;
        }

        // Pass to post-trade monitoring (guarded)
        if (m_postTradeMonitor) {
            std::cout << "[TTL_DEBUG] Forwarding to PostTradeMonitor... ptr=" << static_cast<const void*>(m_postTradeMonitor.get()) << std::endl;
            try {
                m_postTradeMonitor->processExecutionReport(executionReport);
            } catch (const std::exception& e) {
                std::cerr << "Exception in PostTradeMonitor::processExecutionReport: " << e.what() << std::endl;
                RiskEvent event(
                    RiskEventType::SYSTEM_ERROR,
                    std::chrono::system_clock::now(),
                    std::string("Error in PostTradeMonitor::processExecutionReport: ") + e.what(),
                    "high"
                );
                notifyRiskEvent(event);
            } catch (...) {
                std::cerr << "Unknown exception in PostTradeMonitor::processExecutionReport" << std::endl;
                RiskEvent event(
                    RiskEventType::SYSTEM_ERROR,
                    std::chrono::system_clock::now(),
                    std::string("Unknown error in PostTradeMonitor::processExecutionReport"),
                    "high"
                );
                notifyRiskEvent(event);
            }
        } else {
            std::cout << "[TTL_DEBUG] ERROR: m_postTradeMonitor is NULL!" << std::endl;
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
            // Execute emergency procedures synchronously when mode transitions to EMERGENCY
            LOG_WARN("Risk manager set to EMERGENCY mode - executing emergency procedures");
            try {
                // Cancel all orders and notify strategies
                if (m_executionInterface && m_portfolioManager) {
                    auto activePositions = m_portfolioManager->getAllPositions();
                    std::set<std::pair<core::ExchangeId, core::Symbol>> tradingPairs;
                    for (const auto& position : activePositions) {
                        tradingPairs.insert(std::make_pair(position.getExchangeId(), core::Symbol(position.getTradingPair())));
                    }

                    if (tradingPairs.empty()) {
                        // Fallback to a default cancellation across exchanges
                        m_executionInterface->cancelAllOrders(core::ExchangeId::binance());
                    } else {
                        for (const auto& [exchangeId, symbol] : tradingPairs) {
                            m_executionInterface->cancelAllOrders(exchangeId, symbol);
                        }
                    }
                }

                if (m_strategyInterface) {
                    m_strategyInterface->notifyEmergencyState("Entered emergency mode");
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Error executing emergency procedures: {}", e.what());
            }
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
            // No specific exchange provided - reconcile across all exchanges.
            // PortfolioManager::reconcilePositions expects an ExchangeId. Use
            // a sentinel/all Exchanges id or call a default behavior. Here we
            // choose ExchangeId::all() as a convention for "all exchanges".
            return m_portfolioManager->reconcilePositions(core::ExchangeId::all());
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
        // Ensure mode reflects emergency when emergencyAction is called directly
        m_mode.store(RiskManagerMode::EMERGENCY);
        
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
                // Try single-arg cancelAllOrders fallback first (some mocks implement this)
                try {
                    m_executionInterface->cancelAllOrders(core::ExchangeId::binance());
                } catch (...) {
                    // If fallback fails, build a set of default trading pairs
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
    // Do NOT call setMode here to avoid recursive calls. The caller should set the mode
    // and setMode will call emergencyAction when transitioning to EMERGENCY.
    // (Previously this caused setMode->emergencyAction->setMode recursion.)
        
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
// getAllowedPositionSize
double RiskManager::getAllowedPositionSize(const TradingPair& tradingPair) const {
    if (m_preTradeFilter) {
        return m_preTradeFilter->getAllowedPositionSize(tradingPair);
    }
    // Default to 0 if pre-trade filter is unavailable
    return 0.0;
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

// Testing helper - inject raw pointers for internal subsystems. RiskManager will take
// ownership by wrapping them in unique_ptr/shared_ptr as appropriate. This is only
// intended for unit tests.
void RiskManager::injectTestMocks(
    PortfolioManager* portfolioManager,
    PreTradeRiskFilter* preTradeFilter,
    PostTradeMonitor* postTradeMonitor,
    StrategyInterface* strategyInterface,
    ExecutionInterface* executionInterface,
    DataInterface* dataInterface
) {
    if (portfolioManager) m_portfolioManager.reset(portfolioManager);
    if (preTradeFilter) m_preTradeFilter.reset(preTradeFilter);
    if (postTradeMonitor) m_postTradeMonitor.reset(postTradeMonitor);
    if (strategyInterface) m_strategyInterface.reset(strategyInterface);
    if (executionInterface) m_executionInterface.reset(executionInterface);
    if (dataInterface) m_dataInterface.reset(dataInterface);
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

std::shared_ptr<ExecutionInterface> RiskManager::getExecutionInterface() const {
    return m_executionInterface;
}

std::optional<double> RiskManager::getCachedPosition(const ExchangeId& exchangeId, const TradingPair& tradingPair) const {
    // 🚀 FAST O(1) position lookup from cached PositionTracker
    // Avoids expensive getAllPositions() API calls (100+ positions)
    try {
        // Try PositionSynchronizer cache first if available (very fast)
        if (m_positionSync) {
            try {
                // positionSync API uses symbol, exchangeId string ordering
                std::string symbol = tradingPair.toString();
                std::string exch = exchangeId.toString();
                if (m_positionSync->hasPosition(symbol, exch)) {
                    auto pOpt = m_positionSync->getPosition(symbol, exch);
                    if (pOpt.has_value()) {
                        double qty = (*pOpt).getQuantity();
                        LOG_INFO("RiskManager - ✅ Found position in PositionSync cache: {}@{} = {}", symbol, exch, qty);
                        return qty;
                    }
                }
            } catch (const std::exception& e) {
                LOG_WARN("RiskManager - PositionSync read error: {}", e.what());
                // Fall through to tracker fallback
            }
        }

        // Try PreTradeFilter's position tracker next
        if (m_preTradeFilter && m_preTradeFilter->getPositionTracker()) {
            // Diagnostic: print pointer and timestamp to help debug missing positions
            auto pt_ptr = m_preTradeFilter->getPositionTracker().get();
            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            LOG_INFO("RiskManager", "🔍 Checking position tracker for {}@{} ptr={} ts_ms={}", tradingPair.toString(), exchangeId.toString(), pt_ptr, now_ms);
            auto positionOpt = m_preTradeFilter->getPositionTracker()->getPosition(exchangeId, tradingPair);
            if (positionOpt.has_value()) {
                LOG_INFO("RiskManager - ✅ Found position in tracker: {}@{} = {} (pt_ptr={})", 
                         tradingPair.toString(), exchangeId.toString(), positionOpt->getQuantity(), pt_ptr);
                return positionOpt->getQuantity();
            } else {
                LOG_WARN("RiskManager - ❌ NO position found in tracker for {}@{}", tradingPair.toString(), exchangeId.toString());
            }
        } else {
            LOG_WARN("RiskManager - ❌ PreTradeFilter or PositionTracker is null!");
        }
        
        // Fallback: Direct ExecutionInterface lookup (still better than getAllPositions)
        LOG_INFO("RiskManager - 🔄 Falling back to ExecutionInterface for {}@{}", tradingPair.toString(), exchangeId.toString());
        if (m_executionInterface) {
            try {
                auto positions = m_executionInterface->getPositions(exchangeId);
                LOG_INFO("RiskManager - 📊 ExecutionInterface returned {} positions total", positions.size());
                auto it = positions.find(tradingPair.toString());
                if (it != positions.end() && std::abs(it->second) > 1e-8) {
                    LOG_INFO("RiskManager - ✅ Found position in ExecutionInterface: {}@{} = {}", 
                             tradingPair.toString(), exchangeId.toString(), it->second);
                    return it->second;
                } else if (it != positions.end()) {
                    LOG_WARN("RiskManager - ⚠️ Position found but too small: {}@{} = {} (< 1e-8)", 
                             tradingPair.toString(), exchangeId.toString(), it->second);
                } else {
                    LOG_WARN("RiskManager - ❌ Position not found in ExecutionInterface for {}@{}", 
                             tradingPair.toString(), exchangeId.toString());
                }
            } catch (const std::exception& e) {
                // LOG_ERROR("RiskManager", "💥 ExecutionInterface exception: {}", e.what());
                LOG_ERROR("RiskManager - 💥 ExecutionInterface exception: {}", e.what());
            } catch (...) {
                LOG_ERROR("RiskManager - 💥 Unknown ExecutionInterface exception");
            }
        } else {
            LOG_WARN("RiskManager - ❌ ExecutionInterface is null!");
        }
        
        return std::nullopt;
    } catch (const std::exception& e) {
        LOG_WARN("RiskManager - Error getting cached position for {}@{}: {}", 
                 tradingPair.toString(), exchangeId.toString(), e.what());
        return std::nullopt;
    }
}

} // namespace risk
} // namespace trading