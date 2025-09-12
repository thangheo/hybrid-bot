/**
 * @file full_system_integration_demo.cpp
 * @brief Complete trading system integration demonstration
 * 
 * This demo integrates all major components:
 * - Data Ingestion Service (order book data)
 * - Strategy Engine (signal generation)
 * - Risk Manager (validation and monitoring)
 * - Execution Engine (order placement)
 * - Monitoring (performance tracking)
 */

#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>
#include <fstream>
#include <signal.h>

// Common includes
#include "common/event_bus.hpp"
#include "common/trading_events.hpp"
#include "common/logger.hpp"
#include "common/zeromq_client.hpp"
#include "common/exchange_types.hpp"
// Data ingestion
#include "data_ingestion/data_ingestion_service.hpp"

// Strategy engine
#include "strategy/engine/strategy_engine.hpp"
#include "strategy/config/strategy_config_loader.hpp"

// Risk management
#include "risk/risk_manager.hpp"
#include "risk/risk_config.hpp"
#include "risk/risk_types.hpp"

// Execution engine
#include "execution/execution_service.hpp"
#include "execution/trading_signal.hpp"

// Core types
#include "core/market_types.hpp"
#include "core/direction.hpp"
#include "core/types.hpp"

using namespace trading;
using namespace std::chrono_literals;
using namespace trading::common;
using namespace trading::core;
using namespace trading::data_ingestion;
using namespace trading::strategy;
using namespace trading::risk;
using namespace trading::execution;

class FullSystemDemo {
public:
    FullSystemDemo() : running_(false), systemHealthy_(true) {
        setupConfigurations();
        setupStatistics();
    }
    
    ~FullSystemDemo() {
        shutdown();
    }
    void initializeExecutionService() {
        // Create the authentication service first
        auto authService = std::make_shared<AuthenticationService>();
        
        // Create API client manager with auth service
        auto api_client_manager = std::make_shared<ApiClientManager>(authService);
        
        // Set up credentials and clients (your existing code)
        //get from environment .env file
        const char* apiKey = std::getenv("BINANCE_API_KEY");
        const char* apiSecret = std::getenv("BINANCE_API_SECRET");
        
        if(!apiKey || !apiSecret) {
            std::cerr << "Error: Missing Binance API credentials in environment variables." << std::endl;
            systemHealthy_ = false;
            return;
        }
        
        auto credentials = ExchangeCredentials{
            apiKey,
            apiSecret,
            "", 
            true
        };
        //debug api
        // std::cout << "API KEY: " << std::getenv("BINANCE_API_KEY") << std::endl;
        // std::cout << "API SECRET: " << std::getenv("BINANCE_API_SECRET") << std::endl;
        authService->setCredentials(core::ExchangeId::binance(), credentials);
        api_client_manager->addClient(core::ExchangeId::binance(),
            std::make_shared<BinanceApiClient>(credentials, 
            std::make_shared<RateLimitController>(core::ExchangeId::binance())));

        // Create other components
        auto order_generator = std::make_shared<OrderGenerator>();
        auto smart_order_router = std::make_shared<SmartOrderRouter>();
        auto order_type_selector = std::make_shared<OrderTypeSelector>();
        auto execution_algorithm_factory = std::make_shared<ExecutionAlgorithmFactory>();
        auto order_lifecycle_tracker = std::make_shared<OrderLifecycleTracker>();
        auto fill_reconciliation = std::make_shared<FillReconciliation>();

        // **CRITICAL: Initialize the smart order router with exchange information**
        std::vector<ExchangeInfo> exchanges;
        ExchangeInfo binanceInfo;
        binanceInfo.exchangeId = core::ExchangeId::binance();
        binanceInfo.trading_fee = 0.001;
        binanceInfo.liquidity_score = 0.9;
        binanceInfo.latency_ms = 50;
        binanceInfo.success_rate = 0.99;
        exchanges.push_back(binanceInfo);
        
        // After initializing the router, add this debug check:
        smart_order_router->initialize(exchanges);

        // Debug: Test if the exchange supports our trading pairs
        std::cout << "Testing exchange support for BTC/USDT: " << 
            (binanceInfo.supports_trading_pair(core::Symbol("BTC/USDT")) ? "YES" : "NO") << std::endl;

        std::vector<std::string> available = smart_order_router->getAvailableExchanges(core::Symbol("BTC/USDT"));
        std::cout << "Available exchanges for BTC/USDT: " << available.size() << std::endl;
        for (const auto& ex : available) {
            std::cout << "  - " << ex << std::endl;
        }
        exchangeGateway_ = std::make_shared<common::ExchangeGateway>(api_client_manager);
        if (!exchangeGateway_->isHealthy()) {
            std::cout << "Exchange Gateway is not healthy!" << std::endl;
            std::cout << "start using fallback ExecutionService" << std::endl;
            executionService_ = std::make_shared<ExecutionService>(
            order_generator,
            smart_order_router,
            order_type_selector,
            execution_algorithm_factory,
            order_lifecycle_tracker,
            fill_reconciliation,
            api_client_manager
            );
        } else {
            std::cout << "Exchange Gateway initialized and healthy." << std::endl;
            executionService_ = std::make_shared<ExecutionService>(
            exchangeGateway_,
            order_generator,
            smart_order_router,
            order_type_selector,
            execution_algorithm_factory,
            order_lifecycle_tracker,
            fill_reconciliation
            );
        }       
       
    }
    bool initialize() {
        initializeExecutionService();
        // Debug: Verify execution service and router
        if (!executionService_) {
            std::cerr << "Execution Service failed to initialize!" << std::endl;
            return false;
        }
        std::cout << "=== INITIALIZING FULL TRADING SYSTEM ===" << std::endl;
        
        // 1. Initialize event bus first
        std::cout << "Starting event bus..." << std::endl;
        common::EventBus::getInstance().start();
        
        // 2. Initialize data ingestion service
        std::cout << "Initializing data ingestion service..." << std::endl;
        dataIngestionService_ = std::make_unique<data_ingestion::DataIngestionService>(
            "config/data_ingestion.json");
        
        if (!dataIngestionService_->initialize()) {
            std::cerr << "Failed to initialize data ingestion service" << std::endl;
            return false;
        }
        
        // 3. Initialize risk manager
        std::cout << "Initializing risk manager..." << std::endl;
        // riskManager_ = std::make_unique<risk::RiskManager>(executionService_, riskConfig_);
        // ✅ NEW - Use the factory method
        auto riskManager_ = RiskManager::create(executionService_, riskConfig_);
        if (!riskManager_) {
            LOG_ERROR("Application", "Failed to initialize risk manager");
            return false;
        }
        std::cout << "RiskManager constructor called executionService_ with config. Start initializing..." << std::endl;
        // sleep for 5s
        std::this_thread::sleep_for(std::chrono::seconds(1));
        if (!riskManager_->initialize()) {
            std::cerr << "Failed to initialize risk manager" << std::endl;
            return false;
        }
        
        // 4. Initialize strategy engine
        std::cout << "Initializing strategy engine..." << std::endl;
        strategyEngine_ = std::make_unique<strategy::StrategyEngine>(StrategyId("integrated_strategy_test_v1"));
        
        if (!strategyEngine_->initialize(strategyConfig_)) {
            std::cerr << "Failed to initialize strategy engine" << std::endl;
            return false;
        }
        
        // 5. Initialize execution engine
        std::cout << "Initializing execution engine..." << std::endl;
        setupExecutionEngine();
        
        // 6. Setup component connections
        setupComponentConnections();
        
        std::cout << "System initialization complete!" << std::endl;
        return true;
    }
    
    bool start() {
        if (!running_) {
            std::cout << "\n=== STARTING FULL TRADING SYSTEM ===" << std::endl;
            
            running_ = true;
            systemHealthy_ = true;
            
            // Start services in order
            std::vector<std::string> symbols = {
                "BTC/USDT", "ETH/USDT", "BNB/USDT", "ADA/USDT"
            };
            
            // 1. Start data ingestion
            std::cout << "Starting data ingestion with symbols: ";
            for (const auto& symbol : symbols) {
                std::cout << symbol << " ";
            }
            std::cout << std::endl;
            
            if (!dataIngestionService_->start(symbols)) {
                std::cerr << "Failed to start data ingestion service" << std::endl;
                return false;
            }
            
            // 2. Start strategy engine
            if (!strategyEngine_->start()) {
                std::cerr << "Failed to start strategy engine" << std::endl;
                return false;
            }
            
            // 3. Start monitoring threads
            startMonitoring();
            
            std::cout << "🚀 FULL TRADING SYSTEM IS NOW LIVE!" << std::endl;
            std::cout << "Monitoring system health and processing..." << std::endl;
            
            return true;
        }
        return false;
    }
    
    void runDemo(int durationMinutes = 5) {
        std::cout << "\n=== RUNNING INTEGRATED DEMO FOR " << durationMinutes << " MINUTES ===" << std::endl;
        
        auto startTime = std::chrono::steady_clock::now();
        auto endTime = startTime + std::chrono::minutes(durationMinutes);
        
        // Demo phases
        demonstrateDataFlow();
        std::this_thread::sleep_for(30s);
        
        demonstrateSignalGeneration();
        std::this_thread::sleep_for(30s);
        
        demonstrateRiskValidation();
        std::this_thread::sleep_for(30s);
        
        demonstrateExecution();
        std::this_thread::sleep_for(30s);
        
        // Continue running and monitoring
        while (running_ && std::chrono::steady_clock::now() < endTime) {
            std::this_thread::sleep_for(1s);
            
            // Periodic health checks
            if (!checkSystemHealth()) {
                std::cout << "⚠️  System health issues detected!" << std::endl;
                systemHealthy_ = false;
            }
        }
        
        printFinalReport();
    }
    
    void shutdown() {
        if (running_) {
            std::cout << "\n=== SHUTTING DOWN TRADING SYSTEM ===" << std::endl;
            
            running_ = false;
            
            // Stop services in reverse order
            if (strategyEngine_) {
                strategyEngine_->stop();
            }
            
            if (dataIngestionService_) {
                dataIngestionService_->stop();
            }
            
            // Stop monitoring threads
            stopMonitoring();
            
            // Stop event bus last
            common::EventBus::getInstance().stop();
            
            std::cout << "System shutdown complete" << std::endl;
        }
    }

private:
    // Core components
    std::unique_ptr<data_ingestion::DataIngestionService> dataIngestionService_;
    std::unique_ptr<strategy::StrategyEngine> strategyEngine_;
    std::unique_ptr<risk::RiskManager> riskManager_;
    std::shared_ptr<execution::ExecutionService> executionService_;
    // gateway
    std::shared_ptr<common::ExchangeGateway> exchangeGateway_;

    // Configurations
    risk::RiskConfig riskConfig_;
    strategy::StrategyConfig strategyConfig_;
    
    // System state
    std::atomic<bool> running_;
    std::atomic<bool> systemHealthy_;
    
    // Monitoring threads
    std::vector<std::thread> monitoringThreads_;
    
    // Statistics
    std::atomic<size_t> orderBookUpdatesReceived_{0};
    std::atomic<size_t> signalsGenerated_{0};
    std::atomic<size_t> signalsValidated_{0};
    std::atomic<size_t> signalsApproved_{0};
    std::atomic<size_t> signalsRejected_{0};
    std::atomic<size_t> ordersExecuted_{0};
    std::atomic<size_t> riskEventsTriggered_{0};
    
    void setupConfigurations() {
        // Create configuration files
        createDataIngestionConfig();
        createStrategyConfig();
        createRiskConfig();
    }
    
    void createDataIngestionConfig() {
        const char* configContent = R"({
    "exchanges": {
        "BINANCE": {
            "id": "BINANCE",
            "name": "Binance",
            "ws_url": "wss://stream.binance.com:9443/ws",
            "rest_url": "https://api.binance.com",
            "symbols": ["BTC/USDT", "ETH/USDT", "BNB/USDT", "ADA/USDT"],
            "connection_timeout_ms": 5000,
            "max_reconnect_attempts": 3
        }
    },
    "order_book": {
        "depth": 20,
        "snapshot_interval_ms": 30000
    },
    "publisher": {
        "zmq_endpoint": "tcp://*:5555",
        "max_queue_size": 1000,
        "publish_interval_ms": 100
    }
})";
        
        std::ofstream configFile("system_config.json");
        configFile << configContent;
        configFile.close();
    }
    
    void createStrategyConfig() {
      
        std::string configPath = "config/strategy_config.json";
        strategyConfig_ = loadStrategyConfigFromFile(configPath);
            //  std::map<std::string, core::ExchangeId> tradingPairs;
        for (const auto& [name, pair] : strategyConfig_.tradingPairs) {
            std::cout << "Configured trading pair: " << name 
                      << " on exchange " << pair.toString() << std::endl;            
        }
    }

    void createRiskConfig() {
        // Setup risk configuration (similar to risk demo)
        // load config from file
        std::string configPath = "config/risk_config.json";
        riskConfig_.loadFromFile(configPath);
        if (!riskConfig_.validate()) {
            std::cerr << "Invalid risk configuration!" << std::endl;
        }
        auto& positionConfig = const_cast<PositionLimitConfig&>(riskConfig_.getPositionLimitConfig());
        positionConfig.assetLimits["BTC"] = 2.0;
        positionConfig.assetLimits["ETH"] = 20.0;
        positionConfig.assetLimits["BNB"] = 100.0;
        positionConfig.assetLimits["ADA"] = 10000.0;
        positionConfig.defaultLimit = 5000.0;
        positionConfig.enforceStrictly = true;
        
        auto& exposureConfig = const_cast<ExposureLimitConfig&>(riskConfig_.getExposureLimitConfig());
        exposureConfig.maxTotalExposure = 100000.0;
        exposureConfig.maxAssetExposure = 50000.0;
        exposureConfig.maxStrategyExposure = 75000.0;
        exposureConfig.maxExchangeExposure = 80000.0;
    }
    
    void setupStatistics() {
        // Reset all counters
        orderBookUpdatesReceived_ = 0;
        signalsGenerated_ = 0;
        signalsValidated_ = 0;
        signalsApproved_ = 0;
        signalsRejected_ = 0;
        ordersExecuted_ = 0;
        riskEventsTriggered_ = 0;
    }
    
    void setupExecutionEngine() {
        // This would normally be more complex, but for demo purposes
        // we'll create a simple mock execution engine
        std::cout << "Execution engine setup (mock implementation for demo)" << std::endl;
    }
    
    void setupComponentConnections() {
        std::cout << "Setting up component connections..." << std::endl;
        
        // 1. Connect data ingestion to strategy engine
        dataIngestionService_->setOrderBookCallback([this](const auto& update) {
            orderBookUpdatesReceived_++;
            
            // Convert to OrderBook and send to strategy
            auto orderBook = std::make_shared<data_ingestion::OrderBook>(
                update.exchangeId, update.tradingPair);
            
            // Apply the update to create a valid order book
            orderBook->applySnapshot(update.bids, update.asks, 
                                   update.sequenceNumber, update.exchangeTimestamp);
            
            // Send to strategy engine
            if (strategyEngine_ && strategyEngine_->processOrderBook(*orderBook)) {
                // Order book processed successfully
            }
        });
        
        // 2. Connect strategy engine to risk manager
        strategyEngine_->registerSignalCallback([this](const strategy::TradingSignal& signal) {
            signalsGenerated_++;
            
            // Convert strategy signal to execution signal for risk validation
            execution::TradingSignal execSignal;
            execSignal.id = signal.id;
            execSignal.symbol = signal.symbol.toString();
            execSignal.direction = signal.direction;
            execSignal.strength = signal.strength;
            execSignal.targetSize = signal.targetSize;
            execSignal.strategyId = signal.strategyId;
            
            // Validate with risk manager
            if (riskManager_) {
                auto validationResult = riskManager_->validateSignal(execSignal);
                signalsValidated_++;
                
                if (validationResult.approved) {
                    signalsApproved_++;
                    std::cout << "✅ Signal " << signal.id << " approved for execution" << std::endl;
                    
                    // Here we would normally send to execution engine
                    simulateExecution(execSignal);
                } else {
                    signalsRejected_++;
                    std::cout << "❌ Signal " << signal.id << " rejected by risk management" << std::endl;
                }
            }
        });
        
        // 3. Connect risk manager events
        riskManager_->registerRiskEventCallback([this](const risk::RiskEvent& event) {
            riskEventsTriggered_++;
            std::cout << "🚨 Risk Event: " << event.description 
                      << " (Severity: " << event.severity << ")" << std::endl;
        });
    }
    
    void startMonitoring() {
        std::cout << "Starting monitoring threads..." << std::endl;
        
        // System health monitor
        monitoringThreads_.emplace_back([this]() {
            while (running_) {
                std::this_thread::sleep_for(10s);
                
                if (running_) {
                    bool healthy = checkSystemHealth();
                    if (!healthy && systemHealthy_) {
                        systemHealthy_ = false;
                        std::cout << "⚠️  System health degraded!" << std::endl;
                    } else if (healthy && !systemHealthy_) {
                        systemHealthy_ = true;
                        std::cout << "✅ System health restored!" << std::endl;
                    }
                }
            }
        });
        
        // Statistics reporter
        monitoringThreads_.emplace_back([this]() {
            while (running_) {
                std::this_thread::sleep_for(30s);
                if (running_) {
                    printPeriodicStats();
                }
            }
        });
    }
    
    void stopMonitoring() {
        for (auto& thread : monitoringThreads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        monitoringThreads_.clear();
    }
    
    bool checkSystemHealth() {
        bool healthy = true;
        
        // Check data ingestion health
        if (!dataIngestionService_ || !dataIngestionService_->isHealthy()) {
            healthy = false;
        }
        
        // Check strategy engine health
        if (!strategyEngine_) {
            healthy = false;
        }
        
        // Check risk manager health
        if (!riskManager_ || !riskManager_->checkSystemHealth()) {
            healthy = false;
        }
        
        return healthy;
    }
    
    void simulateExecution(const execution::TradingSignal& signal) {
        ordersExecuted_++;
        
        // Simulate execution delay
        std::thread([this, signal]() {
            std::this_thread::sleep_for(100ms);
            
            // Create mock execution report
            execution::models::ExecutionReport report;
            report.setOrderId("ORDER_" + signal.id);
            report.setSymbol(signal.symbol);
            report.setSide(signal.direction == core::Direction::BUY ? 
                          execution::models::OrderSide::BUY : 
                          execution::models::OrderSide::SELL);
            report.setQuantity(signal.targetSize);
            report.setFilledQuantity(signal.targetSize);
            report.setStatus(execution::models::OrderState::FILLED);
            
            // Send back to risk manager for position tracking
            if (riskManager_) {
                riskManager_->processExecutionReport(report);
            }

            std::cout << "📈 Executed: " << signal.symbol.toString()
                      << " " << (signal.direction == core::Direction::BUY ? "BUY" : "SELL")
                      << " " << signal.targetSize << std::endl;
        }).detach();
    }
    
    void demonstrateDataFlow() {
        std::cout << "\n--- Demonstrating Data Flow ---" << std::endl;
        std::cout << "Monitoring order book updates..." << std::endl;
        
        // Wait for some order book updates
        size_t initialUpdates = orderBookUpdatesReceived_.load();
        auto startTime = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - startTime < 10s) {
            std::this_thread::sleep_for(1s);
            size_t currentUpdates = orderBookUpdatesReceived_.load();
            if (currentUpdates > initialUpdates + 10) {
                break;
            }
        }
        
        std::cout << "Order book updates flowing: " 
                  << (orderBookUpdatesReceived_.load() - initialUpdates) << " received" << std::endl;
    }
    
    void demonstrateSignalGeneration() {
        std::cout << "\n--- Demonstrating Signal Generation ---" << std::endl;
        std::cout << "Waiting for strategy signals..." << std::endl;
        
        size_t initialSignals = signalsGenerated_.load();
        auto startTime = std::chrono::steady_clock::now();
        
        while (std::chrono::steady_clock::now() - startTime < 30s) {
            std::this_thread::sleep_for(1s);
            size_t currentSignals = signalsGenerated_.load();
            if (currentSignals > initialSignals) {
                std::cout << "Signal generated! Total: " << currentSignals << std::endl;
                break;
            }
        }
        
        if (signalsGenerated_.load() == initialSignals) {
            std::cout << "No signals generated yet (normal for demo)" << std::endl;
        }
    }
    
    void demonstrateRiskValidation() {
        std::cout << "\n--- Demonstrating Risk Validation ---" << std::endl;
        
        // Create some test signals to validate
        std::vector<execution::TradingSignal> testSignals = {
            createTestSignal("TEST_001", "BTC/USDT", core::Direction::BUY, 0.1),
            createTestSignal("TEST_002", "ETH/USDT", core::Direction::SELL, 2.0),
            createTestSignal("TEST_003", "BTC/USDT", core::Direction::BUY, 10.0)  // Large size
        };
        
        for (const auto& signal : testSignals) {
            auto result = riskManager_->validateSignal(signal);
            signalsValidated_++;
            
            if (result.approved) {
                signalsApproved_++;
                std::cout << "✅ Test signal " << signal.id << " approved" << std::endl;
            } else {
                signalsRejected_++;
                std::cout << "❌ Test signal " << signal.id << " rejected: ";
                for (const auto& violation : result.violations) {
                    std::cout << violation.description << " ";
                }
                std::cout << std::endl;
            }
        }
    }
    
    void demonstrateExecution() {
        std::cout << "\n--- Demonstrating Execution Flow ---" << std::endl;
        
        // Create and process a valid signal
        auto testSignal = createTestSignal("EXEC_001", "BTC/USDT", core::Direction::BUY, 0.0000060);
        auto result = riskManager_->validateSignal(testSignal);
        
        if (result.approved) {
            std::cout << "Executing approved signal..." << std::endl;
            simulateExecution(testSignal);
        } else {
            std::cout << "Signal not approved for execution" << std::endl;
        }
    }
    
    execution::TradingSignal createTestSignal(const std::string& id, const std::string& pair,
                                             core::Direction direction, double size) {
        execution::TradingSignal signal;
        signal.id = id;
        signal.symbol = pair;
        signal.direction = direction;
        signal.strength = 0.7;
        signal.targetSize = size;
        signal.strategyId = core::StrategyId::fromString("test v1.1");
        // signal.strategyVersion = "1.0";
        return signal;
    }
    
    void printPeriodicStats() {
        std::cout << "\n=== SYSTEM STATISTICS ===" << std::endl;
        std::cout << "Order Book Updates: " << orderBookUpdatesReceived_.load() << std::endl;
        std::cout << "Signals Generated: " << signalsGenerated_.load() << std::endl;
        std::cout << "Signals Validated: " << signalsValidated_.load() << std::endl;
        std::cout << "Signals Approved: " << signalsApproved_.load() << std::endl;
        std::cout << "Signals Rejected: " << signalsRejected_.load() << std::endl;
        std::cout << "Orders Executed: " << ordersExecuted_.load() << std::endl;
        std::cout << "Risk Events: " << riskEventsTriggered_.load() << std::endl;
        std::cout << "System Health: " << (systemHealthy_.load() ? "HEALTHY" : "DEGRADED") << std::endl;
        std::cout << "=========================" << std::endl;
    }
    
    void printFinalReport() {
        std::cout << "\n=== FINAL INTEGRATION DEMO REPORT ===" << std::endl;
        std::cout << "Total Order Book Updates: " << orderBookUpdatesReceived_.load() << std::endl;
        std::cout << "Total Signals Generated: " << signalsGenerated_.load() << std::endl;
        std::cout << "Total Signals Validated: " << signalsValidated_.load() << std::endl;
        std::cout << "Total Signals Approved: " << signalsApproved_.load() << std::endl;
        std::cout << "Total Signals Rejected: " << signalsRejected_.load() << std::endl;
        std::cout << "Total Orders Executed: " << ordersExecuted_.load() << std::endl;
        std::cout << "Total Risk Events: " << riskEventsTriggered_.load() << std::endl;
        
        if (signalsValidated_.load() > 0) {
            double approvalRate = (double)signalsApproved_.load() / signalsValidated_.load() * 100.0;
            std::cout << "Signal Approval Rate: " << std::fixed << std::setprecision(2) 
                      << approvalRate << "%" << std::endl;
        }
        
        std::cout << "Final System Health: " << (systemHealthy_.load() ? "HEALTHY" : "DEGRADED") << std::endl;
        std::cout << "\n🎉 INTEGRATION DEMO COMPLETED SUCCESSFULLY!" << std::endl;
        std::cout << "=====================================\n" << std::endl;
    }
};

// Global demo instance for signal handling
std::unique_ptr<FullSystemDemo> demoInstance;

void signalHandler(int signal) {
    std::cout << "\nShutdown signal received..." << std::endl;
    if (demoInstance) {
        demoInstance->shutdown();
    }
    exit(0);
}

int main(int argc, char* argv[]) {
    // Setup signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    try {
        std::cout << "🚀 STARTING FULL TRADING SYSTEM INTEGRATION DEMO 🚀" << std::endl;
        std::cout << "=====================================================" << std::endl;
        
        // Create demo instance
        demoInstance = std::make_unique<FullSystemDemo>();
        
        // Initialize system
        if (!demoInstance->initialize()) {
            std::cerr << "Failed to initialize trading system" << std::endl;
            return 1;
        }
        
        // Start system
        if (!demoInstance->start()) {
            std::cerr << "Failed to start trading system" << std::endl;
            return 1;
        }
        
        // Determine demo duration
        int durationMinutes = 5;  // Default 5 minutes
        if (argc > 1) {
            durationMinutes = std::stoi(argv[1]);
        }
        
        std::cout << "\nSystem is now running!" << std::endl;
        std::cout << "Demo will run for " << durationMinutes << " minutes" << std::endl;
        std::cout << "Press Ctrl+C to stop early" << std::endl;
        
        // Run demo
        demoInstance->runDemo(durationMinutes);
        
        // Cleanup
        demoInstance->shutdown();
        demoInstance.reset();
        
    } catch (const std::exception& e) {
        std::cerr << "Demo failed with exception: " << e.what() << std::endl;
        if (demoInstance) {
            demoInstance->shutdown();
        }
        return 1;
    }
    
    return 0;
}