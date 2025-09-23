// exchange_api_client.hpp
#pragma once
#include <map>
#include <string>
#include <memory>
#include <vector>
#include <future>
#include "execution/models/order.hpp"
#include "execution/models/execution_report.hpp"
#include <algorithm>
#include <cctype>

namespace trading {
namespace execution {
namespace util {
// Normalize trading symbol to acceptable characters for exchanges.
// Removes separators like '/', ':' and spaces, uppercases letters, and
// keeps only A-Z, 0-9, '-', '_' and '.'. Implemented inline so it can
// be used across translation units without adding a new source file.
static inline std::string normalizeSymbolForExchange(const std::string& sym) {
    std::string out;
    out.reserve(sym.size());
    for (unsigned char c : sym) {
        if (c == '/' || c == ':' || c == ' ') continue;
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(std::toupper(c)));
        }
    }
    return out;
}
} // namespace util
} // namespace execution
} // namespace trading

namespace trading {
namespace execution {


enum class ExchangeRequestType {
    PlaceOrder,
    CancelOrder,
    GetOrderStatus,
    GetAccountBalance,
    GetMarketData,
    GetHealthStatus,     // Added for health check
    GetExchangeStatuses,  // Added for exchange statuses,
    CONNECTION,
    SUBSCRIPTION,
    STREAM_REQUEST
};

struct ExchangeCredentials {
    std::string apiKey;
    std::string apiSecret;
    std::string passphrase;  // Some exchanges require this
    // testnet
    bool testnet = false;
    // Additional authentication data
};

class ExchangeApiClient {
public:
    virtual ~ExchangeApiClient() = default;
    // 
    // Order operations
    virtual std::future<models::ExecutionReport> placeOrder(const models::Order& order) = 0;
    virtual std::future<models::ExecutionReport> cancelOrder(const std::string& orderId, const core::Symbol& symbol) = 0;
    virtual std::future<models::ExecutionReport> getOrderStatus(const std::string& orderId, const core::Symbol& symbol) = 0;
    // virtual std::future<models::ExecutionReport> getOrderStatus(const std::string& orderId) = 0;
    // Account operations
    virtual std::future<std::map<std::string, double>> getAccountBalances() = 0;
    
    // Market data operations
    virtual std::future<double> getCurrentPrice(const core::Symbol& symbol) = 0;

    // Connection management
    virtual bool isConnected() const = 0;
    virtual void connect() = 0;
    virtual void disconnect() = 0;
    
    // Exchange information
    virtual std::string getExchangeName() const = 0;
    virtual std::vector<std::string> getSupportedSymbols() const = 0;
    // Health status
    virtual std::string getHealthStatus() = 0;
    // getExchangeStatuse
    virtual std::unordered_map<std::string, std::string> getExchangeStatuses() const = 0;
    // Rate limit information
    virtual int getRemainingRequests(ExchangeRequestType requestType) const = 0;
    virtual std::chrono::milliseconds getResetTimeForRequestType(ExchangeRequestType requestType) const = 0;
    virtual bool checkConnection() const = 0; 
};

} // namespace execution
} // namespace trading