// test_binance_api_client.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "execution/exchanges/binance_api_client.hpp"
#include "execution/rate_limit_controller.hpp"
#include <curl/curl.h>
#include <memory>
#include <string>
#include <future>

using namespace trading::execution;
using namespace testing;

// Mock for RateLimitController
class MockRateLimitController : public RateLimitController {
public:
    MOCK_METHOD(void, waitForPermission, (ExchangeRequestType requestType), (override));
    MOCK_METHOD(int, getRemainingRequests, (ExchangeRequestType requestType), (const, override));
    MOCK_METHOD(std::chrono::milliseconds, getResetTime, (ExchangeRequestType requestType), (const, override));
    MOCK_METHOD(void, updateLimits, (ExchangeRequestType requestType, int limit, int remaining, 
                                    std::chrono::milliseconds resetTime), (override));
};

// Mock for CURL to avoid actual HTTP requests
class CurlMock {
public:
    static std::string nextResponse;
    static long nextHttpCode;
    static CURLcode nextResult;
    
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append(nextResponse);
        return nextResponse.size();
    }
    
    static CURLcode easy_perform(CURL* curl) {
        // Set the HTTP code
        curl_easy_setinfo(curl, CURLINFO_RESPONSE_CODE, &nextHttpCode);
        return nextResult;
    }
};

std::string CurlMock::nextResponse = "";
long CurlMock::nextHttpCode = 200;
CURLcode CurlMock::nextResult = CURLE_OK;

// Test fixture for BinanceApiClient
class BinanceApiClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset mock responses
        CurlMock::nextResponse = R"({
            "symbols": [
                {
                    "symbol": "BTCUSDT",
                    "status": "TRADING"
                }
            ]
        })";
        CurlMock::nextHttpCode = 200;
        CurlMock::nextResult = CURLE_OK;
        
        mockRateLimitController = std::make_shared<NiceMock<MockRateLimitController>>();
        
        ExchangeCredentials credentials;
        credentials.apiKey = "test_api_key";
        credentials.apiSecret = "test_api_secret";
        
        binanceClient = std::make_unique<BinanceApiClient>(credentials, mockRateLimitController);
    }
    
    void TearDown() override {
        binanceClient.reset();
        mockRateLimitController.reset();
    }
    
    std::shared_ptr<MockRateLimitController> mockRateLimitController;
    std::unique_ptr<BinanceApiClient> binanceClient;
};

// Test connecting to Binance
TEST_F(BinanceApiClientTest, ConnectSuccess) {
    EXPECT_NO_THROW(binanceClient->connect());
    EXPECT_TRUE(binanceClient->isConnected());
}

// Test connecting with error
TEST_F(BinanceApiClientTest, ConnectError) {
    CurlMock::nextHttpCode = 403;
    CurlMock::nextResponse = R"({"code": -1000, "msg": "Error"})";
    EXPECT_THROW(binanceClient->connect(), std::runtime_error);
    EXPECT_FALSE(binanceClient->isConnected());
}

// Test getting supported symbols
TEST_F(BinanceApiClientTest, GetSupportedSymbols) {
    binanceClient->connect();
    
    CurlMock::nextResponse = R"({
        "symbols": [
            {
                "symbol": "BTCUSDT",
                "status": "TRADING"
            },
            {
                "symbol": "ETHUSDT",
                "status": "TRADING"
            },
            {
                "symbol": "LTCUSDT",
                "status": "HALT"
            }
        ]
    })";
    
    auto symbols = binanceClient->getSupportedSymbols();
    EXPECT_EQ(symbols.size(), 2);
    EXPECT_THAT(symbols, Contains("BTCUSDT"));
    EXPECT_THAT(symbols, Contains("ETHUSDT"));
    EXPECT_THAT(symbols, Not(Contains("LTCUSDT")));  // Halted
}

// Test placing a market order
TEST_F(BinanceApiClientTest, PlaceMarketOrder) {
    binanceClient->connect();
    
    CurlMock::nextResponse = R"({
        "symbol": "BTCUSDT",
        "orderId": 12345,
        "clientOrderId": "my_order_1",
        "transactTime": 1499827319559,
        "price": "0.00000000",
        "origQty": "1.00000000",
        "executedQty": "1.00000000",
        "status": "FILLED",
        "timeInForce": "GTC",
        "type": "MARKET",
        "side": "BUY"
    })";
    
    // Create a market buy order
    Order order;
    order.setTradingPair("BTCUSDT");
    order.setSide(OrderSide::BUY);
    order.setType(OrderType::MARKET);
    order.setQuantity(1.0);
    order.setClientOrderId("my_order_1");
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_PLACEMENT)).Times(1);
    
    // Place the order
    auto futureReport = binanceClient->placeOrder(order);
    auto report = futureReport.get();
    
    // Verify the execution report
    EXPECT_EQ(report.symbol(), "BTCUSDT");
    EXPECT_EQ(report.orderId(), "my_order_1");
    EXPECT_EQ(report.exchangeOrderId(), "12345");
    EXPECT_EQ(report.status(), OrderState::FILLED);
    EXPECT_EQ(report.side(), OrderSide::BUY);
    EXPECT_EQ(report.type(), OrderType::MARKET);
    EXPECT_EQ(report.quantity(), 1.0);
    EXPECT_EQ(report.filledQuantity(), 1.0);
}

// Test placing a limit order
TEST_F(BinanceApiClientTest, PlaceLimitOrder) {
    binanceClient->connect();
    
    CurlMock::nextResponse = R"({
        "symbol": "BTCUSDT",
        "orderId": 12345,
        "clientOrderId": "my_order_2",
        "transactTime": 1499827319559,
        "price": "50000.00000000",
        "origQty": "0.50000000",
        "executedQty": "0.00000000",
        "status": "NEW",
        "timeInForce": "GTC",
        "type": "LIMIT",
        "side": "SELL"
    })";
    
    // Create a limit sell order
    Order order;
    order.setTradingPair("BTCUSDT");
    order.setSide(OrderSide::SELL);
    order.setType(OrderType::LIMIT);
    order.setQuantity(0.5);
    order.setPrice(50000.0);
    order.setClientOrderId("my_order_2");
    order.setTimeInForce(TimeInForce::GTC);
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_PLACEMENT)).Times(1);
    
    // Place the order
    auto futureReport = binanceClient->placeOrder(order);
    auto report = futureReport.get();
    
    // Verify the execution report
    EXPECT_EQ(report.symbol(), "BTCUSDT");
    EXPECT_EQ(report.orderId(), "my_order_2");
    EXPECT_EQ(report.exchangeOrderId(), "12345");
    EXPECT_EQ(report.status(), OrderState::OPEN);
    EXPECT_EQ(report.side(), OrderSide::SELL);
    EXPECT_EQ(report.type(), OrderType::LIMIT);
    EXPECT_EQ(report.quantity(), 0.5);
    EXPECT_EQ(report.filledQuantity(), 0.0);
    EXPECT_EQ(report.price().value_or(0.0), 50000.0);
}

// Test cancelling an order
TEST_F(BinanceApiClientTest, CancelOrder) {
    binanceClient->connect();
    
    CurlMock::nextResponse = R"({
        "symbol": "BTCUSDT",
        "origClientOrderId": "my_order_2",
        "orderId": 12345,
        "clientOrderId": "cancel_order_1",
        "status": "CANCELED",
        "price": "50000.00000000",
        "origQty": "0.50000000",
        "executedQty": "0.00000000",
        "timeInForce": "GTC",
        "type": "LIMIT",
        "side": "SELL"
    })";
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_CANCELLATION)).Times(1);
    
    // Cancel the order
    auto futureReport = binanceClient->cancelOrder("12345", "BTCUSDT");
    auto report = futureReport.get();
    
    // Verify the execution report
    EXPECT_EQ(report.symbol(), "BTCUSDT");
    EXPECT_EQ(report.exchangeOrderId(), "12345");
    EXPECT_EQ(report.status(), OrderState::CANCELED);
}

// Test getting order status
TEST_F(BinanceApiClientTest, GetOrderStatus) {
    binanceClient->connect();
    
    CurlMock::nextResponse = R"({
        "symbol": "BTCUSDT",
        "orderId": 12345,
        "clientOrderId": "my_order_3",
        "price": "50000.00000000",
        "origQty": "0.50000000",
        "executedQty": "0.25000000",
        "status": "PARTIALLY_FILLED",
        "timeInForce": "GTC",
        "type": "LIMIT",
        "side": "BUY",
        "stopPrice": "0.00000000",
        "icebergQty": "0.00000000",
        "time": 1499827319559,
        "updateTime": 1499827319559,
        "isWorking": true
    })";
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_STATUS)).Times(1);
    
    // Get order status
    auto futureReport = binanceClient->getOrderStatus("12345", "BTCUSDT");
    auto report = futureReport.get();
    
    // Verify the execution report
    EXPECT_EQ(report.symbol(), "BTCUSDT");
    EXPECT_EQ(report.orderId(), "my_order_3");
    EXPECT_EQ(report.exchangeOrderId(), "12345");
    EXPECT_EQ(report.status(), OrderState::PARTIALLY_FILLED);
    EXPECT_EQ(report.side(), OrderSide::BUY);
    EXPECT_EQ(report.type(), OrderType::LIMIT);
    EXPECT_EQ(report.quantity(), 0.5);
    EXPECT_EQ(report.filledQuantity(), 0.25);
    EXPECT_EQ(report.price().value_or(0.0), 50000.0);
}

// Test getting account balances
TEST_F(BinanceApiClientTest, GetAccountBalances) {
    binanceClient->connect();
    
    CurlMock::nextResponse = R"({
        "makerCommission": 15,
        "takerCommission": 15,
        "buyerCommission": 0,
        "sellerCommission": 0,
        "canTrade": true,
        "canWithdraw": true,
        "canDeposit": true,
        "updateTime": 123456789,
        "balances": [
            {
                "asset": "BTC",
                "free": "0.50000000",
                "locked": "0.10000000"
            },
            {
                "asset": "ETH",
                "free": "10.00000000",
                "locked": "5.00000000"
            },
            {
                "asset": "USDT",
                "free": "5000.00000000",
                "locked": "0.00000000"
            }
        ]
    })";
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ACCOUNT_INFO)).Times(1);
    
    // Get account balances
    auto futureBalances = binanceClient->getAccountBalances();
    auto balances = futureBalances.get();
    
    // Verify the balances
    EXPECT_EQ(balances.size(), 3);
    EXPECT_EQ(balances["BTC"], 0.6);  // free + locked
    EXPECT_EQ(balances["ETH"], 15.0);
    EXPECT_EQ(balances["USDT"], 5000.0);
}

// Test getting current price
TEST_F(BinanceApiClientTest, GetCurrentPrice) {
    binanceClient->connect();
    
    CurlMock::nextResponse = R"({
        "symbol": "BTCUSDT",
        "price": "50000.00000000"
    })";
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::MARKET_DATA)).Times(1);
    
    // Get current price
    auto futurePrice = binanceClient->getCurrentPrice("BTCUSDT");
    auto price = futurePrice.get();
    
    // Verify the price
    EXPECT_EQ(price, 50000.0);
}

// Test order rejection
TEST_F(BinanceApiClientTest, OrderRejection) {
    binanceClient->connect();
    
    CurlMock::nextResponse = R"({
        "code": -2010,
        "msg": "Account has insufficient balance for requested action."
    })";
    CurlMock::nextHttpCode = 400;
    
    // Create a market buy order
    Order order;
    order.setTradingPair("BTCUSDT");
    order.setSide(OrderSide::BUY);
    order.setType(OrderType::MARKET);
    order.setQuantity(100.0);  // Very large order
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_PLACEMENT)).Times(1);
    
    // Place the order and expect rejection
    auto futureReport = binanceClient->placeOrder(order);
    EXPECT_THROW(futureReport.get(), std::runtime_error);
}

// Test exchange name
TEST_F(BinanceApiClientTest, GetExchangeName) {
    EXPECT_EQ(binanceClient->getExchangeName(), "BINANCE");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}