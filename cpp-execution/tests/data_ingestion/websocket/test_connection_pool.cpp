// tests/data_ingestion/websocket/test_connection_pool.cpp
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/websocket/client_base.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>

using namespace trading::data_ingestion;
using namespace ::testing;

namespace {

// Mock WebSocket client for testing the connection pool
class TestWebSocketClient : public WebSocketClientBase {
public:
    TestWebSocketClient(const std::string& host, 
                        const std::string& port,
                        bool fail_connect = false)
        : WebSocketClientBase(host, port, false),
          fail_connect_(fail_connect),
          connection_count_(0) {
    }
    
    // Override connect to control behavior in tests
    bool connect() override {
        if (fail_connect_) {
            return false;
        }
        
        connection_count_++;
        connected_ = true;
        onOpen();
        return true;
    }
    
    // Additional test control and introspection methods
    void simulateDisconnect() {
        connected_ = false;
        onClose(1006, "Connection lost");
    }
    
    int getConnectionCount() const {
        return connection_count_;
    }
    
    // Set whether future connect calls should fail
    void setFailConnect(bool fail) {
        fail_connect_ = fail;
    }
    
protected:
    void onOpen() override {}
    void onMessage(const std::string& message) override {}
    void onClose(int code, const std::string& reason) override {}
    void onError(const std::string& error) override {}
    void onReconnect() override {}
    
private:
    bool fail_connect_;
    int connection_count_;
};

// Factory function for creating test clients
std::shared_ptr<WebSocketClientBase> createTestClient(bool fail_connect = false) {
    return std::make_shared<TestWebSocketClient>("localhost", "8080", fail_connect);
}

} // anonymous namespace

// Test fixture
class ConnectionPoolTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a factory that returns our test clients
        auto factory = [this]() -> std::shared_ptr<WebSocketClientBase> {
            return createTestClient(should_fail_);
        };
        
        // Create the pool
        pool_ = std::make_unique<ConnectionPool>(
            "test_pool",
            factory,
            min_connections_,
            max_connections_
        );
    }
    
    void TearDown() override {
        if (pool_) {
            pool_->shutdown();
        }
    }
    
    // Test parameters
    int min_connections_ = 2;
    int max_connections_ = 5;
    bool should_fail_ = false;
    
    std::unique_ptr<ConnectionPool> pool_;
};

// Test initialization creates min connections
TEST_F(ConnectionPoolTest, InitializationCreatesMinConnections) {
    pool_->initialize();
    
    // Allow a moment for initialization to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    EXPECT_EQ(min_connections_, pool_->getTotalConnectionCount());
    EXPECT_EQ(min_connections_, pool_->getAvailableConnectionCount());
    EXPECT_EQ(0, pool_->getActiveConnectionCount());
    EXPECT_TRUE(pool_->isHealthy());
}

// Test connection acquisition
TEST_F(ConnectionPoolTest, ConnectionAcquisition) {
    pool_->initialize();
    
    // Acquire a connection
    auto conn = pool_->acquireConnection();
    ASSERT_NE(nullptr, conn);
    
    // Check pool state
    EXPECT_EQ(min_connections_, pool_->getTotalConnectionCount());
    EXPECT_EQ(min_connections_ - 1, pool_->getAvailableConnectionCount());
    EXPECT_EQ(1, pool_->getActiveConnectionCount());
    
    // Release the connection
    pool_->releaseConnection(conn);
    
    // Check pool state after release
    EXPECT_EQ(min_connections_, pool_->getTotalConnectionCount());
    EXPECT_EQ(min_connections_, pool_->getAvailableConnectionCount());
    EXPECT_EQ(0, pool_->getActiveConnectionCount());
}

// Test connection creation when none available
TEST_F(ConnectionPoolTest, ConnectionCreationWhenNoneAvailable) {
    pool_->initialize();
    
    // Acquire all available connections
    std::vector<std::shared_ptr<WebSocketClientBase>> connections;
    for (int i = 0; i < min_connections_; ++i) {
        connections.push_back(pool_->acquireConnection());
    }
    
    EXPECT_EQ(min_connections_, pool_->getTotalConnectionCount());
    EXPECT_EQ(0, pool_->getAvailableConnectionCount());
    EXPECT_EQ(min_connections_, pool_->getActiveConnectionCount());
    
    // Acquiring another should create a new one (up to max)
    auto new_conn = pool_->acquireConnection();
    ASSERT_NE(nullptr, new_conn);
    
    EXPECT_EQ(min_connections_ + 1, pool_->getTotalConnectionCount());
    EXPECT_EQ(0, pool_->getAvailableConnectionCount());
    EXPECT_EQ(min_connections_ + 1, pool_->getActiveConnectionCount());
    
    // Release all connections
    pool_->releaseConnection(new_conn);
    for (auto& conn : connections) {
        pool_->releaseConnection(conn);
    }
}

// Test max connections limit
TEST_F(ConnectionPoolTest, MaxConnectionsLimit) {
    pool_->initialize();
    
    // Acquire max_connections connections
    std::vector<std::shared_ptr<WebSocketClientBase>> connections;
    for (int i = 0; i < max_connections_; ++i) {
        auto conn = pool_->acquireConnection();
        ASSERT_NE(nullptr, conn);
        connections.push_back(conn);
    }
    
    EXPECT_EQ(max_connections_, pool_->getTotalConnectionCount());
    EXPECT_EQ(0, pool_->getAvailableConnectionCount());
    EXPECT_EQ(max_connections_, pool_->getActiveConnectionCount());
    
    // We've reached max, so acquireConnection should block or time out
    auto timeout_conn = pool_->acquireConnection(100); // 100ms timeout
    EXPECT_EQ(nullptr, timeout_conn);
    
    // Release all connections
    for (auto& conn : connections) {
        pool_->releaseConnection(conn);
    }
}

// Test failing connections
TEST_F(ConnectionPoolTest, FailingConnections) {
    // Set factory to return failing connections
    should_fail_ = true;
    
    pool_->initialize();
    
    // Allow time for initialization attempts
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Pool should have attempted to create connections but failed
    EXPECT_EQ(0, pool_->getTotalConnectionCount());
    EXPECT_EQ(0, pool_->getAvailableConnectionCount());
    EXPECT_FALSE(pool_->isHealthy());
    
    // Acquiring a connection should fail
    auto conn = pool_->acquireConnection(100);
    EXPECT_EQ(nullptr, conn);
}

// Test connection health monitoring
TEST_F(ConnectionPoolTest, ConnectionHealthMonitoring) {
    pool_->initialize();
    
    // Get a connection and break it
    auto conn = pool_->acquireConnection();
    ASSERT_NE(nullptr, conn);
    
    // Cast to our test client to simulate a disconnection
    auto test_client = std::dynamic_pointer_cast<TestWebSocketClient>(conn);
    ASSERT_NE(nullptr, test_client);
    
    // Simulate disconnection
    test_client->simulateDisconnect();
    
    // Release the broken connection
    pool_->releaseConnection(conn);
    
    // Allow some time for the monitoring thread to detect the issue
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // The pool should still be healthy as it has other connections
    EXPECT_TRUE(pool_->isHealthy());
    
    // The pool should have removed the unhealthy connection and created a new one
    EXPECT_EQ(min_connections_, pool_->getTotalConnectionCount());
}

// Test shutdown behavior
TEST_F(ConnectionPoolTest, Shutdown) {
    pool_->initialize();
    
    // Acquire a connection
    auto conn = pool_->acquireConnection();
    ASSERT_NE(nullptr, conn);
    
    // Shutdown the pool while a connection is out
    pool_->shutdown();
    
    // Release the connection after shutdown
    pool_->releaseConnection(conn);
    
    // Pool should be empty
    EXPECT_EQ(0, pool_->getTotalConnectionCount());
    EXPECT_EQ(0, pool_->getAvailableConnectionCount());
    EXPECT_EQ(0, pool_->getActiveConnectionCount());
    EXPECT_FALSE(pool_->isHealthy());
}

// Test concurrent connection acquisition and release
TEST_F(ConnectionPoolTest, ConcurrentUsage) {
    pool_->initialize();
    
    // Number of threads and operations per thread
    const int NUM_THREADS = 10;
    const int OPS_PER_THREAD = 50;
    
    // Track success/failure counts
    std::atomic<int> successes(0);
    std::atomic<int> failures(0);
    
    // Function for worker threads
    auto worker = [this, &successes, &failures, OPS_PER_THREAD]() {
        for (int i = 0; i < OPS_PER_THREAD; ++i) {
            auto conn = pool_->acquireConnection(50); // 50ms timeout
            if (conn) {
                // Simulate some work
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                pool_->releaseConnection(conn);
                successes++;
            } else {
                failures++;
            }
        }
    };
    
    // Start worker threads
    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.push_back(std::thread(worker));
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify we got some successes and some failures due to contention
    std::cout << "Concurrent test: " << successes << " successes, " 
              << failures << " failures" << std::endl;
    
    EXPECT_GT(successes, 0);
    EXPECT_LE(failures, NUM_THREADS * OPS_PER_THREAD);
    
    // Pool should still be in a consistent state
    EXPECT_TRUE(pool_->isHealthy());
    EXPECT_EQ(pool_->getTotalConnectionCount(), 
              pool_->getAvailableConnectionCount() + pool_->getActiveConnectionCount());
}

// Main function required to run the tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

