// exchanges/binance_api_client.hpp
#pragma once

#include <string>
#include <memory>
#include <map>
#include <unordered_map>
#include "../exchange_api_client.hpp"
#include "../rate_limit_controller.hpp"
#include "../models/order.hpp"  // Include header that defines Order class
#include "../models/execution_report.hpp"  // Include header that defines ExecutionReport class
// using namespace trading::execution::models;
namespace trading {
namespace execution {

class BinanceApiClient : public ExchangeApiClient {
public:
    BinanceApiClient(const ExchangeCredentials& credentials, 
                    std::shared_ptr<RateLimitController> rateLimitController);
    ~BinanceApiClient() override;
   
    // Implement ExchangeApiClient interface
    std::future<models::ExecutionReport> placeOrder(const models::Order& order) override;
    std::future<models::ExecutionReport> cancelOrder(const std::string& orderId, const core::Symbol& symbol) override;
    std::future<models::ExecutionReport> getOrderStatus(const std::string& orderId, const core::Symbol& symbol) override;
    std::future<std::map<std::string, double>> getAccountBalances() override;
    std::future<double> getCurrentPrice(const core::Symbol& symbol) override;
    bool isConnected() const override;
    void connect() override;
    void disconnect() override;
    std::string getExchangeName() const override;
    std::vector<std::string> getSupportedSymbols() const override;
    int getRemainingRequests(ExchangeRequestType requestType) const override;
    std::chrono::milliseconds getResetTimeForRequestType(ExchangeRequestType requestType) const override;
    bool checkConnection() const override;
    // Additional methods
    // getHealthStatus
    // std::string getHealthStatus();
    std::string getHealthStatus() override; // Make sure this matches the base class
    // getExchangeStatuses
    std::unordered_map<std::string, std::string> getExchangeStatuses() const override;
    
private:
    struct BinanceImpl;
    std::unique_ptr<BinanceImpl> pImpl_;
    std::shared_ptr<RateLimitController> rateLimitController_;
    
    // Binance-specific methods
    std::string generateSignature(const std::string& queryString) const;
    // std::string makeApiRequest(const std::string& endpoint, const std::string& method, 
    //                            const std::map<std::string, std::string>& params, bool isPrivate);
    std::string makeApiRequest(std::string endpoint, std::string method,
    const std::map<std::string, std::string>& params, bool isPrivate) const ;
                        
    
    // Helper methods for converting between internal and Binance-specific formats
    std::string translateOrderType(models::OrderType type) const;
    std::string translateTimeInForce(models::TimeInForce tif) const;
    std::string translateOrderSide(models::OrderSide side) const;
    models::ExecutionReport parseOrderResponse(const std::string& response) const;
};

} // namespace execution
} // namespace trading