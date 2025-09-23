// Fixed data_ingestion_service.cpp - Use direct subscription
#include "data_ingestion/data_ingestion_service.hpp"
#include "common/logger.hpp"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace trading {
namespace data_ingestion {

DataIngestionService::DataIngestionService(const std::string& configPath)
    : configPath_(configPath)
    , config_(nullptr)
    , reconstructor_(nullptr)
    , startTime_(std::chrono::steady_clock::now()) {
}

DataIngestionService::~DataIngestionService() {
    stop();
}

bool DataIngestionService::initialize() {
    try {
        std::cout << "Initializing DataIngestionService..." << std::endl;
        
        // 1. Load configuration first
        if (!loadConfiguration()) {
            std::cerr << "Failed to load configuration" << std::endl;
            return false;
        }
        
        // 2. Setup event bus (still needed for other components)
        eventBus_ = std::make_unique<common::EventBus>();
        eventBus_->start();
        setupEventHandling();
        
        // 3. Setup publisher + order book manager
        if (!setupPublisher()) {
            std::cerr << "Failed to setup publisher" << std::endl;
            return false;
        }
        
        // 3. Setup order book manager
        OrderBookManagerConfig managerConfig;  // Use default config
        // orderBookManager_ = std::make_shared<OrderBookManager>(managerConfig);
        //create high frequency manager using factory
        orderBookManager_ = OrderBookManagerFactory::createHighFrequencyManager();
        // setup configuration managerConfig?
        orderBookManager_->setConfig(managerConfig);
        
        // Set up callback for order book changes
        orderBookManager_->setManagerCallback([this](const OrderBookKey& key, const std::string& eventType) {
            // Convert OrderBookKey event to OrderBook and call our handler
            if (eventType == "updated" || eventType == "changed") {
                auto orderBook = orderBookManager_->getOrderBook(key.exchangeId, key.symbol);
                if (orderBook) {
                    this->onOrderBookChanged(*orderBook);
                }
            }
        });
\
        // 4. Setup order book components
        if (!setupOrderBookComponents()) {
            std::cerr << "Failed to setup order book components" << std::endl;
            return false;
        }
        
        // 5. Setup exchange manager (last)
        if (!setupExchangeManager()) {
            std::cerr << "Failed to setup exchange manager" << std::endl;
            return false;
        }
        
        initialized_.store(true);
        std::cout << "DataIngestionService initialized successfully" << std::endl;
        return true;
       
    } catch (const std::exception& e) {
        std::cerr << "Initialization failed: " << e.what() << std::endl;
        cleanup();
        return false;
    }
}

bool DataIngestionService::start(const std::vector<Symbol>& symbols, int depth) {
    subscribedSymbols_ = symbols;  // Store for later use
    //get depth from config
    // int depth = config_->
    int defaultDepth = 20;
    if (depth <= 0) {
        depth = defaultDepth;
    }
    if (!initialized_.load()) {
        std::cerr << "Service not initialized" << std::endl;
        return false;
    }
    
    try {
        std::cout << "Starting DataIngestionService with " << symbols.size() << " symbols..." << std::endl;
        
        // 1. Start publisher
        if (!publisher_->isRunning()) {
            publisher_->start();
        }
        //  for (const auto& symbol : symbols) {
        //     std::vector<PriceLevel> empty_bids;
        //     std::vector<PriceLevel> empty_asks;
            
        //     reconstructor_->processSnapshot(
        //         ExchangeId::binance(), 
        //         symbol,
        //         empty_bids,
        //         empty_asks,
        //         0,
        //         std::chrono::system_clock::now()
        //     );
        // }

        // 2. Start exchange connections first (order books will be created when real data arrives)
        // 3. Start exchange connections
        if (!exchangeManager_->initialize()) {
            std::cerr << "Failed to initialize exchange manager" << std::endl;
            return false;
        }
        
        if (!exchangeManager_->connect()) {
            std::cerr << "Failed to connect to exchanges" << std::endl;
            return false;
        }
        
        // 4. DIRECT SUBSCRIPTION - No more event bus complexity
        std::vector<DataType> dataTypes = {DataType::ORDER_BOOK};        
        
        std::cout << "=== STARTING SUBSCRIPTION PROCESS ===" << std::endl;
        for (const auto& symbol : symbols) {
            // std::cout << "Will subscribe to symbol: " << symbol << std::endl;
        }
        std::cout << "=====================================" << std::endl;
        
        if (!exchangeManager_->subscribe(symbols, dataTypes, depth)) {
            std::cerr << "Failed to subscribe to symbols" << std::endl;
            return false;
        }
        
        std::cout << "Successfully subscribed to all symbols!" << std::endl;
        
        // 5. Start monitoring
        running_.store(true);
        monitoringThread_ = std::thread(&DataIngestionService::runMonitoring, this);
        
        std::cout << "DataIngestionService started successfully" << std::endl;
        publishStatus("service", "started");
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Start failed: " << e.what() << std::endl;
        stop();
        return false;
    }
}

void DataIngestionService::stop() {
    if (!running_.load()) {
        return;
    }
    
    std::cout << "Stopping DataIngestionService..." << std::endl;
    running_.store(false);
    
    // Stop in reverse order of startup
    if (monitoringThread_.joinable()) {
        monitoringThread_.join();
    }
    
    if (exchangeManager_) {
        exchangeManager_->disconnect();
    }
    
    if (publisher_) {
        publisher_->stop();
    }
    
    if (eventBus_) {
        eventBus_->stop();
    }
    
    cleanup();
    publishStatus("service", "stopped");
    std::cout << "DataIngestionService stopped" << std::endl;
}

bool DataIngestionService::loadConfiguration() {
    try {
        config_ = &Configuration::getInstance();
        return config_->loadFromFile(configPath_);
    } catch (const std::exception& e) {
        std::cerr << "Configuration loading failed: " << e.what() << std::endl;
        return false;
    }
}

bool DataIngestionService::setupPublisher() {
    try {
        // check if publisher_ is already set (for testing)
        if (publisher_) {
            std::cout << "Using existing publisher instance" << std::endl;
            return true;
        }
        const auto& pubConfig = config_->getPublisherConfig();
        publisher_ = std::make_shared<GenericZmqServer>(pubConfig);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Publisher setup failed: " << e.what() << std::endl;
        return false;
    }
}

bool DataIngestionService::setupOrderBookComponents() {
    try {
        // Initialize reconstructor
        reconstructor_ = &OrderBookReconstructor::getInstance();
        if (!reconstructor_) {
            std::cerr << "Failed to get OrderBookReconstructor instance (null)" << std::endl;
            return false;
        }
        std::cout << "Attempting to initialize OrderBookReconstructor..." << std::endl;
        if (!reconstructor_->initialize(publisher_, orderBookManager_)) {
            std::cerr << "Failed to initialize order book reconstructor: publisher=" 
                      << (publisher_ ? "set" : "null") << ", manager=" 
                      << (orderBookManager_ ? "set" : "null") << std::endl;
            return false;
        }
        std::cout << "OrderBookReconstructor initialized and ready" << std::endl;
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Order book setup failed: " << e.what() << std::endl;
        return false;
    }
}

bool DataIngestionService::setupExchangeManager() {
    try {
       
        // Setup rate limits for Binance
        auto rateLimitController = std::make_shared<execution::RateLimitController>(core::ExchangeId::binance());
          
        exchangeManager_ = std::make_unique<ExchangeManager>(
            core::ExchangeId::binance(),
            rateLimitController,
            [this](const OrderBookUpdate& update) { onNormalizedOrderBook(update); }
        );
        

        // Configure memory limit for ExchangeManager from config or environment
        try {
            // First try exchange-specific config additional_params
            const auto& exchConf = config_->getExchangeConfig(core::ExchangeId::binance());
            size_t memoryLimitKB = 0;
            auto it = exchConf.additional_params.find("memory_limit_kb");
            if (it != exchConf.additional_params.end()) {
                try {
                    memoryLimitKB = std::stoull(it->second);
                    std::cout << "Found exchange-specific memory limit: " << memoryLimitKB << " KB\n";
                } catch (...) {
                    std::cerr << "Invalid exchange additional_params.memory_limit_kb; ignoring" << std::endl;
                }
            }
            // Next try environment variable
            // memoryLimitKB = 307200; // 300 MB hardcoded for now //disable hardcoded limit
            if (memoryLimitKB > 0) {
                exchangeManager_->setMemoryLimitKB(memoryLimitKB);
                std::cout << "ExchangeManager memory limit set to " << memoryLimitKB << " KB\n";
            }
        } catch (const std::exception& e) {
            std::cerr << "Error reading memory limit config: " << e.what() << std::endl;
        }
        
        std::cout << "Exchange manager setup completed" << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exchange manager setup failed: " << e.what() << std::endl;
        return false;
    }
}

void DataIngestionService::setupEventHandling() {
    // Keep minimal event handling for other components that might need it
    // Remove subscription-related event handling since we're doing direct subscription
}

void DataIngestionService::onNormalizedOrderBook(const OrderBookUpdate& update) {
    try {
        messagesProcessed_.fetch_add(1);
        if (reconstructor_) {
            if (update.isSnapshot) {
                reconstructor_->processSnapshot(
                    update.exchangeId,
                    update.tradingPair,
                    update.bids,
                    update.asks,
                    update.sequenceNumber,
                    update.receivedTimestamp
                );
            } else {
                reconstructor_->processUpdate(
                    update.exchangeId,
                    update.tradingPair,
                    update.bids,
                    update.asks,
                    update.sequenceNumber,
                    update.receivedTimestamp
                );
            }
        } else {
            std::cerr << "Warning: reconstructor_ is null in onNormalizedOrderBook" << std::endl;
            errorsEncountered_.fetch_add(1);
        }
        // Notify external callback
        if (orderBookCallback_) {
            orderBookCallback_(update);
        } else {
            std::cout << "[ERROR] orderBookCallback_ is null!" << std::endl;
        }
        
    } catch (const std::exception& e) {
        errorsEncountered_.fetch_add(1);
        std::cerr << "Error processing order book update: " << e.what() << std::endl;
    }
}

void DataIngestionService::onOrderBookChanged(const OrderBook& orderBook) {
    // Handle order book changes for strategy components
}

void DataIngestionService::onConnectionStatus(const std::string& exchange, const std::string& status) {
    publishStatus("connection", "exchange=" + exchange + ",status=" + status);
}

std::shared_ptr<OrderBook> DataIngestionService::getOrderBook(
    const ExchangeId& exchange, 
    const core::Symbol& symbol) const {
    
    if (reconstructor_) {
        return reconstructor_->getOrderBook(exchange, symbol);
    }
    return nullptr;
}

SimpleStats DataIngestionService::getStatistics() const {
    SimpleStats stats;
    stats.messagesProcessed = messagesProcessed_.load();
    stats.errorsEncountered = errorsEncountered_.load();
    
    // Calculate updates per second
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - startTime_).count();
    
    if (elapsed > 0) {
        stats.updatesPerSecond = static_cast<double>(stats.messagesProcessed) / elapsed;
    }
    
    stats.totalOrderBooks = subscribedSymbols_.size();
    
    return stats;
}

std::vector<Symbol> DataIngestionService::getActiveOrderBooks() const {
    return subscribedSymbols_;
}

bool DataIngestionService::isHealthy() const {
    if (!running_.load() || !initialized_.load()) {
        return false;
    }
    
    if (exchangeManager_ && !exchangeManager_->isConnected()) {
        return false;
    }
    
    return true;
}

std::string DataIngestionService::getStatusReport() const {
    auto stats = getStatistics();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - startTime_).count();
    
    std::ostringstream oss;
    oss << "DataIngestionService Status:\n"
        << "  Running: " << (running_.load() ? "Yes" : "No") << "\n"
        << "  Healthy: " << (isHealthy() ? "Yes" : "No") << "\n"
        << "  Uptime: " << elapsed << " seconds\n"
        << "  Messages Processed: " << messagesProcessed_.load() << "\n"
        << "  Errors: " << errorsEncountered_.load() << "\n"
        << "  Active Order Books: " << stats.totalOrderBooks << "\n"
        << "  Updates/sec: " << stats.updatesPerSecond << "\n";
    
    return oss.str();
}

void DataIngestionService::runMonitoring() {
    auto lastReport = std::chrono::steady_clock::now();
    
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastReport).count() >= 30) {
            if (statusCallback_) {
                statusCallback_("monitoring", getStatusReport());
            }
            lastReport = now;
        }
    }
}

void DataIngestionService::publishStatus(const std::string& component, const std::string& status) {
    if (statusCallback_) {
        statusCallback_(component, status);
    }
}

void DataIngestionService::cleanup() {
    // Reset in reverse order
    exchangeManager_.reset();
    normalizer_.reset();
    
    if (reconstructor_) {
        reconstructor_->shutdown();
        reconstructor_ = nullptr;
    }
    
    publisher_.reset();
    eventBus_.reset();
}

} // namespace data_ingestion
} // namespace trading