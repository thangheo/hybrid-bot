// test_request_prioritizer.cpp

#include <gtest/gtest.h>
#include "execution/request_prioritizer.hpp"
#include <thread>
#include <atomic>
#include <future>
#include <vector>
#include <algorithm>

namespace trading {
namespace execution {
namespace test {

class RequestPrioritizerTest : public ::testing::Test {
protected:
    RequestPrioritizer prioritizer;
};

TEST_F(RequestPrioritizerTest, InitiallyEmpty) {
    EXPECT_FALSE(prioritizer.hasPendingRequests());
    
    auto queueSizes = prioritizer.getQueueSizes();
    EXPECT_EQ(queueSizes.size(), 0);
}

TEST_F(RequestPrioritizerTest, ProcessesSingleRequest) {
    // Use a promise to verify the request was processed
    std::promise<bool> promise;
    auto future = promise.get_future();
    
    // Submit a request
    prioritizer.submitRequest(
        RequestPriority::Normal,
        ExchangeRequestType::PlaceOrder,
        [&promise]() {
            promise.set_value(true);
            return 42;
        }
    );
    
    EXPECT_TRUE(prioritizer.hasPendingRequests());
    
    // Wait for the request to be processed
    auto status = future.wait_for(std::chrono::seconds(1));
    EXPECT_EQ(status, std::future_status::ready);
    EXPECT_TRUE(future.get());
    
    // The queue should be empty now
    EXPECT_FALSE(prioritizer.hasPendingRequests());
}

TEST_F(RequestPrioritizerTest, ProcessesMultipleRequests) {
    const int requestCount = 100;
    std::vector<std::future<int>> futures;
    std::atomic<int> counter{0};
    
    // Submit multiple requests
    for (int i = 0; i < requestCount; ++i) {
        futures.push_back(prioritizer.submitRequest(
            RequestPriority::Normal,
            ExchangeRequestType::PlaceOrder,
            [&counter, i]() {
                counter++;
                return i;
            }
        ));
    }
    
    // Wait for all requests to be processed
    for (auto& future : futures) {
        future.wait();
    }
    
    // All requests should be processed
    EXPECT_EQ(counter, requestCount);
    EXPECT_FALSE(prioritizer.hasPendingRequests());
    
    // Verify that each request returned its corresponding value
    std::vector<int> results;
    for (int i = 0; i < requestCount; ++i) {
        results.push_back(futures[i].get());
    }
    
    // Results should be in order since all requests had the same priority
    for (int i = 0; i < requestCount; ++i) {
        EXPECT_EQ(results[i], i);
    }
}

TEST_F(RequestPrioritizerTest, RespectsPriorities) {
    std::vector<std::future<int>> futures;
    std::vector<int> executionOrder;
    std::mutex mutex;
    
    // Helper function to record execution order
    auto recordExecution = [&executionOrder, &mutex](int id) {
        std::lock_guard<std::mutex> lock(mutex);
        executionOrder.push_back(id);
        return id;
    };
    
    // Submit requests with different priorities
    futures.push_back(prioritizer.submitRequest(
        RequestPriority::Normal,
        ExchangeRequestType::PlaceOrder,
        recordExecution, 1
    ));
    
    futures.push_back(prioritizer.submitRequest(
        RequestPriority::Low,
        ExchangeRequestType::GetOrderStatus,
        recordExecution, 2
    ));
    
    futures.push_back(prioritizer.submitRequest(
        RequestPriority::High,
        ExchangeRequestType::CancelOrder,
        recordExecution, 3
    ));
    
    futures.push_back(prioritizer.submitRequest(
        RequestPriority::Critical,
        ExchangeRequestType::CancelOrder,
        recordExecution, 4
    ));
    
    futures.push_back(prioritizer.submitRequest(
        RequestPriority::Normal,
        ExchangeRequestType::GetAccountBalance,
        recordExecution, 5
    ));
    
    // Wait for all requests to be processed
    for (auto& future : futures) {
        future.wait();
    }
    
    // Verify that the requests were executed in priority order
    // With the highest priority first
    ASSERT_EQ(executionOrder.size(), 5);
    
    // Critical should be first
    EXPECT_EQ(executionOrder[0], 4);
    
    // High priority should be next
    EXPECT_EQ(executionOrder[1], 3);
    
    // Normal priorities should be next, in the order they were submitted
    // (Note: this assumes the implementation preserves order within same priority)
    EXPECT_EQ(executionOrder[2], 1);
    EXPECT_EQ(executionOrder[3], 5);
    
    // Low priority should be last
    EXPECT_EQ(executionOrder[4], 2);
}

TEST_F(RequestPrioritizerTest, StopAndStart) {
    std::atomic<int> counter{0};
    std::vector<std::future<int>> futures;
    
    // Submit some initial requests
    for (int i = 0; i < 10; ++i) {
        futures.push_back(prioritizer.submitRequest(
            RequestPriority::Normal,
            ExchangeRequestType::PlaceOrder,
            [&counter, i]() {
                counter++;
                return i;
            }
        ));
    }
    
    // Wait for initial requests to complete
    for (auto& future : futures) {
        future.wait();
    }
    
    // Stop the prioritizer
    prioritizer.stop();
    
    // Submit more requests while stopped
    std::vector<std::future<int>> futuresAfterStop;
    for (int i = 0; i < 5; ++i) {
        futuresAfterStop.push_back(prioritizer.submitRequest(
            RequestPriority::Normal,
            ExchangeRequestType::PlaceOrder,
            [&counter, i]() {
                counter++;
                return i + 100;
            }
        ));
    }
    
    // These requests should be queued but not processed
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(counter, 10);
    
    // Start the prioritizer again
    prioritizer.start();
    
    // Wait for remaining requests to complete
    for (auto& future : futuresAfterStop) {
        future.wait();
    }
    
    // All requests should now be processed
    EXPECT_EQ(counter, 15);
    EXPECT_FALSE(prioritizer.hasPendingRequests());
}

TEST_F(RequestPrioritizerTest, HandlesExceptions) {
    std::atomic<int> counter{0};
    
    // Submit a request that throws an exception
    auto future1 = prioritizer.submitRequest(
        RequestPriority::Normal,
        ExchangeRequestType::PlaceOrder,
        []() -> int {
            throw std::runtime_error("Test exception");
        }
    );
    
    // Submit another normal request
    auto future2 = prioritizer.submitRequest(
        RequestPriority::Normal,
        ExchangeRequestType::PlaceOrder,
        [&counter]() {
            counter++;
            return 42;
        }
    );
    
    // The exception should be propagated to the future
    EXPECT_THROW(future1.get(), std::runtime_error);
    
    // But the second request should still be processed
    future2.wait();
    EXPECT_EQ(counter, 1);
    EXPECT_EQ(future2.get(), 42);
}

TEST_F(RequestPrioritizerTest, QueueSizeTracking) {
    // Submit requests with different priorities
    for (int i = 0; i < 5; ++i) {
        prioritizer.submitRequest(
            RequestPriority::Critical,
            ExchangeRequestType::CancelOrder,
            []() {}
        );
    }
    
    for (int i = 0; i < 3; ++i) {
        prioritizer.submitRequest(
            RequestPriority::High,
            ExchangeRequestType::CancelOrder,
            []() {}
        );
    }
    
    for (int i = 0; i < 10; ++i) {
        prioritizer.submitRequest(
            RequestPriority::Normal,
            ExchangeRequestType::PlaceOrder,
            []() {}
        );
    }
    
    for (int i = 0; i < 2; ++i) {
        prioritizer.submitRequest(
            RequestPriority::Low,
            ExchangeRequestType::GetOrderStatus,
            []() {}
        );
    }
    
    // Stop processing to examine queue sizes
    prioritizer.stop();
    
    // Check queue sizes
    auto queueSizes = prioritizer.getQueueSizes();
    EXPECT_EQ(queueSizes[RequestPriority::Critical], 5);
    EXPECT_EQ(queueSizes[RequestPriority::High], 3);
    EXPECT_EQ(queueSizes[RequestPriority::Normal], 10);
    EXPECT_EQ(queueSizes[RequestPriority::Low], 2);
    
    // Resume processing
    prioritizer.start();
    
    // Wait for all requests to be processed
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Queue should be empty now
    queueSizes = prioritizer.getQueueSizes();
    EXPECT_EQ(queueSizes.size(), 0);
}

} // namespace test
} // namespace execution
} // namespace trading

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}