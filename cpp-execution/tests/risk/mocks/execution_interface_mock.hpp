/**
 * Mock classes for testing
 * 
 * File: mocks/execution_interface_mock.hpp
 * Created: 2025-04-30
 */

 #pragma once

 #include <gmock/gmock.h>
 #include "../../include/risk/interfaces/execution_interface.hpp"
 
 namespace risk {
 namespace test {
 
 class MockExecutionInterface : public ExecutionInterface {
 public:
     MOCK_METHOD(OrderResponse, submitOrder, (const OrderRequest&), (override));
     MOCK_METHOD(CancelResponse, cancelOrder, (const CancelRequest&), (override));
     MOCK_METHOD(std::optional<OrderStatus>, getOrderStatus, (const std::string&, const std::string&), (const, override));
     MOCK_METHOD(std::vector<AccountBalance>, getAccountBalances, (const std::string&), (const, override));
     MOCK_METHOD(std::vector<OrderStatus>, getOpenOrders, (const std::string&), (const, override));
 };
 
 } // namespace test
 } // namespace risk