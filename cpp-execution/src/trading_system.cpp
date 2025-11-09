#include "trading_system.hpp"
#include "strategy/core_analysis/regime_subscriber.hpp"
#include "common/quantity_precision.hpp" // symbol precision helper for exchange-compliant quantities
#include "strategy/pattern/imbalance_detection_engine.hpp"
#include "risk/interfaces/execution_interface.hpp"
#include "risk/risk_types.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include "common/logger.hpp"
#include "execution/models/order.hpp"

// Use fully-qualified ::execution::SymbolPrecisionManager to avoid ambiguity
using namespace ::trading::core;
using namespace ::trading::execution;
using namespace ::trading::risk;
using namespace ::trading::common;
// using namespace ::trading::intelligence;

Trading_system::Trading_system() : running_(false), systemHealthy_(true) {
    setupConfigurations();
    setupStatistics();
    // Initialize adaptive intelligence
    adaptiveIntel_ = std::make_unique<trading::intelligence::AdaptiveMarketIntelligence>();
    
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
    LOG_INFO("Trading_system constructed - ZMQ server and client created (server={}, client={})",
             static_cast<const void*>(zmq_server_.get()), static_cast<const void*>(zmq_client_.get()));
    
    // Initialize MarketDataInterface
    // Subscriber should connect to the server's PUB endpoint; publisher endpoint
    // is the server's REQ/REP endpoint for requests. Ensure these are not swapped.
    riskConfig_.zmqSubscriberEndpoint = SERVER_PUBLISHER_ENDPOINT;
    riskConfig_.zmqPublisherEndpoint = SERVER_REQUEST_ENDPOINT;
    // marketDataInterface_ = std::make_shared<trading::risk::MarketDataInterface>(riskConfig_);

    // Use shared singleton MarketDataInterface to ensure a single cache instance.
    // Defer creating/initializing MarketDataInterface until initialize()
    // so we can ensure the internal ZMQ server is bound and the shared
    // ZMQ client/context can be injected before the subscriber starts.
    // Initialize position sizer with config loaded in riskConfig_
    positionSizer_ = std::make_unique<trading::risk::PositionSizer>(riskConfig_.getPositionSizingConfig());
    
}
    
Trading_system::~Trading_system() {
    shutdown();
}

trading::ExecutionAcknowledgment Trading_system::handleTradingIntent(const trading::TradingIntent& intent) {
    std::cout << "\n=== PROCESSING TRADING INTENT ===" << std::endl;
    std::cout << "Intent ID: " << intent.intent_id() << std::endl;
    std::cout << "Symbol: " << intent.symbol() << std::endl;
    std::cout << "Side: " << (intent.side() == trading::SIDE_BUY ? "BUY" : "SELL") << std::endl;
    std::cout << "Quantity: " << intent.target_quantity() << std::endl;
    std::cout << "Signal Strength: " << intent.signal_strength() << std::endl;
    std::cout << "Confidence: " << intent.confidence() << std::endl;
    
    // Get current market intelligence for this symbol
    trading::MarketIntelligence currentIntel;
    bool hasIntel = false;
    {
        std::lock_guard<std::mutex> lock(intelligenceMutex_);
        auto it = latestIntelligence_.find(intent.symbol());
        if (it != latestIntelligence_.end()) {
            currentIntel = it->second;
            hasIntel = true;
            std::cout << "Current Market Intelligence:" << std::endl;
            std::cout << "  Spread: " << currentIntel.bid_ask_spread() << std::endl;
            std::cout << "  Imbalance: " << currentIntel.order_book_imbalance() << std::endl;
            std::cout << "  Liquidity: " << currentIntel.liquidity_ratio() << std::endl;
            std::cout << "  Regime: " << currentIntel.current_regime() << std::endl;
        }
    }
    
    // Create acknowledgment
    trading::ExecutionAcknowledgment ack;
    ack.set_intent_id(intent.intent_id());
    ack.set_accepted(true);
    ack.set_execution_id("exec_" + intent.intent_id() + "_" + std::to_string(std::time(nullptr)));
    
    if (hasIntel) {
        ack.set_reason("Intent accepted - Market intelligence integrated (" + 
                        std::to_string(currentIntel.liquidity_ratio()) + " liquidity, " +
                        std::to_string(currentIntel.bid_ask_spread()) + " spread)");
    } else {
        ack.set_reason("Intent accepted - No market intelligence available yet");
    }
    
    // Estimate completion time
    int64_t baseTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int completionDelay = calculateCompletionDelay(intent, currentIntel, hasIntel);
    ack.set_estimated_completion_time(baseTime + completionDelay);
    
    std::cout << "Acknowledged intent with " << completionDelay << "ms estimated completion" << std::endl;
    std::cout << "=================================" << std::endl;
    
    return ack;
}


bool Trading_system::initialize() {
   
    // First, initialize the execution service
    setupExecutionEngine();
    // Debug: Verify execution service and router
    if (!executionService_) {
        std::cerr << "Execution Service failed to initialize!" << std::endl;
        return false;
    }
    std::cout << "=== INITIALIZING FULL TRADING SYSTEM ===" << std::endl;
    
    // 🚨 EMERGENCY: Load current PnL state to block losing symbols
    loadEmergencyState();
    
    // 1. Initialize event bus first
    common::EventBus::getInstance().start();
    
    // 2. Initialize data ingestion service
    dataIngestionService_ = std::make_unique<data_ingestion::DataIngestionService>(
        "config/data_ingestion.json");
    //set comm server
    dataIngestionService_->setCommServer(zmq_server_);
    // Ensure internal ZMQ server is started/bound before components that create
    // ZMQ clients/subscribers (important for inproc transport which requires
    // the bind to exist prior to connects in the same context).
    if (!zmq_server_->isRunning()) {
        if (!zmq_server_->start()) {
            std::cerr << "Failed to start internal ZMQ server during initialization: " << zmq_server_->getLastError() << std::endl;
            return false;
        }
        // brief pause to let sockets bind
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    // sleep for 2s
    std::this_thread::sleep_for(std::chrono::seconds(2));
    if (!dataIngestionService_->initialize()) {
        std::cerr << "Failed to initialize data ingestion service" << std::endl;
        return false;
    }
    // 3. Initialize market data interface
    if (!marketDataInterface_) {
        // Obtain the shared singleton instance using the overload that accepts
        // an injected ZMQ client/context so the subscriber is created with the
        // correct shared inproc context (avoid late injection).
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
    } catch (const std::exception& e) {
        LOG_WARN("Failed to query MarketDataInterface health after init: {}", e.what());
    }
    // 3. Initialize risk manager
            
    // ✅ NEW - Use the factory method
    // riskManager_ = RiskManager::create(executionService_, riskConfig_, marketDataInterface_);
    // Create shared PositionTracker for synchronized position tracking
    std::shared_ptr<trading::risk::PositionTracker> sharedPositionTracker = 
        std::make_shared<trading::risk::PositionTracker>(riskConfig_, marketDataInterface_);
    
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

bool Trading_system::start() {
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

        // CRITICAL DEBUG - Check if threads are actually running
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
        
        if (!dataIngestionService_->start(symbols, depth)) {
            std::cerr << "Failed to start data ingestion service" << std::endl;
            return false;
        }
    // Start orderbook worker pool after data ingestion starts
    startOrderbookWorkerPool();
    // Start dedicated strategy orderbook worker pool (decoupled)
        startStrategyOrderbookWorkerPool(); 
        
        // 2. Start strategy engine
        if (!strategyEngine_->start()) {
            std::cerr << "Failed to start strategy engine" << std::endl;
            return false;
        }

        // Start signal worker pool
        startSignalWorkerPool();
         
        lastPriceUpdate_ = std::chrono::steady_clock::now();
        // Start exec worker pool
        startExecWorkerPool();
        
        std::cout << "🚀 FULL TRADING SYSTEM IS NOW LIVE!" << std::endl;
        std::cout << "Monitoring system health and processing..." << std::endl;
        
        return true;
    }
    return false;
}
// Or run without time limit:
void Trading_system::runIndefinitely() {
    auto lastStatusTime = std::chrono::steady_clock::now();
    
    while (running_ && systemHealthy_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        auto now = std::chrono::steady_clock::now();
        
        // Print status every 30 seconds - simplified like full_bridge.cpp
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastStatusTime).count() >= 30) {
            printStatus();
            lastStatusTime = now;
        }
        
        if (!checkSystemHealth()) {
            std::cout << "⚠️ System health issues detected!" << std::endl;
            systemHealthy_ = false;
        }
    }
} 
void Trading_system::shutdown() {
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

        
        // Stop other services
        if (strategyEngine_) {
            std::cout << "Stopping strategy engine..." << std::endl;
            strategyEngine_->stop();
        }
    // Stop worker pools
    stopOrderbookWorkerPool();
    // Stop strategy-specific orderbook processing pool
    stopStrategyOrderbookWorkerPool();
    stopSignalWorkerPool();
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
            
        // // Clear data ingestion service pointer after shutdown
        // std::cout << "Clearing data ingestion service..." << std::endl;
        
        // Stop event bus last
        std::cout << "Stopping event bus..." << std::endl;
        common::EventBus::getInstance().stop();
        
        std::cout << "System shutdown complete" << std::endl;
    }
} 
// Helper method to get current signals for a symbol (you'll need to implement this)
std::vector<strategy::TradingSignal> Trading_system::getCurrentSignalsForSymbol(const core::Symbol& symbol) {
    // This should return current active signals for the given symbol
    // You'll need to implement this based on your signal storage
    std::vector<strategy::TradingSignal> signals;
    // Your implementation here...
    return signals;
}

void Trading_system::printStatus() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();

    std::cout << "\n=== FULL TRADING SYSTEM STATUS ===" << std::endl;
    std::cout << "Runtime: " << elapsed << "s" << std::endl;  //this will be always 0s
    std::cout << "Service Health: " << (dataIngestionService_->isHealthy() ? "HEALTHY" : "UNHEALTHY") << std::endl;
    std::cout << "Order book updates received: " << messagesProcessed_.load() << std::endl;
    // std::cout << "internal zmq msg sent: " << orderBookUpdatesReceived_.load() << std::endl;
    // List active symbols with intelligence

    // Print publish stats if available
    uint64_t calls = publishCalls_.load();
    if (calls > 0) {
        double avg_ns = static_cast<double>(publishTimeNs_.load()) / static_cast<double>(calls);
        double avg_ms = avg_ns / 1e6;
        std::cout << "Avg publish latency: " << std::fixed << std::setprecision(3) << avg_ms << " ms over " << calls << " calls" << std::endl;
    }
    // Print process RSS to detect memory growth
    try {
        FILE* f = fopen("/proc/self/statm", "r");
        if (f) {
            long size = 0, resident = 0;
            if (fscanf(f, "%ld %ld", &size, &resident) >= 2) {
                long pageSizeKb = sysconf(_SC_PAGESIZE) / 1024; // in KB
                long rssKb = resident * pageSizeKb;
                std::cout << "RSS: " << (rssKb / 1024) << " MB" << std::endl;
            }
            fclose(f);
        }
    } catch (...) {
        // ignore
    }

                // Warmup diagnostics
                if (isWarmupActive()) {
                    auto now = std::chrono::steady_clock::now();
                    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(warmupDuration_ - (now - startTime_)).count();
                    std::cout << "Warmup active - time remaining: " << remaining << "s" << std::endl;
                    std::cout << "Suppressed orders during warmup: " << suppressedOrdersDuringWarmup_.load() << std::endl;
                }

                std::cout << "=========================================" << std::endl;
}

bool Trading_system::isWarmupActive() const {
    try {
        if (!running_) return false;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_);
        return elapsed < warmupDuration_;
    } catch (...) {
        return false;
    }
}
 

bool Trading_system::checkSystemHealth() {
    try {
        // Basic health checks
        if (!running_ || !systemHealthy_) {
            return false;
        }
        
        // Check if services are responsive
        bool dataIngestionHealthy = dataIngestionService_ ? dataIngestionService_->isHealthy() : false;
        bool strategyEngineHealthy = strategyEngine_ ? true : false;
        bool riskManagerHealthy = riskManager_ ? true : false;
        bool zmqHealthy = zmq_server_ ? zmq_server_->isRunning() : false;

        bool overallHealthy = dataIngestionHealthy && strategyEngineHealthy &&
                            riskManagerHealthy && zmqHealthy ;

        if (!overallHealthy) {
            std::cout << "⚠️ System health issues detected!" << std::endl;
            std::cout << "  Data Ingestion: " << (dataIngestionHealthy ? "✅" : "❌") << std::endl;
            std::cout << "  Strategy Engine: " << (strategyEngineHealthy ? "✅" : "❌") << std::endl;
            std::cout << "  Risk Manager: " << (riskManagerHealthy ? "✅" : "❌") << std::endl;
            std::cout << "  ZMQ Server: " << (zmqHealthy ? "✅" : "❌") << std::endl;

            systemHealthy_ = false;
            return false;
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error in system health check: " << e.what() << std::endl;
        return false;
    }
}

int Trading_system::calculateCompletionDelay(const trading::TradingIntent& intent, 
                            const trading::MarketIntelligence& intel,
                            bool hasIntel) {
    int baseDelay = 15000; // 15 seconds base
    
    // Adjust for execution style
    switch (intent.execution_style()) {
        case trading::EXEC_AGGRESSIVE:
            baseDelay = 5000;
            break;
        case trading::EXEC_STEALTH:
            baseDelay = std::min(60000, intent.max_execution_time_ms());
            break;
        case trading::EXEC_PASSIVE:
            baseDelay = 30000;
            break;
        default:
            baseDelay = 15000;
            break;
    }
    
    // Adjust for market conditions if we have intelligence
    if (hasIntel) {
        if (intel.current_regime() == trading::OB_THIN) {
            baseDelay *= 2.0; // Slower in thin markets
        } else if (intel.current_regime() == trading::OB_LIQUID) {
            baseDelay *= 0.8; // Faster in liquid markets
        }
        
        // Adjust for spread
        if (intel.bid_ask_spread() > 0.005) {
            baseDelay *= 1.2; // Slower for wide spreads
        }
    }
    
    return static_cast<int>(baseDelay);
}

void Trading_system::setupConfigurations() {
    // Create configuration files
    // createDataIngestionConfig();
    createStrategyConfig();
    createRiskConfig();                
    // createPTCAConfig();
}
    
void Trading_system::createStrategyConfig() {
    
    std::string configPath = "config/strategy_config.json";
    strategyConfig_ = loadStrategyConfigFromFile(configPath);
        //  std::map<std::string, core::ExchangeId> tradingPairs;
    for (const auto& [name, pair] : strategyConfig_.tradingPairs) {
        std::cout << "Configured trading pair: " << name 
                    << " on exchange " << pair.toString() << std::endl;            
    }
}

void Trading_system::onMarketPriceUpdate(const core::Symbol& symbol, double price) {
    // ptcaManager_->updateMarketPrice(symbol, price);
}
    // Add this to your Trading_system class:
void Trading_system::setupPTCAConnections() {
    
    
    // 2. Execution callback - use executionService_ directly
    if (executionService_) {
        executionService_->registerExecutionCallback([this](const ExecutionResponse& response) {
            onExecutionResponse(response);
        });
    }
}

void Trading_system::onExecutionResponse(const execution::ExecutionResponse& response) {
    std::cout << "📈 Execution: " << response.client_order_id
            << " Status: " << response.order_status  << std::endl;

    ordersExecuted_++;
    
    // // Register with PTCA if needed
    // if (ptcaManager_) {
    //     // Convert ExecutionResponse to ExecutionReport format if needed
    //     // ptcaManager_->registerExecution(response.client_order_id, response.order_status);
    // }
}

 

void Trading_system::createRiskConfig() {
    // Setup risk configuration (similar to risk demo)
    // load config from file
    std::string configPath = "config/risk_config.json";
    riskConfig_.loadFromFile(configPath);
    if (!riskConfig_.validate()) {
        std::cerr << "Invalid risk configuration!" << std::endl;
    }
    // Use `risk_config.json` as the single source of truth for position limits.
    // Historically the code here injected a large list of hard-coded per-asset
    // overrides for testing. That was confusing and prevented the JSON file
    // from being authoritative. Remove those hard-coded overrides so operators
    // can control limits from `config/risk_config.json` instead.
    auto& positionConfig = const_cast<PositionLimitConfig&>(riskConfig_.getPositionLimitConfig());

    // Ensure every configured trading pair has at least a default limit entry
    // (convert pair like "BTC/USDT" -> base asset "BTC") so missing keys in
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

void Trading_system::setupStatistics() {
    // Reset all counters
    orderBookUpdatesReceived_ = 0;
    signalsGenerated_ = 0;
    signalsValidated_ = 0;
    signalsApproved_ = 0;
    signalsRejected_ = 0;
    ordersExecuted_ = 0;
    riskEventsTriggered_ = 0;
}

void Trading_system::setupExecutionEngine() {
    std::cout << "=== SETTING UP EXECUTION ENGINE ===" << std::endl;
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
    
    authService->setCredentials(core::ExchangeId::binance(), credentials);
    api_client_manager->addClient(core::ExchangeId::binance(),
        std::make_shared<BinanceApiClient>(credentials, 
        std::make_shared<RateLimitController>(core::ExchangeId::binance())));
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
    binanceInfo.liquidity_score = 0.9;
    binanceInfo.latency_ms = 50;
    binanceInfo.success_rate = 0.99;
    exchanges.push_back(binanceInfo);
    
    // After initializing the router, add this debug check:
    smart_order_router->initialize(exchanges);

    // Create or obtain shared MarketDataInterface and pass it into the gateway.
    // Important: use the overload that injects the shared ZMQ context so the
    // inproc://server_pub publisher and subscriber share the same context and
    // messages are delivered. Passing nullptr for injectedClient and the
    // shared zmq_context_ for context.
    std::shared_ptr<trading::risk::DataInterface> sharedMdIface;
    try {
        sharedMdIface = trading::risk::MarketDataInterface::createDataInterface(
            riskConfig_, /*injectedClient=*/std::unique_ptr<trading::communication::GenericZmqClient>(nullptr), zmq_context_);
    } catch (...) {
        sharedMdIface = nullptr;
    }

    exchangeGateway_ = std::make_shared<execution::ExchangeGateway>(api_client_manager, sharedMdIface);
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
        executionService_->setLimitOrderHandling(trading::execution::ExecutionService::LimitOrderHandling::REJECT_IF_NO_PRICE);
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
        executionService_->setLimitOrderHandling(trading::execution::ExecutionService::LimitOrderHandling::REJECT_IF_NO_PRICE);
    }       
    
}
// Orderbook worker pool control
void Trading_system::startOrderbookWorkerPool() {
    if (orderbookWorkersRunning_.exchange(true)) return; // already running
    orderbookWorkers_.clear();
    for (size_t i = 0; i < orderbookWorkerCount_; ++i) {
        orderbookWorkers_.emplace_back([this, i]() {
            this->orderbookWorkerLoop(i);
        });
    }
    std::cout << "Started " << orderbookWorkers_.size() << " orderbook worker(s)" << std::endl;
}

void Trading_system::stopOrderbookWorkerPool() {
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

void Trading_system::startStrategyOrderbookWorkerPool() {
    if (strategyOrderbookWorkersRunning_.exchange(true)) return;
    strategyOrderbookWorkers_.clear();
    for (size_t i = 0; i < strategyOrderbookWorkerCount_; ++i) {
        strategyOrderbookWorkers_.emplace_back([this, i]() { this->strategyOrderbookWorkerLoop(i); });
    }
    std::cout << "Started " << strategyOrderbookWorkers_.size() << " strategy orderbook worker(s)" << std::endl;
}

void Trading_system::stopStrategyOrderbookWorkerPool() {
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

void Trading_system::orderbookWorkerLoop(size_t workerId) {
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

void Trading_system::strategyOrderbookWorkerLoop(size_t workerId) {
    // Use a local deque to swap the shared queue with -- this minimizes lock hold time
    std::deque<std::shared_ptr<OrderBook>> localDeque;
    std::cout << "Strategy orderbook worker " << workerId << " started" << std::endl;
    while (strategyOrderbookWorkersRunning_.load()) {
        // Wait until there's work or we are shutting down
        std::unique_lock<std::mutex> lk(strategyOrderbookQueueMutex_);
        strategyOrderbookQueueCv_.wait_for(lk, std::chrono::milliseconds(100), [this]() {
            return !strategyOrderbookQueue_.empty() || !strategyOrderbookWorkersRunning_.load();
        });
        if (!strategyOrderbookWorkersRunning_.load() && strategyOrderbookQueue_.empty()) break;

        // Swap the entire shared queue into localDeque. This is O(1) and very cheap.
        localDeque.clear();
        localDeque.swap(strategyOrderbookQueue_);
        lk.unlock();

        if (localDeque.empty()) continue;

        // Process up to the configured batch size from localDeque
        size_t toProcess = std::min(strategyOrderbookBatchSize_, localDeque.size());
        for (size_t i = 0; i < toProcess; ++i) {
            auto ob_ptr = std::move(localDeque.front());
            localDeque.pop_front();
            try {
                if (strategyEngine_ && ob_ptr) {
                    strategyEngine_->processOrderBook(*ob_ptr);
                }
            } catch (const std::exception &e) {
                std::cerr << "Strategy orderbook worker " << workerId << " error: " << e.what() << std::endl;
            }
        }

        // If there are leftovers in localDeque, push them back into the shared queue in a single locked section.
        if (!localDeque.empty()) {
            std::unique_lock<std::mutex> lk2(strategyOrderbookQueueMutex_);
            // Ensure we respect the queue limit: drop oldest when necessary
            while (!localDeque.empty()) {
                if (strategyOrderbookQueue_.size() >= strategyOrderbookQueueLimit_) {
                    strategyOrderbookQueue_.pop_front();
                }
                strategyOrderbookQueue_.push_back(std::move(localDeque.front()));
                localDeque.pop_front();
            }
            // Notify one worker that new items are available again
            lk2.unlock();
            strategyOrderbookQueueCv_.notify_one();
        }
    }
    std::cout << "Strategy orderbook worker " << workerId << " stopped" << std::endl;
}
// Signal worker pool control
void Trading_system::startSignalWorkerPool() {
    if (signalWorkersRunning_.exchange(true)) return;
    signalWorkers_.clear();
    for (size_t i = 0; i < signalWorkerCount_; ++i) {
        signalWorkers_.emplace_back([this, i]() {
            this->signalWorkerLoop(i);
        });
    }
    std::cout << "Started " << signalWorkers_.size() << " signal worker(s)" << std::endl;
}

void Trading_system::stopSignalWorkerPool() {
    if (!signalWorkersRunning_.exchange(false)) return;
    signalQueueCv_.notify_all();
    for (auto &t : signalWorkers_) if (t.joinable()) t.join();
    signalWorkers_.clear();
    {
        std::lock_guard<std::mutex> lk(signalQueueMutex_);
        signalQueue_.clear();
    }
    std::cout << "Signal worker pool stopped" << std::endl;
}

void Trading_system::signalWorkerLoop(size_t workerId) {
    std::vector<strategy::TradingSignal> batch;
    // signal_dedup set; // optional deduplication
    trading::execution::SignalDeduplicator::Config cfg;
    cfg.includeExchangeInKey = true;
    trading::execution::SignalDeduplicator dedup(cfg);

    batch.reserve(signalBatchSize_);
    while (signalWorkersRunning_.load()) {
        // Wait for data
        std::unique_lock<std::mutex> lk(signalQueueMutex_);

        signalQueueCv_.wait_for(lk, std::chrono::milliseconds(100), [this]() {
            return !signalQueue_.empty() || !signalWorkersRunning_.load();
        });
        if (!signalWorkersRunning_.load() && signalQueue_.empty()) break;
        batch.clear();
        for (size_t i = 0; i < signalBatchSize_ && !signalQueue_.empty(); ++i) {
            batch.push_back(std::move(signalQueue_.front()));
            signalQueue_.pop_front();
        }
        lk.unlock();
        if (batch.empty()) continue;

        for (auto &signal : batch) {
            // CRITICAL FIX: Exit immediately if shutdown is in progress
            if (!signalWorkersRunning_.load()) {
                break;
            }
            
            try {
                //if signal is neutral, skip
                if (signal.direction == core::Direction::NEUTRAL) continue;
                
                // 🚨 EMERGENCY FREQUENCY & RISK CHECK - Determine order type
                std::string orderType = "ENTRY"; // Default to ENTRY for new signals
                // Note: This is for ENTRY signals. EXIT signals are handled separately in optimal_exit_strategy.cpp
                
                if (!emergencyFrequencyCheckWithType(signal.symbol.toString(), orderType)) {
                    continue; // Skip if trading too frequently
                }
                
                signalsGenerated_++;
                             double adjustedStrength = signal.strength;
              

                // convert and validate
                execution::TradingSignal execSignal;
                // Copy needed fields from the strategy signal BEFORE we move it
                execSignal.id = signal.id;
                execSignal.symbol = signal.symbol;
                execSignal.direction = signal.direction;
                execSignal.strength = adjustedStrength;
                // strategy provides targetSize as a fractional multiplier (0..1)
                execSignal.targetSize = signal.targetSize;
                execSignal.strategyId = signal.strategyId;
                execSignal.confidence = signal.confidence;
                execSignal.quality = signal.quality;
                execSignal.expirationTime = signal.expirationTime;
                execSignal.targets = signal.targets;
                execSignal.exchangeId = signal.exchangeId;
                // execSignal.
                if (!dedup.shouldProcessSignal(execSignal)) {
                    // Duplicate — skip processing, do NOT register the original with PTCA
                    continue;
                }

                // Basic filtering
                if (execSignal.direction == core::Direction::BUY) {
                    // if (execSignal.strength < 0.5) continue;
                    if (execSignal.confidence < 0.5) continue;
                } else if (execSignal.direction == core::Direction::SELL) {
                    // if (execSignal.strength < 0.3) continue;
                    //wow, this is stupid, why sell need higher confidence than buy
                    if (execSignal.confidence < 0.3) continue;
                }
                

                //check if price target percentage > 0.2%
                for (const auto& target : execSignal.targets) {
                    if (target.percentage > 0.2) {
                        std::cout << "  Target size: " << target.size
                                    << " at " << target.price 
                                    << " (" << target.percentage * 100 << "%)" 
                                    << std::endl;
                    }
                }                   

                // CRITICAL FIX: Check if system is shutting down before accessing riskManager_
                if (!signalWorkersRunning_.load()) {
                    break; // Exit immediately if shutdown is in progress
                }
                
                // Validate with risk manager (with additional safety check)
                if (riskManager_ && signalWorkersRunning_.load()) {
                    auto validationResult = riskManager_->validateSignal(execSignal);
                    signalsValidated_++;
                    if (validationResult.approved) {
                        signalsApproved_++;
                        realExecution(execSignal); 
                    } else {
                        signalsRejected_++;
                    }
                }
            } catch (const std::exception &e) {
                std::cerr << "Signal worker " << workerId << " error: " << e.what() << std::endl;
            }
        }
    }
}
// Execution worker pool control
void Trading_system::startExecWorkerPool() {
    if (execWorkersRunning_.exchange(true)) return;
    execWorkers_.clear();
    for (size_t i = 0; i < execWorkerCount_; ++i) {
        execWorkers_.emplace_back([this, i]() { this->execWorkerLoop(i); });
    }
    std::cout << "Started " << execWorkers_.size() << " exec worker(s)" << std::endl;
}

void Trading_system::stopExecWorkerPool() {
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

void Trading_system::execWorkerLoop(size_t workerId) {
    while (execWorkersRunning_.load()) {
        execution::TradingSignal task;
        {
            std::unique_lock<std::mutex> lk(execQueueMutex_);
            execQueueCv_.wait_for(lk, std::chrono::milliseconds(100), [this]() {
                return !execQueue_.empty() || !execWorkersRunning_.load();
            });
            if (!execWorkersRunning_.load() && execQueue_.empty()) break;
            if (execQueue_.empty()) continue;
            task = std::move(execQueue_.front());
            execQueue_.pop_front();
        }
        
        try {
            std::this_thread::sleep_for(1ms); // simulate network/processing delay
            
            if (!executionService_) {
                std::cerr << "Execution service not initialized!" << std::endl;
                continue;
            }

            // --- Step 1: Get allowed position size from risk manager ---
            double allowedSize = 0.0;
            if (riskManager_) {
                allowedSize = riskManager_->getAllowedPositionSize(task.symbol);
            }
            // Clamp allowed USD budget to min/max order size
            allowedSize = std::clamp(allowedSize, 10.0, 5000.0);

            // --- Step 2: Apply position sizing (only for BUY orders) ---
            double sizerFraction = 1.0;
            if (positionSizer_ && task.direction == core::Direction::BUY) {
                std::map<std::string, double> metrics;
                metrics["win_rate"] = task.confidence;
                
                // Fetch real volatility from market data
                double vol = 0.05;
                try {
                    if (marketDataInterface_) {
                        vol = marketDataInterface_->getVolatility(task.symbol, "1d", task.exchangeId.toString());
                        vol = std::clamp(vol, 0.0001, 1.0);
                    }
                } catch (const std::exception& e) {
                    LOG_WARN("Failed to get volatility for {}: {}", task.symbol.toString(), e.what());
                    vol = 0.05;
                }
                metrics["volatility"] = vol;
                
                std::map<std::string, double> portfolio;
                sizerFraction = positionSizer_->calculateSize(task.symbol.toString(), metrics, portfolio);
            }

            // --- Step 3: Calculate position size with signal strength tilt ---
            double positionSize = allowedSize * sizerFraction;
            
            // Apply target tilt as a mild multiplier [0.5, 1.0]
            double clampedTarget = std::clamp(task.targetSize, 0.0, 1.0);
            double targetMultiplier = 0.5 + 0.5 * clampedTarget;
            double notionalUsd = positionSize * targetMultiplier;

            // --- Step 4: Get current price and convert to quantity ---
            std::optional<double> currentPriceOpt = dataIngestionService_->getLastPrice(task.exchangeId, task.symbol);
            
            if (!currentPriceOpt.has_value() || currentPriceOpt.value() <= 0.0) {
                LOG_WARN("ExecWorker: current price unavailable for {} - skipping order", task.symbol.toString());
                std::cout << "❌ Execution skipped for signal " << task.id 
                          << ": Price unavailable" << std::endl;
                continue;
            }
            
            double currentPrice = currentPriceOpt.value();
            double finalQty = notionalUsd / currentPrice;

            // Use the centralized symbol precision manager to quantize quantity
            // This replaces the ad-hoc precision selection logic and ensures
            // the quantity adheres to exchange LOT_SIZE (stepSize), min/max
            // and decimal precision rules. The helper lives in
            // include/common/quantity_precision.hpp
            try {
                // Use symbol.toString() which yields e.g. "XRP/USDT"; the
                // precision helper will normalize it internally (remove '/').
                std::string symStr = task.symbol.toString();

                // Round to the nearest valid increment and precision for this symbol
                double roundedQty = ::execution::SymbolPrecisionManager::roundQuantity(symStr, finalQty);

                // If rounding produced an invalid quantity (e.g., below minQty),
                // attempt to bump to minQty if that's acceptable by notional.
                if (!::execution::SymbolPrecisionManager::isValidQuantity(symStr, roundedQty)) {
                   
                    double bumped = roundedQty;
                    bumped = std::max(bumped, 0.0);
                    for (int attempt = 0; attempt < 3 && !::execution::SymbolPrecisionManager::isValidQuantity(symStr, bumped); ++attempt) {
                        // increase by one stepSize worth implicitly by adding a tiny amount
                        bumped = roundedQty + std::pow(10.0, -8 + attempt);
                        bumped = ::execution::SymbolPrecisionManager::roundQuantity(symStr, bumped);
                    }
                    if (::execution::SymbolPrecisionManager::isValidQuantity(symStr, bumped)) {
                        roundedQty = bumped;
                    } else {
                        // Fall back to roundedQty even if invalid; the exchange will
                        // respond and we already log details below.
                        LOG_WARN("Exec", "Rounded qty for {} still invalid (qty={})", symStr, roundedQty);
                    }
                }

                finalQty = roundedQty;
            } catch (const std::exception &e) {
                // Best-effort: keep previous finalQty if helper fails.
                LOG_WARN("Exec: Quantity precision helper failed for {}: {}", task.symbol.toString(), e.what());
            }

            // --- Step 5: Enforce minimum notional AFTER precision rounding ---
            const double minNotionalUsd = 10.0;
            double finalNotionalUsd = currentPrice * finalQty;
            if (finalNotionalUsd < minNotionalUsd) {
                // Recalculate quantity for $12 minimum and apply precision again
                double minQtyForNotional = 12.0 / currentPrice;
                std::string symStr = task.symbol.toString();
                finalQty = ::execution::SymbolPrecisionManager::roundQuantity(symStr, minQtyForNotional);
                finalNotionalUsd = currentPrice * finalQty;
                
                // If still below minimum after rounding, increase by one step
                if (finalNotionalUsd < minNotionalUsd) {
                    LOG_WARN("Exec: Adjusted quantity still insufficient: {}", "Order value below minimum notional requirement");
                    std::cout << "❌ Adjusted quantity still insufficient: Order value below minimum notional requirement" << std::endl;
                    continue; // Skip this order
                }
            }
            
            task.setQuantity(finalQty);

            // --- Step 6: Log resolved sizing before execution ---
            std::cout << "[EXEC] Prepared order: symbol=" << task.symbol.toString()
                      << " qty=" << finalQty 
                      << " price=" << currentPrice
                      << " notionalUsd=" << finalNotionalUsd
                      << " minNotionalUsd=" << minNotionalUsd 
                      << std::endl;

            // --- Step 7: Execute the signal ---
            ExecutionResponse response = executionService_->executeSignal(task);
            
            if (!response.success) {
                std::cout << "❌ Execution failed for signal " << task.id 
                          << ": " << response.error_message << std::endl;
                std::cout << "Signal details - Symbol: " << task.symbol.toString()
                          << ", Direction: " << (task.direction == core::Direction::BUY ? "BUY" : 
                                                 (task.direction == core::Direction::SELL ? "SELL" : "NEUTRAL"))
                          << ", Strength: " << task.strength
                          << ", Confidence: " << task.confidence
                          << ", Quality: " << task.quality
                          << ", TargetSize: " << task.targetSize
                          << ", StrategyId: " << task.strategyId
                          << ", ExchangeId: " << task.exchangeId.toString()
                          << std::endl;
                continue;
            }

            // --- Step 8: Create and process execution report ---
            execution::models::ExecutionReport report;
            report.setOrderId("ORDER_" + task.id);
            report.setExchangeId(task.exchangeId);
            report.setSymbol(task.symbol);
            report.setSide(task.direction == core::Direction::BUY ? 
                          execution::models::OrderSide::BUY : 
                          execution::models::OrderSide::SELL);
            report.setQuantity(finalQty);
            report.setFilledQuantity(finalQty);
            report.setStatus(execution::models::OrderState::FILLED);
            
            if (riskManager_) {
                std::cout << "[TTL_DEBUG] Sending ExecutionReport to RiskManager: " 
                         << task.symbol.toString() << " qty=" << finalQty << std::endl;
                riskManager_->processExecutionReport(report);
            } else {
                std::cout << "[TTL_DEBUG] ERROR: riskManager_ is NULL - ExecutionReport NOT sent!" << std::endl;
            }

            std::cout << "📈 Executed: " << task.symbol.toString() << " " 
                      << (task.direction == core::Direction::BUY ? "BUY" : "SELL") 
                      << " qty=" << finalQty
                      << " notional=$" << (currentPrice * finalQty)
                      << std::endl;
            
            // 🚨 RECORD TRADE FOR EMERGENCY FREQUENCY TRACKING
            recordTradeWithType(task.symbol.toString(), "ENTRY");
                      
        } catch (const std::exception &e) {
            std::cerr << "Exec worker " << workerId << " error: " << e.what() << std::endl;
        }
    }
}


void Trading_system::setupComponentConnections() {        
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
        
    // 2. Connect strategy engine to risk manager
    // Enqueue signals into bounded queue; processing done by worker pool
    strategyEngine_->registerSignalCallback([this](const strategy::TradingSignal& signal) {
        // quick enqueue
        signalsGenerated_++;
        std::unique_lock<std::mutex> lk(signalQueueMutex_);
        // std::cout << "Enqueuing signal " << signal.id << " from strategy " << signal.strategyId << std::endl;
        //print debug info for every 20th signal
        static std::atomic<int> signalCount{0};
        int count = signalCount.fetch_add(1);
      
        if (signalQueue_.size() >= signalQueueLimit_) {
            // drop oldest to make room
            signalQueue_.pop_front();
        }
        signalQueue_.push_back(signal);
        lk.unlock();
        signalQueueCv_.notify_one();
    });
    
    // 3. Connect risk manager events
    riskManager_->registerRiskEventCallback([this](const risk::RiskEvent& event) {
        riskEventsTriggered_++;
        std::cout << "🚨 Risk Event: " << event.description 
                    << " (Severity: " << event.severity << ")" << std::endl;
    });

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
                // swallow - return false on error
            }

            // No active buy signal found; consult RegimeSubscriber diagnostics (log-only)
            try {
                trading::strategy::core_analysis::RegimeInfo rinfo;
                if (strategyEngine_->getRegimeInfo(symbol.toString(), rinfo)) {
                    if (!rinfo.recommended_action.empty()) {
                        // Log the recommendation for observability; do not change behavior
                        LOG_INFO("SlowGate: Regime recommendation for {}: recommended={} stay_prob={} reason={}",
                                 symbol.toString(), rinfo.recommended_action,
                                 rinfo.stay_prob.has_value() ? std::to_string(rinfo.stay_prob.value()) : std::string("n/a"),
                                 rinfo.recommendation_reason);
                    }
                }
            } catch (...) {
                // best-effort logging only
            }

            return false;
        });
    }

    // 4. Register exit callback for OptimalExitStrategy
    LOG_INFO("ExitCallback: 🔧 Setting up OptimalExitStrategy callback...");
    if (riskManager_) {
        LOG_INFO("ExitCallback: ✅ RiskManager is available");
        auto executionInterface = riskManager_->getExecutionInterface();
        if (executionInterface) {
            LOG_INFO("ExitCallback: ✅ ExecutionInterface is available");
            LOG_INFO("ExitCallback: Registering exit callback for OptimalExitStrategy");

            // Capture weak_ptrs to avoid extending lifetimes
            std::weak_ptr<trading::risk::ExecutionInterface> execWeak = executionInterface;
            std::weak_ptr<trading::risk::RiskManager> riskWeak = riskManager_;

            registerGlobalOptimalExitCallback(
                [execWeak, riskWeak](const std::string& symbol, core::Direction exitDirection, double currentPrice, const std::string& reason) {
                    try {
                        LOG_INFO("ExitCallback: 🔥 EXIT SIGNAL: {} {} at price {} (reason: {})", 
                                symbol, 
                                (exitDirection == core::Direction::SELL ? "SELL" : "BUY"), 
                                currentPrice, 
                                reason);

                        // Lock weak_ptr to get shared_ptr for this call
                        auto executionInterface = execWeak.lock();
                        if (!executionInterface) {
                            LOG_ERROR("ExitCallback: ExecutionInterface expired - skipping exit for {}", symbol);
                            return;
                        }

                        // Log pointer address for easier debugging if something later fails
                        // LOG_INFO("ExitCallback: Using ExecutionInterface ptr={}", static_cast<const void*>(executionInterface.get()));

                        // Create OrderRequest for exit
                        ::risk::OrderRequest exitOrder;
                        exitOrder.orderId = "EXIT_" + symbol + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                        exitOrder.clientOrderId = "exit_" + symbol + "_" + std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());
                        exitOrder.set_trading_pair(trading::core::Symbol(symbol));
                        exitOrder.set_side(exitDirection);
                        exitOrder.set_time_in_force("GTC"); // Good-Til-Canceled
                        exitOrder.set_strategy_id("OptimalExitStrategy");
                        exitOrder.set_signal_id("exit_signal_" + symbol);
                        exitOrder.set_exchange_id(trading::core::ExchangeId::binance()); 
                        exitOrder.set_price(currentPrice); // not used for 
                        

                        // 🚀 OPTIMIZED: Use cached position data instead of expensive API calls
                        core::ExchangeId targetExchange = trading::core::ExchangeId::binance(); // fallback
                        double exitQuantity = 0.0;
                        
                        // First try: Get cached position from RiskManager (O(1) lookup vs O(n) API scan)
                        // This avoids expensive getAllPositions() API calls that return 100+ positions
                        bool foundCachedPosition = false;
                        
                        // Try RiskManager's cached position first (fastest O(1) lookup)
                        auto riskManager = riskWeak.lock();
                        if (riskManager) {
                            try {
                                auto cachedPos = riskManager->getCachedPosition(targetExchange, trading::core::Symbol(symbol));
                                LOG_INFO("ExitCallback: 🔍 Cached position lookup for {}: {}", 
                                         symbol, cachedPos.has_value() ? std::to_string(cachedPos.value()) : "NO_VALUE");
                                if (cachedPos.has_value() && std::abs(cachedPos.value()) > 1e-8) {
                                    exitQuantity = std::abs(cachedPos.value());
                                    foundCachedPosition = true;
                                    LOG_INFO("ExitCallback: 🚀 FAST: Found cached position for {}: {} (will exit {})", 
                                             symbol, cachedPos.value(), exitQuantity);
                                }
                            } catch (const std::exception& e) {
                                LOG_WARN("ExitCallback: Cached position lookup failed for {}: {}", symbol, e.what());
                            }
                        } else {
                            LOG_WARN("ExitCallback: RiskManager weak_ptr expired during position lookup");
                        }
                        
                        // Fallback: Single exchange API call (faster than getAllPositions)
                        if (!foundCachedPosition) {
                            try {
                                auto positions = executionInterface->getPositions(targetExchange);
                                auto positionIt = positions.find(symbol);
                                if (positionIt != positions.end() && positionIt->second != 0.0) {
                                    exitQuantity = std::abs(positionIt->second);
                                    foundCachedPosition = true;
                                    LOG_INFO("ExitCallback: Found position for {} on {}: {} (will exit {})", symbol, targetExchange.toString(), positionIt->second, exitQuantity);
                                }
                            } catch (const std::exception& e) {
                                LOG_WARN("ExitCallback: Error fetching positions for {}: {}", symbol, e.what());
                            }
                        }
                        
                        if (!foundCachedPosition) {
                            LOG_WARN("ExitCallback: No position found for {} - this may indicate a tracking issue", symbol);
                        }

                        double signedPosQty = 0.0; // signed quantity as reported by ExecutionInterface

                        if (exitQuantity <= 0.0) {
                            LOG_WARN("ExitCallback: No non-zero position found for {} - skipping exit", symbol);
                            return;
                        }

                        // At this point exitQuantity is absolute value; attempt to fetch the signed quantity
                        try {
                            auto positions = executionInterface->getPositions(targetExchange);
                            auto pit = positions.find(symbol);
                            if (pit != positions.end()) {
                                signedPosQty = pit->second;
                            } else {
                                try {
                                    auto allPos = executionInterface->getAllPositions();
                                    auto itx = allPos.find(targetExchange);
                                    if (itx != allPos.end()) {
                                        auto p2 = itx->second.find(symbol);
                                        if (p2 != itx->second.end()) signedPosQty = p2->second;
                                    }
                                } catch (...) {
                                    // ignore
                                }
                            }
                        } catch (...) {
                            // leave signedPosQty as 0.0 (best-effort)
                        }

                        // Decide final exit side based on signedPosQty: negative => short => BUY to close; positive => long => SELL to close
                        core::Direction finalSide = exitDirection; // default to strategy suggestion
                        if (std::abs(signedPosQty) > 1e-12) {
                            if (signedPosQty < 0.0) finalSide = core::Direction::BUY;
                            else finalSide = core::Direction::SELL;

                            if (finalSide != exitDirection) {
                                LOG_WARN("ExitCallback: Requested exit direction ({}) differs from position sign (qty={}) - overriding to {}",
                                         (exitDirection == core::Direction::SELL ? "SELL" : "BUY"), signedPosQty,
                                         (finalSide == core::Direction::SELL ? "SELL" : "BUY"));
                            }
                        }

                        // Use MARKET type for immediate exit to avoid limit hangs; can be tuned via config
                        exitOrder.set_exchange_id(targetExchange);

                        // SHORT-CIRCUIT: Round DOWN the exit quantity to the exchange LOT_SIZE (stepSize)
                        // and enforce minimum order quantity. If the rounded quantity is below
                        // the exchange's minQty, skip the exit early to avoid creating unusable signals.
                        try {
                            std::string symStr = symbol; // e.g. "AAVE/USDT"
                            const auto& prec = ::execution::SymbolPrecisionManager::getPrecision(symStr);

                            double finalQtyRounded = exitQuantity;
                            if (prec.stepSize > 0.0 && exitQuantity > 0.0) {
                                double steps = std::floor(exitQuantity / prec.stepSize);
                                finalQtyRounded = steps * prec.stepSize;
                            }

                            // Clean up floating point artifacts and apply display precision
                            double multiplier = std::pow(10.0, prec.precision);
                            double cleaned = std::floor(finalQtyRounded * multiplier + 0.5) / multiplier;
                            char buf[64];
                            snprintf(buf, sizeof(buf), "%.*f", prec.precision, cleaned);
                            finalQtyRounded = std::strtod(buf, nullptr);

                            // If the rounded quantity is below the exchange minimum, skip exit
                            if (finalQtyRounded < prec.minQty - 1e-12) {
                                LOG_WARN("ExitCallback: Rounded exit quantity {} for {} is below minQty {} - skipping exit", finalQtyRounded, symbol, prec.minQty);
                                return;
                            }

                            // Use the rounded quantity from now on
                            exitOrder.quantity = finalQtyRounded;
                            exitQuantity = finalQtyRounded; // update local copy for signal
                        } catch (const std::exception& e) {
                            // Best-effort fallback - use unrounded quantity if something goes wrong
                            LOG_WARN("ExitCallback: Quantity rounding failed for {}: {} - using raw quantity {}", symbol, e.what(), exitQuantity);
                            exitOrder.quantity = exitQuantity;
                        }

                        exitOrder.set_order_type(models::OrderType::LIMIT);
                        exitOrder.set_side(finalSide);

                        // Create TradingSignal for proper risk validation flow
                        execution::TradingSignal exitSignal;
                        exitSignal.id = exitOrder.clientOrderId;
                        exitSignal.setStrategyId(core::StrategyId("OptimalExitStrategy"));
                        exitSignal.setTradingPair(trading::core::Symbol(symbol));
                        exitSignal.setDirection(finalSide);
                        exitSignal.setConfidence(1.0);
                        exitSignal.setStrength(1.0);
                        // exitSignal.setQuantity(exitQuantity);
                        // exitSignal.setOrderType(models::OrderType::MARKET);
                        exitSignal.setExchangeId(targetExchange);
                        exitSignal.setGenerationTime(std::chrono::system_clock::now());
                        exitSignal.expirationTime = exitSignal.getGenerationTime() + std::chrono::minutes(5); // 5 min expiry for exit

                        // Add exit-specific metadata and set quantity
                        // set both quantity and targetSize so pre-trade validation accepts the exit
                        exitSignal.setQuantity(exitQuantity);
                        exitSignal.setTargetSize(exitQuantity);
                        exitSignal.setPrice(currentPrice);
                        exitSignal.metadata["exit_reason"] = reason;
                        exitSignal.metadata["exit_price"] = std::to_string(currentPrice);
                        exitSignal.metadata["position_qty"] = std::to_string(signedPosQty);
                        exitSignal.metadata["is_exit_signal"] = "true";

                        // Validate through RiskManager instead of direct execution
                        auto riskManagerForValidation = riskWeak.lock();
                        if (!riskManagerForValidation) {
                            LOG_ERROR("ExitCallback: RiskManager expired - cannot validate exit signal for {}", symbol);
                            return;
                        }

                        try {
                            auto validationResult = riskManagerForValidation->validateSignal(exitSignal);
                            if (validationResult.approved) {
                                // Now submit through ExecutionInterface after risk validation
                                std::string orderId = executionInterface->submitOrder(exitOrder);
                                LOG_INFO("ExitCallback: ✅ EXIT ORDER SUBMITTED after risk validation: {} (order_id: {})", 
                                        exitOrder.clientOrderId, orderId);
                            } else {
                                LOG_WARN("ExitCallback: 🚫 EXIT SIGNAL REJECTED by risk management for {}: {}", 
                                        symbol, validationResult.violations.empty() ? "No specific violations provided" : validationResult.violations.front().description);
                            }
                        } catch (const std::exception& e) {
                            LOG_ERROR("ExitCallback: ❌ Risk validation or order submission failed for {}: {}", symbol, e.what());
                            return;
                        }

                    } catch (const std::exception& e) {
                        LOG_ERROR("ExitCallback: ❌ FAILED to submit exit order for {}: {}", symbol, e.what());
                    }
                }
            );

            LOG_INFO("ExitCallback: ✅ OptimalExitStrategy exit callback registered successfully");
        } else {
            LOG_ERROR("ExitCallback: ❌ Cannot register exit callback: ExecutionInterface is null");
        }
    } else {
        LOG_ERROR("ExitCallback: ❌ Cannot register exit callback: RiskManager is null");
    }
    LOG_INFO("ExitCallback: 🔧 OptimalExitStrategy setup completed");
}

void Trading_system::realExecution(const execution::TradingSignal& signal) {
    // If we're in the warmup period, suppress sending real orders. Count them
    // for diagnostics and return early. This prevents large orders at startup.
    if (isWarmupActive()) {
        suppressedOrdersDuringWarmup_.fetch_add(1);
        LOG_INFO("Warmup", "Suppressing execution for signal {} (warmup active)", signal.id);
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

// 🚨 EMERGENCY RISK & FREQUENCY CONTROLS IMPLEMENTATION

bool Trading_system::emergencyFrequencyCheck(const std::string& symbol) {
    return emergencyFrequencyCheckWithType(symbol, "ENTRY");
}

bool Trading_system::emergencyFrequencyCheckWithType(const std::string& symbol, const std::string& orderType) {
    std::lock_guard<std::mutex> lock(emergencyMutex_);
    
    if (emergencyStop_.load()) {
        std::cout << "🚫 EMERGENCY STOP ACTIVE - Blocking all signals for " << symbol << std::endl;
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    
    // 1. Check minimum gap between trades - RELAXED for EXIT orders
    auto lastTradeIt = lastTradeTime_.find(symbol);
    if (lastTradeIt != lastTradeTime_.end()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - lastTradeIt->second);
        
        // EXIT orders have relaxed timing constraints (risk management priority)
        int requiredGap = (orderType == "EXIT") ? 5 : MIN_GAP_SECONDS; // 5s for exits, 30s for entries
        
        if (elapsed.count() < requiredGap) {
            if (orderType != "EXIT") { // Don't block exit orders for frequency
                std::cout << "🚫 FREQUENCY BLOCK (" << orderType << "): " << symbol << " - " << elapsed.count() 
                          << "s < " << requiredGap << "s required gap" << std::endl;
                return false;
            } else {
                std::cout << "⚠️ EXIT ORDER frequency warning: " << symbol << " - " << elapsed.count() 
                          << "s gap (allowed for risk management)" << std::endl;
            }
        }
    }
    
    // 2. Check hourly trade limit - SEPARATE counters for ENTRY vs EXIT
    auto currentHour = std::chrono::duration_cast<std::chrono::hours>(now.time_since_epoch()).count();
    std::string hourKey = symbol + "_" + orderType + "_" + std::to_string(currentHour);
    
    // EXIT orders have higher hourly limit (risk management priority)
    int maxTradesThisType = (orderType == "EXIT") ? (MAX_TRADES_PER_HOUR * 2) : MAX_TRADES_PER_HOUR;
    
    if (hourlyTradeCount_[hourKey] >= maxTradesThisType) {
        if (orderType != "EXIT") { // Don't block exit orders for hourly limits
            std::cout << "🚫 HOURLY LIMIT (" << orderType << "): " << symbol << " - " << hourlyTradeCount_[hourKey] 
                      << "/" << maxTradesThisType << " trades this hour" << std::endl;
            return false;
        } else {
            std::cout << "⚠️ EXIT ORDER hourly limit warning: " << symbol << " - " << hourlyTradeCount_[hourKey] 
                      << "/" << maxTradesThisType << " (allowed for risk management)" << std::endl;
        }
    }
    
    // 3. Check symbol PnL limit
    auto pnlIt = symbolPnL_.find(symbol);
    if (pnlIt != symbolPnL_.end() && pnlIt->second < MAX_LOSS_PER_SYMBOL) {
        std::cout << "🚫 LOSS LIMIT: " << symbol << " - Current PnL " << pnlIt->second 
                  << " below limit " << MAX_LOSS_PER_SYMBOL << std::endl;
        return false;
    }
    
    // 4. Check total portfolio loss
    double totalLoss = 0.0;
    for (const auto& [sym, pnl] : symbolPnL_) {
        if (pnl < 0) totalLoss += pnl;
    }
    if (totalLoss < MAX_TOTAL_DAILY_LOSS) {
        emergencyStop_.store(true);
        std::cout << "🚨 EMERGENCY STOP TRIGGERED: Total loss " << totalLoss 
                  << " exceeds limit " << MAX_TOTAL_DAILY_LOSS << std::endl;
        return false;
    }
    
    return true; // All checks passed
}

bool Trading_system::emergencyRiskCheck(const std::string& symbol, double currentPnL) {
    std::lock_guard<std::mutex> lock(emergencyMutex_);
    
    symbolPnL_[symbol] = currentPnL;
    
    if (currentPnL < MAX_LOSS_PER_SYMBOL) {
        std::cout << "🚫 RISK BLOCK: " << symbol << " PnL " << currentPnL 
                  << " below safety limit " << MAX_LOSS_PER_SYMBOL << std::endl;
        return false;
    }
    
    return true;
}

void Trading_system::recordTrade(const std::string& symbol) {
    recordTradeWithType(symbol, "ENTRY");
}

void Trading_system::recordTradeWithType(const std::string& symbol, const std::string& orderType) {
    std::lock_guard<std::mutex> lock(emergencyMutex_);
    
    auto now = std::chrono::system_clock::now();
    lastTradeTime_[symbol] = now;
    
    // Update hourly counter with order type
    auto currentHour = std::chrono::duration_cast<std::chrono::hours>(now.time_since_epoch()).count();
    std::string hourKey = symbol + "_" + orderType + "_" + std::to_string(currentHour);
    hourlyTradeCount_[hourKey]++;
    
    // Also update general counter for backward compatibility
    std::string generalKey = symbol + "_" + std::to_string(currentHour);
    hourlyTradeCount_[generalKey]++;
    
    int maxForType = (orderType == "EXIT") ? (MAX_TRADES_PER_HOUR * 2) : MAX_TRADES_PER_HOUR;
    std::cout << "📊 TRADE RECORDED (" << orderType << "): " << symbol << " (Count: " 
              << hourlyTradeCount_[hourKey] << "/" << maxForType << ")" << std::endl;
}

void Trading_system::loadEmergencyState() {
    // Load current PnL state from analysis file if available
    try {
        std::ifstream file("/home/e8/works/hybrid-bot/tools/pnl_analyzer/trade_analysis.json");
        if (file.is_open()) {
            nlohmann::json data;
            file >> data;
            
            if (data.contains("pairs")) {
                std::lock_guard<std::mutex> lock(emergencyMutex_);
                
                for (auto& [symbol, pairData] : data["pairs"].items()) {
                    double totalPnl = pairData.value("total_pnl", 0.0);
                    symbolPnL_[symbol] = totalPnl;
                    
                    std::cout << "💰 LOADED PnL: " << symbol << " = $" << totalPnl << std::endl;
                    
                    // Auto-block symbols already at loss limit
                    if (totalPnl < MAX_LOSS_PER_SYMBOL) {
                        std::cout << "🚫 AUTO-BLOCKED: " << symbol << " due to existing losses" << std::endl;
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        std::cout << "⚠️  Could not load PnL state: " << e.what() << std::endl;
    }
}

// REMOVED: executeExitOrder() - This was dead code never called anywhere  
// The actual exit execution happens through:
// OptimalExitStrategy::closePosition() -> exitCallback_ -> ExecutionInterface::submitOrder()
// See lines 1300-1427 for the real exit callback registration

// REMOVED: handleExitSignal() - This was dead code never called anywhere
// The actual exit strategy works through OptimalExitStrategy -> registerGlobalOptimalExitCallback()
// Exit flow: ImbalanceDetectionEngine calls g_optimalExit.evaluateExit() -> exitCallback_ -> ExecutionInterface


        
