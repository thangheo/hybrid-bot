/**
 * @file mock_api_client_manager.hpp
 * @brief Mock implementation of ApiClientManager
 */
#pragma once

#include <gmock/gmock.h>
#include "execution/api_client_manager.hpp"

namespace trading {
namespace execution {

class MockApiClientManager : public ApiClientManager {
public:
    MOCK_METHOD(std::shared_ptr<ExchangeApiClient>, getClient, (const std::string& exchange_id), (override));
    MOCK_METHOD(bool, initialize, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(std::string, getHealthStatus, (), (const, override));
    MOCK_METHOD(nlohmann::json, getExchangeStatuses, (), (const, override));
};

}  // namespace execution
}  // namespace trading