// include/data_ingestion/exchange_connector.hpp
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <mutex>
#include <chrono>
#include <thread>
#include <data_ingestion/orderbook/order_book_types.hpp>

#include <core/market_types.hpp>
namespace trading {
namespace data_ingestion {

using namespace core;
// enum class DataType {
//     TRADES,
//     ORDER_BOOK,
//     TICKER
// };

/**
 * Types of market data we can collect
 */
enum class DataType {
    ORDER_BOOK,    // Order book updates (depth data)
    TRADES,        // Individual trade executions  
    TICKER,        // 24hr price statistics
    BOOK_TICKER    // Best bid/offer prices
};


struct SubscriptionRequest {
    ExchangeId exchangeId;
    std::vector<std::string> tradingPairs;
    std::vector<DataType> dataTypes;
    int32_t depth;  // For order book subscriptions
};

// struct MarketDataUpdate {
//     ExchangeId exchangeId;
//     core::Symbol tradingPair;
//     std::string updateType;
//     int64_t exchangeTimestamp;
//     int64_t receivedTimestamp;
//     std::vector<uint8_t> data;  // Exchange-specific data format
// };

// Callback type for market data updates
using MarketDataCallback = std::function<void(const MarketDataUpdate&)>;

class ExchangeConnector {
public:
    ExchangeConnector(const core::ExchangeId& exchangeId, 
                      MarketDataCallback callback);
    virtual ~ExchangeConnector() = default;
    
    // Connect to the exchange WebSocket
    virtual bool connect() = 0;
    
    // Disconnect from the exchange WebSocket
    virtual void disconnect() = 0;
    
    // Subscribe to market data
    virtual bool subscribe(const SubscriptionRequest& request) = 0;
    
    // Get exchange status
    virtual bool isConnected() const { return connected_; }
    
    // Get exchange identifier
    core::ExchangeId getExchangeId() const { return exchangeId_; }
    
    // Get the last error
    std::string getLastError() const;
    
    // Set reconnection parameters
    void setReconnectionParameters(int attempt_limit, int delay_ms);
    
    // Utility functions for data type conversion
    static DataType stringToDataType(const std::string& type_str);
    static std::string dataTypeToString(DataType type);
    
protected:
    core::ExchangeId exchangeId_;
    MarketDataCallback data_callback_;
    bool connected_;
    
    // Error handling
    mutable std::mutex error_mutex_;
    std::string last_error_;
    std::chrono::system_clock::time_point last_error_time_;
    void reportError(const std::string& error_msg, bool is_fatal = false);
    
    // Reconnection logic
    int reconnect_attempt_limit_;
    int current_reconnect_attempts_;
    int reconnect_delay_ms_;
    bool tryReconnect();
    
    // Active subscriptions management
    std::vector<SubscriptionRequest> active_subscriptions_;
    void addSubscriptionRequest(const SubscriptionRequest& request);
    void clearSubscriptionRequests();
    std::vector<SubscriptionRequest> getActiveSubscriptions() const;
};

} // namespace data_ingestion
} // namespace trading