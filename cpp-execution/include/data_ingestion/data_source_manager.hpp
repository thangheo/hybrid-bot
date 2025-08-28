/**
 * @file data_source_manager.hpp
 * @brief Orchestrates data collection across different exchanges and data types
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * Coordinates WebSocket clients, protocol adapters, and ZeroMQ publishers.
 * 
 * Created on: May 12, 2025
 */

#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

#include "core/market_types.hpp"
#include "data_ingestion/websocket/client_interface.hpp"
#include "data_ingestion/protocol/adapter_interface.hpp"
#include "data_ingestion/publisher/zeromq_publisher.hpp"
#include "data_ingestion/websocket/exchanges/exchange_connector.hpp"

namespace trading {
namespace data_ingestion {

using ExchangeId = core::ExchangeId;
using Symbol = core::Symbol;
/**
 * Configuration for a data subscription
 */
struct DataSubscription {
    ExchangeId exchangeId;          // Which exchange (e.g., "binance")
    std::vector<Symbol> symbols;    // Which trading pairs to monitor
    std::vector<DataType> dataTypes; // What types of data to collect
    
    DataSubscription() = default;
    DataSubscription(const ExchangeId& exchange, 
                    const std::vector<Symbol>& syms,
                    const std::vector<DataType>& types)
        : exchangeId(exchange), symbols(syms), dataTypes(types) {}
};

/**
 * Statistics for data collection
 */
struct DataSourceStats {
    uint64_t messagesReceived = 0;
    uint64_t messagesPublished = 0;
    uint64_t messagesDropped = 0;
    uint64_t bytesReceived = 0;
    uint64_t bytesPublished = 0;
    std::chrono::system_clock::time_point lastMessageTime;
    std::chrono::system_clock::time_point startTime;
};

/**
 * @class DataSourceManager
 * @brief Central coordinator for all market data collection
 * 
 * The DataSourceManager orchestrates:
 * 1. WebSocket connections to exchanges
 * 2. Protocol adapters for message parsing
 * 3. ZeroMQ publishers for data distribution
 * 4. Subscription management across exchanges
 */
class DataSourceManager {
public:
    DataSourceManager();
    ~DataSourceManager();
    
    // Lifecycle management
    bool initialize();
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }
    
    // Subscription management
    bool subscribe(const DataSubscription& subscription);
    bool unsubscribe(const ExchangeId& exchangeId, const std::vector<Symbol>& symbols);
    std::vector<DataSubscription> getActiveSubscriptions() const;
    
    // Component registration
    bool registerWebSocketClient(const ExchangeId& exchangeId, 
                                std::shared_ptr<ClientInterface> client);
    bool registerProtocolAdapter(const ExchangeId& exchangeId,
                                std::shared_ptr<AdapterInterface> adapter);
    bool setPublisher(std::shared_ptr<MarketDataPublisher> publisher);
    
    // Statistics
    DataSourceStats getStats() const;
    DataSourceStats getExchangeStats(const ExchangeId& exchangeId) const;
    
private:
    // Internal state
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    
    // Thread safety
    mutable std::mutex componentsMutex_;
    mutable std::mutex statsMutex_;
    
    // Core components
    std::unordered_map<ExchangeId, std::shared_ptr<ClientInterface>> webSocketClients_;
    std::unordered_map<ExchangeId, std::shared_ptr<AdapterInterface>> protocolAdapters_;
    std::shared_ptr<MarketDataPublisher> publisher_;
    
    // Active subscriptions tracking
    std::unordered_map<ExchangeId, DataSubscription> activeSubscriptions_;
    
    // Statistics
    DataSourceStats overallStats_;
    std::unordered_map<ExchangeId, DataSourceStats> exchangeStats_;
    
    // Message processing callbacks
    void onWebSocketMessage(const ExchangeId& exchangeId, 
                           const std::string& message, 
                           const Timestamp& receivedTime);
    void onWebSocketConnection(const ExchangeId& exchangeId,
                              ConnectionStatus status,
                              const std::string& message);
    
    // Helper methods
    std::string buildStreamName(const ExchangeId& exchangeId, 
                               const Symbol& symbol, 
                               DataType dataType) const;
    std::string buildZeroMQTopic(const ExchangeId& exchangeId,
                                 const Symbol& symbol,
                                 DataType dataType) const;
    std::vector<std::string> buildSubscriptionStreams(const DataSubscription& subscription) const;
    
    // Statistics helpers
    void updateStats(const ExchangeId& exchangeId, size_t bytesReceived, bool published);
};

/**
 * Factory function to create pre-configured DataSourceManager
 */
std::unique_ptr<DataSourceManager> createDataSourceManager(
    const std::vector<ExchangeId>& exchanges,
    const PublisherConfig& publisherConfig = PublisherConfig{}
);

} // namespace data_ingestion
} // namespace trading