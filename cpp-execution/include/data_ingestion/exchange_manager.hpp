// include/data_ingestion/exchange_manager.hpp
#pragma once

#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include "data_ingestion/normalization/order_book_normalizer.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/websocket/exchanges/exchange_connector.hpp"
#include "common/event_bus.hpp"
#include "common/trading_events.hpp"
#include <string>
#include <memory>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <future>
#include <unordered_map>  

namespace trading {
namespace data_ingestion {

// Forward declarations
class ConnectionPool;
class OrderBookNormalizer;
class BinanceWebSocketClient;

// Type definitions
using MarketDataCallback = std::function<void(const MarketDataUpdate&)>;
using NormalizedOrderBookCallback = std::function<void(const OrderBookUpdate&)>;

class ExchangeManager {
public:
    ExchangeManager(NormalizedOrderBookCallback callback, 
                   int minConnections = 2,
                   int maxConnections = 10);
    ~ExchangeManager();
    
    bool initialize();
    bool connect();
    void disconnect();
    inline std::string toLowercase(const std::string& str) ;

    // bool subscribe(const std::vector<std::string>& tradingPairs,
    //               const std::vector<DataType>& dataTypes,
    //               int32_t depth = 10);
    std::future<bool> subscribe(const std::vector<std::string>& tradingPairs,
                                           const std::vector<DataType>& dataTypes,
                                           int32_t depth=10) ;

    bool isConnected() const;
    
    OrderBookUpdate getOrderBook(const std::string& tradingPair,
                               int32_t depth = 0) const;
    
    std::string getConnectionStats() const;

#ifdef TESTING
    void setConnectionPoolForTesting(std::unique_ptr<ConnectionPool> pool) {
        connectionPool_ = std::move(pool);
    }
#endif

private:
    common::EventBus& eventBus_;
    size_t subscriptionRequestHandlerId_;
    
    // For tracking async responses
    std::unordered_map<std::string, std::promise<bool>> pendingRequests_;
    std::mutex pendingRequestsMutex_;  // Only for the promises map
    
    std::atomic<uint64_t> requestIdCounter_{0};

    // Member variables in proper initialization order
    NormalizedOrderBookCallback normalizedCallback_;
    int minConnections_;
    int maxConnections_;
    std::atomic<bool> running_{false};
    std::atomic<bool> isConnected_{false};
    
    // Mutex must be mutable for const methods
    mutable std::mutex mutex_;
    
    // Complex objects
    std::unique_ptr<OrderBookNormalizer> normalizer_;
    std::unique_ptr<ConnectionPool> connectionPool_;
    std::thread monitorThread_;
    
    // WebSocket callback
    std::function<void(const core::ExchangeId&, const std::string&, const std::vector<uint8_t>&)> websocketCallback_;
    
    // Active subscriptions
    std::vector<SubscriptionRequest> activeSubscriptions_;
    
    // Private methods
    void onWebSocketData(const core::ExchangeId& exchange,
                        const std::string& symbol, 
                        const std::vector<uint8_t>& data);
    void monitorConnections();
    
    std::vector<std::string> createStreamNames(
        const std::vector<std::string>& tradingPairs,
        const std::vector<DataType>& dataTypes,
        int32_t depth);
    void handleSubscriptionRequest(const events::SubscriptionRequestEvent& event);
    void processSubscriptionInternal(const events::SubscriptionRequestEvent& event);
    std::string generateRequestId();
};

} // namespace data_ingestion
} // namespace trading
