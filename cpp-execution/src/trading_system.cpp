#include "trading_system.hpp"

using namespace trading;

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
    
    // Initialize MarketDataInterface
    // Subscriber should connect to the server's PUB endpoint; publisher endpoint
    // is the server's REQ/REP endpoint for requests. Ensure these are not swapped.
    riskConfig_.zmqSubscriberEndpoint = SERVER_PUBLISHER_ENDPOINT;
    riskConfig_.zmqPublisherEndpoint = SERVER_REQUEST_ENDPOINT;
    marketDataInterface_ = std::make_shared<trading::risk::MarketDataInterface>(riskConfig_);
    // set zmq client to marketDataInterface_
    marketDataInterface_->setZmqClient(std::move(zmq_client_));
    // Initialize position sizer with default config for now
    positionSizer_ = std::make_unique<trading::risk::PositionSizer>();
    
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
    
    // 1. Initialize event bus first
    common::EventBus::getInstance().start();
    
    // 2. Initialize data ingestion service
    dataIngestionService_ = std::make_unique<data_ingestion::DataIngestionService>(
        "config/data_ingestion.json");
    //set comm server
    dataIngestionService_->setCommServer(zmq_server_);
    // sleep for 2s
    std::this_thread::sleep_for(std::chrono::seconds(2));
    if (!dataIngestionService_->initialize()) {
        std::cerr << "Failed to initialize data ingestion service" << std::endl;
        return false;
    }
    // 3. Initialize market data interface
    if (!marketDataInterface_->initialize()) {
        std::cerr << "Failed to initialize MarketDataInterface" << std::endl;
        return false;
    }
    // 3. Initialize risk manager
            
    // ✅ NEW - Use the factory method
    // riskManager_ = RiskManager::create(executionService_, riskConfig_, marketDataInterface_);
    riskManager_ = trading::risk::RiskManager::create(
        executionService_, 
        riskConfig_, 
        marketDataInterface_,
        zmq_context_
        // zmq_server_->getContext() // Pass shared ZMQ context
        // shared_context
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
        
        if (!zmq_server_->start()) {
            std::cerr << "Failed to start internal ZMQ server: " << zmq_server_->getLastError() << std::endl;
            return -1;
        }

        // CRITICAL DEBUG - Check if threads are actually running
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Try to trigger the diagnostic logs by checking queue state
        // std::cout << "=== THREAD DEBUG ===" << std::endl;
        // std::cout << "Intel queue size: " << zmq_server_->getQueueSize("intel") << std::endl;
        // std::cout << "Internal queue size: " << zmq_server_->getQueueSize("internal") << std::endl;
        // std::cout << "Health check: " << (zmq_server_->healthCheck() ? "HEALTHY" : "UNHEALTHY") << std::endl;

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

        // Stop data ingestion FIRST and wait for cleanup
        if (dataIngestionService_) {
            std::cout << "Stopping data ingestion service..." << std::endl;
            dataIngestionService_->stop();
            
            // Give WebSocket connections time to close gracefully
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }

        
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
        // dataIngestionService_.reset();
        
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

    std::cout << "=========================================" << std::endl;
}

// void Trading_system::generateMarketIntelligence(const OrderBookUpdate& update, std::shared_ptr<OrderBook> orderBook) {
//     try {
//         if (!orderBook) {
//             return; // No order book available
//         }           
//         // Validate order book has data
//         auto bids = orderBook->getBids(5);
//         auto asks = orderBook->getAsks(5);
        
//         if (bids.empty() || asks.empty()) {
//             return;
//         }
        
//         // Verify prices are valid
//         if (bids[0].price <= 0 || asks[0].price <= 0 || asks[0].price <= bids[0].price) {
//             return;
//         }
        
//         // Generate adaptive market intelligence
//         std::string tradingPair = update.tradingPair.toString();
        
//         // Throttle intelligence generation (use instance maps to avoid data races)
//         auto now = std::chrono::steady_clock::now();
//         bool shouldGenerate = false;
//         {
//             std::lock_guard<std::mutex> lm(intelMapsMutex_);
//             if (lastIntelTime_.find(tradingPair) == lastIntelTime_.end()) {
//                 shouldGenerate = true; // First time
//             } else {
//                 auto timeSinceLastIntel = now - lastIntelTime_[tradingPair];
//                 if (timeSinceLastIntel >= std::chrono::milliseconds(500)) { // Generate every 500 ms
//                     shouldGenerate = true;
//                 }
//             }
//             if (!shouldGenerate) return;
//             lastIntelTime_[tradingPair] = now;
//         }

//         trading::MarketIntelligence intel = adaptiveIntel_->GenerateAdaptiveMarketIntelligence(tradingPair, orderBook);

//         if (intel.symbol().empty()) {
//             return; // No valid intelligence generated
//         }

//         // Debug output for intelligence generations
//         {
//             std::lock_guard<std::mutex> lm(intelMapsMutex_);
//             intelDebugCounts_[tradingPair]++;
//             if (intelDebugCounts_[tradingPair] <= 1 || intelDebugCounts_[tradingPair] % 5000 == 0) {
//                 std::cout << "🧠 Generated intelligence #" << intelDebugCounts_[tradingPair]
//                             << " for " << tradingPair << " (spread: " << intel.bid_ask_spread()
//                             << ", imbalance: " << intel.order_book_imbalance() << ")" << std::endl;
//             }
//         }
        
//         // Store latest intelligence
//         {
//             std::lock_guard<std::mutex> lock(intelligenceMutex_);
//             latestIntelligence_[tradingPair] = intel;
//         }
        
//         // ✅ Direct ZMQ publishing like full_bridge.cpp
//         if (zmq_server_ && !intel.symbol().empty()) {
//             // Debug output for publishing
//             static std::atomic<int> publishCount{0};
//             int count = publishCount.fetch_add(1);
//             if (count < 5 || count % 100 == 0) {
//                 std::cout << "📢 Publishing intelligence #" << count
//                             << " for " << tradingPair << std::endl;
//             }   
//             if (intel.DebugString().size() > 1000) {
//                 std::cerr << "Warning: Intelligence message size is large ("
//                             << intel.DebugString().size() << " bytes)" << std::endl;
//             }
//             if (zmq_server_) {
//                 // Serialize protobuf safely into a std::string (SerializeToString returns bool)
//                 std::string serialized_intel;
//                 if (!intel.SerializeToString(&serialized_intel)) {
//                     std::cerr << "Failed to serialize MarketIntelligence protobuf" << std::endl;
//                 } else {
//                     // Backpressure: check queue size for topic and skip if too large
//                     std::string topic = "MARKET_INTEL.";
//                     // try {
//                     //     size_t qsize = zmq_server_->getQueueSize(topic);
//                     //     if (qsize > 1000) { // arbitrary high watermark
//                     //         //drop all the old data. you are not a fucking historian.
//                     //         zmq_server_->dropMessages(topic, qsize - 500); // keep last 500 messages
//                     //         // publishAllowed = false;
//                     //         std::cerr << "Backpressure: ZMQ queue for " << topic << " size=" << qsize
//                     //                     << ", skipping publish to avoid memory growth" << std::endl;
//                     //     }
//                     // } catch (...) {
//                     //     // If getQueueSize not implemented or fails, allow publish
//                     // }
//                     //what the fucking i am doing?
//                     // Use publishAsync to avoid blocking the intelligence generation path.
//                     // Move the serialized string to avoid an extra copy.
//                     zmq_server_->publishAsync("MARKET_INTEL", std::move(serialized_intel), MessagePriority::NORMAL);
//                     // zmq_server_->publishProtobuf("MARKET_INTEL", intel, MessagePriority::NORMAL);
                    
//                 }
//             }
//             else {
//                 std::cerr << "ZMQ server not available for publishing intelligence" << std::endl;
//                 // Handle case where ZMQ server is not available
//             }
//         }
        
//     } catch (const std::exception& e) {
//         std::cerr << "Error generating market intelligence: " << e.what() << std::endl;
//     }
// }


// FIXED VERSION of publishOrderBookForRisk method
void Trading_system::publishOrderBookForRisk(const OrderBookUpdate& update) {
    try {
        // Validate input
        if (update.bids.empty() && update.asks.empty()) {
            std::cerr << "Warning: Orderbook update is empty for "
                        << update.exchangeId.toString() << ":" << update.tradingPair.toString() << std::endl;
            return;
        }
        
        // Create protobuf message (this part is correct)
        orderbook::OrderbookMessage obMsg;
        auto* data = obMsg.mutable_data();
        data->set_exchange(update.exchangeId.toString());
        data->set_symbol(update.tradingPair.toString());
        data->set_sequence(update.sequenceNumber);
        data->set_timestamp(std::chrono::duration_cast<std::chrono::milliseconds>(
            update.exchangeTimestamp.time_since_epoch()).count());
        
        // Best bid/ask
        data->set_bid(!update.bids.empty() ? update.bids[0].price : 0.0);
        data->set_ask(!update.asks.empty() ? update.asks[0].price : 0.0);
        
        // Mid price
        data->set_price((!update.bids.empty() && !update.asks.empty())
            ? (update.bids[0].price + update.asks[0].price) / 2.0 : 0.0);
        
        // Volume at top 5 levels
        double bidVolume = 0.0, askVolume = 0.0;
        for (int i = 0; i < std::min(5, (int)update.bids.size()); ++i) bidVolume += update.bids[i].quantity;
        for (int i = 0; i < std::min(5, (int)update.asks.size()); ++i) askVolume += update.asks[i].quantity;
        data->set_volume(bidVolume + askVolume);
        
        // Add bids/asks
        for (const auto& bid : update.bids) {
            auto* level = data->add_bids();
            level->set_price(bid.price);
            level->set_quantity(bid.quantity);
        }
        for (const auto& ask : update.asks) {
            auto* level = data->add_asks();
            level->set_price(ask.price);
            level->set_quantity(ask.quantity);
        }
        
        // Placeholders for 24h stats and last trade
        data->set_lasttradeprice(data->price());
        data->set_lasttradequantity(0.0);
        data->set_open24h(data->price());
        data->set_high24h(data->price());
        data->set_low24h(data->price());
        data->set_volumequote24h(0.0);
        data->set_pricechange24h(0.0);
        data->set_pricechangepercent24h(0.0);
        
        // Populate metadata
        auto* metadata = obMsg.mutable_metadata();
        metadata->set_correlation_id(""); // Empty for now
        metadata->set_priority("HIGH");
        auto now = std::chrono::system_clock::now();
        metadata->set_timestamp(std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count()));
        
        // Serialize protobuf to string
        std::string serializedOb;
        if (!obMsg.SerializeToString(&serializedOb)) {
            std::cerr << "Failed to serialize OrderBook protobuf" << std::endl;
            return;
        }
        
        // CRITICAL FIX: Create proper topic WITHOUT embedding in protobuf
        std::string topic = "orderbook." + update.exchangeId.toString() + "." + update.tradingPair.toString();
        
        // CRITICAL FIX: Use publishAsync correctly - it should send topic + data as separate frames
        // The issue was likely that the topic was being embedded in the protobuf data itself
        
        // Validate before publishing
        if (topic.empty() || serializedOb.empty()) {
            std::cerr << "Empty topic or serialized data" << std::endl;
            return;
        }
        
        // Debug output for first few messages
        static std::atomic<int> publishCount{0};
        int count = publishCount.fetch_add(1);
        if (count < 10) {
            std::cout << "=== PUBLISH DEBUG #" << count << " ===" << std::endl;
            std::cout << "Topic: '" << topic << "'" << std::endl;
            std::cout << "Protobuf size: " << serializedOb.size() << " bytes" << std::endl;
            std::cout << "Protobuf starts with: ";
            for (size_t i = 0; i < std::min<size_t>(16, serializedOb.size()); ++i) {
                printf("%02x ", static_cast<unsigned char>(serializedOb[i]));
            }
            std::cout << std::endl;
            
            // Check if topic is accidentally embedded in protobuf
            if (serializedOb.find(topic) != std::string::npos) {
                std::cout << "ERROR: Topic found embedded in protobuf data!" << std::endl;
            } else {
                std::cout << "Good: Topic not embedded in protobuf" << std::endl;
            }
            std::cout << "===============================" << std::endl;
        }
        
        // IMPORTANT: Use internal queue (not intel) for orderbook data to internal consumers
        bool success = zmq_server_->publishAsync(topic, std::move(serializedOb), MessagePriority::HIGH);
        //use publishSync to ensure message is sent before we move on                
        if (success) {
            orderBookUpdatesReceived_.fetch_add(1);
            
            if (count < 5 || count % 5000 == 0) {
                std::cout << "📢 Published orderbook #" << count << " (" << topic << ")" << std::endl;
            }
        } else {
            std::cerr << "Failed to publish orderbook: " << topic << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error publishing orderbook for risk: " << e.what() << std::endl;
    }
}

// ✅ Simple health check like full_bridge.cpp
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


//create ptca config
// void Trading_system::createPTCAConfig() {
//     trading::learning::PTCAIntegrationConfig ptcaConfig;        
//     // Configure PTCA for your trading environment
//     ptcaConfig.outcomeEvaluationDelay = std::chrono::minutes(2);  // Evaluate outcomes after 2 minutes
//     ptcaConfig.signalTrackingWindow = std::chrono::minutes(15);   // Track signals for 15 minutes
//     ptcaConfig.minimumPnLThreshold = 0.0005;                     // 0.05% minimum P&L to learn from
//     ptcaConfig.priceMovementThreshold = 0.003;                   // 0.3% price movement significance
//     ptcaConfig.pnlSuccessThreshold = 0.008;                      // 0.8% P&L success threshold
//     ptcaConfig.strongOutcomeThreshold = 0.02;                    // 2% strong outcome threshold
    
//     // Set up model persistence
//     ptcaConfig.modelSaveDirectory = "models/ptca/";
//     ptcaConfig.modelSaveInterval = std::chrono::minutes(20);
//     ptcaConfig.enableContinuousLearning = true;
//     ptcaConfig.enableDetailedLogging = true;
    
//     // Performance limits
//     ptcaConfig.maxSignalsToTrack = 500;  // Limit memory usage
    
//     ptcaManager_ = std::make_unique<trading::learning::PTCAIntegrationManager>(ptcaConfig);

// }

void Trading_system::createRiskConfig() {
    // Setup risk configuration (similar to risk demo)
    // load config from file
    std::string configPath = "config/risk_config.json";
    riskConfig_.loadFromFile(configPath);
    if (!riskConfig_.validate()) {
        std::cerr << "Invalid risk configuration!" << std::endl;
    }
    auto& positionConfig = const_cast<PositionLimitConfig&>(riskConfig_.getPositionLimitConfig());
    
    positionConfig.assetLimits["BTC"] = 0.0001;
    positionConfig.assetLimits["ETH"] = 0.1;
    positionConfig.assetLimits["BNB"] = 0.1;
    positionConfig.assetLimits["ADA"] = 10.0;
    positionConfig.assetLimits["DOGE"] = 100.0;
    positionConfig.assetLimits["USDT"] = 50.0;
    positionConfig.assetLimits["USDC"] = 50.0;
    positionConfig.assetLimits["XRP"] = 10.0;
    positionConfig.assetLimits["SOL"] = 1.0;
    positionConfig.assetLimits["LTC"] = 0.1;
    positionConfig.assetLimits["DOT"] = 1.0;
    positionConfig.assetLimits["MATIC"] = 10.0;
    positionConfig.assetLimits["AVAX"] = 1.0;
    positionConfig.assetLimits["SHIB"] = 1000.0;
    positionConfig.assetLimits["TRX"] = 100.0;
    positionConfig.assetLimits["UNI"] = 1.0;
    positionConfig.assetLimits["LINK"] = 1.0;
    positionConfig.assetLimits["BCH"] = 0.1;
    positionConfig.assetLimits["XLM"] = 10.0;
    positionConfig.assetLimits["ATOM"] = 1.0;
    positionConfig.assetLimits["VET"] = 100.0;
    positionConfig.assetLimits["FIL"] = 1.0;
    positionConfig.assetLimits["ICP"] = 0.5;
    positionConfig.assetLimits["ALGO"] = 10.0;
    positionConfig.assetLimits["XTZ"] = 1.0;
    positionConfig.assetLimits["AAVE"] = 0.5;
    positionConfig.assetLimits["EGLD"] = 0.1;
    positionConfig.assetLimits["SAND"] = 5.0;
    positionConfig.assetLimits["MANA"] = 5.0;
    positionConfig.assetLimits["GALA"] = 10.0;
    positionConfig.assetLimits["AXS"] = 1.0;
    positionConfig.assetLimits["SUI"] = 5.0;
    positionConfig.assetLimits["APT"] = 5.0;
    positionConfig.assetLimits["KAS"] = 50.0;
    positionConfig.assetLimits["XMR"] = 0.1;
    positionConfig.assetLimits["ZEC"] = 0.1;
    positionConfig.assetLimits["PEPE"] = 1000.0;
    positionConfig.assetLimits["FTM"] = 5.0;
    positionConfig.assetLimits["HBAR"] = 50.0;
    positionConfig.assetLimits["PAXG"] = 0.01;
    positionConfig.assetLimits["ONDO"] = 50.0;
    positionConfig.assetLimits["BONK"] = 1000.0;
    positionConfig.assetLimits["FLOKI"] = 1000.0;
    positionConfig.assetLimits["AGIX"] = 5.0;
    positionConfig.assetLimits["FET"] = 10.0;
    positionConfig.assetLimits["INJ"] = 1.0;
    positionConfig.assetLimits["OP"] = 5.0;
    positionConfig.assetLimits["ARB"] = 5.0;
    positionConfig.assetLimits["STARK"] = 1.0;
    positionConfig.assetLimits["TIA"] = 5.0;
    positionConfig.assetLimits["XRP"] = 10.0;
    positionConfig.assetLimits["SOL"] = 1.0;
    positionConfig.assetLimits["ADA"] = 10.0;
    positionConfig.defaultLimit = 100; // dont care because btc =110k white doge or other maybe 0.001,
    positionConfig.enforceStrictly = true;
    
    auto& exposureConfig = const_cast<ExposureLimitConfig&>(riskConfig_.getExposureLimitConfig());
    exposureConfig.maxTotalExposure = 1000.0;
    exposureConfig.maxAssetExposure = 300.0;
    exposureConfig.maxStrategyExposure = 750.0;
    exposureConfig.maxExchangeExposure = 800.0;
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
            try {
                //if signal is neutral, skip
                if (signal.direction == core::Direction::NEUTRAL) continue;
                signalsGenerated_++;
             
                // std::cout << "PTCA recommendation for signal " << signal.id 
                //           << ": " << ptcaRecommendation << std::endl;
                double adjustedStrength = signal.strength;
              

                // convert and validate
                execution::TradingSignal execSignal;
                // Copy needed fields from the strategy signal BEFORE we move it
                execSignal.id = signal.id;
                execSignal.symbol = signal.symbol;
                execSignal.direction = signal.direction;
                execSignal.strength = adjustedStrength;
                execSignal.targetSize = signal.targetSize;
                execSignal.strategyId = signal.strategyId;
                execSignal.confidence = signal.confidence;
                execSignal.quality = signal.quality;
                execSignal.expirationTime = signal.expirationTime;
                execSignal.targets = signal.targets;
                execSignal.exchangeId = signal.exchangeId;

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
                    if (execSignal.confidence < 0.6) continue;
                }
                //recalculate targetSize based on limit money
                double allowedSize = 0.0;
                if (riskManager_) {
                    allowedSize = riskManager_->getAllowedPositionSize(execSignal.symbol);
                }

                double sizerFraction = 1.0;
                if (positionSizer_) {
                    std::map<std::string, double> metrics;
                    // Minimal metrics: use confidence as a rough proxy for win_rate
                    metrics["win_rate"] = execSignal.confidence;
                    // placeholder volatility - can be replaced with real vol from market data
                    metrics["volatility"] = 0.05;
                    std::map<std::string, double> portfolio;
                    sizerFraction = positionSizer_->calculateSize(execSignal.symbol.toString(), metrics, portfolio);
                    sizerFraction = std::clamp(sizerFraction, 0.0, 1.0);
                }
                double positionSize = allowedSize * sizerFraction;
                // clamp this to
                double finalQty = positionSize * execSignal.targetSize;
                // clamp to limit exposure or it will be rejected by risk
                finalQty = std::clamp(finalQty, 0.0, allowedSize);

                execSignal.targetSize = finalQty; // now this is absolute size
                
                // std::cout << "🛎️ Signal passed filtering: " << execSignal.id 
                //           << " for " << execSignal.symbol.toString() 
                //           << " Direction: " << (execSignal.direction == core::Direction::BUY ? "BUY" : "SELL")
                //           << " Strength: " << execSignal.strength 
                //           << " Confidence: " << execSignal.confidence 
                //           << " Quality: " << execSignal.quality 
                //           << " TargetSize: " << execSignal.targetSize 
                //           << " PTCA Recommendation: " << ptcaRecommendation
                //           << std::endl;
                //check if price target percentage > 0.2%
                for (const auto& target : execSignal.targets) {
                    if (target.percentage > 0.2) {
                        std::cout << "  Target size: " << target.size
                                    << " at " << target.price 
                                    << " (" << target.percentage * 100 << "%)" 
                                    << std::endl;
                    }
                }                   

                // Validate with risk manager
                if (riskManager_) {
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
            std::this_thread::sleep_for(1ms); // simulate delay .why?
            if (!executionService_) {
                std::cerr << "Execution service not initialized!" << std::endl;
                continue;
            }
            ExecutionResponse response = executionService_->executeSignal(task);
            if (!response.success) {
                std::cout << "❌ Execution failed for signal " << task.id 
                            << ": " << response.error_message << std::endl;
                // print all of data in this failed order/signal
                std::cout << "Signal details - Symbol: " << task.symbol.toString()
                            << ", Direction: " << (task.direction == core::Direction::BUY ? "BUY" : (task.direction == core::Direction::SELL ? "SELL" : "NEUTRAL"))
                            << ", Strength: " << task.strength
                            << ", Confidence: " << task.confidence
                            << ", Quality: " << task.quality
                            << ", TargetSize: " << task.targetSize
                            << ", StrategyId: " << task.strategyId
                            << ", ExchangeId: " << task.exchangeId.toString()
                            << std::endl;
                continue;
            }
            execution::models::ExecutionReport report;
            report.setOrderId("ORDER_" + task.id);
            report.setSymbol(task.symbol);
            report.setSide(task.direction == core::Direction::BUY ? execution::models::OrderSide::BUY : execution::models::OrderSide::SELL);
            // targetSize is a scale from 0 to 1 (1 == full position as allowed by risk)
            // We compute an allowed size from the risk manager then modulate it by the PositionSizer
            double allowedSize = 0.0;
            if (riskManager_) {
                allowedSize = riskManager_->getAllowedPositionSize(task.symbol);
            }

            double sizerFraction = 1.0;
            if (positionSizer_) {
                std::map<std::string, double> metrics;
                // Minimal metrics: use confidence as a rough proxy for win_rate
                metrics["win_rate"] = task.confidence;
                // placeholder volatility - can be replaced with real vol from market data
                metrics["volatility"] = 0.05;
                std::map<std::string, double> portfolio;
                sizerFraction = positionSizer_->calculateSize(task.symbol.toString(), metrics, portfolio);
                sizerFraction = std::clamp(sizerFraction, 0.0, 1.0);
            }

            double positionSize = allowedSize * sizerFraction;
            // clamp this to
            double finalQty = positionSize * task.targetSize;

            // clamp to limit exposure or it will be rejected by risk
            finalQty = std::clamp(finalQty, 0.0, allowedSize);
            report.setQuantity(finalQty);
            report.setFilledQuantity(finalQty);
            report.setStatus(execution::models::OrderState::FILLED);
            if (riskManager_) {
                riskManager_->processExecutionReport(report);
            }
            std::cout << "📈 Executed: " << task.symbol.toString() << " " 
                        << (task.direction == core::Direction::BUY ? "BUY" : "SELL") 
                        << " " << task.targetSize << std::endl;
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
        // if (count < 20 || count % 500 == 0) {
        //     std::cout << "🛎️ Signal #" << count << ": "
        //               << signal.symbol.toString() 
        //               << " Dir: " << (signal.direction == core::Direction::BUY ? "BUY" : "SELL")
        //               << " Strength: " << signal.strength 
        //               << " Confidence: " << signal.confidence 
        //               << " Quality: " << signal.quality 
        //               << " Targets: " << signal.targets.size() << std::endl;
        // }
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
    setupPTCAConnections();
}

void Trading_system::realExecution(const execution::TradingSignal& signal) {
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

        
