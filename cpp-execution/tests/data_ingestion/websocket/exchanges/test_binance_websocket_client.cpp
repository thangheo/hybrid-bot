// tests/data_ingestion/websocket/exchanges/test_binance_websocket_client.cpp
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/websocket/exchanges/exchange_connector.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <nlohmann/json.hpp>

using namespace trading::data_ingestion;
using namespace ::testing;
using json = nlohmann::json;

namespace {

// Mock market data callback handler
class MockMarketDataHandler {
public:
    MOCK_METHOD(void, onMarketData, (const MarketDataUpdate&), ());
};

// Callback wrapper for the mock
void marketDataCallback(const MarketDataUpdate& update, MockMarketDataHandler* handler) {
    handler->onMarketData(update);
}

// Partial mock of BinanceWebSocketClient to intercept WebSocket events
class TestBinanceWebSocketClient : public BinanceWebSocketClient {
public:
    TestBinanceWebSocketClient(const std::string& host,
                              const std::string& port,
                              MarketDataCallback callback)
        : BinanceWebSocketClient(host, port, callback),
          opened_(false),
          connected_(false) {
        // Override connected state that would normally be set by WebSocketClientBase
        WebSocketClientBase::connected_ = false;
    }
    
    // Override connect to avoid actual WebSocket connections in tests
    bool connect() override {
        WebSocketClientBase::connected_ = true;
        connected_ = true;
        onOpen();
        return true;
    }
    
    // Override isConnected to use our test flag
    bool isConnected() const override {
        return connected_;
    }
    
    // Methods to simulate WebSocket events
    void simulateOpen() {
        onOpen();
    }
    
    void simulateMessage(const std::string& message) {
        this->onMessage(message);
    }
    
    void simulateClose(int code, const std::string& reason) {
        WebSocketClientBase::connected_ = false;
        connected_ = false;
        this->onClose(code, reason);
    }
    
    void simulateError(const std::string& error) {
        this->onError(error);
    }
    
    void simulateReconnect() {
        WebSocketClientBase::connected_ = true;
        connected_ = true;
        this->onReconnect();
    }
    
    // Test-specific methods
    bool wasOpened() const { return opened_; }
    void setConnected(bool connected) {
        WebSocketClientBase::connected_ = connected;
        connected_ = connected;
    }
    
    // Expose subscription state for testing
    std::vector<std::string> getSubscribedStreams() const {
        // Access the base class's private members through protected methods
        return BinanceWebSocketClient::getSubscribedStreams();
    }
    

protected:
    // Override callbacks for testing
    void onOpen() override {
        opened_ = true;
        // Call the parent class implementation without direct access to BinanceWebSocketClient's protected methods
        WebSocketClientBase::onOpen();
    }
    
    void onMessage(const std::string& message) override {
        // Process the message but don't try to call the parent's protected method
        WebSocketClientBase::onMessage(message);
    }
    
    void onClose(int code, const std::string& reason) override {
        // Call the base class implementation that we have access to
        WebSocketClientBase::onClose(code, reason);
    }
    
    void onError(const std::string& error) override {
        // Call the base class implementation that we have access to
        WebSocketClientBase::onError(error);
    }
    
    void onReconnect() override {
        // Call the base class implementation that we have access to
        WebSocketClientBase::onReconnect();
    }
private:
    bool opened_;
    bool connected_;
};

} // anonymous namespace

// Test fixture
class BinanceWebSocketClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_handler_ = std::make_unique<MockMarketDataHandler>();
        
        // Create callback that forwards to the mock
        auto callback = std::bind(marketDataCallback, std::placeholders::_1, mock_handler_.get());
        
        // Create test client
        client_ = std::make_unique<TestBinanceWebSocketClient>(
            "test-host", "443", callback);
    }
    
    void TearDown() override {
        client_.reset();
        mock_handler_.reset();
    }
    
    std::unique_ptr<MockMarketDataHandler> mock_handler_;
    std::unique_ptr<TestBinanceWebSocketClient> client_;
    
    // Helper to create JSON test messages
    std::string createOrderBookMessage(const std::string& symbol, bool is_snapshot = false) {
        json data;
        
        if (is_snapshot) {
            data["lastUpdateId"] = 12345678;
            
            // Add some bids
            json bids = json::array();
            bids.push_back({"45000.00", "1.000"});
            bids.push_back({"44999.50", "2.500"});
            data["bids"] = bids;
            
            // Add some asks
            json asks = json::array();
            asks.push_back({"45001.00", "0.500"});
            asks.push_back({"45002.00", "1.200"});
            data["asks"] = asks;
            
            // No wrapping for snapshots
            return data.dump();
        } else {
            json stream_data;
            
            // Depth update
            stream_data["e"] = "depthUpdate";
            stream_data["E"] = 1625239046123;
            stream_data["s"] = symbol;
            stream_data["U"] = 12345678;
            stream_data["u"] = 12345680;
            
            // Add some bids
            json bids = json::array();
            bids.push_back({"45000.00", "1.200"});
            stream_data["b"] = bids;
            
            // Add some asks
            json asks = json::array();
            asks.push_back({"45001.00", "0.300"});
            stream_data["a"] = asks;
            
            // Wrap in stream format
            json wrapper;
            wrapper["stream"] = symbol.substr(0, symbol.find('/')) + "@depth";
            wrapper["data"] = stream_data;
            
            return wrapper.dump();
        }
    }
    
    std::string createTradeMessage(const std::string& symbol) {
        json stream_data;
        
        // Trade data
        stream_data["e"] = "trade";
        stream_data["E"] = 1625239046123;
        stream_data["s"] = symbol;
        stream_data["t"] = 12345678;
        stream_data["p"] = "45000.50";
        stream_data["q"] = "0.123";
        stream_data["b"] = 12345;
        stream_data["a"] = 12346;
        stream_data["T"] = 1625239046122;
        stream_data["m"] = false;
        
        // Wrap in stream format
        json wrapper;
        wrapper["stream"] = symbol.substr(0, symbol.find('/')) + "@trade";
        wrapper["data"] = stream_data;
        
        return wrapper.dump();
    }
    
    std::string createTickerMessage(const std::string& symbol) {
        json stream_data;
        
        // Ticker data
        stream_data["e"] = "24hrTicker";
        stream_data["E"] = 1625239046123;
        stream_data["s"] = symbol;
        stream_data["p"] = "1000.50";
        stream_data["P"] = "2.25";
        stream_data["c"] = "45000.50";
        stream_data["b"] = "45000.00";
        stream_data["a"] = "45001.00";
        stream_data["v"] = "10000.123";
        
        // Wrap in stream format
        json wrapper;
        wrapper["stream"] = symbol.substr(0, symbol.find('/')) + "@ticker";
        wrapper["data"] = stream_data;
        
        return wrapper.dump();
    }
    
    std::string createSubscriptionResponse(int id, bool success = true) {
        json response;
        response["id"] = id;
        
        if (success) {
            response["result"] = nullptr;
        } else {
            json error;
            error["code"] = -1;
            error["msg"] = "Invalid request";
            response["error"] = error;
        }
        
        return response.dump();
    }
};

// Test connection and initialization
TEST_F(BinanceWebSocketClientTest, ConnectionAndInitialization) {
    ASSERT_TRUE(client_->connect());
    EXPECT_TRUE(client_->isConnected());
    EXPECT_TRUE(client_->wasOpened());
}

// Test subscription
TEST_F(BinanceWebSocketClientTest, Subscription) {
    ASSERT_TRUE(client_->connect());
    
    // Define streams to subscribe to
    std::vector<std::string> streams = {
        "btcusdt@depth",
        "btcusdt@trade",
        "ethusdt@depth"
    };
    
    // Expect subscription to succeed
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Simulate a subscription response from Binance
    client_->simulateMessage(createSubscriptionResponse(1, true));
    
    // Verify streams were tracked
    auto subscribed = client_->getSubscribedStreams();
    EXPECT_EQ(streams.size(), subscribed.size());
    
    for (const auto& stream : streams) {
        EXPECT_NE(std::find(subscribed.begin(), subscribed.end(), stream), subscribed.end());
    }
}

// Test order book message handling
TEST_F(BinanceWebSocketClientTest, OrderBookMessageHandling) {
    ASSERT_TRUE(client_->connect());
    
    const std::string symbol = "BTCUSDT";
    
    // Set up expectations
    EXPECT_CALL(*mock_handler_, onMarketData(AllOf(
        Field(&MarketDataUpdate::exchange_id, "binance"),
        Field(&MarketDataUpdate::trading_pair, symbol),
        Field(&MarketDataUpdate::update_type, "orderbook")
    ))).Times(1);
    
    // Simulate a depth update message
    client_->simulateMessage(createOrderBookMessage(symbol));
}

// Test trade message handling
TEST_F(BinanceWebSocketClientTest, TradeMessageHandling) {
    ASSERT_TRUE(client_->connect());
    
    const std::string symbol = "BTCUSDT";
    
    // Set up expectations
    EXPECT_CALL(*mock_handler_, onMarketData(AllOf(
        Field(&MarketDataUpdate::exchange_id, "binance"),
        Field(&MarketDataUpdate::trading_pair, symbol),
        Field(&MarketDataUpdate::update_type, "trade")
    ))).Times(1);
    
    // Simulate a trade message
    client_->simulateMessage(createTradeMessage(symbol));
}

// Test ticker message handling
TEST_F(BinanceWebSocketClientTest, TickerMessageHandling) {
    ASSERT_TRUE(client_->connect());
    
    const std::string symbol = "BTCUSDT";
    
    // Set up expectations
    EXPECT_CALL(*mock_handler_, onMarketData(AllOf(
        Field(&MarketDataUpdate::exchange_id, "binance"),
        Field(&MarketDataUpdate::trading_pair, symbol),
        Field(&MarketDataUpdate::update_type, "ticker")
    ))).Times(1);
    
    // Simulate a ticker message
    client_->simulateMessage(createTickerMessage(symbol));
}

// Test unsubscription
TEST_F(BinanceWebSocketClientTest, Unsubscription) {
    ASSERT_TRUE(client_->connect());
    
    // Define streams 
    std::vector<std::string> streams = {
        "btcusdt@depth",
        "btcusdt@trade",
        "ethusdt@depth"
    };
    
    // Subscribe first
    ASSERT_TRUE(client_->subscribe(streams));
    client_->simulateMessage(createSubscriptionResponse(1, true));
    
    // Now unsubscribe from one stream
    std::vector<std::string> unsubscribe_streams = {
        "btcusdt@trade"
    };
    
    ASSERT_TRUE(client_->unsubscribe(unsubscribe_streams));
    client_->simulateMessage(createSubscriptionResponse(2, true));
    
    // Verify the unsubscribed stream is removed
    auto subscribed = client_->getSubscribedStreams();
    EXPECT_EQ(streams.size() - unsubscribe_streams.size(), subscribed.size());
    
    // The unsubscribed stream should not be present
    EXPECT_EQ(std::find(subscribed.begin(), subscribed.end(), unsubscribe_streams[0]), subscribed.end());
    
    // The other streams should still be present
    EXPECT_NE(std::find(subscribed.begin(), subscribed.end(), "btcusdt@depth"), subscribed.end());
    EXPECT_NE(std::find(subscribed.begin(), subscribed.end(), "ethusdt@depth"), subscribed.end());
}

// Test reconnection behavior
TEST_F(BinanceWebSocketClientTest, ReconnectionBehavior) {
    ASSERT_TRUE(client_->connect());
    
    // Define streams and subscribe
    std::vector<std::string> streams = {
        "btcusdt@depth",
        "ethusdt@trade"
    };
    
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Simulate a disconnection
    client_->simulateClose(1006, "Connection lost");
    EXPECT_FALSE(client_->isConnected());
    
    // Simulate a reconnection
    client_->simulateReconnect();
    EXPECT_TRUE(client_->isConnected());
    
    // Verify that subscriptions are reestablished
    EXPECT_EQ(streams.size(), client_->getSubscribedStreams().size());
}

// Test error handling
TEST_F(BinanceWebSocketClientTest, ErrorHandling) {
    ASSERT_TRUE(client_->connect());
    
    // No direct way to verify error handling in this mock setup,
    // but we can at least ensure the error event doesn't crash the client
    client_->simulateError("Test error message");
    
    // Client should still be functional
    EXPECT_TRUE(client_->isConnected());
    
    // Should still be able to subscribe after error
    std::vector<std::string> streams = {"btcusdt@depth"};
    EXPECT_TRUE(client_->subscribe(streams));
}

// Test subscription failure
TEST_F(BinanceWebSocketClientTest, SubscriptionFailure) {
    ASSERT_TRUE(client_->connect());
    
    // Define streams
    std::vector<std::string> streams = {
        "btcusdt@depth"
    };
    
    // Subscribe
    ASSERT_TRUE(client_->subscribe(streams));
    
    // Simulate a subscription failure response
    client_->simulateMessage(createSubscriptionResponse(1, false));
    
    // Difficult to verify the internal state directly,
    // but we've tested that the client doesn't crash on error
    
    // Client should still be connected
    EXPECT_TRUE(client_->isConnected());
}

// Test invalid message handling
TEST_F(BinanceWebSocketClientTest, InvalidMessageHandling) {
    ASSERT_TRUE(client_->connect());
    
    // No market data callback should be triggered for invalid messages
    EXPECT_CALL(*mock_handler_, onMarketData(_)).Times(0);
    
    // Simulate some invalid messages
    client_->simulateMessage("not json");
    client_->simulateMessage("{}"); // Empty JSON
    client_->simulateMessage("{\"unknown\": \"field\"}"); // Missing required fields
    
    // Client should still be functional
    EXPECT_TRUE(client_->isConnected());
}

// Main function required to run the tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}