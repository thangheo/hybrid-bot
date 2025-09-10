// exchanges/ftx_api_client.hpp
#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include "../exchange_api_client.hpp"
#include "../rate_limit_controller.hpp"

namespace trading {
namespace execution {

class FtxApiClient : public ExchangeApiClient {
public:
    FtxApiClient(const ExchangeCredentials& credentials, 
                std::shared_ptr<RateLimitController> rateLimitController);
    ~FtxApiClient() override;
    
    // Implement ExchangeApiClient interface
    std::string getHealthStatus() override;
    std::unordered_map<std::string, std::string> getExchangeStatuses() const override;

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
    // std::unordered_map<std::string, bool> checkConnections() const override;
    std::string makeApiRequest(const std::string& endpoint, const std::string& method, 
        const std::map<std::string, std::string>& params, bool isPrivate) const ;
private:
    struct FtxImpl;
    std::unique_ptr<FtxImpl> pImpl_;
    std::shared_ptr<RateLimitController> rateLimitController_;
    
    // FTX-specific methods
    std::string generateSignature(const std::string& endpoint, const std::string& method, 
                                 const std::string& timestamp, const std::string& body = "") const;
    // std::string makeApiRequest(const std::string& endpoint, const std::string& method, 
    //                            const std::map<std::string, std::string>& params, bool isPrivate);
    
    // Helper methods for converting between internal and FTX-specific formats
    std::string translateOrderType(models::OrderType type) const;
    std::string translateOrderSide(models::OrderSide side) const;
    models::ExecutionReport parseOrderResponse(const std::string& response) const;
};

} // namespace execution
} // namespace trading