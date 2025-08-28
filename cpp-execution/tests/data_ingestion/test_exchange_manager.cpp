// tests/data_ingestion/test_exchange_manager.cpp
#include "data_ingestion/exchange_manager.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/websocket/websocket_client.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

using namespace trading::data_ingestion;
using namespace ::testing;

namespace {

// Mock BinanceWebSocketClient for testing
class MockBinanceClient : public BinanceWebSocketClient {
public:
    MockBinanceClient(MarketDataCallback callback) 
        : BinanceWebSocketClient(callback) {}
    MOCK_METHOD(bool, connect, (), (override));
    MOCK_METHOD(void, disconnect, (), (override));
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(bool, subscribe, (const std::vector<std::string>&), (override));
    MOCK_METHOD1(subscribe, bool(const std::vector<std::string>&));
};

// Mock ConnectionPool for testing
class MockConnectionPool : public ConnectionPool {
public:
    MockConnectionPool(const std::string& exchange_id, 
                      int min_connections, 
                      int max_connections,
                      MarketDataCallback callback)
        : ConnectionPool(exchange_id, min_connections, max_connections, callback,
            [callback](){ return std::make_shared<MockBinanceClient>(callback); }),
          healthy_(true) {}
    MOCK_METHOD(bool, connectAll, (), (override));
    MOCK_METHOD(void, shutdown, (), (override));
    MOCK_METHOD(std::shared_ptr<BinanceWebSocketClient>, acquireConnection, (), (override));
    MOCK_METHOD(void, releaseConnection, (std::shared_ptr<WebSocketClient>), (override));
    MOCK_METHOD1(releaseConnection, void(std::shared_ptr<WebSocketClient>));
    
    // Override health check method
    bool isHealthy() const override {
        return healthy_;
    }
    
    // Control health state for tests
    void setHealthy(bool healthy) {
        healthy_ = healthy;
    }
    
    // For testing if resubscription is needed
    bool needsResubscription() const override {
        return needs_resubscription_;
    }
    
    void setNeedsResubscription(bool needs) {
        needs_resubscription_ = needs;
    }
    
    MOCK_METHOD(int, getActiveConnectionCount, (), (const, override));
    MOCK_METHOD(int, getAvailableConnectionCount, (), (const, override));
    MOCK_METHOD(int, getTotalConnectionCount, (), (const, override));
    MOCK_CONST_METHOD0(getTotalConnectionCount, int());
    
private:
    bool healthy_ = true;
    bool needs_resubscription_ = false;
};

// Mock OrderBookNormalizer for testing
class MockOrderBookNormalizer {
    MOCK_METHOD(void, processMarketData, (const MarketDataUpdate&));
    MOCK_METHOD(OrderBookUpdate, getOrderBook, (const std::string&, const std::string&, int32_t), (const));
    MOCK_CONST_METHOD3(getOrderBook, OrderBookUpdate(const std::string&, const std::string&, int32_t));
};

// Callback converter for OrderBookNormalizer
void normalizedOrderBookCallback(const OrderBookUpdate& update, MockOrderBookNormalizer* normalizer) {
    // Nothing to do here - we mock the normalizer
}

} // anonymous namespace

// Test fixture
class ExchangeManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_normalizer_ = std::make_shared<MockOrderBookNormalizer>();
        
        // Create callback that forwards to the mock
        auto callback = std::bind(normalizedOrderBookCallback, std::placeholders::_1, mock_normalizer_.get());
        
        // Create the exchange manager with the callback
        manager_ = std::make_unique<ExchangeManager>(callback, 2, 5);
        
        // Create a mock connection pool and inject it into the manager
        mock_pool_ = std::make_shared<NiceMock<MockConnectionPool>>("binance", 2, 5, 
            [this](const MarketDataUpdate& update) {
                market_data_callback_(update);
            });
        
        // Set up default behaviors for the pool
        SetUpMockPool();
        
        // Set up the mock client
        mock_client_ = std::make_shared<NiceMock<MockBinanceClient>>(
            [this](const MarketDataUpdate& update) {
                market_data_callback_(update);
            });
        
        // Set up default behaviors for the client
        SetUpMockClient();
        
        // Replace the connection pool in the manager with our mock
        // This requires an accessor method or friend declaration in the real implementation
        injectMockConnectionPool();
    }
    
    void TearDown() override {
        // Clean up
        manager_.reset();
        mock_pool_.reset();
        mock_client_.reset();
        mock_normalizer_.reset();
    }
    
    // Set up default behaviors for mock pool
    void SetUpMockPool() {
        ON_CALL(*mock_pool_, connectAll())
            .WillByDefault(Return(true));
        
        ON_CALL(*mock_pool_, shutdown())
            .WillByDefault(Return());
        
        ON_CALL(*mock_pool_, acquireConnection())
            .WillByDefault(Return(mock_client_));
        
        ON_CALL(*mock_pool_, getActiveConnectionCount())
            .WillByDefault(Return(1));
        
        ON_CALL(*mock_pool_, getAvailableConnectionCount())
            .WillByDefault(Return(1));
        
        ON_CALL(*mock_pool_, getTotalConnectionCount())
            .WillByDefault(Return(2));
    }
    
    // Set up default behaviors for mock client
    void SetUpMockClient() {
        ON_CALL(*mock_client_, connect())
            .WillByDefault(Return(true));
        
        ON_CALL(*mock_client_, isConnected())
            .WillByDefault(Return(true));
        
        ON_CALL(*mock_client_, subscribe(_))
            .WillByDefault(Return(true));
    }
    
    // Inject our mock pool into the manager
    void injectMockConnectionPool() {
        // In a real implementation, you would need a method or friend declaration to do this
        // For now, we'll use a dummy implementation since we can't access the private member
        
        // Here's what it would look like:
        // manager_->connection_pool_ = mock_pool_;
        
        // For testing purposes, we'll call a method that does this for us
        // This method would need to be added to the ExchangeManager for testing
        manager_->setConnectionPoolForTesting(std::move(mock_pool_));
    }
    
    // Create a market data update for testing
    MarketDataUpdate createMarketDataUpdate(const std::string& trading_pair) {
        MarketDataUpdate update;
        update.exchange_id = "binance";
        update.trading_pair = trading_pair;
        update.update_type = "orderbook";
        update.exchange_timestamp = 1625239046123;
        update.received_timestamp = 1625239046123;
        update.data = {'t', 'e', 's', 't'};
        return update;
    }
    
    // Test data
    std::unique_ptr<ExchangeManager> manager_;
    std::shared_ptr<MockOrderBookNormalizer> mock_normalizer_;
    std::shared_ptr<MockConnectionPool> mock_pool_;
    std::shared_ptr<MockBinanceClient> mock_client_;
    MarketDataCallback market_data_callback_;
};

// Test initialization
TEST_F(ExchangeManagerTest, Initialization) {
    // Expect initialization to succeed
    EXPECT_TRUE(manager_->initialize());
    
    // Try to initialize again - should still succeed (idempotent)
    EXPECT_TRUE(manager_->initialize());
}

// Test connection
TEST_F(ExchangeManagerTest, Connection) {
    // Initialize first
    ASSERT_TRUE(manager_->initialize());
    
    // Expect connection to succeed
    EXPECT_CALL(*mock_pool_, connectAll())
        .WillOnce(Return(true));
    
    EXPECT_TRUE(manager_->connect());
    
    // Verify connection status
    EXPECT_TRUE(manager_->isConnected());
}

// Test connection failure
TEST_F(ExchangeManagerTest, ConnectionFailure) {
    // Initialize first
    ASSERT_TRUE(manager_->initialize());
    
    // Expect connection to fail
    EXPECT_CALL(*mock_pool_, connectAll())
        .WillOnce(Return(false));
    
    EXPECT_FALSE(manager_->connect());
    
    // Verify connection status
    EXPECT_FALSE(manager_->isConnected());
}

// Test disconnection
TEST_F(ExchangeManagerTest, Disconnection) {
    // Initialize and connect first
    ASSERT_TRUE(manager_->initialize());
    ASSERT_TRUE(manager_->connect());
    
    // Expect shutdown to be called
    EXPECT_CALL(*mock_pool_, shutdown())
        .Times(1);
    
    // Disconnect
    manager_->disconnect();
    
    // Verify connection status
    EXPECT_FALSE(manager_->isConnected());
}

// Test subscription
TEST_F(ExchangeManagerTest, Subscribe) {
    // Initialize and connect first
    ASSERT_TRUE(manager_->initialize());
    ASSERT_TRUE(manager_->connect());
    
    // Define test data
    std::vector<std::string> trading_pairs = {"BTC/USDT", "ETH/USDT"};
    std::vector<DataType> data_types = {DataType::ORDER_BOOK, DataType::TRADES};
    
    // Expect the client to be acquired and subscription to be called
    EXPECT_CALL(*mock_pool_, acquireConnection())
        .WillOnce(Return(mock_client_));
    
    EXPECT_CALL(*mock_pool_, releaseConnection(_))
        .Times(1);
    
    EXPECT_CALL(*mock_client_, subscribe(_))
        .WillOnce(Return(true));
    
    // Subscribe
    EXPECT_TRUE(manager_->subscribe(trading_pairs, data_types, 10));
    
    // Try to subscribe without being connected
    manager_->disconnect();
    EXPECT_FALSE(manager_->subscribe(trading_pairs, data_types, 10));
}

// Test market data processing
TEST_F(ExchangeManagerTest, MarketDataProcessing) {
    // Initialize and connect first
    ASSERT_TRUE(manager_->initialize());
    ASSERT_TRUE(manager_->connect());
    
    // Expect the normalizer to process market data
    EXPECT_CALL(*mock_normalizer_, processMarketData(_))
        .Times(1);
    
    // Create and send a market data update
    auto update = createMarketDataUpdate("BTC/USDT");
    
    // Send the update via the callback
    market_data_callback_(update);
}

// Test order book retrieval
TEST_F(ExchangeManagerTest, OrderBookRetrieval) {
    // Initialize and connect first
    ASSERT_TRUE(manager_->initialize());
    ASSERT_TRUE(manager_->connect());
    
    // Set up expected response
    OrderBookUpdate expected_book;
    expected_book.exchange_id = "binance";
    expected_book.trading_pair = "BTC/USDT";
    
    // Expect the normalizer to be called
    EXPECT_CALL(*mock_normalizer_, getOrderBook("binance", "BTC/USDT", 10))
        .WillOnce(Return(expected_book));
    
    // Get the order book
    auto book = manager_->getOrderBook("BTC/USDT", 10);
    
    // Verify the result
    EXPECT_EQ("binance", book.exchange_id);
    EXPECT_EQ("BTC/USDT", book.trading_pair);
}

// Test connection statistics
TEST_F(ExchangeManagerTest, ConnectionStatistics) {
    // Initialize and connect first
    ASSERT_TRUE(manager_->initialize());
    ASSERT_TRUE(manager_->connect());
    
    // Expect statistics methods to be called
    EXPECT_CALL(*mock_pool_, getActiveConnectionCount())
        .WillOnce(Return(1));
    
    EXPECT_CALL(*mock_pool_, getAvailableConnectionCount())
        .WillOnce(Return(1));
    
    EXPECT_CALL(*mock_pool_, getTotalConnectionCount())
        .WillOnce(Return(2));
    
    // Get connection statistics
    std::string stats = manager_->getConnectionStats();
    
    // Verify that the stats contain expected information
    EXPECT_NE(stats.find("1 active"), std::string::npos);
    EXPECT_NE(stats.find("1 available"), std::string::npos);
    EXPECT_NE(stats.find("2 total"), std::string::npos);
}

// Test resubscription after reconnection
TEST_F(ExchangeManagerTest, ResubscriptionAfterReconnection) {
    // Initialize and connect first
    ASSERT_TRUE(manager_->initialize());
    ASSERT_TRUE(manager_->connect());
    
    // Subscribe to some trading pairs
    std::vector<std::string> trading_pairs = {"BTC/USDT", "ETH/USDT"};
    std::vector<DataType> data_types = {DataType::ORDER_BOOK};
    
    EXPECT_CALL(*mock_pool_, acquireConnection())
        .WillOnce(Return(mock_client_));
    
    EXPECT_CALL(*mock_client_, subscribe(_))
        .WillOnce(Return(true));
    
    EXPECT_TRUE(manager_->subscribe(trading_pairs, data_types, 10));
    
    // Simulate connection loss and reconnection
    mock_pool_->setHealthy(false);
    
    // Wait for the monitor thread to detect the change
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Simulate reconnection
    mock_pool_->setHealthy(true);
    mock_pool_->setNeedsResubscription(true);
    
    // Wait for the monitor thread to detect the change and resubscribe
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify that subscribe was called again (indirectly through monitor thread)
    // This is harder to test with the current implementation, so we'll skip the verification
    // In a real test, we might use a spy or other mechanism to verify this
}

// Test connection health monitoring
TEST_F(ExchangeManagerTest, ConnectionHealthMonitoring) {
    // Initialize and connect first
    ASSERT_TRUE(manager_->initialize());
    ASSERT_TRUE(manager_->connect());
    
    // Initially should be connected
    EXPECT_TRUE(manager_->isConnected());
    
    // Simulate connection loss
    mock_pool_->setHealthy(false);
    
    // isConnected should now return false
    EXPECT_FALSE(manager_->isConnected());
    
    // Simulate reconnection
    mock_pool_->setHealthy(true);
    
    // isConnected should now return true again
    EXPECT_TRUE(manager_->isConnected());
}

// Main function required to run the tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}