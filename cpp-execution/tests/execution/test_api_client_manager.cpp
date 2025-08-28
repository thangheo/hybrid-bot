// test_api_client_manager.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "execution/api_client_manager.hpp"
#include "execution/authentication_service.hpp"
#include "execution/exchange_api_client.hpp"

using namespace trading::execution;
using namespace testing;

// Mock classes for testing
class MockAuthenticationService : public AuthenticationService {
public:
    MOCK_METHOD(ApiCredentials, getCredentials, (const std::string&), (const, override));
};

class MockExchangeApiClient : public ExchangeApiClient {
public:
    MockExchangeApiClient(const ApiCredentials& credentials) {}
    MOCK_METHOD(bool, checkConnection, (), (const, override));
    MOCK_METHOD(OrderResponse, submitOrder, (const OrderRequest&), (override));
    MOCK_METHOD(CancelResponse, cancelOrder, (const CancelRequest&), (override));
    MOCK_METHOD(OrderStatus, getOrderStatus, (const StatusRequest&), (const, override));
    MOCK_METHOD(AccountBalances, getAccountBalances, (const AccountRequest&), (const, override));
};

// Test fixture
class ApiClientManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        authService = std::make_shared<MockAuthenticationService>();
        manager = std::make_shared<ApiClientManager>(authService);
    }
    
    std::shared_ptr<MockAuthenticationService> authService;
    std::shared_ptr<ApiClientManager> manager;
};

// Define tests
TEST_F(ApiClientManagerTest, InitializesWithAuthService) {
    EXPECT_NE(manager, nullptr);
}

TEST_F(ApiClientManagerTest, ThrowsWithNullAuthService) {
    EXPECT_THROW({
        ApiClientManager invalidManager(nullptr);
    }, std::invalid_argument);
}

TEST_F(ApiClientManagerTest, AddAndRetrieveClient) {
    auto mockClient = std::make_shared<MockExchangeApiClient>(ApiCredentials{});
    manager->addClient("test_exchange", mockClient);
    
    EXPECT_TRUE(manager->hasClient("test_exchange"));
    auto retrievedClient = manager->getClient("test_exchange");
    EXPECT_EQ(retrievedClient, mockClient);
}

TEST_F(ApiClientManagerTest, RemoveClient) {
    auto mockClient = std::make_shared<MockExchangeApiClient>(ApiCredentials{});
    manager->addClient("test_exchange", mockClient);
    EXPECT_TRUE(manager->hasClient("test_exchange"));
    
    manager->removeClient("test_exchange");
    EXPECT_FALSE(manager->hasClient("test_exchange"));
}

TEST_F(ApiClientManagerTest, GetAvailableExchanges) {
    auto mockClient1 = std::make_shared<MockExchangeApiClient>(ApiCredentials{});
    auto mockClient2 = std::make_shared<MockExchangeApiClient>(ApiCredentials{});
    
    manager->addClient("exchange1", mockClient1);
    manager->addClient("exchange2", mockClient2);
    
    auto exchanges = manager->getAvailableExchanges();
    EXPECT_THAT(exchanges, UnorderedElementsAre("exchange1", "exchange2"));
}

TEST_F(ApiClientManagerTest, CheckConnections) {
    auto mockClient1 = std::make_shared<MockExchangeApiClient>(ApiCredentials{});
    auto mockClient2 = std::make_shared<MockExchangeApiClient>(ApiCredentials{});
    
    EXPECT_CALL(*mockClient1, checkConnection())
        .WillOnce(Return(true));
        
    EXPECT_CALL(*mockClient2, checkConnection())
        .WillOnce(Return(false));
    
    manager->addClient("exchange1", mockClient1);
    manager->addClient("exchange2", mockClient2);
    
    auto statuses = manager->checkConnections();
    EXPECT_EQ(statuses.size(), 2);
    EXPECT_TRUE(statuses["exchange1"]);
    EXPECT_FALSE(statuses["exchange2"]);
}

TEST_F(ApiClientManagerTest, CreateClientForSupportedExchange) {
    ApiCredentials creds{"api_key", "secret"};
    EXPECT_CALL(*authService, getCredentials("binance"))
        .WillOnce(Return(creds));
    
    EXPECT_NO_THROW({
        auto client = manager->getClient("binance");
        EXPECT_NE(client, nullptr);
    });
}

TEST_F(ApiClientManagerTest, ThrowsForUnsupportedExchange) {
    ApiCredentials creds{"api_key", "secret"};
    EXPECT_CALL(*authService, getCredentials("unsupported"))
        .WillOnce(Return(creds));
    
    EXPECT_THROW({
        auto client = manager->getClient("unsupported");
    }, std::runtime_error);
}

// Main function
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}