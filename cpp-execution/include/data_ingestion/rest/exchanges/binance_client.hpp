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
    // Optional: adjust clock offset (in ms) and default recvWindow (ms)
    void setClockOffsetMs(long long offset) { clockOffsetMs_ = offset; }
    void setDefaultRecvWindowMs(int ms) { defaultRecvWindowMs_ = ms; }

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
    // Private helper methods
    std::string generateSignature(const std::string& query_string) const;
    std::string buildQueryString(const std::map<std::string, std::string>& params) const;
    std::string makeHttpRequest(const std::string& url, const std::string& method = "GET", 
                               const std::string& body = "", 
                               const std::map<std::string, std::string>& headers = {}) const;
    long long getCurrentTimestamp() const;
    std::string urlEncode(const std::string& value) const;

    // Member variables
    std::string m_api_key;
    std::string m_api_secret;
    std::string m_base_url;
    // Clock offset (server_time - local_time) in milliseconds. Use to correct timestamp.
    long long clockOffsetMs_ = 0;
    // Default recvWindow in milliseconds (can be tuned). Binance allows up to 60000.
    int defaultRecvWindowMs_ = 5000;
    
    // Default URLs
    static const std::string MAINNET_URL;
    static const std::string TESTNET_URL;
    // Protects m_api_key, m_api_secret, clockOffsetMs_, defaultRecvWindowMs_
    mutable std::mutex apiMutex_;
};


} // namespace data_ingestion
} // namespace trading