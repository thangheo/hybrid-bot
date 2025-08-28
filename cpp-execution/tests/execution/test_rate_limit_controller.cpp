// test_rate_limit_controller.cpp

#include <gtest/gtest.h>
#include "execution/rate_limit_controller.hpp"
#include <thread>
#include <atomic>
#include <future>

namespace trading {
namespace execution {
namespace test {

class RateLimitControllerTest : public ::testing::Test {
protected:
    const std::string exchangeId = "binance";
    RateLimitController controller{exchangeId};
    
    void SetUp() override {
        // Define some default rate limit rules
        std::unordered_map<ExchangeRequestType, RateLimitRule> rules = {
            {ExchangeRequestType::ORDER_PLACEMENT, {10, std::chrono::milliseconds(1000)}},
            {ExchangeRequestType::ORDER_CANCELLATION, {20, std::chrono::milliseconds(1000)}},
            {ExchangeRequestType::ORDER_STATUS, {50, std::chrono::milliseconds(1000)}},
            {ExchangeRequestType::ACCOUNT_BALANCE, {5, std::chrono::milliseconds(1000)}}
        };
        
        controller.setRateLimitRules(rules);
    }
};

TEST_F(RateLimitControllerTest, InitiallyNotRateLimited) {
    EXPECT_FALSE(controller.wouldBeRateLimited(ExchangeRequestType::ORDER_PLACEMENT));
    EXPECT_FALSE(controller.wouldBeRateLimited(ExchangeRequestType::ORDER_CANCELLATION));
    EXPECT_FALSE(controller.wouldBeRateLimited(ExchangeRequestType::ORDER_STATUS));
    EXPECT_FALSE(controller.wouldBeRateLimited(ExchangeRequestType::ACCOUNT_BALANCE));
}

TEST_F(RateLimitControllerTest, UpdateRateLimitStatus) {
    // Update status to indicate we're running out of requests
    controller.updateRateLimitStatus(
        ExchangeRequestType::ORDER_PLACEMENT,
        1, // Only 1 request remaining
        std::chrono::milliseconds(500) // Reset in 500ms
    );
    
    // Status should show the update
    auto status = controller.getRateLimitStatus(ExchangeRequestType::ORDER_PLACEMENT);
    EXPECT_EQ(status.remainingRequests, 1);
    EXPECT_EQ(status.resetTime, std::chrono::milliseconds(500));
    EXPECT_FALSE(status.isLimited);
    
    // Update status to indicate we're rate limited
    controller.updateRateLimitStatus(
        ExchangeRequestType::ORDER_PLACEMENT,
        0, // No requests remaining
        std::chrono::milliseconds(1000) // Reset in 1000ms
    );
    
    // Status should show we're rate limited
    status = controller.getRateLimitStatus(ExchangeRequestType::ORDER_PLACEMENT);
    EXPECT_EQ(status.remainingRequests, 0);
    EXPECT_EQ(status.resetTime, std::chrono::milliseconds(1000));
    EXPECT_TRUE(status.isLimited);
    
    // We should be rate limited for order placement
    EXPECT_TRUE(controller.wouldBeRateLimited(ExchangeRequestType::ORDER_PLACEMENT));
    
    // But not for other types
    EXPECT_FALSE(controller.wouldBeRateLimited(ExchangeRequestType::ORDER_CANCELLATION));
}

TEST_F(RateLimitControllerTest, ExecuteRequestWithRateLimit) {
    // Create a counter to track executions
    std::atomic<int> executionCount{0};
    
    // Function to execute
    auto testFunc = [&executionCount]() {
        executionCount++;
        return executionCount.load();
    };
    
    // Execute several requests of the same type
    const int requestCount = 15;
    std::vector<std::future<int>> futures;
    
    for (int i = 0; i < requestCount; ++i) {
        futures.push_back(controller.executeRequest(
            ExchangeRequestType::ORDER_PLACEMENT,
            testFunc
        ));
    }
    
    // Let the rate limiter process requests
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Update rate limit to indicate we're limited
    controller.updateRateLimitStatus(
        ExchangeRequestType::ORDER_PLACEMENT,
        0, // No requests remaining
        std::chrono::milliseconds(500) // Reset in 500ms
    );
    
    // Add a few more requests
    for (int i = 0; i < 5; ++i) {
        futures.push_back(controller.executeRequest(
            ExchangeRequestType::ORDER_PLACEMENT,
            testFunc
        ));
    }
    
    // Sleep to allow for rate limit reset
    std::this_thread::sleep_for(std::chrono::milliseconds(600));
    
    // Update rate limit to indicate reset
    controller.updateRateLimitStatus(
        ExchangeRequestType::ORDER_PLACEMENT,
        10, // Requests available again
        std::chrono::milliseconds(1000) // Next reset in 1000ms
    );
    
    // Wait for all futures to complete
    for (auto& future : futures) {
        future.wait();
    }
    
    // We should have executed all requests eventually
    EXPECT_EQ(executionCount, requestCount + 5);
    
    // The first 10 requests should have executed immediately
    EXPECT_LE(futures[0].get(), 10);
    EXPECT_LE(futures[9].get(), 10);
    
    // Later requests should have higher numbers
    EXPECT_GE(futures[requestCount + 4].get(), 15);
}

TEST_F(RateLimitControllerTest, DifferentRequestTypes) {
    // Create a counter to track executions
    std::atomic<int> executionCount{0};
    
    // Function to execute
    auto testFunc = [&executionCount]() {
        executionCount++;
        return executionCount.load();
    };
    
    // Execute requests of different types
    auto future1 = controller.executeRequest(
        ExchangeRequestType::ORDER_PLACEMENT,
        testFunc
    );
    
    auto future2 = controller.executeRequest(
        ExchangeRequestType::ORDER_CANCELLATION,
        testFunc
    );
    
    auto future3 = controller.executeRequest(
        ExchangeRequestType::ACCOUNT_BALANCE,
        testFunc
    );
    
    // Rate limit only ORDER_PLACEMENT
    controller.updateRateLimitStatus(
        ExchangeRequestType::ORDER_PLACEMENT,
        0, // No requests remaining
        std::chrono::milliseconds(500) // Reset in 500ms
    );
    
    // Another ORDER_PLACEMENT request should be limited
    auto future4 = controller.executeRequest(
        ExchangeRequestType::ORDER_PLACEMENT,
        testFunc
    );
    
    // But ORDER_CANCELLATION should still work
    auto future5 = controller.executeRequest(
        ExchangeRequestType::ORDER_CANCELLATION,
        testFunc
    );
    
    // Wait for a bit to let some requests execute
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Reset the rate limit
    controller.updateRateLimitStatus(
        ExchangeRequestType::ORDER_PLACEMENT,
        10, // Requests available again
        std::chrono::milliseconds(1000) // Next reset in 1000ms
    );
    
    // Wait for all futures to complete
    future1.wait();
    future2.wait();
    future3.wait();
    future4.wait();
    future5.wait();
    
    // We should have executed all requests eventually
    EXPECT_EQ(executionCount, 5);
    
    // The first 3 and 5th requests should have executed before the 4th
    EXPECT_LT(future1.get(), future4.get());
    EXPECT_LT(future2.get(), future4.get());
    EXPECT_LT(future3.get(), future4.get());
    EXPECT_LT(future5.get(), future4.get());
}

} // namespace test
} // namespace execution
} // namespace trading

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}