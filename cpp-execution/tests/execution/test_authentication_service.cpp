// test_authentication_service.cpp

#include <gtest/gtest.h>
#include "execution/authentication_service.hpp"
#include <thread>
#include <vector>

 using namespace trading::execution;
 using namespace testing;
 using namespace trading::execution::models;
 using namespace trading::core;
 using namespace trading;

namespace trading {
namespace execution {
namespace test {

class AuthenticationServiceTest : public ::testing::Test {
protected:
    AuthenticationService service;
    
    // Test credentials
    const ExchangeCredentials testCredentials{
        "test_api_key_12345678901234",
        "test_api_secret_abcdefghijklmnopqrstuvwxyz"
    };
    
    core::ExchangeId exchangeId = core::ExchangeId::fromString("BINANCE");
};

TEST_F(AuthenticationServiceTest, InitiallyNoCredentials) {
    EXPECT_FALSE(service.hasCredentials(exchangeId));
    EXPECT_FALSE(service.getCredentials(exchangeId).has_value());
}

TEST_F(AuthenticationServiceTest, SetAndGetCredentials) {
    // Set credentials
    service.setCredentials(exchangeId, testCredentials);
    
    // Check that the service reports having credentials
    EXPECT_TRUE(service.hasCredentials(exchangeId));
    
    // Retrieve and verify credentials
    auto credentials = service.getCredentials(exchangeId);
    ASSERT_TRUE(credentials.has_value());
    EXPECT_EQ(credentials->apiKey, testCredentials.apiKey);
    EXPECT_EQ(credentials->apiSecret, testCredentials.apiSecret);
}

TEST_F(AuthenticationServiceTest, RemoveCredentials) {
    // Set credentials
    service.setCredentials(exchangeId, testCredentials);
    EXPECT_TRUE(service.hasCredentials(exchangeId));
    
    // Remove credentials
    service.removeCredentials(exchangeId);
    EXPECT_FALSE(service.hasCredentials(exchangeId));
    EXPECT_FALSE(service.getCredentials(exchangeId).has_value());
}

TEST_F(AuthenticationServiceTest, ValidateCredentials) {
    // Valid credentials
    ExchangeCredentials valid{
        "valid_api_key_1234567890",
        "valid_api_secret_abcdefghijklmnopqrstuvwxyz"
    };
    
    // Invalid credentials (too short API key)
    ExchangeCredentials invalid{
        "short",
        "valid_api_secret"
    };
    
    // Test validation
    EXPECT_TRUE(service.validateCredentials(exchangeId, valid));
    EXPECT_FALSE(service.validateCredentials(exchangeId, invalid));
    
    // Check that valid credentials were stored
    EXPECT_TRUE(service.hasCredentials(exchangeId));
    auto storedCredentials = service.getCredentials(exchangeId);
    ASSERT_TRUE(storedCredentials.has_value());
    EXPECT_EQ(storedCredentials->apiKey, valid.apiKey);
}

TEST_F(AuthenticationServiceTest, MultipleExchanges) {
    core::ExchangeId exchange1 = core::ExchangeId::fromString("BINANCE");
    core::ExchangeId exchange2 = core::ExchangeId::fromString("FTX");
    
    ExchangeCredentials credentials1{
        "api_key_for_exchange1",
        "api_secret_for_exchange1"
    };
    
    ExchangeCredentials credentials2{
        "api_key_for_exchange2",
        "api_secret_for_exchange2"
    };
    
    // Set credentials for both exchanges
    service.setCredentials(exchange1, credentials1);
    service.setCredentials(exchange2, credentials2);
    
    // Verify credentials for each exchange
    EXPECT_TRUE(service.hasCredentials(exchange1));
    EXPECT_TRUE(service.hasCredentials(exchange2));
    
    auto stored1 = service.getCredentials(exchange1);
    auto stored2 = service.getCredentials(exchange2);
    
    ASSERT_TRUE(stored1.has_value());
    ASSERT_TRUE(stored2.has_value());
    
    EXPECT_EQ(stored1->apiKey, credentials1.apiKey);
    EXPECT_EQ(stored2->apiKey, credentials2.apiKey);
    
    // Remove one exchange's credentials
    service.removeCredentials(exchange1);
    EXPECT_FALSE(service.hasCredentials(exchange1));
    EXPECT_TRUE(service.hasCredentials(exchange2));
}

TEST_F(AuthenticationServiceTest, ThreadSafety) {
    const int numThreads = 10;
    const int operationsPerThread = 100;
    
    std::vector<std::thread> threads;
    
    // Create multiple threads that set and get credentials
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, operationsPerThread]() {
            // const std::string exchangeId = "exchange_" + std::to_string(i);
            core::ExchangeId exchangeId = core::ExchangeId::fromString("EXCHANGE_" + std::to_string(i));
            
            for (int j = 0; j < operationsPerThread; ++j) {
                ExchangeCredentials credentials{
                    "api_key_" + std::to_string(i) + "_" + std::to_string(j),
                    "api_secret_" + std::to_string(i) + "_" + std::to_string(j)
                };
                
                service.setCredentials(exchangeId, credentials);
                auto stored = service.getCredentials(exchangeId);
                ASSERT_TRUE(stored.has_value());
                EXPECT_EQ(stored->apiKey, credentials.apiKey);
                
                // Occasionally remove credentials
                if (j % 10 == 0) {
                    service.removeCredentials(exchangeId);
                    EXPECT_FALSE(service.hasCredentials(exchangeId));
                }
            }
        });
    }
    
    // Join all threads
    for (auto& thread : threads) {
        thread.join();
    }
}

} // namespace test
} // namespace execution
} // namespace trading

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}