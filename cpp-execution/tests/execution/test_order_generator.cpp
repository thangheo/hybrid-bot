#include "execution/order_generator.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <string>

using namespace trading::execution;

// Mock TradingSignal for testing
class TradingSignal {
public:
    TradingSignal(const std::string& pair, const std::string& dir, 
                 double size, const std::string& strat, const std::string& sigId)
        : pair_(pair), dir_(dir), size_(size), strategyId_(strat), signalId_(sigId) {}
    
    std::string getTradingPair() const { return pair_; }
    std::string getDirection() const { return dir_; }
    double getTargetSize() const { return size_; }
    std::string getStrategyId() const { return strategyId_; }
    std::string getSignalId() const { return signalId_; }
    
private:
    std::string pair_;
    std::string dir_;
    double size_;
    std::string strategyId_;
    std::string signalId_;
};

class OrderGeneratorTest : public ::testing::Test {
protected:
    OrderGenerator generator;
};

// Test creating a basic order
TEST_F(OrderGeneratorTest, CreateBasicOrder) {
    OrderGenerationParams params;
    params.trading_pair = "BTC/USDT";
    params.side = OrderSide::BUY;
    params.quantity = 1.0;
    params.strategy_id = "test_strategy";
    
    OrderPtr order = generator.createOrder(params);
    
    EXPECT_EQ("BTC/USDT", order->getTradingPair());
    EXPECT_EQ(OrderSide::BUY, order->getSide());
    EXPECT_EQ(1.0, order->getQuantity());
    EXPECT_EQ("test_strategy", order->getStrategyId());
    EXPECT_FALSE(order->getClientOrderId().empty());
}

// Test creating a limit order
TEST_F(OrderGeneratorTest, CreateLimitOrder) {
    OrderGenerationParams params;
    params.trading_pair = "ETH/USDT";
    params.side = OrderSide::SELL;
    params.type = OrderType::LIMIT;
    params.quantity = 2.0;
    params.price = 3000.0;
    params.strategy_id = "limit_strategy";
    
    OrderPtr order = generator.createOrder(params);
    
    EXPECT_EQ("ETH/USDT", order->getTradingPair());
    EXPECT_EQ(OrderSide::SELL, order->getSide());
    EXPECT_EQ(OrderType::LIMIT, order->getType());
    EXPECT_EQ(2.0, order->getQuantity());
    EXPECT_EQ(3000.0, order->getPrice());
}

// Test client order ID generation
TEST_F(OrderGeneratorTest, GenerateClientOrderId) {
    std::string id1 = generator.generateClientOrderId();
    std::string id2 = generator.generateClientOrderId();
    
    EXPECT_FALSE(id1.empty());
    EXPECT_FALSE(id2.empty());
    EXPECT_NE(id1, id2);
    EXPECT_TRUE(id1.find("OID-") == 0);
}

// Test order creation from signal
TEST_F(OrderGeneratorTest, CreateOrderFromSignal) {
    auto signal = std::make_shared<TradingSignal>("XRP/USDT", "buy", 100.0, "signal_strategy", "sig123");
    
    OrderPtr order = generator.createOrderFromSignal(signal);
    
    EXPECT_EQ("XRP/USDT", order->getTradingPair());
    EXPECT_EQ(OrderSide::BUY, order->getSide());
    EXPECT_EQ(100.0, order->getQuantity());
    EXPECT_EQ("signal_strategy", order->getStrategyId());
    EXPECT_EQ("sig123", order->getSignalId());
}

// Test parameter transformer
TEST_F(OrderGeneratorTest, ParameterTransformer) {
    generator.setParameterTransformer([](OrderGenerationParams& params) {
        params.quantity *= 2.0;  // Double the quantity
        if (params.price > 0) {
            params.price *= 0.99;  // Apply a 1% discount
        }
    });
    
    OrderGenerationParams params;
    params.trading_pair = "BTC/USDT";
    params.side = OrderSide::BUY;
    params.type = OrderType::LIMIT;
    params.quantity = 1.0;
    params.price = 50000.0;
    
    OrderPtr order = generator.createOrder(params);
    
    EXPECT_EQ(2.0, order->getQuantity());  // Should be doubled
    EXPECT_NEAR(49500.0, order->getPrice(), 0.001);  // Should be discounted
}

// Test order callback
TEST_F(OrderGeneratorTest, OrderCallback) {
    bool callback_called = false;
    OrderPtr callback_order;
    
    generator.setOrderCallback([&](const OrderPtr& order) {
        callback_called = true;
        callback_order = order;
    });
    
    OrderGenerationParams params;
    params.trading_pair = "BTC/USDT";
    params.side = OrderSide::BUY;
    params.quantity = 1.0;
    
    OrderPtr order = generator.createOrder(params);
    
    EXPECT_TRUE(callback_called);
    EXPECT_EQ(order, callback_order);
}

// Test validation
TEST_F(OrderGeneratorTest, ValidationFailure) {
    OrderGenerationParams params;
    params.trading_pair = "";  // Empty trading pair should fail
    params.side = OrderSide::BUY;
    params.quantity = 1.0;
    
    EXPECT_THROW(generator.createOrder(params), std::invalid_argument);
    
    params.trading_pair = "BTC/USDT";
    params.quantity = -1.0;  // Negative quantity should fail
    
    EXPECT_THROW(generator.createOrder(params), std::invalid_argument);
    
    params.quantity = 1.0;
    params.type = OrderType::LIMIT;
    params.price = 0.0;  // Zero price for limit order should fail
    
    EXPECT_THROW(generator.createOrder(params), std::invalid_argument);
}