#include <gtest/gtest.h>
#include "execution/balance_validator.hpp"
#include "execution/models/order.hpp"
#include "execution/exchange_api_client.hpp"
#include <memory>
#include <future>
#include <map>

using namespace trading::execution;
using namespace trading::execution::models;
using namespace trading::core;

// Mock API client for testing
class MockApiClient : public ExchangeApiClient {
public:
    std::map<std::string, double> mockBalances;

    std::future<std::map<std::string, double>> getAccountBalances() override {
        return std::async(std::launch::deferred, [this]() {
            return mockBalances;
        });
    }

    // Minimal implementations for abstract methods so this mock is instantiable
    std::future<models::ExecutionReport> placeOrder(const models::Order& order) override {
        return std::async(std::launch::deferred, []() { return models::ExecutionReport{}; });
    }

    std::future<models::ExecutionReport> cancelOrder(const std::string& orderId, const Symbol& symbol) override {
        return std::async(std::launch::deferred, []() { return models::ExecutionReport{}; });
    }

    std::future<models::ExecutionReport> getOrderStatus(const std::string& orderId, const Symbol& symbol) override {
        return std::async(std::launch::deferred, []() { return models::ExecutionReport{}; });
    }

    std::future<double> getCurrentPrice(const Symbol& symbol) override {
        return std::async(std::launch::deferred, []() { return 50000.0; });
    }

    bool isConnected() const override { return true; }
    void connect() override {}
    void disconnect() override {}
        std::string getExchangeName() const override {
            return "mock";
        }
        std::vector<std::string> getSupportedSymbols() const override {
            return {};
        }
        std::string getHealthStatus() override {
            return "ok";
        }
        std::unordered_map<std::string, std::string> getExchangeStatuses() const override {
            return {};
        }
        int getRemainingRequests(ExchangeRequestType /*requestType*/) const override {
            return 1000;
        }
        std::chrono::milliseconds getResetTimeForRequestType(ExchangeRequestType /*requestType*/) const override {
            return std::chrono::milliseconds(0);
        }
    bool checkConnection() const override { return true; }
};

class BalanceValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Configure balance validator
        BalanceValidatorConfig config;
        config.defaultTradingFee = 0.001; // 0.1%
        config.balanceBufferPercent = 0.01; // 1%
        config.minNotionalValue = 10.0;
        config.enableRealTimeValidation = true;
        
        validator = std::make_unique<BalanceValidator>(config);
        mockClient = std::make_shared<MockApiClient>();
        validator->setApiClient(mockClient);
        
    exchangeId = ExchangeId::fromString("BINANCE");
    }

    std::unique_ptr<BalanceValidator> validator;
    std::shared_ptr<MockApiClient> mockClient;
    ExchangeId exchangeId;
    
    Order createTestOrder(OrderSide side, double quantity, double price, const std::string& symbol = "BTCUSDT") {
        Order order;
        order.setTradingPair(Symbol(symbol));
        order.setSide(side);
        order.setQuantity(quantity);
        order.setPrice(price);
        order.setType(OrderType::LIMIT);
        return order;
    }
};

TEST_F(BalanceValidatorTest, BuyOrderSufficientBalance) {
    // Setup: User has 10000 USDT
    mockClient->mockBalances["USDT"] = 10000.0;
    mockClient->mockBalances["BTC"] = 0.0;
    
    // Test: Buy 0.1 BTC at 50000 USDT (5000 USDT + fees ≈ 5005 USDT required)
    auto order = createTestOrder(OrderSide::BUY, 0.1, 50000.0);
    auto result = validator->validateOrder(order, exchangeId, false);
    
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.asset, "USDT");
    EXPECT_GT(result.availableBalance, result.requiredBalance);
}

TEST_F(BalanceValidatorTest, BuyOrderInsufficientBalance) {
    // Setup: User has only 1000 USDT
    mockClient->mockBalances["USDT"] = 1000.0;
    mockClient->mockBalances["BTC"] = 0.0;
    
    // Test: Try to buy 0.1 BTC at 50000 USDT (5000 USDT + fees required)
    auto order = createTestOrder(OrderSide::BUY, 0.1, 50000.0);
    auto result = validator->validateOrder(order, exchangeId, false);
    
    EXPECT_FALSE(result.isValid);
    EXPECT_EQ(result.asset, "USDT");
    EXPECT_LT(result.availableBalance, result.requiredBalance);
    EXPECT_TRUE(result.reason.find("Insufficient USDT balance") != std::string::npos);
}

TEST_F(BalanceValidatorTest, SellOrderSufficientBalance) {
    // Setup: User has 1.0 BTC
    mockClient->mockBalances["USDT"] = 0.0;
    mockClient->mockBalances["BTC"] = 1.0;
    
    // Test: Sell 0.1 BTC
    auto order = createTestOrder(OrderSide::SELL, 0.1, 50000.0);
    auto result = validator->validateOrder(order, exchangeId, false);
    
    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.asset, "BTC");
    EXPECT_GT(result.availableBalance, result.requiredBalance);
}

TEST_F(BalanceValidatorTest, SellOrderInsufficientBalance) {
    // Setup: User has only 0.05 BTC
    mockClient->mockBalances["USDT"] = 0.0;
    mockClient->mockBalances["BTC"] = 0.05;
    
    // Test: Try to sell 0.1 BTC
    auto order = createTestOrder(OrderSide::SELL, 0.1, 50000.0);
    auto result = validator->validateOrder(order, exchangeId, false);
    
    EXPECT_FALSE(result.isValid);
    EXPECT_EQ(result.asset, "BTC");
    EXPECT_LT(result.availableBalance, result.requiredBalance);
    EXPECT_TRUE(result.reason.find("Insufficient BTC balance") != std::string::npos);
}

TEST_F(BalanceValidatorTest, OrderBelowMinNotional) {
    // Setup: User has sufficient balance
    mockClient->mockBalances["USDT"] = 1000.0;
    mockClient->mockBalances["BTC"] = 1.0;
    
    // Test: Order value below minimum notional (5 USDT < 10 USDT minimum)
    auto order = createTestOrder(OrderSide::BUY, 0.0001, 50000.0);
    auto result = validator->validateOrder(order, exchangeId, false);
    
    EXPECT_FALSE(result.isValid);
    EXPECT_TRUE(result.reason.find("minimum notional") != std::string::npos);
}

TEST_F(BalanceValidatorTest, SuggestedQuantityAdjustment) {
    // Setup: User has limited balance
    mockClient->mockBalances["USDT"] = 2000.0;  // Can afford less than requested
    mockClient->mockBalances["BTC"] = 0.0;
    
    // Test: Try to buy more than affordable
    auto order = createTestOrder(OrderSide::BUY, 0.1, 50000.0); // Wants 5000 USDT worth
    auto result = validator->validateOrder(order, exchangeId, false);
    
    EXPECT_FALSE(result.isValid);
    EXPECT_TRUE(result.canAdjust);
    EXPECT_GT(result.suggestedQuantity, 0.0);
    EXPECT_LT(result.suggestedQuantity, 0.1); // Should be less than requested
}

TEST_F(BalanceValidatorTest, CaseInsensitiveAssetLookup) {
    // Setup: Balance stored in different case
    mockClient->mockBalances["usdt"] = 10000.0; // lowercase
    mockClient->mockBalances["btc"] = 1.0;      // lowercase
    
    // Test: Order uses uppercase symbols
    auto order = createTestOrder(OrderSide::BUY, 0.1, 50000.0, "BTCUSDT");
    auto result = validator->validateOrder(order, exchangeId, false);
    
    EXPECT_TRUE(result.isValid); // Should find balance despite case difference
}

TEST_F(BalanceValidatorTest, AlternativeSymbolFormats) {
    // Setup: User has balance
    mockClient->mockBalances["ETH"] = 10.0;
    mockClient->mockBalances["USDT"] = 10000.0;
    
    // Test different symbol formats
    auto order1 = createTestOrder(OrderSide::SELL, 1.0, 2000.0, "ETHUSDT");
    auto order2 = createTestOrder(OrderSide::SELL, 1.0, 2000.0, "ETH/USDT");
    auto order3 = createTestOrder(OrderSide::SELL, 1.0, 2000.0, "ETH-USDT");
    
    auto result1 = validator->validateOrder(order1, exchangeId, false);
    auto result2 = validator->validateOrder(order2, exchangeId, false);
    auto result3 = validator->validateOrder(order3, exchangeId, false);
    
    EXPECT_TRUE(result1.isValid);
    EXPECT_TRUE(result2.isValid);
    EXPECT_TRUE(result3.isValid);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}