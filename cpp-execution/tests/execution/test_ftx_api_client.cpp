// test_ftx_api_client.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "execution/exchanges/ftx_api_client.hpp"
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

// Test fixture for FtxApiClient
class FtxApiClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Reset mock responses
        CurlMock::nextResponse = R"({
            "success": true,
            "result": [
                {
                    "name": "BTC/USD",
                    "enabled": true
                }
            ]
        })";
        CurlMock::nextHttpCode = 200;
        CurlMock::nextResult = CURLE_OK;
        
        mockRateLimitController = std::make_shared<NiceMock<MockRateLimitController>>();
        
        ExchangeCredentials credentials;
        credentials.apiKey = "test_api_key";
        credentials.apiSecret = "test_api_secret";
        credentials.extraParams["subaccount"] = "test_subaccount";
        
        ftxClient = std::make_unique<FtxApiClient>(credentials, mockRateLimitController);
    }
    
    void TearDown() override {
        ftxClient.reset();
        mockRateLimitController.reset();
    }
    
    std::shared_ptr<MockRateLimitController> mockRateLimitController;
    std::unique_ptr<FtxApiClient> ftxClient;
};

// Test connecting to FTX
TEST_F(FtxApiClientTest, ConnectSuccess) {
    EXPECT_NO_THROW(ftxClient->connect());
    EXPECT_TRUE(ftxClient->isConnected());
}

// Test connecting with error
TEST_F(FtxApiClientTest, ConnectError) {
    CurlMock::nextHttpCode = 403;
    CurlMock::nextResponse = R"({
        "success": false,
        "error": "Invalid API key"
    })";
    EXPECT_THROW(ftxClient->connect(), std::runtime_error);
    EXPECT_FALSE(ftxClient->isConnected());
}

// Test getting supported symbols
TEST_F(FtxApiClientTest, GetSupportedSymbols) {
    ftxClient->connect();
    
    CurlMock::nextResponse = R"({
        "success": true,
        "result": [
            {
                "name": "BTC/USD",
                "enabled": true
            },
            {
                "name": "ETH/USD",
                "enabled": true
            },
            {
                "name": "LTC/USD",
                "enabled": false
            }
        ]
    })";
    
    auto symbols = ftxClient->getSupportedSymbols();
    EXPECT_EQ(symbols.size(), 2);
    EXPECT_THAT(symbols, Contains("BTC/USD"));
    EXPECT_THAT(symbols, Contains("ETH/USD"));
    EXPECT_THAT(symbols, Not(Contains("LTC/USD")));  // Disabled
}

// Test placing a market order
TEST_F(FtxApiClientTest, PlaceMarketOrder) {
    ftxClient->connect();
    
    CurlMock::nextResponse = R"({
        "success": true,
        "result": {
            "id": 123456,
            "clientId": "my_order_1",
            "market": "BTC/USD",
            "type": "market",
            "side": "buy",
            "size": 1.0,
            "price": null,
            "reduceOnly": false,
            "ioc": false,
            "postOnly": false,
            "status": "closed",
            "filledSize": 1.0,
            "remainingSize": 0.0,
            "avgFillPrice": 50000.0,
            "createdAt": "2021-06-02T12:59:13.577Z"
        }
    })";
    
    // Create a market buy order
    Order order;
    order.setTradingPair("BTC/USD");
    order.setSide(OrderSide::BUY);
    order.setType(OrderType::MARKET);
    order.setQuantity(1.0);
    order.setClientOrderId("my_order_1");
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_PLACEMENT)).Times(1);
    
    // Place the order
    auto futureReport = ftxClient->placeOrder(order);
    auto report = futureReport.get();
    
    // Verify the execution report
    EXPECT_EQ(report.symbol(), "BTC/USD");
    EXPECT_EQ(report.clientOrderId(), "my_order_1");
    EXPECT_EQ(report.exchangeOrderId(), "123456");
    EXPECT_EQ(report.status(), OrderState::FILLED);
    EXPECT_EQ(report.side(), OrderSide::BUY);
    EXPECT_EQ(report.type(), OrderType::MARKET);
    EXPECT_EQ(report.quantity(), 1.0);
    EXPECT_EQ(report.filledQuantity(), 1.0);
    EXPECT_EQ(report.averageFillPrice(), 50000.0);
}

// Test placing a limit order
TEST_F(FtxApiClientTest, PlaceLimitOrder) {
    ftxClient->connect();
    
    CurlMock::nextResponse = R"({
        "success": true,
        "result": {
            "id": 123457,
            "clientId": "my_order_2",
            "market": "ETH/USD",
            "type": "limit",
            "side": "sell",
            "size": 0.5,
            "price": 3500.0,
            "reduceOnly": false,
            "ioc": false,
            "postOnly": true,
            "status": "new",
            "filledSize": 0.0,
            "remainingSize": 0.5,
            "avgFillPrice": null,
            "createdAt": "2021-06-02T13:01:45.123Z"
        }
    })";
    
    // Create a limit sell order with post-only
    Order order;
    order.setTradingPair("ETH/USD");
    order.setSide(OrderSide::SELL);
    order.setType(OrderType::LIMIT);
    order.setQuantity(0.5);
    order.setPrice(3500.0);
    order.setClientOrderId("my_order_2");
    order.setExtraParam("postOnly", "true");
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_PLACEMENT)).Times(1);
    
    // Place the order
    auto futureReport = ftxClient->placeOrder(order);
    auto report = futureReport.get();
    
    // Verify the execution report
    EXPECT_EQ(report.symbol(), "ETH/USD");
    EXPECT_EQ(report.clientOrderId(), "my_order_2");
    EXPECT_EQ(report.exchangeOrderId(), "123457");
    EXPECT_EQ(report.status(), OrderState::OPEN);
    EXPECT_EQ(report.side(), OrderSide::SELL);
    EXPECT_EQ(report.type(), OrderType::LIMIT);
    EXPECT_EQ(report.quantity(), 0.5);
    EXPECT_EQ(report.filledQuantity(), 0.0);
    EXPECT_EQ(report.price().value_or(0.0), 3500.0);
    EXPECT_EQ(report.getExtraData("postOnly"), "true");
}

// Test cancelling an order
TEST_F(FtxApiClientTest, CancelOrder) {
    ftxClient->connect();
    
    // First, mock the response for the order status request after cancel
    CurlMock::nextResponse = R"({
        "success": true,
        "result": {
            "id": 123457,
            "clientId": "my_order_2",
            "market": "ETH/USD",
            "type": "limit",
            "side": "sell",
            "size": 0.5,
            "price": 3500.0,
            "reduceOnly": false,
            "ioc": false,
            "postOnly": true,
            "status": "closed",
            "filledSize": 0.0,
            "remainingSize": 0.0,
            "avgFillPrice": null,
            "createdAt": "2021-06-02T13:01:45.123Z"
        }
    })";
    
    // Mock the cancel response
    CurlMock::nextResponse = R"({
        "success": true,
        "result": "Order cancelled"
    })";
    
    // Expect the rate limit controller to be called twice (for cancel and status check)
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_CANCELLATION)).Times(1);
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_STATUS)).Times(1);
    
    // Cancel the order
    auto futureReport = ftxClient->cancelOrder("123457", "ETH/USD");
    auto report = futureReport.get();
    
    // Verify the execution report
    EXPECT_EQ(report.exchangeOrderId(), "123457");
    EXPECT_EQ(report.status(), OrderState::CANCELED);
}

// Test getting order status
TEST_F(FtxApiClientTest, GetOrderStatus) {
    ftxClient->connect();
    
    CurlMock::nextResponse = R"({
        "success": true,
        "result": {
            "id": 123456,
            "clientId": "my_order_1",
            "market": "BTC/USD",
            "type": "limit",
            "side": "buy",
            "size": 1.0,
            "price": 50000.0,
            "reduceOnly": false,
            "ioc": false,
            "postOnly": false,
            "status": "open",
            "filledSize": 0.5,
            "remainingSize": 0.5,
            "avgFillPrice": 49500.0,
            "createdAt": "2021-06-02T12:59:13.577Z"
        }
    })";
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_STATUS)).Times(1);
    
    // Get order status
    auto futureReport = ftxClient->getOrderStatus("123456", "BTC/USD");
    auto report = futureReport.get();
    
    // Verify the execution report
    EXPECT_EQ(report.symbol(), "BTC/USD");
    EXPECT_EQ(report.clientOrderId(), "my_order_1");
    EXPECT_EQ(report.exchangeOrderId(), "123456");
    EXPECT_EQ(report.status(), OrderState::PARTIALLY_FILLED);
    EXPECT_EQ(report.side(), OrderSide::BUY);
    EXPECT_EQ(report.type(), OrderType::LIMIT);
    EXPECT_EQ(report.quantity(), 1.0);
    EXPECT_EQ(report.filledQuantity(), 0.5);
    EXPECT_EQ(report.price().value_or(0.0), 50000.0);
    EXPECT_EQ(report.averageFillPrice(), 49500.0);
}

// Test getting account balances
TEST_F(FtxApiClientTest, GetAccountBalances) {
    ftxClient->connect();
    
    CurlMock::nextResponse = R"({
        "success": true,
        "result": [
            {
                "coin": "BTC",
                "free": 0.5,
                "total": 0.6,
                "availableWithoutBorrow": 0.5,
                "usdValue": 30000.0
            },
            {
                "coin": "ETH",
                "free": 10.0,
                "total": 15.0,
                "availableWithoutBorrow": 10.0,
                "usdValue": 45000.0
            },
            {
                "coin": "USD",
                "free": 5000.0,
                "total": 5000.0,
                "availableWithoutBorrow": 5000.0,
                "usdValue": 5000.0
            }
        ]
    })";
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ACCOUNT_INFO)).Times(1);
    
    // Get account balances
    auto futureBalances = ftxClient->getAccountBalances();
    auto balances = futureBalances.get();
    
    // Verify the balances
    EXPECT_EQ(balances.size(), 3);
    EXPECT_EQ(balances["BTC"], 0.6);
    EXPECT_EQ(balances["ETH"], 15.0);
    EXPECT_EQ(balances["USD"], 5000.0);
}

// Test getting current price
TEST_F(FtxApiClientTest, GetCurrentPrice) {
    ftxClient->connect();
    
    CurlMock::nextResponse = R"({
        "success": true,
        "result": {
            "name": "BTC/USD",
            "enabled": true,
            "postOnly": false,
            "priceIncrement": 1.0,
            "sizeIncrement": 0.0001,
            "last": 50000.0,
            "bid": 49990.0,
            "ask": 50010.0,
            "price": 50000.0,
            "type": "spot",
            "baseCurrency": "BTC",
            "quoteCurrency": "USD",
            "underlying": null,
            "restricted": false,
            "highLeverageFeeExempt": true,
            "change1h": 0.0,
            "change24h": 0.0,
            "changeBod": 0.0,
            "quoteVolume24h": 0.0,
            "volumeUsd24h": 0.0
        }
    })";
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::MARKET_DATA)).Times(1);
    
    // Get current price
    auto futurePrice = ftxClient->getCurrentPrice("BTC/USD");
    auto price = futurePrice.get();
    
    // Verify the price
    EXPECT_EQ(price, 50000.0);
}

// Test order rejection
TEST_F(FtxApiClientTest, OrderRejection) {
    ftxClient->connect();
    
    CurlMock::nextResponse = R"({
        "success": false,
        "error": "Account has insufficient balance for requested action."
    })";
    
    // Create a market buy order
    Order order;
    order.setTradingPair("BTC/USD");
    order.setSide(OrderSide::BUY);
    order.setType(OrderType::MARKET);
    order.setQuantity(100.0);  // Very large order
    
    // Expect the rate limit controller to be called
    EXPECT_CALL(*mockRateLimitController, waitForPermission(ExchangeRequestType::ORDER_PLACEMENT)).Times(1);
    
    // Place the order and expect a rejected execution report
    auto futureReport = ftxClient->placeOrder(order);
    auto report = futureReport.get();
    
    // Verify the execution report
    EXPECT_EQ(report.status(), OrderState::REJECTED);
    EXPECT_EQ(report.rejectReason(), "Account has insufficient balance for requested action.");
}

// Test exchange name
TEST_F(FtxApiClientTest, GetExchangeName) {
    EXPECT_EQ(ftxClient->getExchangeName(), "FTX");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}