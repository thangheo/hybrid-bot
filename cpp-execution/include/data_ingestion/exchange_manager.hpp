/**
 * @file exchange_manager.hpp
 * @brief Simplified ExchangeManager without event bus circular dependencies
 */

#pragma once
#include <data_ingestion/websocket/exchanges/exchange_connector.hpp>
#include "data_ingestion/types.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/normalization/order_book_normalizer.hpp"
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <future>
#include <unordered_map>

namespace trading {
namespace data_ingestion {

class ExchangeManager {
public:
    using NormalizedOrderBookCallback = std::function<void(const OrderBookUpdate&)>;
    using WebSocketDataCallback = std::function<void(const core::ExchangeId&, const std::string&, const std::vector<uint8_t>&)>;
    
    ExchangeManager(NormalizedOrderBookCallback callback,
                   int minConnections = 3,
                   int maxConnections = 8);
    ~ExchangeManager();
    
    // Core functionality
    bool initialize();
    bool connect();
    void disconnect();
    bool isConnected() const;
    
    // SIMPLIFIED: Direct subscription without event bus
    bool subscribe(const std::vector<std::string>& tradingPairs,
                  const std::vector<DataType>& dataTypes,
                  int depth = 20);
    
    // Query methods
    OrderBookUpdate getOrderBook(const std::string& tradingPair, int32_t depth = 20) const;
    std::string getConnectionStats() const;

private:
    // Configuration
    int minConnections_;
    int maxConnections_;
    
    // Core components
    std::unique_ptr<ConnectionPool> connectionPool_;
    std::unique_ptr<OrderBookNormalizer> normalizer_;
    
    // Callbacks
    NormalizedOrderBookCallback normalizedCallback_;
    WebSocketDataCallback websocketCallback_;
    
    // Connection management
    std::atomic<bool> isConnected_{false};
    std::atomic<bool> running_{false};
    std::thread monitorThread_;
    // mutable std::mutex mutex_;
    mutable std::recursive_mutex mutex_;
    // Subscription tracking
    struct SubscriptionRequest {
        core::ExchangeId exchangeId;
        std::vector<std::string> tradingPairs;
        std::vector<DataType> dataTypes;
        int depth;
    };
    std::vector<SubscriptionRequest> activeSubscriptions_;
    
    // Request ID generation
    std::atomic<uint64_t> requestIdCounter_{1};
    
    // Internal methods
    void onWebSocketData(const core::ExchangeId& exchange,
                        const std::string& symbol,
                        const std::vector<uint8_t>& data);
    
    void monitorConnections();
    
    std::vector<std::string> createStreamNames(
        const std::vector<std::string>& tradingPairs,
        const std::vector<DataType>& dataTypes,
        int depth);
    
    std::string generateRequestId();
    std::string toLowercase(const std::string& str);
};

} // namespace data_ingestion
} // namespace trading