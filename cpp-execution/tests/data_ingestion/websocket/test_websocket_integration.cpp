// tests/data_ingestion/websocket/test_websocket_integration.cpp
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "mock_exchange_server.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>

using namespace trading::data_ingestion;
using namespace trading::testing;
using namespace ::testing;

namespace {

// Helper class to track received market data
class MarketDataTracker {
public:
    MarketDataTracker() 
        : order_book_count_(0),
          trade_count_(0),
          ticker_count_(0) {
    }
    
    void onMarketData(const MarketDataUpdate& update) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Record the update
        last_update_ = update;
        
        // Increment the appropriate counter
        if (update.update_type == "orderbook") {
            order_book_count_++;
            last_order_book_update_ = update;
            order_book_cv_.notify_all();
        }
        else if (update.update_type == "trade") {
            trade_count_++;
            last_trade_update_ = update;
            trade_cv_.notify_all();
        }
        else if (update.update_type == "ticker") {
            ticker_count_++;
            last_ticker_update_ = update;
            ticker_cv_.notify_all();
        }
        
        // Notify all waiting threads
        update_cv_.notify_all();
    }
    
    // Wait for any update
    bool waitForUpdate(int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);
        return update_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this] { return getUpdateCount() > 0; });
    }
    
    // Wait for a specific number of total updates
    bool waitForUpdateCount(int count, int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);
        return update_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this, count] { return getUpdateCount() >= count; });
    }
    
    // Wait for a specific number of order book updates
    bool waitForOrderBookUpdates(int count, int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);
        return order_book_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this, count] { return order_book_count_ >= count; });
    }
    
    // Wait for a specific number of trade updates
    bool waitForTradeUpdates(int count, int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);
        return trade_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this, count] { return trade_count_ >= count; });
    }
    
    // Wait for a specific number of ticker updates
    bool waitForTickerUpdates(int count, int timeout_ms = 5000) {
        std::unique_lock<std::mutex> lock(mutex_);
        return ticker_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
            [this, count] { return ticker_count_ >= count; });
    }
    
    // Access methods
    int getOrderBookCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return order_book_count_;
    }
    
    int getTradeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return trade_count_;
    }
    
    int getTickerCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return ticker_count_;
    }
    
    int getUpdateCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return order_book_count_ + trade_count_ + ticker_count_;
    }
    
    MarketDataUpdate getLastUpdate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_update_;
    }
    
    MarketDataUpdate getLastOrderBookUpdate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_order_book_update_;
    }
    
    MarketDataUpdate getLastTradeUpdate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_trade_update_;
    }
    
    MarketDataUpdate getLastTickerUpdate() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return last_ticker_update_;
    }
    
    // Reset all counters
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        order_book_count_ = 0;
        trade_count_ = 0;
        ticker_count_ = 0;
    }
    
private:
    // Counters for different types of updates
    int order_book_count_;
    int trade_count_;
    int ticker_count_;
    
    // Last received updates
    MarketDataUpdate last_update_;
    MarketDataUpdate last_order_book_update_;
    MarketDataUpdate last_trade_update_;
    MarketDataUpdate last_ticker_update_;
    
    // Synchronization
    mutable std::mutex mutex_;
    std::condition_variable update_cv_;
    std::condition_variable order_book_cv_;
    std::condition_variable trade_cv_;
    std::condition_variable ticker_cv_;
};

// Market data callback function that forwards to a tracker
void marketDataCallback(const MarketDataUpdate& update, MarketDataTracker* tracker) {
    tracker->onMarketData(update);
}

} // anonymous namespace

// Test fixture for WebSocket integration tests
class WebSocketIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Start the mock exchange server
        mock_server_.start();
        
        // Wait for the server to start
        ASSERT_TRUE(waitForServerStart());
        
        // Create a market data tracker
        tracker_ = std::make_shared<MarketDataTracker>();
        
        // Create a callback function that forwards to the tracker
        auto callback = std::bind(marketDataCallback, std::placeholders::_1, tracker_.get());
        
        // Create the WebSocket client
        std::string server_url = "localhost";
        std::string server_port = std::to_string(SERVER_PORT);
        client_ = std::make_shared<BinanceWebSocketClient>(server_url, server_port, callback);
    }
    
    void TearDown() override {
        // Disconnect the client
        if (client_ && client_->isConnected()) {
            client_->disconnect();
        }
        
        // Stop the mock server
        mock_server_.stop();
        
        // Clean up
        client_.reset();
        tracker_.reset();
    }
    
    // Wait for the server to start
    bool waitForServerStart(int timeout_ms = 5000) {
        auto start_time = std::chrono::steady_clock::now();
        
        while (std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_time).count() < timeout_ms) {
            if (mock_server_.isRunning()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        return false;
    }
    
    // Constants
    static constexpr uint16_t SERVER_PORT = 9001;
    
    // Test components
    MockExchangeServer mock_server_{SERVER_PORT};
    std::shared_ptr<BinanceWebSocketClient> client_;
    std::shared_ptr<MarketDataTracker> tracker_;
};

// Test basic connection to the server
TEST_F(WebSocketIntegrationTest, Connection) {
    ASSERT_TRUE(client_->connect());
    EXPECT_TRUE(client_->isConnected());
    
    // There should be one connection to the server
    EXPECT_EQ(1, mock_server_.getConnectionCount());
}

// Test subscribing to order book updates
TEST_F(WebSocketIntegrationTest, OrderBookSubscription) {
    ASSERT_TRUE(client_->connect());
    
    // Subscribe to BTC/USDT order book
    std::vector<std::string> streams = {"btcusdt@depth@100ms"};
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Wait for some order book updates
    ASSERT_TRUE(tracker_->waitForOrderBookUpdates(3, 10000));
    
    // Verify the updates
    EXPECT_GT(tracker_->getOrderBookCount(), 0);
    
    // Check the last order book update
    auto last_update = tracker_->getLastOrderBookUpdate();
    EXPECT_EQ("binance", last_update.exchange_id);
    EXPECT_EQ("BTCUSDT", last_update.trading_pair);
    EXPECT_EQ("orderbook", last_update.update_type);
    EXPECT_GT(last_update.data.size(), 0);
}

// Test subscribing to trade updates
TEST_F(WebSocketIntegrationTest, TradeSubscription) {
    ASSERT_TRUE(client_->connect());
    
    // Subscribe to ETH/USDT trades
    std::vector<std::string> streams = {"ethusdt@trade"};
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Wait for some trade updates
    ASSERT_TRUE(tracker_->waitForTradeUpdates(1, 10000));
    
    // Verify the updates
    EXPECT_GT(tracker_->getTradeCount(), 0);
    
    // Check the last trade update
    auto last_update = tracker_->getLastTradeUpdate();
    EXPECT_EQ("binance", last_update.exchange_id);
    EXPECT_EQ("ETHUSDT", last_update.trading_pair);
    EXPECT_EQ("trade", last_update.update_type);
    EXPECT_GT(last_update.data.size(), 0);
}

// Test subscribing to ticker updates
TEST_F(WebSocketIntegrationTest, TickerSubscription) {
    ASSERT_TRUE(client_->connect());
    
    // Subscribe to BTC/USDT ticker
    std::vector<std::string> streams = {"btcusdt@ticker"};
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Wait for some ticker updates
    ASSERT_TRUE(tracker_->waitForTickerUpdates(1, 10000));
    
    // Verify the updates
    EXPECT_GT(tracker_->getTickerCount(), 0);
    
    // Check the last ticker update
    auto last_update = tracker_->getLastTickerUpdate();
    EXPECT_EQ("binance", last_update.exchange_id);
    EXPECT_EQ("BTCUSDT", last_update.trading_pair);
    EXPECT_EQ("ticker", last_update.update_type);
    EXPECT_GT(last_update.data.size(), 0);
}

// Test subscribing to multiple streams
TEST_F(WebSocketIntegrationTest, MultipleSubscriptions) {
    ASSERT_TRUE(client_->connect());
    
    // Subscribe to multiple streams
    std::vector<std::string> streams = {
        "btcusdt@depth@100ms",
        "btcusdt@trade",
        "ethusdt@depth@100ms",
        "ethusdt@trade"
    };
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Wait for updates
    ASSERT_TRUE(tracker_->waitForOrderBookUpdates(3, 10000));
    ASSERT_TRUE(tracker_->waitForTradeUpdates(1, 10000));
    
    // Verify we received different types of data
    EXPECT_GT(tracker_->getOrderBookCount(), 0);
    EXPECT_GT(tracker_->getTradeCount(), 0);
}

// Test unsubscribing from streams
TEST_F(WebSocketIntegrationTest, Unsubscription) {
    ASSERT_TRUE(client_->connect());
    
    // Subscribe to multiple streams
    std::vector<std::string> streams = {
        "btcusdt@depth@100ms",
        "btcusdt@trade"
    };
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Wait for some updates
    ASSERT_TRUE(tracker_->waitForOrderBookUpdates(3, 10000));
    ASSERT_TRUE(tracker_->waitForTradeUpdates(1, 10000));
    
    // Reset the counters
    tracker_->reset();
    
    // Unsubscribe from the trade stream
    std::vector<std::string> unsubscribe_streams = {"btcusdt@trade"};
    ASSERT_TRUE(client_->unsubscribe(unsubscribe_streams));
    
    // Wait for some more updates
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // We should have received order book updates but no trade updates
    EXPECT_GT(tracker_->getOrderBookCount(), 0);
    EXPECT_EQ(0, tracker_->getTradeCount());
}

// Test reconnection
TEST_F(WebSocketIntegrationTest, Reconnection) {
    ASSERT_TRUE(client_->connect());
    
    // Subscribe to a stream
    std::vector<std::string> streams = {"btcusdt@depth@100ms"};
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Wait for some updates
    ASSERT_TRUE(tracker_->waitForOrderBookUpdates(3, 10000));
    
    // Reset the counters
    tracker_->reset();
    
    // Disconnect and reconnect
    client_->disconnect();
    ASSERT_FALSE(client_->isConnected());
    
    // Wait a moment before reconnecting
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // Reconnect
    ASSERT_TRUE(client_->connect());
    ASSERT_TRUE(client_->isConnected());
    
    // Resubscribe
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Wait for more updates
    ASSERT_TRUE(tracker_->waitForOrderBookUpdates(3, 10000));
    
    // Verify we received updates again
    EXPECT_GT(tracker_->getOrderBookCount(), 0);
}

// Test with connection pool
TEST_F(WebSocketIntegrationTest, ConnectionPool) {
    // Create a connection factory
    std::string server_url = "localhost";
    std::string server_port = std::to_string(SERVER_PORT);
    
    auto factory = [server_url, server_port, this]() -> std::shared_ptr<WebSocketClientBase> {
        auto callback = std::bind(marketDataCallback, std::placeholders::_1, tracker_.get());
        return std::make_shared<BinanceWebSocketClient>(server_url, server_port, callback);
    };
    
    // Create a connection pool
    auto pool = std::make_unique<ConnectionPool>("test_pool", factory, 2, 5);
    pool->initialize();
    
    // Acquire two connections
    auto conn1 = std::dynamic_pointer_cast<BinanceWebSocketClient>(pool->acquireConnection());
    ASSERT_NE(nullptr, conn1);
    
    auto conn2 = std::dynamic_pointer_cast<BinanceWebSocketClient>(pool->acquireConnection());
    ASSERT_NE(nullptr, conn2);
    
    // Subscribe to different streams on each connection
    std::vector<std::string> streams1 = {"btcusdt@depth@100ms"};
    std::vector<std::string> streams2 = {"ethusdt@trade"};
    
    ASSERT_TRUE(conn1->subscribe(streams1));
    ASSERT_TRUE(conn2->subscribe(streams2));
    
    // Wait for updates
    ASSERT_TRUE(tracker_->waitForOrderBookUpdates(3, 10000));
    ASSERT_TRUE(tracker_->waitForTradeUpdates(1, 10000));
    
    // Verify we received different types of data
    EXPECT_GT(tracker_->getOrderBookCount(), 0);
    EXPECT_GT(tracker_->getTradeCount(), 0);
    
    // Release the connections
    pool->releaseConnection(conn1);
    pool->releaseConnection(conn2);
    
    // Shutdown the pool
    pool->shutdown();
}

// Test performance under load
TEST_F(WebSocketIntegrationTest, PerformanceUnderLoad) {
    ASSERT_TRUE(client_->connect());
    
    // Subscribe to multiple streams
    std::vector<std::string> streams = {
        "btcusdt@depth@100ms",
        "ethusdt@depth@100ms",
        "bnbusdt@depth@100ms",
        "solusdt@depth@100ms",
        "adausdt@depth@100ms",
        "btcusdt@trade",
        "ethusdt@trade",
        "bnbusdt@trade",
        "solusdt@trade",
        "adausdt@trade",
        "btcusdt@ticker",
        "ethusdt@ticker",
        "bnbusdt@ticker",
        "solusdt@ticker",
        "adausdt@ticker"
    };
    
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Wait for a significant number of updates
    auto start_time = std::chrono::steady_clock::now();
    ASSERT_TRUE(tracker_->waitForUpdateCount(100, 30000));
    auto end_time = std::chrono::steady_clock::now();
    
    // Calculate message rate
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end_time - start_time).count();
    
    double messages_per_second = (tracker_->getUpdateCount() * 1000.0) / duration_ms;
    
    std::cout << "Received " << tracker_->getUpdateCount() << " messages in " 
              << duration_ms << "ms (" << messages_per_second << " msgs/sec)" << std::endl;
    
    // Verify we're receiving a reasonable number of messages per second
    // This is just a basic performance check - the actual threshold would depend on the system
    EXPECT_GT(messages_per_second, 10.0);
    
    // Verify we received all types of data
    EXPECT_GT(tracker_->getOrderBookCount(), 0);
    EXPECT_GT(tracker_->getTradeCount(), 0);
    EXPECT_GT(tracker_->getTickerCount(), 0);
}

// Test error handling and resilience
TEST_F(WebSocketIntegrationTest, ErrorHandling) {
    ASSERT_TRUE(client_->connect());
    
    // Try to subscribe to an invalid stream
    std::vector<std::string> invalid_streams = {"invalid@stream"};
    
    // This might fail at the client level (validation) or server level
    // Either way, the client should remain functional
    client_->subscribe(invalid_streams);
    
    // Subscribe to a valid stream to verify the client is still functional
    std::vector<std::string> valid_streams = {"btcusdt@depth@100ms"};
    ASSERT_TRUE(client_->subscribe(valid_streams));
    
    // Wait for updates
    ASSERT_TRUE(tracker_->waitForOrderBookUpdates(3, 10000));
    
    // Verify we received data
    EXPECT_GT(tracker_->getOrderBookCount(), 0);
}

// Main function required to run the tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
