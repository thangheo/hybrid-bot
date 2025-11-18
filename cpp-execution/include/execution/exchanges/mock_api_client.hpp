#pragma once

#include "execution/exchange_api_client.hpp"
#include "execution/models/execution_report.hpp"

namespace trading {
namespace execution {

class MockApiClient : public ExchangeApiClient {
public:
    MockApiClient(const ExchangeCredentials& cred);
    ~MockApiClient() override = default;

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

    std::string getHealthStatus() override;
    std::unordered_map<std::string, std::string> getExchangeStatuses() const override;
    int getRemainingRequests(ExchangeRequestType requestType) const override;
    std::chrono::milliseconds getResetTimeForRequestType(ExchangeRequestType requestType) const override;
    bool checkConnection() const override;

private:
    ExchangeCredentials credentials_;
    bool connected_ = true;
};

} // namespace execution
} // namespace trading
