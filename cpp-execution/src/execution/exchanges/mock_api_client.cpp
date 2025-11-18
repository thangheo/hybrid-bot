#include "execution/exchanges/mock_api_client.hpp"
#include <future>

namespace trading {
namespace execution {

MockApiClient::MockApiClient(const ExchangeCredentials& cred) : credentials_(cred), connected_(true) {}

std::future<models::ExecutionReport> MockApiClient::placeOrder(const models::Order& order) {
    return std::async(std::launch::async, [order]() {
        models::ExecutionReport report;
        report.setOrderId(order.getOrderId());
        report.setClientOrderId(order.getClientOrderId());
        report.setExchangeOrderId("MOCK-ORDER-" + order.getClientOrderId());
        report.setSymbol(order.getTradingPair());
        report.setExchangeId(order.getExchangeId());
        report.setQuantity(order.getQuantity());
        // Mock a full fill
        report.setFilledQuantity(order.getQuantity());
        report.setStatus(models::OrderState::FILLED);
        report.setAverageFillPrice(order.getPrice() > 0 ? order.getPrice() : 1.0);
        return report;
    });
}

std::future<models::ExecutionReport> MockApiClient::cancelOrder(const std::string& orderId, const core::Symbol& symbol) {
    return std::async(std::launch::async, [orderId, symbol]() {
        models::ExecutionReport report;
        report.setOrderId(orderId);
        report.setClientOrderId(orderId);
        report.setExchangeOrderId("MOCK-CANCEL-" + orderId);
        report.setSymbol(symbol);
        report.setExchangeId(core::ExchangeId::binance());
        report.setStatus(models::OrderState::CANCELED);
        return report;
    });
}

std::future<models::ExecutionReport> MockApiClient::getOrderStatus(const std::string& orderId, const core::Symbol& symbol) {
    return std::async(std::launch::async, [orderId, symbol]() {
        models::ExecutionReport report;
        report.setOrderId(orderId);
        report.setClientOrderId(orderId);
        report.setExchangeOrderId("MOCK-STATUS-" + orderId);
        report.setSymbol(symbol);
        report.setExchangeId(core::ExchangeId::binance());
        report.setQuantity(1.0);
        report.setFilledQuantity(1.0);
        report.setStatus(models::OrderState::FILLED);
        return report;
    });
}

std::future<std::map<std::string, double>> MockApiClient::getAccountBalances() {
    return std::async(std::launch::async, []() {
        std::map<std::string, double> balances;
        balances["USDT"] = 100000.0;
        balances["BTC"] = 10.0;
        return balances;
    });
}

std::future<double> MockApiClient::getCurrentPrice(const core::Symbol& symbol) {
    return std::async(std::launch::async, [symbol]() {
        // Simple mock price for BTC/USDT
        if (symbol.toString().find("BTC") != std::string::npos) return 50000.0;
        return 1.0; // default
    });
}

bool MockApiClient::isConnected() const { return connected_; }

void MockApiClient::connect() { connected_ = true; }
void MockApiClient::disconnect() { connected_ = false; }

std::string MockApiClient::getExchangeName() const { return "mock_exchange"; }

std::vector<std::string> MockApiClient::getSupportedSymbols() const { return {}; }

std::string MockApiClient::getHealthStatus() { return "OK"; }

std::unordered_map<std::string, std::string> MockApiClient::getExchangeStatuses() const {
    return {{"mock_exchange", "OK"}};
}

int MockApiClient::getRemainingRequests(ExchangeRequestType) const { return 1000; }

std::chrono::milliseconds MockApiClient::getResetTimeForRequestType(ExchangeRequestType) const {
    return std::chrono::milliseconds(0);
}

bool MockApiClient::checkConnection() const { return connected_; }

} // namespace execution
} // namespace trading
