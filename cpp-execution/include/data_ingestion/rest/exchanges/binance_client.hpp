#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include "core/types.hpp"
#include "common/logger.hpp"
#include <common/types.hpp>
#include <data_ingestion/orderbook/order_book_types.hpp>
#include <core/market_types.hpp>
// nlohmann json for JSON parsing
#include <nlohmann/json.hpp>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

using namespace trading::core;
// Forward declarations
// struct OrderBookUpdate {
//     struct Level {
//         double price;
//         double quantity;
//     };
    
//     std::string symbol;
//     std::vector<Level> bids;
//     std::vector<Level> asks;
//     long long timestamp;
// };

enum class OrderStatus {
    NEW,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED,
    REJECTED,
    EXPIRED
};

// enum class OrderSide {
//     BUY,
//     SELL
// };

// enum class OrderType {
//     MARKET,
//     LIMIT,
//     STOP_LOSS,
//     STOP_LOSS_LIMIT,
//     TAKE_PROFIT,
//     TAKE_PROFIT_LIMIT
// };

struct OrderInfo {
    std::string orderId;
    std::string symbol;
    OrderStatus status;
    OrderSide side;
    trading::execution::models::OrderType type;
    double price;
    double quantity;
    double executedQty;
    long long timestamp;
};

namespace trading {
namespace data_ingestion {


using OrderBookUpdate = trading::data_ingestion::OrderBookUpdate;
// using OrderStatus = trading::common::OrderState;
using OrderSide = trading::common::OrderSide;
using OrderType = trading::execution::models::OrderType;
using PriceLevel = trading::data_ingestion::PriceLevel;

class BinanceClient {
public:
    BinanceClient();
    ~BinanceClient();

    // Authentication & Setup
    void setApiKey(const std::string& key, const std::string& secret);
    void setBaseUrl(const std::string& url);
    
    // Thread-safe setters for configuration
    void setClockOffsetMs(long long offset) { clockOffsetMs_.store(offset); }
    void setDefaultRecvWindowMs(int ms) { defaultRecvWindowMs_.store(ms); }

    // Account & Balance
    std::map<std::string, double> getBalances();
    double getBalance(const std::string& asset);

    // Market Data
    OrderBookUpdate getOrderBook(const std::string& symbol, int limit = 100);
    double getPrice(const std::string& symbol);

    // Order Management
    std::string placeOrder(const std::string& symbol, OrderSide side, OrderType type, 
                          double quantity, double price = 0.0);
    bool cancelOrder(const std::string& symbol, const std::string& orderId);
    OrderInfo getOrderStatus(const std::string& symbol, const std::string& orderId);

    // General REST Helpers
    std::string sendSignedRequest(const std::string& endpoint, 
                                 const std::map<std::string, std::string>& params = {});
    std::string sendPublicRequest(const std::string& endpoint, 
                                 const std::map<std::string, std::string>& params = {});

private:
    // Helper methods
    std::string generateSignature(const std::string& query_string, 
                                 const std::string& api_secret) const;

    std::string buildQueryString(const std::map<std::string, std::string>& params) const;
    std::string makeHttpRequest(const std::string& url, const std::string& method = "GET", 
                               const std::string& body = "", 
                               const std::map<std::string, std::string>& headers = {}) const;
    long long getCurrentTimestamp() const;
    std::string urlEncode(const std::string& value) const;
    
    // New helper method for thread-safe server time sync
    bool resyncServerTime();

    // Member variables
    std::string m_api_key;          // Protected by apiMutex_
    std::string m_api_secret;       // Protected by apiMutex_
    std::string m_base_url;         // Thread-safe (set once during construction)
    
    // Thread-safe atomic variables
    std::atomic<long long> clockOffsetMs_;      // Clock offset in milliseconds
    std::atomic<int> defaultRecvWindowMs_;      // Default recvWindow in milliseconds
    
    // Default URLs
    static const std::string MAINNET_URL;
    static const std::string TESTNET_URL;
    
    // Mutex for protecting API credentials only
    mutable std::mutex apiMutex_;

    // Background thread for periodic server time resync
    std::thread timeResyncThread_;
    std::atomic<bool> timeResyncRunning_{false};
    std::condition_variable timeResyncCv_;
    std::mutex timeResyncMutex_;
    // Interval in seconds between automatic server time resyncs
    int timeResyncIntervalSec_{300};
};



} // namespace data_ingestion
} // namespace trading