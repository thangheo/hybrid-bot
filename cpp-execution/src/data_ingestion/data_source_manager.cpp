/**
 * @file data_source_manager.cpp
 * @brief Implementation of DataSourceManager
 * 
 * Created on: May 12, 2025
 */

#include "data_ingestion/data_source_manager.hpp"
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/protocol/binance_adapter.hpp"
#include "common/logger.hpp"

#include <algorithm>
#include <memory>
#include <sstream>

namespace trading {
namespace data_ingestion {

DataSourceManager::DataSourceManager() {
    overallStats_.startTime = std::chrono::system_clock::now();
}

DataSourceManager::~DataSourceManager() {
    stop();
}

bool DataSourceManager::initialize() {
    std::lock_guard<std::mutex> lock(componentsMutex_);
    
    if (initialized_.load()) {
        LOG_WARN("DataSourceManager already initialized");
        return true;
    }
    
    // Validate that we have required components
    if (!publisher_) {
        LOG_ERROR("No publisher configured - call setPublisher() first");
        return false;
    }
    
    // Initialize publisher
    if (!publisher_->start()) {
        LOG_ERROR("Failed to start market data publisher");
        return false;
    }
    
    LOG_INFO("DataSourceManager initialized successfully");
    initialized_.store(true);
    return true;
}

bool DataSourceManager::start() {
    if (!initialized_.load()) {
        LOG_ERROR("DataSourceManager not initialized - call initialize() first");
        return false;
    }
    
    if (running_.load()) {
        LOG_WARN("DataSourceManager already running");
        return true;
    }
    
    std::lock_guard<std::mutex> lock(componentsMutex_);
    
    // Start all WebSocket clients
    for (auto& [exchangeId, client] : webSocketClients_) {
        if (!client->connect()) {
            LOG_ERROR("Failed to connect WebSocket client for exchange: " + exchangeId.toString());
            return false;
        }
        LOG_INFO("Connected to exchange: " + exchangeId.toString());
    }
    
    running_.store(true);
    LOG_INFO("DataSourceManager started successfully");
    return true;
}

void DataSourceManager::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_.store(false);
    
    std::lock_guard<std::mutex> lock(componentsMutex_);
    
    // Disconnect all WebSocket clients
    for (auto& [exchangeId, client] : webSocketClients_) {
        client->disconnect();
        LOG_INFO("Disconnected from exchange: " + exchangeId.toString());
    }
    
    // Stop publisher
    if (publisher_) {
        publisher_->stop();
    }
    
    LOG_INFO("DataSourceManager stopped");
}

bool DataSourceManager::subscribe(const DataSubscription& subscription) {
    if (!running_.load()) {
        LOG_ERROR("DataSourceManager not running - call start() first");
        return false;
    }
    
    std::lock_guard<std::mutex> lock(componentsMutex_);
    
    // Find the WebSocket client for this exchange
    auto clientIt = webSocketClients_.find(subscription.exchangeId);
    if (clientIt == webSocketClients_.end()) {
        LOG_ERROR("No WebSocket client registered for exchange: " + subscription.exchangeId.toString());
        return false;
    }
    
    // Build subscription streams based on exchange and data types
    auto streams = buildSubscriptionStreams(subscription);
    
    // Subscribe to the WebSocket streams
    auto binanceClient = std::dynamic_pointer_cast<BinanceWebSocketClient>(clientIt->second);
    if (binanceClient) {
        // Pass the symbols directly from the subscription instead of the stream names
        if (!binanceClient->subscribe(subscription.symbols)) {
            LOG_ERROR("Failed to subscribe to streams for exchange: " + subscription.exchangeId.toString());
            return false;
        }
    } else {
        LOG_ERROR("WebSocket client type not supported for exchange: " + subscription.exchangeId.toString());
        return false;
    }
    
    // Store the active subscription
    activeSubscriptions_[subscription.exchangeId] = subscription;
    
    LOG_INFO("Successfully subscribed to " + std::to_string(streams.size()) + 
             " streams for exchange: " + subscription.exchangeId.toString());
    
    return true;
}

bool DataSourceManager::unsubscribe(const ExchangeId& exchangeId, const std::vector<Symbol>& symbols) {
    std::lock_guard<std::mutex> lock(componentsMutex_);
    
    auto clientIt = webSocketClients_.find(exchangeId);
    if (clientIt == webSocketClients_.end()) {
        LOG_ERROR("No WebSocket client registered for exchange: " + exchangeId.toString());
        return false;
    }
    
    // For simplicity, we'll remove the entire subscription
    // In a more sophisticated implementation, you'd remove only specific symbols
    activeSubscriptions_.erase(exchangeId);
    
    LOG_INFO("Unsubscribed from exchange: " + exchangeId.toString());
    return true;
}

std::vector<DataSubscription> DataSourceManager::getActiveSubscriptions() const {
    std::lock_guard<std::mutex> lock(componentsMutex_);
    
    std::vector<DataSubscription> subscriptions;
    for (const auto& [exchangeId, subscription] : activeSubscriptions_) {
        subscriptions.push_back(subscription);
    }
    
    return subscriptions;
}

bool DataSourceManager::registerWebSocketClient(const ExchangeId& exchangeId, 
                                               std::shared_ptr<ClientInterface> client) {
    std::lock_guard<std::mutex> lock(componentsMutex_);
    
    if (running_.load()) {
        LOG_ERROR("Cannot register components while DataSourceManager is running");
        return false;
    }
    
    // Set up callbacks for this client
    client->setMessageCallback([this, exchangeId](const std::string& message, const Timestamp& timestamp) {
        onWebSocketMessage(exchangeId, message, timestamp);
    });
    
    client->setConnectionCallback([this, exchangeId](ConnectionStatus status, const std::string& message) {
        onWebSocketConnection(exchangeId, status, message);
    });
    
    webSocketClients_[exchangeId] = client;
    exchangeStats_[exchangeId].startTime = std::chrono::system_clock::now();
    
    LOG_INFO("Registered WebSocket client for exchange: " + exchangeId.toString());
    return true;
}

bool DataSourceManager::registerProtocolAdapter(const ExchangeId& exchangeId,
                                               std::shared_ptr<AdapterInterface> adapter) {
    std::lock_guard<std::mutex> lock(componentsMutex_);
    
    if (running_.load()) {
        LOG_ERROR("Cannot register components while DataSourceManager is running");
        return false;
    }
    
    protocolAdapters_[exchangeId] = adapter;
    LOG_INFO("Registered protocol adapter for exchange: " + exchangeId.toString());
    return true;
}

bool DataSourceManager::setPublisher(std::shared_ptr<MarketDataPublisher> publisher) {
    std::lock_guard<std::mutex> lock(componentsMutex_);
    
    if (running_.load()) {
        LOG_ERROR("Cannot set publisher while DataSourceManager is running");
        return false;
    }
    
    publisher_ = publisher;
    LOG_INFO("Set market data publisher");
    return true;
}

DataSourceStats DataSourceManager::getStats() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return overallStats_;
}

DataSourceStats DataSourceManager::getExchangeStats(const ExchangeId& exchangeId) const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    auto it = exchangeStats_.find(exchangeId);
    if (it != exchangeStats_.end()) {
        return it->second;
    }
    
    return DataSourceStats{}; // Return empty stats if exchange not found
}

void DataSourceManager::onWebSocketMessage(const ExchangeId& exchangeId, 
                                          const std::string& message, 
                                          const Timestamp& receivedTime) {
    // Find the protocol adapter for this exchange
    std::shared_ptr<AdapterInterface> adapter;
    {
        std::lock_guard<std::mutex> lock(componentsMutex_);
        auto adapterIt = protocolAdapters_.find(exchangeId);
        if (adapterIt == protocolAdapters_.end()) {
            LOG_WARN("No protocol adapter registered for exchange: " + exchangeId.toString());
            return;
        }
        adapter = adapterIt->second;
    }
    
    // Create ExchangeData structure with required fields
    Symbol symbol; // Create a proper symbol object based on message content
    core::ExchangeData exchangeData(
        exchangeId,
        symbol,
        message,
        std::chrono::duration_cast<std::chrono::nanoseconds>(receivedTime.time_since_epoch()).count(),
        "websocket"
    );
    
    // Check if adapter can handle this data
    if (!adapter->canHandle(exchangeData)) {
        LOG_DEBUG("Adapter cannot handle message from exchange: " + exchangeId.toString());
        return;
    }
    
    try {
        // Convert to standard format
        auto orderBookUpdate = adapter->convertToStandardFormat(exchangeData);
        
        // Build ZeroMQ topic
        Symbol symbol = orderBookUpdate.symbol;
        auto topic = buildZeroMQTopic(exchangeId, symbol, DataType::ORDER_BOOK);
        
        // Serialize and publish (simplified - you'd use proper serialization)
        std::vector<uint8_t> serializedData(message.begin(), message.end());
        
        bool published = false;
        if (publisher_) {
            published = publisher_->publishRawData(topic, serializedData);
        }
        
        // Update statistics
        updateStats(exchangeId, message.size(), published);
        
        if (published) {
            LOG_DEBUG("Published message to topic: " + topic);
        } else {
            LOG_WARN("Failed to publish message to topic: " + topic);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing message from " + exchangeId.toString() + ": " + e.what());
        updateStats(exchangeId, message.size(), false);
    }
}

void DataSourceManager::onWebSocketConnection(const ExchangeId& exchangeId,
                                             ConnectionStatus status,
                                             const std::string& message) {
    switch (status) {
        case ConnectionStatus::CONNECTED:
            LOG_INFO("WebSocket connected to " + exchangeId.toString() + ": " + message);
            break;
        case ConnectionStatus::DISCONNECTED:
            LOG_WARN("WebSocket disconnected from " + exchangeId.toString() + ": " + message);
            break;
        case ConnectionStatus::ERROR:
            LOG_ERROR("WebSocket error for " + exchangeId.toString() + ": " + message);
            break;
        default:
            LOG_DEBUG("WebSocket status change for " + exchangeId.toString() + ": " + message);
            break;
    }
}

std::string DataSourceManager::buildStreamName(const ExchangeId& exchangeId, 
                                              const Symbol& symbol, 
                                              DataType dataType) const {
    std::string symbolStr = symbol.base + symbol.quote;
    std::transform(symbolStr.begin(), symbolStr.end(), symbolStr.begin(), ::tolower);
    
    switch (dataType) {
        case DataType::ORDER_BOOK:
            return symbolStr + "@depth";
        case DataType::TRADES:
            return symbolStr + "@trade";
        case DataType::TICKER:
            return symbolStr + "@ticker";
        case DataType::BOOK_TICKER:
            return symbolStr + "@bookTicker";
        default:
            return symbolStr + "@depth";
    }
}

std::string DataSourceManager::buildZeroMQTopic(const ExchangeId& exchangeId,
                                               const Symbol& symbol,
                                               DataType dataType) const {
    std::string dataTypeStr;
    switch (dataType) {
        case DataType::ORDER_BOOK: dataTypeStr = "orderbook"; break;
        case DataType::TRADES: dataTypeStr = "trade"; break;
        case DataType::TICKER: dataTypeStr = "ticker"; break;
        case DataType::BOOK_TICKER: dataTypeStr = "bookticker"; break;
        default: dataTypeStr = "orderbook"; break;
    }
    
    return "market." + dataTypeStr + "." + exchangeId.toString() + "." + symbol.toString();
}

std::vector<std::string> DataSourceManager::buildSubscriptionStreams(const DataSubscription& subscription) const {
    std::vector<std::string> streams;
    
    for (const auto& symbol : subscription.symbols) {
        for (const auto& dataType : subscription.dataTypes) {
            auto stream = buildStreamName(subscription.exchangeId, symbol, dataType);
            streams.push_back(stream);
        }
    }
    
    return streams;
}

void DataSourceManager::updateStats(const ExchangeId& exchangeId, size_t bytesReceived, bool published) {
    std::lock_guard<std::mutex> lock(statsMutex_);
    
    auto now = std::chrono::system_clock::now();
    
    // Update overall stats
    overallStats_.messagesReceived++;
    overallStats_.bytesReceived += bytesReceived;
    overallStats_.lastMessageTime = now;
    
    if (published) {
        overallStats_.messagesPublished++;
        overallStats_.bytesPublished += bytesReceived;
    } else {
        overallStats_.messagesDropped++;
    }
    
    // Update exchange-specific stats
    auto& exchangeStats = exchangeStats_[exchangeId];
    exchangeStats.messagesReceived++;
    exchangeStats.bytesReceived += bytesReceived;
    exchangeStats.lastMessageTime = now;
    
    if (published) {
        exchangeStats.messagesPublished++;
        exchangeStats.bytesPublished += bytesReceived;
    } else {
        exchangeStats.messagesDropped++;
    }
}


std::unique_ptr<DataSourceManager> createDataSourceManager(
    const std::vector<ExchangeId>& exchanges,        // ✅ Fixed: removed & from reference
    const PublisherConfig& publisherConfig) {        // ✅ Fixed: removed & from reference
    
    auto manager = std::make_unique<DataSourceManager>();
    
    // Create and set publisher
    auto publisher = std::make_shared<MarketDataPublisher>(publisherConfig);
    if (!manager->setPublisher(publisher)) {
        LOG_ERROR("Failed to set publisher in DataSourceManager");
        return nullptr;
    }
    
    // Register components for each exchange
    for (const auto& exchangeId : exchanges) {
        if (exchangeId == ExchangeId::binance()) {
            try {
                // Create Binance WebSocket client
                auto wsClient = std::make_shared<BinanceWebSocketClient>(
                    "stream.binance.com", "9443",
                    [](const core::ExchangeId&, const std::string&, const std::vector<uint8_t>&) {
                        // This callback will be overridden by DataSourceManager
                    });
                
                // Create Binance protocol adapter
                auto adapter = std::make_shared<BinanceAdapter>();
                
                // Register with manager
                if (!manager->registerWebSocketClient(exchangeId, wsClient)) {
                    LOG_ERROR("Failed to register WebSocket client for exchange: " + exchangeId.toString());
                    return nullptr;
                }
                
                if (!manager->registerProtocolAdapter(exchangeId, adapter)) {
                    LOG_ERROR("Failed to register protocol adapter for exchange: " + exchangeId.toString());
                    return nullptr;
                }
                
                LOG_INFO("Successfully registered components for exchange: " + exchangeId.toString());
                
            } catch (const std::exception& e) {
                LOG_ERROR("Exception while creating components for " + exchangeId.toString() + ": " + e.what());
                return nullptr;
            }
        }
        // Add other exchanges here...
        else {
            LOG_WARN("Unsupported exchange: " + exchangeId.toString());
        }
    }
    
    LOG_INFO("DataSourceManager created successfully with " + 
             std::to_string(exchanges.size()) + " exchanges");
    
    return manager;
}
} // namespace data_ingestion
} // namespace trading