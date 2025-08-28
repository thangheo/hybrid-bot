/**
 * @file mock_smart_order_router.hpp
 * @brief Mock implementation of SmartOrderRouter
 */
#pragma once

#include <gmock/gmock.h>
#include "execution/smart_order_router.hpp"

namespace trading {
namespace execution {

class MockSmartOrderRouter : public SmartOrderRouter {
public:
    MOCK_METHOD(std::string, selectExchange, (const std::string& trading_pair, OrderType order_type), (override));
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(std::string, getHealthStatus, (), (const, override));
};

}  // namespace execution
}  // namespace trading