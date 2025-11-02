/**
 * Mock classes for testing
 * 
 * File: mocks/execution_interface_mock.hpp
 * Created: 2025-04-30
 */

#pragma once

#include <gmock/gmock.h>
#include <string>
#include <vector>
#include <optional>
#include "../../../include/risk/interfaces/execution_interface.hpp"
// Include execution types so CancelResponse and other execution types are visible
#include "../../../include/execution/execution_types.hpp"

namespace trading {
namespace risk {

struct OrderStatus;

namespace test {

class MockExecutionInterface : public ::trading::risk::ExecutionInterface {
public:
    // submitOrder matches ExecutionInterface::submitOrder
    testing::MockFunction<std::string(const ::trading::risk::OrderRequest&)> submitOrderMock;
    std::string submitOrder(const ::trading::risk::OrderRequest& req) override {
        return submitOrderMock.Call(req);
    }

    // cancelOrder in ExecutionInterface takes an orderId string and returns bool
    testing::MockFunction<bool(const std::string&)> cancelOrderMock;
    bool cancelOrder(const std::string& orderId) override {
        return cancelOrderMock.Call(orderId);
    }

    // getOrderStatus returns a JSON string for a given orderId
    testing::MockFunction<std::string(const std::string&)> getOrderStatusMock;
    std::string getOrderStatus(const std::string& orderId) override {
        return getOrderStatusMock.Call(orderId);
    }

    // getAccountBalances returns a map<string,double> for an exchange id
    testing::MockFunction<std::map<std::string, double>(const ::trading::core::ExchangeId&)> getAccountBalancesMock;
    std::map<std::string, double> getAccountBalances(const ::trading::core::ExchangeId& exchangeId) override {
        return getAccountBalancesMock.Call(exchangeId);
    }

    // getOpenOrders returns a vector of OrderRequest; the interface accepts an optional exchange id
    testing::MockFunction<std::vector<::trading::risk::OrderRequest>(const std::optional<std::string>&)> getOpenOrdersMock;
    std::vector<::trading::risk::OrderRequest> getOpenOrders(const std::optional<std::string>& exchangeId = std::nullopt) override {
        return getOpenOrdersMock.Call(exchangeId);
    }
};

} // namespace test
} // namespace risk
} // namespace trading