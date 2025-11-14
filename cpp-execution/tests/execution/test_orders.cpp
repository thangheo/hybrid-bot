#include <gtest/gtest.h>
#include "execution/models/order.hpp"
#include <thread>
#include <vector>
#include <future>

using namespace trading::execution;

using namespace trading::execution;
using namespace trading::execution::models;
using namespace trading::core;
using namespace trading;

class OrderTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Common setup for all tests
    }

    void TearDown() override {
        // Common cleanup for all tests
    }
};

// Test the default constructor
TEST_F(OrderTest, DefaultConstructor) {
    Order order;
    EXPECT_FALSE(order.getClientOrderId().empty());
    EXPECT_EQ(order.getState(), OrderState::CREATED);
    EXPECT_EQ(order.getSide(), OrderSide::BUY);
    EXPECT_EQ(order.getType(), OrderType::MARKET);
    EXPECT_EQ(order.getQuantity(), 0.0);
    EXPECT_EQ(order.getFilledQuantity(), 0.0);
    EXPECT_TRUE(order.getOrderId().empty());
    EXPECT_TRUE(order.getExchangeId().isValid() == false);
    EXPECT_TRUE(order.getExchangeOrderId().empty());
    EXPECT_EQ(order.getTimeInForce(), TimeInForce::GTC);
}

// Test constructor with client order ID
TEST_F(OrderTest, ClientOrderIdConstructor) {
    std::string clientOrderId = "test-order-123";
    Order order(clientOrderId);
    EXPECT_EQ(order.getClientOrderId(), clientOrderId);
    EXPECT_EQ(order.getState(), OrderState::CREATED);
}

// Test factory method for market orders
TEST_F(OrderTest, CreateMarketOrder) {
    std::string symbol = "BTC/USDT";
    OrderSide side = OrderSide::BUY;
    double quantity = 1.5;
    
    auto order = Order::createMarketOrder(symbol, side, quantity);
    
    EXPECT_EQ(order->getTradingPair(), symbol);
    EXPECT_EQ(order->getSide(), side);
    EXPECT_EQ(order->getType(), OrderType::MARKET);
    EXPECT_EQ(order->getQuantity(), quantity);
    EXPECT_EQ(order->getState(), OrderState::CREATED);
}

// Test factory method for limit orders
TEST_F(OrderTest, CreateLimitOrder) {
    std::string symbol = "ETH/USDT";
    OrderSide side = OrderSide::SELL;
    double quantity = 2.0;
    double price = 1800.0;
    
    auto order = Order::createLimitOrder(symbol, side, quantity, price);
    
    EXPECT_EQ(order->getTradingPair(), symbol);
    EXPECT_EQ(order->getSide(), side);
    EXPECT_EQ(order->getType(), OrderType::LIMIT);
    EXPECT_EQ(order->getQuantity(), quantity);
    EXPECT_EQ(order->getPrice(), price);
    EXPECT_EQ(order->getState(), OrderState::CREATED);
}

// Test setters
TEST_F(OrderTest, Setters) {
    Order order;
    
    order.setOrderId("order-123");
    EXPECT_EQ(order.getOrderId(), "order-123");
    
    order.setExchangeId(core::ExchangeId::binance());
    EXPECT_EQ(order.getExchangeId(), core::ExchangeId::binance());

    order.setExchangeOrderId("exchange-order-456");
    EXPECT_EQ(order.getExchangeOrderId(), "exchange-order-456");
    
    order.setTradingPair(core::Symbol("LTC/USDT"));
    EXPECT_EQ(order.getTradingPair(), core::Symbol("LTC/USDT"));
    
    order.setSide(OrderSide::SELL);
    EXPECT_EQ(order.getSide(), OrderSide::SELL);
    
    order.setType(OrderType::LIMIT);
    EXPECT_EQ(order.getType(), OrderType::LIMIT);
    
    order.setQuantity(3.5);
    EXPECT_EQ(order.getQuantity(), 3.5);
    
    order.setPrice(200.5);
    EXPECT_EQ(order.getPrice(), 200.5);
    
    order.setStopPrice(195.0);
    EXPECT_EQ(order.getStopPrice().value(), 195.0);
    
    order.setTimeInForce(TimeInForce::IOC);
    EXPECT_EQ(order.getTimeInForce(), TimeInForce::IOC);
    
    auto now = std::chrono::system_clock::now();
    auto expiryTime = now + std::chrono::hours(1);
    order.setExpiryTime(expiryTime);
    EXPECT_EQ(order.getExpiryTime().value(), expiryTime);
    
    order.setStrategyId("test-strategy");
    EXPECT_EQ(order.getStrategyId(), "test-strategy");
    
    order.setSignalId("test-signal");
    EXPECT_EQ(order.getSignalId(), "test-signal");
    
    order.setExtraParam("test-key", "test-value");
    EXPECT_EQ(order.getExtraParams().at("test-key"), "test-value");
}

// Test state transitions
TEST_F(OrderTest, StateTransitionsHappyPath) {
    auto order = Order::createMarketOrder(core::Symbol("BTC/USDT"), OrderSide::BUY, 1.0);
    
    // Initial state
    EXPECT_EQ(order->getState(), OrderState::CREATED);
    
    // Transition to OPEN
    EXPECT_TRUE(order->open());
    EXPECT_EQ(order->getState(), OrderState::OPEN);
    
    // Create a fill
    Fill fill;
    fill.fill_id = "fill-123";
    fill.orderId = order->getOrderId();
    fill.quantity = 0.5;
    fill.price = 50000.0;
    fill.fee = 0.005;
    fill.fee_currency = "BTC";
    fill.timestamp = std::chrono::system_clock::now();
    
    // Partial fill
    EXPECT_TRUE(order->fill(fill));
    EXPECT_EQ(order->getState(), OrderState::PARTIALLY_FILLED);
    EXPECT_EQ(order->getFilledQuantity(), 0.5);
    EXPECT_EQ(order->getRemainingQuantity(), 0.5);
    EXPECT_EQ(order->getAverageFillPrice(), 50000.0);
    
    // Complete the fill
    fill.fill_id = "fill-456";
    fill.quantity = 0.5;
    EXPECT_TRUE(order->fill(fill));
    EXPECT_EQ(order->getState(), OrderState::FILLED);
    EXPECT_EQ(order->getFilledQuantity(), 1.0);
    EXPECT_EQ(order->getRemainingQuantity(), 0.0);
    EXPECT_EQ(order->getAverageFillPrice(), 50000.0);
}

// Test cancel
TEST_F(OrderTest, CancelOrder) {
    auto order = Order::createMarketOrder(core::Symbol("BTC/USDT"), OrderSide::BUY, 1.0);
    order->open();
    EXPECT_EQ(order->getState(), OrderState::OPEN);
    
    // Cancel the order
    EXPECT_TRUE(order->cancel());
    EXPECT_EQ(order->getState(), OrderState::PENDING_CANCEL);
    
    // Try to fill a canceled order
    Fill fill;
    fill.fill_id = "fill-123";
    fill.orderId = order->getOrderId();
    fill.quantity = 0.5;
    fill.price = 50000.0;
    
    // Should fail since order is in PENDING_CANCEL state
    EXPECT_FALSE(order->fill(fill));
    
    // Completely cancel the order (e.g., when we get confirmation from exchange)
    order->cancel();
    EXPECT_EQ(order->getState(), OrderState::CANCELED);
}

// Test rejection
TEST_F(OrderTest, RejectOrder) {
    auto order = Order::createMarketOrder(core::Symbol("BTC/USDT"), OrderSide::BUY, 1.0);

    // Reject the order
    std::string rejectReason = "Insufficient funds";
    EXPECT_TRUE(order->reject(rejectReason));
    EXPECT_EQ(order->getState(), OrderState::REJECTED);
    
    // Try to fill a rejected order
    Fill fill;
    fill.fill_id = "fill-123";
    fill.orderId = order->getOrderId();
    fill.quantity = 0.5;
    fill.price = 50000.0;
    
    // Should fail since order is in REJECTED state
    EXPECT_FALSE(order->fill(fill));
}

// Test expire
TEST_F(OrderTest, ExpireOrder) {
    auto order = Order::createLimitOrder(core::Symbol("BTC/USDT"), OrderSide::BUY, 1.0, 40000.0);
    order->open();
    
    // Expire the order
    EXPECT_TRUE(order->expire());
    EXPECT_EQ(order->getState(), OrderState::EXPIRED);
    
    // Try to fill an expired order
    Fill fill;
    fill.fill_id = "fill-123";
    fill.orderId = order->getOrderId();
    fill.quantity = 0.5;
    fill.price = 40000.0;
    
    // Should fail since order is in EXPIRED state
    EXPECT_FALSE(order->fill(fill));
}

// Test utility methods
TEST_F(OrderTest, UtilityMethods) {
    auto order = Order::createLimitOrder(core::Symbol("BTC/USDT"), OrderSide::BUY, 1.0, 40000.0);
    
    // Check initial state
    EXPECT_FALSE(order->isActive());
    EXPECT_TRUE(order->canModify());
    EXPECT_FALSE(order->canCancel());
    
    // Open the order
    order->open();
    EXPECT_TRUE(order->isActive());
    EXPECT_TRUE(order->canModify());
    EXPECT_TRUE(order->canCancel());
    
    // Partial fill
    Fill fill;
    fill.fill_id = "fill-123";
    fill.orderId = order->getOrderId();
    fill.quantity = 0.5;
    fill.price = 40000.0;
    order->fill(fill);
    
    EXPECT_TRUE(order->isActive());
    EXPECT_TRUE(order->canModify());
    EXPECT_TRUE(order->canCancel());
    
    // Complete fill
    fill.fill_id = "fill-456";
    fill.quantity = 0.5;
    order->fill(fill);
    
    EXPECT_FALSE(order->isActive());
    EXPECT_FALSE(order->canModify());
    EXPECT_FALSE(order->canCancel());
}

// Test toString
TEST_F(OrderTest, ToString) {
    auto order = Order::createLimitOrder(core::Symbol("BTC/USDT"), OrderSide::BUY, 1.0, 40000.0);
    order->setOrderId("test-order-id");
    
    std::string orderStr = order->toString();
    EXPECT_TRUE(orderStr.find("test-order-id") != std::string::npos);
    EXPECT_TRUE(orderStr.find("BTC/USDT") != std::string::npos);
    EXPECT_TRUE(orderStr.find("BUY") != std::string::npos);
    EXPECT_TRUE(orderStr.find("LIMIT") != std::string::npos);
    EXPECT_TRUE(orderStr.find("40000") != std::string::npos);
}

// Test thread safety - multiple threads updating the same order
TEST_F(OrderTest, ThreadSafety) {
    auto order = Order::createMarketOrder(core::Symbol("BTC/USDT"), OrderSide::BUY, 2.0);
    order->open();
    EXPECT_EQ(order->getState(), OrderState::OPEN);
    
    // Create 10 threads that will all try to fill the order with 0.2 quantity each
    std::vector<std::future<bool>> futures;
    
    for (int i = 0; i < 10; ++i) {
        futures.push_back(std::async(std::launch::async, [order, i]() {
            Fill fill;
            fill.fill_id = "fill-" + std::to_string(i);
            fill.orderId = order->getOrderId();
            fill.quantity = 0.2;
            fill.price = 50000.0 + i;  // Slightly different price for each fill
            fill.timestamp = std::chrono::system_clock::now();
            
            return order->fill(fill);
        }));
    }
    
    // Wait for all threads to complete
    for (auto& future : futures) {
        EXPECT_TRUE(future.get());
    }
    
    // Check the final state
    EXPECT_EQ(order->getState(), OrderState::FILLED);
    EXPECT_EQ(order->getFilledQuantity(), 2.0);
    EXPECT_EQ(order->getRemainingQuantity(), 0.0);
    // Average price should be the average of all fills
    EXPECT_GE(order->getAverageFillPrice(), 50000.0);
    EXPECT_LE(order->getAverageFillPrice(), 50009.0);
    
    // Check that all 10 fills are recorded
    EXPECT_EQ(order->getFills().size(), 10);
}

// Test invalid state transitions
TEST_F(OrderTest, InvalidStateTransitions) {
    auto order = Order::createMarketOrder(core::Symbol("BTC/USDT"), OrderSide::BUY, 1.0);

    // Cannot cancel a CREATED order (should move directly to CANCELED)
    EXPECT_TRUE(order->cancel());
    EXPECT_EQ(order->getState(), OrderState::CANCELED);
    
    // Cannot open a CANCELED order
    EXPECT_FALSE(order->open());
    EXPECT_EQ(order->getState(), OrderState::CANCELED);
    
    // Create a new order for further tests
    order = Order::createMarketOrder(core::Symbol("BTC/USDT"), OrderSide::BUY, 1.0);
    order->open();
    
    // Fill fully
    Fill fill;
    fill.fill_id = "fill-123";
    fill.orderId = order->getOrderId();
    fill.quantity = 1.0;
    fill.price = 50000.0;
    fill.timestamp = std::chrono::system_clock::now();
    
    EXPECT_TRUE(order->fill(fill));
    EXPECT_EQ(order->getState(), OrderState::FILLED);
    
    // Cannot cancel a FILLED order
    EXPECT_FALSE(order->cancel());
    EXPECT_EQ(order->getState(), OrderState::FILLED);
    
    // Cannot expire a FILLED order
    EXPECT_FALSE(order->expire());
    EXPECT_EQ(order->getState(), OrderState::FILLED);
    
    // Cannot reject a FILLED order
    EXPECT_FALSE(order->reject("Some reason"));
    EXPECT_EQ(order->getState(), OrderState::FILLED);
}

// Test overfilling an order
TEST_F(OrderTest, OverfillOrder) {
    auto order = Order::createMarketOrder(core::Symbol("BTC/USDT"), OrderSide::BUY, 1.0);
    order->open();
    
    // Fill with more than the order quantity
    Fill fill;
    fill.fill_id = "fill-123";
    fill.orderId = order->getOrderId();
    fill.quantity = 1.5;  // Order quantity is 1.0
    fill.price = 50000.0;
    fill.timestamp = std::chrono::system_clock::now();
    
    EXPECT_TRUE(order->fill(fill));
    EXPECT_EQ(order->getState(), OrderState::FILLED);
    EXPECT_EQ(order->getFilledQuantity(), 1.5);  // We accept overfills
    EXPECT_EQ(order->getRemainingQuantity(), -0.5);  // Negative remaining
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}