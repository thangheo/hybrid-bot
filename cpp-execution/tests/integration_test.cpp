#include "integration_test.hpp"
#include "strategy/core_analysis/regime_subscriber.hpp"
#include "strategy/exit/adaptive_exit_strategy.hpp"
#include "common/quantity_precision.hpp"  
#include "strategy/pattern/imbalance_detection_engine.hpp"
#include "risk/interfaces/execution_interface.hpp"
#include "risk/risk_types.hpp"
#include "strategy/types/market_regime.hpp"
#include "common/thread_pool.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include "common/logger.hpp"
#include "execution/models/order.hpp"
#include "execution/exchanges/mock_api_client.hpp"

#include <memory>
#include <unordered_map>
#include <filesystem>

// Use fully-qualified ::execution::SymbolPrecisionManager to avoid ambiguity
using namespace ::trading::core;
using namespace ::trading::execution;
using namespace ::trading::risk;
using namespace ::trading::common;
// using namespace ::trading::intelligence;
using namespace ::trading;

// Forward-declare AdaptiveExitStrategy and provide a file-scoped pointer to allow legacy code
namespace trading {
namespace strategy {
class AdaptiveExitStrategy;
} // namespace strategy
} // namespace trading
 
  

INTEGRATION_TEST::INTEGRATION_TEST() : running_(false), systemHealthy_(true) {
    setupConfigurations();
    ZmqServerConfig serverConfig;
    //create shared context for server and client
    // use a single shared_context variable for server/client ZMQ usage
    zmq_context_ = std::make_shared<zmq::context_t>(1);
    // zmq_context_ = shared_context;

    serverConfig.pub_endpoint = SERVER_PUBLISHER_ENDPOINT; // internal endpoint for orderbook
    serverConfig.rep_endpoint = SERVER_REQUEST_ENDPOINT; // request endpoint for risk
    serverConfig.io_threads = 1;
    //use default config for now
    zmq_server_ = std::make_shared<GenericZmqServer>(serverConfig, zmq_context_);
    zmq_server_->registerRequestHandler("ping", [](const std::string& request, 
                                            const std::unordered_map<std::string, std::string>& metadata) -> std::string {
        nlohmann::json response;
        response["status"] = "pong";
        response["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return response.dump();
    });
    
    ZmqClientConfig clientConfig;
    //no long use socket but use inproc
    clientConfig.pub_endpoints = {SERVER_PUBLISHER_ENDPOINT};
    clientConfig.req_endpoint = SERVER_REQUEST_ENDPOINT; // request endpoint for risk
    // clientConfig.auto_reconnect = true;
    // clientConfig.request_timeout = std::chrono::milliseconds(30000); // 30 seconds
    zmq_client_ = std::make_unique<GenericZmqClient>(clientConfig, zmq_context_);
    
    riskConfig_.zmqSubscriberEndpoint = SERVER_PUBLISHER_ENDPOINT;
    riskConfig_.zmqPublisherEndpoint = SERVER_REQUEST_ENDPOINT;
    // marketDataInterface_ = std::make_shared<trading::risk::MarketDataInterface>(riskConfig_);

    positionSizer_ = std::make_unique<trading::risk::PositionSizer>(riskConfig_.getPositionSizingConfig());
    
}

INTEGRATION_TEST::~INTEGRATION_TEST() {
    shutdown();
}

bool INTEGRATION_TEST::initialize() {
   
    // First, initialize the execution service
    setupExecutionEngine();
    // Debug: Verify execution service and router
    if (!executionService_) {
        std::cerr << "Execution Service failed to initialize!" << std::endl;
        return false;
    }     
    
    // 1. Initialize event bus first
    common::EventBus::getInstance().start();
    
    // 2. Initialize data ingestion service
    dataIngestionService_ = std::make_unique<data_ingestion::DataIngestionService>(
        "config/data_ingestion.json");
    //set comm server
    dataIngestionService_->setCommServer(zmq_server_); 
    if (!zmq_server_->isRunning()) {
        if (!zmq_server_->start()) {
            std::cerr << "Failed to start internal ZMQ server during initialization: " << zmq_server_->getLastError() << std::endl;
            return false;
        }
        // brief pause to let sockets bind
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // Short startup pause - scale with marketDataUpdaterIntervalMs to avoid long blocking
    int startupWaitMs = std::min(2000, std::max(500, strategyConfig_.marketDataUpdaterIntervalMs * 5));
    std::this_thread::sleep_for(std::chrono::milliseconds(startupWaitMs));
    if (!dataIngestionService_->initialize()) {
        std::cerr << "Failed to initialize data ingestion service" << std::endl;
        return false;
    }
    // 3. Initialize market data interface
    if (!marketDataInterface_) { 
        auto mdi_base = MarketDataInterface::createDataInterface(
            riskConfig_, std::move(zmq_client_), zmq_context_);
        marketDataInterface_ = std::dynamic_pointer_cast<MarketDataInterface>(mdi_base);
        if (!marketDataInterface_) {
            std::cerr << "Failed to obtain MarketDataInterface singleton" << std::endl;
            return false;
        }
    }

    if (!marketDataInterface_->initialize()) {
        std::cerr << "Failed to initialize MarketDataInterface" << std::endl;
        return false;
    }
    // Diagnostic: log health & cache stats immediately after initialization
    try {
        auto hs = marketDataInterface_->getHealthStatus();
        LOG_INFO("MarketDataInterface health - cacheEntries={}, validEntries={}, cacheHealthy={}, subscriberHealthy={}",
                 hs.cacheStats.totalEntries, hs.cacheStats.validEntries, hs.cacheHealthy, hs.subscriberHealthy);
        // Also log pointer/address for easier debugging if workers see nullptr
        LOG_INFO("MarketDataInterface pointer={}", static_cast<const void*>(marketDataInterface_.get()));
    } catch (const std::exception& e) {
        LOG_WARN("Failed to query MarketDataInterface health after init: {}", e.what());
    }
    // 3. Initialize risk manager
             
    std::shared_ptr<trading::risk::PositionTracker> sharedPositionTracker = 
        std::make_shared<trading::risk::PositionTracker>(riskConfig_, marketDataInterface_);
    
    bool skipRisk = false;
    const char* noRiskEnv = std::getenv("INTEGRATION_TEST_NO_RISK");
    if (noRiskEnv && std::string(noRiskEnv) == "1") {
        skipRisk = true;
        std::cout << "INTEGRATION_TEST: skipping risk manager (INTEGRATION_TEST_NO_RISK=1)" << std::endl;
    }

    if (!skipRisk) {
        riskManager_ = trading::risk::RiskManager::create(
        executionService_, 
        riskConfig_, 
        marketDataInterface_,
        zmq_context_,
        sharedPositionTracker  // Pass shared PositionTracker
    );
        if (!riskManager_) {
        LOG_ERROR("Application", "Failed to initialize risk manager");
        return false;
    }
    if (!riskManager_->initialize()) {
        std::cerr << "Failed to initialize risk manager" << std::endl;
        return false;
    }
    } else {
        std::cout << "Integration test will not use RiskManager; proceeding without risk components" << std::endl;
    }
    
    // 4. Initialize strategy engine
    std::cout << "Initializing strategy engine..." << std::endl;
    strategyEngine_ = std::make_unique<strategy::StrategyEngine>(StrategyId("integrated_strategy_test_v1"));
    
    if (!strategyEngine_->initialize(strategyConfig_)) {
        std::cerr << "Failed to initialize strategy engine" << std::endl;
        return false;
    }
     
    // Load symbol volatility configuration
    std::string volatilityConfigPath = "config/symbol_volatility_config.json";
    if (!strategyEngine_->loadSymbolVolatilityConfig(volatilityConfigPath)) {
        std::cout << "Warning: Failed to load symbol volatility config from " << volatilityConfigPath << std::endl;
        std::cout << "Using default volatility thresholds..." << std::endl;
    } else {
        std::cout << "Successfully loaded symbol volatility configuration" << std::endl;
    }
      
    lastPriceUpdate_ = std::chrono::steady_clock::now();
    // 6. Setup component connections
    setupComponentConnections();
    
    std::cout << "System initialization complete!" << std::endl;
    return true;
}
 

bool INTEGRATION_TEST::start() {
    if (!running_) {
        std::cout << "\n=== STARTING FULL TRADING SYSTEM ===" << std::endl;
        
        running_ = true;
        systemHealthy_ = true;
        startTime_ = std::chrono::steady_clock::now();
        
        if (!zmq_server_->isRunning()) {
            if (!zmq_server_->start()) {
                std::cerr << "Failed to start internal ZMQ server: " << zmq_server_->getLastError() << std::endl;
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
 
        //load symbols from strategy config
        std::vector<Symbol> symbols;
        for (const auto& [name, pair] : strategyConfig_.tradingPairs) {
            symbols.push_back(name);
        } 
        // 1. Start data ingestion
        std::cout << "Starting data ingestion with symbols: ";
        for (const auto& symbol : symbols) {
            std::cout << symbol << " ";
        }
        std::cout << std::endl;
        //order book depth from config
        int depth = 10;//strategyConfig_.orderBookDepth;        
        if (!dataIngestionService_->start(symbols,depth)) {
            std::cerr << "Failed to start data ingestion service" << std::endl;
            return false;
        }
        // Start orderbook worker pool after data ingestion starts
        startOrderbookWorkerPool(); 
     
        // ensure exec workers are active to consume the queue.
        startExecWorkerPool();
        // 2. Start strategy engine
      

        return true;
    }
    return false;
}
// Or run without time limit:
void INTEGRATION_TEST::runIndefinitely() {
    auto lastStatusTime = std::chrono::steady_clock::now();
    
    while (running_ && systemHealthy_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto now = std::chrono::steady_clock::now();
        
        // Print status every 30 seconds - simplified like full_bridge.cpp
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatusTime).count() >= 30) {
            lastStatusTime = now;
        }
         
    }
} 
    
void INTEGRATION_TEST::setupConfigurations() {
   
    // Ensure StrategyConfig contains trading pairs required for the integration test.
    // Without trading pairs data ingestion will subscribe to 0 symbols and orderbooks
    // will not be populated (leading to getLastPrice() returning nullopt).
    createStrategyConfig();
    createRiskConfig();                
}
    

    // Ad
void INTEGRATION_TEST::onExecutionResponse(const execution::ExecutionResponse& response) {
    std::cout << "📈 Execution: " << response.client_order_id
            << " Status: " << response.order_status  << std::endl;

    ordersExecuted_++;
     
} 

void INTEGRATION_TEST::createRiskConfig() {
    // Setup risk configuration (similar to risk demo)
    // load config from file
    std::string configPath = "config/risk_config.json";
    riskConfig_.loadFromFile(configPath);
    if (!riskConfig_.validate()) {
        std::cerr << "Invalid risk configuration!" << std::endl;
    } 
    auto& positionConfig = const_cast<PositionLimitConfig&>(riskConfig_.getPositionLimitConfig());
 
    // the JSON don't result in absent lookups. We defer to the JSON values if
    // present and only set the defaultLimit when missing.
    const double fallbackDefault = positionConfig.defaultLimit > 0.0 ? positionConfig.defaultLimit : 1000000.0;
    for (const auto& [symbol, exchange] : strategyConfig_.tradingPairs) {
        std::string base = symbol.base;
        if (base.empty()) continue;
        if (positionConfig.assetLimits.find(base) == positionConfig.assetLimits.end()) {
            positionConfig.assetLimits[base] = fallbackDefault;
        }
    }
    positionConfig.defaultLimit = 500000; // dont care because btc =110k while doge or other maybe 0.001,
    positionConfig.enforceStrictly = false;
    
    auto& exposureConfig = const_cast<ExposureLimitConfig&>(riskConfig_.getExposureLimitConfig());
    exposureConfig.maxTotalExposure = 50000.0;
    exposureConfig.maxAssetExposure = 50000.0;
    exposureConfig.maxStrategyExposure = 50000.0;
    exposureConfig.maxExchangeExposure = 50000.0;
}

void INTEGRATION_TEST::createStrategyConfig() {
    // Minimal config: subscribe to BTC/USDT on Binance for this integration test
    core::Symbol s("BTC/USDT");
    strategyConfig_.tradingPairs[s] = core::ExchangeId::binance();
    strategyConfig_.marketDataUpdaterIntervalMs = 1000;
    LOG_INFO("INTEGRATION_TEST", "Added trading pair {} for exchange {}", s.toString(), strategyConfig_.tradingPairs[s].toString());
}
 

void INTEGRATION_TEST::setupExecutionEngine() {
    std::cout << "=== SETTING UP EXECUTION ENGINE ===" << std::endl;
    // Create the authentication service first
    auto authService = std::make_shared<AuthenticationService>();
    // Create API client manager with auth service
    auto api_client_manager = std::make_shared<ApiClientManager>(authService);
    // Set up credentials and clients (your existing code)
    //get from environment .env file
    const char* apiKey = std::getenv("BINANCE_API_KEY");
    const char* apiSecret = std::getenv("BINANCE_API_SECRET");
    const char* useMockEnv = std::getenv("INTEGRATION_TEST_USE_MOCK_CLIENT");
    bool forceMock = useMockEnv && std::string(useMockEnv) == "1";
    if(!apiKey || !apiSecret || forceMock) {
        std::cerr << "Warning: Missing Binance credentials or mock mode; using MockApiClient for integration test." << std::endl;
        // Use a mock client for deterministic testing so CI can run without external secrets
        api_client_manager->addClient(core::ExchangeId::binance(), 
        std::make_shared<execution::MockApiClient>(ExchangeCredentials{}));
    } else {
        auto credentials = ExchangeCredentials{
            apiKey,
            apiSecret,
            "",
            true
        };
        authService->setCredentials(core::ExchangeId::binance(), credentials);
        api_client_manager->addClient(core::ExchangeId::binance(),
            std::make_shared<BinanceApiClient>(credentials, 
            std::make_shared<RateLimitController>(core::ExchangeId::binance())));
    }
    // If we're using live credentials, add the Binance client; otherwise the mock client
    if (apiKey && apiSecret && !forceMock) {
        auto credentials = ExchangeCredentials{apiKey, apiSecret, "", true};
        authService->setCredentials(core::ExchangeId::binance(), credentials);
        api_client_manager->addClient(core::ExchangeId::binance(),
            std::make_shared<BinanceApiClient>(credentials,
                std::make_shared<RateLimitController>(core::ExchangeId::binance())));
    }
    std::cout << "=== DEBUG API CLIENT MANAGER ===" << std::endl;
    std::cout << "ApiClientManager created, checking status..." << std::endl;
    if (!api_client_manager->initialize()) {
        std::cerr << "Failed to initialize ApiClientManager" << std::endl;
        systemHealthy_ = false;
        return;
    }
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
    // removed: binanceInfo.liquidityProfileScore (not present on ExchangeInfo)
    binanceInfo.latency_ms = 50;
    binanceInfo.success_rate = 0.99;
    exchanges.push_back(binanceInfo);
    // After initializing the router, add this debug check:
    smart_order_router->initialize(exchanges);
    
    std::shared_ptr<trading::risk::DataInterface> sharedMdIface;
    try {
        sharedMdIface = trading::risk::MarketDataInterface::createDataInterface(
            riskConfig_, /*injectedClient=*/std::unique_ptr<trading::communication::GenericZmqClient>(nullptr), zmq_context_);
    } catch (...) {
        sharedMdIface = nullptr;
    }
    exchangeGateway_ = std::make_shared<execution::ExchangeGateway>(api_client_manager, sharedMdIface);
    if (!exchangeGateway_->isHealthy()) {
        std::cout << "ExchangeId Gateway is not healthy!" << std::endl;
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
        executionService_->setLimitOrderHandling(trading::execution::ExecutionService::LimitOrderHandling::REJECT_IF_NO_PRICE);
    } else {
        std::cout << "ExchangeId Gateway initialized and healthy." << std::endl;
        executionService_ = std::make_shared<ExecutionService>(
        exchangeGateway_,
        order_generator,
        smart_order_router,
        order_type_selector,
        execution_algorithm_factory,
        order_lifecycle_tracker,
        fill_reconciliation
        );
        executionService_->setLimitOrderHandling(trading::execution::ExecutionService::LimitOrderHandling::REJECT_IF_NO_PRICE);
    }
    // Ensure the execution service is initialized and running. If initialization fails,
    // mark system unhealthy so callers do not attempt to send signals to a stopped service.
    if (executionService_) {
        bool init_ok = false;
        try {
            init_ok = executionService_->initialize();
        } catch (const std::exception& e) {
            std::cerr << "ExecutionService::initialize threw exception: " << e.what() << std::endl;
        }
        if (!init_ok) {
            std::cerr << "Failed to initialize ExecutionService - marking system unhealthy." << std::endl;
            systemHealthy_ = false;
            return;
        } else {
            std::cout << "ExecutionService initialized and running." << std::endl;
        }
    }
}

// Orderbook worker pool control
void INTEGRATION_TEST::startOrderbookWorkerPool() {
    if (orderbookWorkersRunning_.exchange(true)) return; // already running
    orderbookWorkers_.clear();
    for (size_t i = 0; i < orderbookWorkerCount_; ++i) {
        orderbookWorkers_.emplace_back([this, i]() {
            this->orderbookWorkerLoop(i);
        });
    }
    std::cout << "Started " << orderbookWorkers_.size() << " orderbook worker(s)" << std::endl;
}

void INTEGRATION_TEST::stopOrderbookWorkerPool() {
    if (!orderbookWorkersRunning_.exchange(false)) return;
    
    orderbookQueueCv_.notify_all();
    
    // Join all threads first
    for (auto &t : orderbookWorkers_) {
        if (t.joinable()) t.join();
    }
    
    // Clear containers and force deallocation
    orderbookWorkers_.clear();
    orderbookWorkers_.shrink_to_fit();
    
    {
        std::lock_guard<std::mutex> lk(orderbookQueueMutex_);
        orderbookQueue_.clear();
        orderbookQueue_.shrink_to_fit(); // Force deallocation
    }
    
    std::cout << "Orderbook worker pool stopped and memory cleared" << std::endl;
}


void INTEGRATION_TEST::stopStrategyOrderbookWorkerPool() {
    if (!strategyOrderbookWorkersRunning_.exchange(false)) return;
    strategyOrderbookQueueCv_.notify_all();
    for (auto &t : strategyOrderbookWorkers_) if (t.joinable()) t.join();
    strategyOrderbookWorkers_.clear();
    {
        std::lock_guard<std::mutex> lk(strategyOrderbookQueueMutex_);
        strategyOrderbookQueue_.clear();
    }
    std::cout << "Strategy orderbook worker pool stopped" << std::endl;
}

void INTEGRATION_TEST::orderbookWorkerLoop(size_t workerId) {
    std::vector<OrderBookUpdate> batch;
    batch.reserve(orderbookBatchSize_);
    std::cout << "Orderbook worker " << workerId << " started" << std::endl;
    while (orderbookWorkersRunning_.load()) {
        // Wait for data
        std::unique_lock<std::mutex> lk(orderbookQueueMutex_);
        orderbookQueueCv_.wait_for(lk, std::chrono::milliseconds(50), [this]() {
            return !orderbookQueue_.empty() || !orderbookWorkersRunning_.load();
        });

        if (!orderbookWorkersRunning_.load() && orderbookQueue_.empty()) break;

        // Pop up to batchSize items
        batch.clear();
        if (batch.capacity() > orderbookBatchSize_ * 2) {
            batch.shrink_to_fit();
            batch.reserve(orderbookBatchSize_);
        }
        for (size_t i = 0; i < orderbookBatchSize_ && !orderbookQueue_.empty(); ++i) {
            batch.push_back(std::move(orderbookQueue_.front()));
            orderbookQueue_.pop_front();
        }
        lk.unlock();

        if (batch.empty()) continue;

        // Process batch
        for (const auto &update : batch) {
            try {
               
                auto orderBook = dataIngestionService_->getOrderBook(update.exchangeId, update.tradingPair);
                
                if (orderBook) {
                    // publishOrderBookForRisk(update);                 
                    // Enqueue a shared ptr to the orderBook for strategy processing
                    try {
                        std::shared_ptr<OrderBook> ob_ptr = orderBook;
                        std::unique_lock<std::mutex> slk(strategyOrderbookQueueMutex_);
                        if (strategyOrderbookQueue_.size() < strategyOrderbookQueueLimit_) {
                            strategyOrderbookQueue_.push_back(ob_ptr);
                        } else {
                            // Drop oldest if full
                            strategyOrderbookQueue_.pop_front();
                            strategyOrderbookQueue_.push_back(ob_ptr);
                        }
                        slk.unlock();
                        strategyOrderbookQueueCv_.notify_one();
                    } catch (const std::exception &e) {
                        std::cerr << "Failed to enqueue orderbook for strategy: " << e.what() << std::endl;
                    }
                    
                }
            } catch (const std::exception &e) {
                std::cerr << "Orderbook worker " << workerId << " error: " << e.what() << std::endl;
            }
        }
    }
} 
  
// Execution worker pool control
void INTEGRATION_TEST::startExecWorkerPool() {
    if (execWorkersRunning_.exchange(true)) return;
    execWorkers_.clear();
    for (size_t i = 0; i < execWorkerCount_; ++i) {
        execWorkers_.emplace_back([this, i]() { this->execWorkerLoop(i); });
    }
    std::cout << "Started " << execWorkers_.size() << " exec worker(s)" << std::endl;
    LOG_TRACE("Exec workers started (count={})", execWorkers_.size());
}

void INTEGRATION_TEST::stopExecWorkerPool() {
    if (!execWorkersRunning_.exchange(false)) return;
    execQueueCv_.notify_all();
    for (auto &t : execWorkers_) if (t.joinable()) t.join();
    execWorkers_.clear();
    {
        std::lock_guard<std::mutex> lk(execQueueMutex_);
        execQueue_.clear();
    }
    std::cout << "Exec worker pool stopped" << std::endl;
}

void INTEGRATION_TEST::execWorkerLoop(size_t workerId) {
    while (execWorkersRunning_.load()) {
        execution::TradingSignal task;
        {
            std::unique_lock<std::mutex> lk(execQueueMutex_);
            // Diagnostic: Log when waiting so we can see wakeups
            LOG_TRACE("ExecWorker {} waiting. execQueueSize={} execWorkersRunning={}", workerId, execQueue_.size(), execWorkersRunning_.load());
            execQueueCv_.wait_for(lk, std::chrono::milliseconds(50), [this]() {
                return !execQueue_.empty() || !execWorkersRunning_.load();
            });
            if (!execWorkersRunning_.load() && execQueue_.empty()) break;
            if (execQueue_.empty()) continue;
            LOG_TRACE("ExecWorker {} woke up, execQueueSize={}", workerId, execQueue_.size());
            task = std::move(execQueue_.front());
            execQueue_.pop_front();
        }
        
        try {
            // Small delay to simulate processing/network latency for entries
            std::this_thread::sleep_for(50ms);

            std::cout << "🔄 ExecWorker processing signal " << task.id << " for " << task.symbol.toString() 
                      << " (execQueueSize=" << execQueue_.size() << ")" << std::endl;

            if (!executionService_) {
                std::cerr << "Execution service not initialized!" << std::endl;
                continue;
            }                               
            double notionalUsd = 20 ;//20$

            //get price from orderbook metric
            trading::strategy::OrderBookMetrics obMetrics;
            double currentPrice = obMetrics.midPrice;
            // --- Step 4: Get current price and convert to quantity ---

            if (currentPrice <= 0.0) {
                //fallback to data ingestion service
                auto priceOpt = dataIngestionService_->getLastPrice(task.exchangeId, task.symbol);
                if (priceOpt.has_value()) {
                    currentPrice = priceOpt.value();
                }
                if (currentPrice <= 0.0) {
                    LOG_WARN("ExecWorker: unable to fetch price for {} - skipping order", task.symbol.toString());
                    std::cout << "❌ Execution skipped for signal " << task.id 
                              << ": Price unavailable" << std::endl;
                    continue;
                }
            }
            
            double finalQty = notionalUsd / currentPrice;
            
            task.setQuantity(finalQty);
            // --- Step 7: Execute the signal ---
            std::cout << "🔄 Attempting execution for signal " << task.id << " on " << task.symbol.toString() << std::endl;
            auto t0 = std::chrono::steady_clock::now();
            ExecutionResponse response = executionService_->executeSignal(task);
            auto t1 = std::chrono::steady_clock::now();
            auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
            std::cout << "🔄 Execution response received: success=" << response.success << ", error='" << response.error_message << "' (exec_duration_ms=" << dur << ")" << std::endl;

            if (!response.success) {
                // Log a longer duration as warn for backpressure troubleshooting
                if (dur > 500) {
                    LOG_WARN("ExecWorker: executeSignal slow ({} ms) for {}: {}", dur, task.symbol.toString(), response.error_message);
                }             
                break;
            }
 
            std::cout << "📈 Executed: " << task.symbol.toString() << " " 
                      << (task.direction == core::Direction::BUY ? "BUY" : "SELL") 
                      << " qty=" << finalQty
                      << " notional=$" << (currentPrice * finalQty)
                      << std::endl;

         
            break; //no more trades in this loop
        } catch (const std::exception &e) {
            std::cerr << "ExecWorker " << workerId << " error: " << e.what() << std::endl;
        }
    }
}

void INTEGRATION_TEST::shutdown() {
    if (running_) {
        std::cout << "\n=== SHUTTING DOWN TRADING SYSTEM ===" << std::endl;
        
        running_ = false;
        // Stop data ingestion FIRST and wait for cleanup
        if (dataIngestionService_) {
            std::cout << "Stopping data ingestion service..." << std::endl;
            dataIngestionService_->stop();
            dataIngestionService_.reset();

            // Give WebSocket connections time to close gracefully
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }        
      
    // Stop exit monitor thread

   
    // Stop market data updater thread
    marketDataUpdaterRunning_.store(false);
    if (marketDataUpdaterThread_.joinable()) {
        marketDataUpdaterThread_.join();
    }
    // Stop worker pools
    stopOrderbookWorkerPool();
    stopExecWorkerPool();

    // Stop services in reverse order
    if (zmq_server_) {
        zmq_server_->stop();
    }
        if (riskManager_) {
            std::cout << "Stopping risk manager..." << std::endl;
            // riskManager_->
            riskManager_.reset();
        }
        if (executionService_) {
            std::cout << "Stopping execution service..." << std::endl;
            executionService_->shutdown();
            executionService_.reset();
        }
            
      
 
    // Stop event bus last
        std::cout << "Stopping event bus..." << std::endl;
        common::EventBus::getInstance().stop();

        // Join monitoring threads if any - ensure they terminate after running_==false
        for (auto &t : monitoringThreads_) {
            try {
                if (t.joinable()) t.join();
            } catch (...) {}
        }

        std::cout << "System shutdown complete" << std::endl;
    }
}

void INTEGRATION_TEST::realExecution(const execution::TradingSignal& signal) {
   
    // If we are running in training mode, do not execute orders
    if (operationMode_ == OperationMode::TRAINING) {
        LOG_INFO("INTEGRATION_TEST: Training mode active - skipping execution for signal {}", signal.id);
        return;
    }

    // Enqueue execution task to worker pool to avoid thread explosion
    ordersExecuted_++;
    std::unique_lock<std::mutex> lk(execQueueMutex_);
    if (execQueue_.size() >= execQueueLimit_) {
        // drop oldest
        execQueue_.pop_front();
    }
    execQueue_.push_back(signal);
    lk.unlock();
    execQueueCv_.notify_one();
}

void INTEGRATION_TEST::setupComponentConnections() {        
    // Minimal callback: validate & enqueue update to worker queue
    dataIngestionService_->setOrderBookCallback([this](auto update) {
        // Quick checks only - avoid heavy work here
        std::string symbolStr = update.tradingPair.toString();
        if (strategyConfig_.tradingPairs.find(symbolStr) == strategyConfig_.tradingPairs.end()) {
            return; // ignore unconfigured symbols early
        }

        // Enqueue (bounded)
        std::unique_lock<std::mutex> lk(orderbookQueueMutex_);
        // Count incoming messages
        messagesProcessed_.fetch_add(1);

        if (orderbookQueue_.size() >= orderbookQueueLimit_) {
            // Drop oldest to make space (or simply drop this update)
            orderbookQueue_.pop_front();
        }
        // Move the update into the queue to avoid copying large OrderBookUpdate objects.
        orderbookQueue_.push_back(std::move(update));
        lk.unlock();
        orderbookQueueCv_.notify_one();
    });         
    
    // 3. Connect risk manager events
    riskManager_->registerRiskEventCallback([this](const risk::RiskEvent& event) {
        riskEventsTriggered_++;
        std::cout << "🚨 Risk Event: " << event.description 
                    << " (Severity: " << event.severity << ")" << std::endl;
    });
 
    try {
        executionService_->registerExecutionCallback([this](const execution::ExecutionResponse& resp) {
            try {
                execution::models::ExecutionReport report;
                report.setOrderId(resp.client_order_id);
                report.setSymbol(resp.symbol);        // NEW
                report.setExchangeId(core::ExchangeId::fromString(resp.exchange_id)); // NEW
                report.setQuantity(resp.quantity);    // NEW
                report.setFilledQuantity(resp.filled_quantity); // NEW - ensure PositionTracker sees fills when available
                // report.setPrice(resp.);          // NEW
                LOG_INFO("INTEGRATION_TEST ,Execution callback received: clientOrder={} success={} filledQty={}",
                         resp.client_order_id, resp.success, resp.filled_quantity);
                if (resp.success) {
                    // Try to fetch the authoritative order state from the execution service
                    try {
                        auto status = executionService_->getOrderStatus(resp.client_order_id);
                        if (status.found && status.order) {
                            // Create report from order to capture fills/quantities
                            auto rpt_ptr = execution::models::ExecutionReport::createFromOrder(status.order);
                            if (rpt_ptr) {
                                report = *rpt_ptr; // copy
                            } else {
                                // Fallback to mark as FILLED if we couldn't create from order
                                report.setStatus(execution::models::OrderState::FILLED);
                            }
                        } else {
                          
                            report.setStatus(execution::models::OrderState::FILLED);
                        }
                    } catch (const std::exception& e) {
                        // If fetching status failed, still set FILLED to avoid losing event
                        std::cerr << "Warning: failed to enrich ExecutionResponse with order status: " << e.what() << std::endl;
                        report.setStatus(execution::models::OrderState::FILLED);
                    }
                } else {
                    report.setStatus(execution::models::OrderState::REJECTED);
                    report.setRejectReason(resp.error_message);
                }

                // Forward to RiskManager if configured; otherwise just log and skip
                if (riskManager_) {
                    riskManager_->processExecutionReport(report);
                } else {
                    // No RiskManager configured in this integration test run - just print
                    std::cout << "[INTEG] Exec callback (no-risk): clientOrder=" << resp.client_order_id
                              << " status=" << static_cast<int>(report.status()) << " qty=" << report.quantity() << " filled=" << report.filledQuantity() << std::endl;
                }
            } catch (...) {
                // swallow - reporting must not crash execution thread
            }
        });
        LOG_INFO("INTEGRATION_TEST", "Registered execution callback to forward reports to RiskManager");
    } catch (...) {
        LOG_WARN("INTEGRATION_TEST", "Failed to register execution callback on ExecutionService");
    }
    

    // Register post-trade signal checker so PostTradeMonitor can consult live strategy signals
    if (riskManager_ && strategyEngine_) {
    riskManager_->registerPostTradeSignalChecker([this](const core::ExchangeId& exchangeId, const core::Symbol& symbol) -> bool {
            try {
                auto signals = strategyEngine_->getActiveSignals();
                auto now = std::chrono::system_clock::now();
                for (const auto& s : signals) {
                    if (s.exchangeId == exchangeId && s.symbol == symbol && s.isBuySignal()) {
                        if (s.isValid(now)) return true;
                    }
                }
            } catch (...) {
            }         

            return false;
        });
    }

    // 4. Register exit callback for AdaptiveExitStrategy
    LOG_INFO("ExitCallback: 🔧 Setting up AdaptiveExitStrategy callback...");
    if (riskManager_) {
        LOG_INFO("ExitCallback: ✅ RiskManager is available");
        auto executionInterface = riskManager_->getExecutionInterface();
        if (executionInterface) {
           
            // Capture weak_ptrs to avoid extending lifetimes
            std::weak_ptr<trading::risk::ExecutionInterface> execWeak = executionInterface;
            std::weak_ptr<trading::risk::RiskManager> riskWeak = riskManager_;
        } 
    } 

    // 5. Subscribe to OrderBook metrics updates so exitMonitorLoop can use them
    if (metricsSubscriptionId_ == 0) {
        metricsSubscriptionId_ = common::EventBus::getInstance().subscribe<trading::events::MetricsUpdatedEvent>(
            [this](const trading::events::MetricsUpdatedEvent& evt) {
                try {
                    const auto& m = evt.metrics;
                    std::string key = m.exchangeId.toString() + ":" + m.symbol.toString();
                    std::lock_guard<std::mutex> lk(metricsMutex_);
                    latestOrderbookMetrics_[key] = m; // copy
                } catch (...) {
                    // swallow - caching is best-effort
                }
            }
        );
        LOG_INFO("Metrics subscription registered with id {}", metricsSubscriptionId_);
    }

    // Wire position lookup callback into ImbalanceDetectionEngine so strategy
    // can consult authoritative cached positions before emitting ENTRY signals.
    if (strategyEngine_ && riskManager_) {
        try {
            auto detector = strategyEngine_->getImbalanceDetector();
            if (detector) {
                // Capture weak_ptr to riskManager_ to avoid ownership cycles
                std::weak_ptr<trading::risk::RiskManager> riskWeakLocal = riskManager_;
                detector->setPositionLookupCallback(
                    [riskWeakLocal](const core::ExchangeId& exch, const core::Symbol& sym) -> std::optional<double> {
                        try {
                            auto rm = riskWeakLocal.lock();
                            if (!rm) return std::nullopt;
                            return rm->getCachedPosition(exch, sym);
                        } catch (...) {
                            return std::nullopt;
                        }
                    }
                );
                LOG_INFO("Wired positionLookup callback into ImbalanceDetectionEngine");
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to wire position lookup into ImbalanceDetectionEngine: {}", e.what());
        }
    }
}
 

// Simple helper to generate unique signal ids for manual test flows
static std::string generateSignalId() {
    static std::atomic<uint64_t> counter{1};
    std::stringstream ss;
    ss << "INTEG-" << std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::system_clock::now().time_since_epoch()).count()
       << "-" << counter.fetch_add(1);
    return ss.str();
}

int main() {
    // Create the trading system and initialize
    INTEGRATION_TEST ts;

    std::cout << "=== Starting integration test: single buy -> sell flow ===" << std::endl;

    if (!ts.initialize()) {
        std::cerr << "Failed to initialize trading system for integration test" << std::endl;
        return 1;
    }

    if (!ts.start()) {
        std::cerr << "Failed to start trading system for integration test" << std::endl;
        return 1;
    }

    // Exec worker pool is started by ts.start()

    // Give system a moment for all workers and data ingestion to warm up
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // Pick a symbol from the config; use BTC/USDT to be safe
    core::Symbol sym("BTC/USDT");

    // Build a BUY signal
    trading::execution::TradingSignal buy;
    buy.setId(generateSignalId());
    buy.setTradingPair(sym);
    buy.setDirection(core::Direction::BUY);
    buy.setExchangeId(core::ExchangeId::binance());
    buy.setTargetSize(0.001); // small size
    buy.setStrength(0.9);
    buy.setTtl(std::chrono::milliseconds(30000));
    buy.setStrategyId(StrategyId("integration_test"));

    std::cout << "Enqueueing BUY signal: " << buy.getSignalId() << " " << sym.toString() << std::endl;
    ts.realExecution(buy);

    // Wait for 1 second before placing exit (sell) order
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Build a SELL signal (exit)
    trading::execution::TradingSignal sell;
    sell.setId(generateSignalId());
    sell.setTradingPair(sym);
    sell.setDirection(core::Direction::SELL);
    sell.setExchangeId(core::ExchangeId::binance());
    sell.setTargetSize(0.001);
    sell.setStrength(0.9);
    sell.setTtl(std::chrono::milliseconds(30000));
    sell.setStrategyId(StrategyId("integration_test"));

    std::cout << "Enqueueing SELL signal: " << sell.getSignalId() << " " << sym.toString() << std::endl;
    ts.realExecution(sell);

    // Wait so the flow can complete and logs arrive. Adjust as needed.
    std::this_thread::sleep_for(std::chrono::seconds(10));

    std::cout << "=== Shutting down trading system (integration test) ===" << std::endl;
    ts.shutdown();

    return 0;
}
// command to run 
// export BINANCE_API_KEY=12312312
// export BINANCE_API_SECRET=asdfasdfdsf
// export BINANCE_TESTNET=true      # if you want testnet
// export HYBRID_LOG_LEVEL=TRACE   # recommended to get verbose logs