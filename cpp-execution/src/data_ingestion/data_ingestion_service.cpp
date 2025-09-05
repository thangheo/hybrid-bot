// src/data_ingestion/data_ingestion_service.cpp
#include "data_ingestion/data_ingestion_service.hpp"
#include "common/logger.hpp"
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
        
        // 2. Setup event bus
        eventBus_ = std::make_unique<common::EventBus>();
        eventBus_->start();
        setupEventHandling();
        
        // 3. Setup publisher
        if (!setupPublisher()) {
            std::cerr << "Failed to setup publisher" << std::endl;
            return false;
        }
        
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

bool DataIngestionService::start(const std::vector<std::string>& symbols) {
    if (!initialized_.load()) {
        std::cerr << "Service not initialized" << std::endl;
        return false;
    }
    
    try {
        std::cout << "Starting DataIngestionService with " << symbols.size() << " symbols..." << std::endl;
        
        // 1. Start publisher
        if (!publisher_->start()) {
            std::cerr << "Failed to start publisher" << std::endl;
            return false;
        }
        
        // 2. Create order books for symbols
        for (const auto& symbol : symbols) {
            if (!orderBookManager_->createOrderBook(
                ExchangeId::binance(), 
                core::Symbol::fromString(symbol),
                [this](const OrderBook& book) { this->onOrderBookChanged(book); }
            )) {
                std::cout << "Order book for " << symbol << " already exists or failed to create" << std::endl;
            }
        }
        
        // 3. Start exchange connections
        if (!exchangeManager_->initialize()) {
            std::cerr << "Failed to initialize exchange manager" << std::endl;
            return false;
        }
        
        if (!exchangeManager_->connect()) {
            std::cerr << "Failed to connect to exchanges" << std::endl;
            return false;
        }
        
        // 4. Subscribe to symbols
        std::vector<Symbol> symbolObjects;
        for (const auto& symbol : symbols) {
            symbolObjects.emplace_back(core::Symbol::fromString(symbol));
        }
        
        std::vector<DataType> dataTypes = {DataType::ORDERBOOK_SNAPSHOT, DataType::ORDERBOOK_UPDATE};
        
        if (!exchangeManager_->subscribe(symbols, dataTypes, 20)) {
            std::cerr << "Failed to subscribe to symbols" << std::endl;
            return false;
        }
        
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
        const auto& pubConfig = config_->getPublisherConfig();
        publisher_ = std::make_shared<ZeroMQPublisher>(pubConfig);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Publisher setup failed: " << e.what() << std::endl;
        return false;
    }
}

bool DataIngestionService::setupOrderBookComponents() {
    try {
        // 1. Initialize reconstructor
        reconstructor_ = &OrderBookReconstructor::getInstance();
        if (!reconstructor_->initialize(publisher_)) {
            std::cerr << "Failed to initialize order book reconstructor" << std::endl;
            return false;
        }
        
        // 2. Create order book manager
        OrderBookManagerConfig managerConfig;
        managerConfig.autoCreateOrderBooks = true;
        managerConfig.enableStatistics = true;
        managerConfig.validateSequenceNumbers = true;
        
        orderBookManager_ = std::make_unique<OrderBookManager>(
            managerConfig,
            [this](const OrderBookKey& key, const std::string& eventType) {
                publishStatus("orderbook_manager", 
                    "exchange=" + key.exchangeId.toString() + 
                    ",symbol=" + key.symbol.toString() + 
                    ",event=" + eventType);
            }
        );
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Order book setup failed: " << e.what() << std::endl;
        return false;
    }
}

bool DataIngestionService::setupExchangeManager() {
    try {
        // Create normalizer
        normalizer_ = std::make_unique<OrderBookNormalizer>();
        
        // Create exchange manager with proper callback
        auto callback = [this](const OrderBookUpdate& update) {
            this->onNormalizedOrderBook(update);
        };
        
        exchangeManager_ = std::make_unique<ExchangeManager>(callback, 2, 5);
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exchange manager setup failed: " << e.what() << std::endl;
        return false;
    }
}

void DataIngestionService::setupEventHandling() {
    // Subscribe to relevant events through the event bus
    // This provides loose coupling between components
}

void DataIngestionService::onNormalizedOrderBook(const OrderBookUpdate& update) {
    try {
        messagesProcessed_.fetch_add(1);
        
        // Process through order book manager
        if (orderBookManager_) {
            orderBookManager_->processUpdate(update);
        }
        
        // Also process through reconstructor for publishing
        if (reconstructor_) {
            if (update.isSnapshot) {
                reconstructor_->processSnapshot(
                    update.exchangeId,
                    update.symbol,
                    update.bids,
                    update.asks,
                    update.sequenceNumber,
                    update.timestamp
                );
            } else {
                reconstructor_->processUpdate(
                    update.exchangeId,
                    update.symbol,
                    update.bids,
                    update.asks,
                    update.sequenceNumber,
                    update.timestamp
                );
            }
        }
        
        // Notify external callback
        if (orderBookCallback_) {
            orderBookCallback_(update);
        }
        
    } catch (const std::exception& e) {
        errorsEncountered_.fetch_add(1);
        std::cerr << "Error processing order book update: " << e.what() << std::endl;
    }
}

void DataIngestionService::onOrderBookChanged(const OrderBook& orderBook) {
    // Handle order book changes for strategy components
    // This is where you'd integrate with your strategy engine
}

void DataIngestionService::onConnectionStatus(const std::string& exchange, const std::string& status) {
    publishStatus("connection", "exchange=" + exchange + ",status=" + status);
}

std::shared_ptr<OrderBook> DataIngestionService::getOrderBook(
    const ExchangeId& exchange, 
    const core::Symbol& symbol) const {
    
    if (orderBookManager_) {
        return orderBookManager_->getOrderBook(exchange, symbol);
    }
    return nullptr;
}

OrderBookManagerStats DataIngestionService::getStatistics() const {
    if (orderBookManager_) {
        return orderBookManager_->getStatistics();
    }
    return OrderBookManagerStats{};
}

std::vector<OrderBookKey> DataIngestionService::getActiveOrderBooks() const {
    if (orderBookManager_) {
        return orderBookManager_->getActiveOrderBooks();
    }
    return {};
}

bool DataIngestionService::isHealthy() const {
    if (!running_.load() || !initialized_.load()) {
        return false;
    }
    
    if (exchangeManager_ && !exchangeManager_->isConnected()) {
        return false;
    }
    
    if (orderBookManager_ && !orderBookManager_->isHealthy()) {
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
    orderBookManager_.reset();
    
    if (reconstructor_) {
        reconstructor_->shutdown();
        reconstructor_ = nullptr;
    }
    
    publisher_.reset();
    eventBus_.reset();
}

} // namespace data_ingestion
} // namespace trading