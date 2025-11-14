#include "execution/order_type_selector.hpp"
#include "execution/models/order.hpp"
#include <gtest/gtest.h>
#include <memory>

using namespace trading::execution;

using namespace trading::execution;

using namespace trading::execution;
using namespace trading::execution::models;
using namespace trading::core;
using namespace trading;

class OrderTypeSelectorTest : public ::testing::Test {
protected:
    OrderTypeSelector selector;
    
    // Create a default market condition for testing
    MarketCondition createDefaultMarketCondition() {
        MarketCondition mc;
        mc.bidPrice = 50000.0;
        mc.askPrice = 50100.0;
        mc.spread = 100.0;
        mc.volatility = 0.01;
        mc.volume = 100.0;
        mc.liquidityProfileScore = 0.8;
        mc.market_impact_estimate = 0.002;
        return mc;
    }
    
    // Create a test order
    OrderPtr createTestOrder(OrderType type, OrderSide side) {
        OrderPtr order = std::make_shared<Order>();
        order->setTradingPair(core::Symbol::fromString("BTC/USDT"));
        order->setSide(side);
        order->setType(type);
        order->setQuantity(1.0);
        if (type == OrderType::LIMIT) {
            order->setPrice(side == OrderSide::BUY ? 50000.0 : 50100.0);
        }
        return order;
    }
};

// Test market condition update and retrieval
TEST_F(OrderTypeSelectorTest, MarketConditionUpdateAndRetrieval) {
    MarketCondition mc = createDefaultMarketCondition();
    selector.updateMarketCondition(core::Symbol::fromString("BTC/USDT"), mc);
    
    MarketCondition retrieved = selector.getMarketCondition(core::Symbol::fromString("BTC/USDT"));
    
    EXPECT_DOUBLE_EQ(mc.bidPrice, retrieved.bidPrice);
    EXPECT_DOUBLE_EQ(mc.askPrice, retrieved.askPrice);
    EXPECT_DOUBLE_EQ(mc.spread, retrieved.spread);
    EXPECT_DOUBLE_EQ(mc.volatility, retrieved.volatility);
    EXPECT_DOUBLE_EQ(mc.liquidityProfileScore, retrieved.liquidityProfileScore);
}

// Test that non-existent market condition returns default values
TEST_F(OrderTypeSelectorTest, NonExistentMarketCondition) {
    MarketCondition retrieved = selector.getMarketCondition(core::Symbol::fromString("ETH/USDT"));

    EXPECT_DOUBLE_EQ(0.0, retrieved.bidPrice);
    EXPECT_DOUBLE_EQ(0.0, retrieved.askPrice);
    EXPECT_DOUBLE_EQ(0.0, retrieved.spread);
}

// Test selecting order type for market order in normal conditions
TEST_F(OrderTypeSelectorTest, SelectMarketOrderNormalConditions) {
    OrderPtr order = createTestOrder(OrderType::MARKET, OrderSide::BUY);
    MarketCondition mc = createDefaultMarketCondition();
    
    // Set narrow spread
    mc.spread = 10.0;
    
    OrderTypeSelection selection = selector.selectOrderType(order, mc);
    
    EXPECT_EQ(OrderType::MARKET, selection.type);
    EXPECT_EQ(TimeInForce::IOC, selection.time_in_force);
}

// Test converting market order to limit under wide spread
TEST_F(OrderTypeSelectorTest, ConvertMarketToLimitWideSpread) {
    OrderPtr order = createTestOrder(OrderType::MARKET, OrderSide::BUY);
    MarketCondition mc = createDefaultMarketCondition();
    
    // Set wide spread and high liquidity
    mc.spread = 200.0;
    mc.liquidityProfileScore = 0.9;
    
    // Set threshold for spread conversion
    selector.setSpreadThreshold(0.003);  // 0.3% spread threshold
    
    OrderTypeSelection selection = selector.selectOrderType(order, mc);
    
    EXPECT_EQ(OrderType::LIMIT, selection.type);
    EXPECT_NEAR(50050.0, selection.price, 100);  // Should be close to bid + some delta
    EXPECT_EQ(TimeInForce::GTC, selection.time_in_force);
}

// Test keeping limit order in stable conditions
TEST_F(OrderTypeSelectorTest, KeepLimitOrderStableConditions) {
    OrderPtr order = createTestOrder(OrderType::LIMIT, OrderSide::SELL);
    order->setPrice(50200.0);  // Set price above current ask
    
    MarketCondition mc = createDefaultMarketCondition();
    mc.volatility = 0.005;  // Low volatility
    
    OrderTypeSelection selection = selector.selectOrderType(order, mc);
    
    EXPECT_EQ(OrderType::LIMIT, selection.type);
    EXPECT_NEAR(50049.9, selection.price, 100);  // Should be close to ask * 0.999
    EXPECT_EQ(TimeInForce::GTC, selection.time_in_force);
}

// Test converting limit order to market in volatile conditions
TEST_F(OrderTypeSelectorTest, ConvertLimitToMarketVolatileConditions) {
    OrderPtr order = createTestOrder(OrderType::LIMIT, OrderSide::BUY);
    
    MarketCondition mc = createDefaultMarketCondition();
    mc.volatility = 0.03;  // High volatility
    
    // Set threshold for volatility conversion
    selector.setVolatilityThreshold(0.02);
    
    OrderTypeSelection selection = selector.selectOrderType(order, mc);
    
    EXPECT_EQ(OrderType::MARKET, selection.type);
    EXPECT_EQ(TimeInForce::IOC, selection.time_in_force);
}

// Test custom selection strategy
TEST_F(OrderTypeSelectorTest, CustomSelectionStrategy) {
    selector.setSelectionStrategy([](const OrderPtr& order, const MarketCondition& mc) {
        // Always select POST_ONLY limit orders
        OrderTypeSelection selection;
        selection.type = OrderType::LIMIT;
        selection.price = order->getSide() == OrderSide::BUY ? mc.bidPrice * 1.001 : mc.askPrice * 0.999;
        selection.time_in_force = TimeInForce::GTC;
        selection.reason = "Custom strategy: always use POST_ONLY";
        return selection;
    });
    
    OrderPtr order = createTestOrder(OrderType::MARKET, OrderSide::BUY);
    MarketCondition mc = createDefaultMarketCondition();
    
    OrderTypeSelection selection = selector.selectOrderType(order, mc);
    
    EXPECT_EQ(OrderType::LIMIT, selection.type);
    EXPECT_NEAR(50050.0, selection.price, 100);
    EXPECT_EQ(TimeInForce::GTC, selection.time_in_force);
    EXPECT_EQ("Custom strategy: always use POST_ONLY", selection.reason);
}

// Test threshold setters
TEST_F(OrderTypeSelectorTest, ThresholdSetters) {
    selector.setVolatilityThreshold(0.05);
    selector.setLiquidityThreshold(0.7);
    selector.setSpreadThreshold(0.004);
    
    // Now create a market condition that would trigger conversion with default thresholds
    MarketCondition mc = createDefaultMarketCondition();
    mc.volatility = 0.03;  // Above default but below new threshold
    mc.spread = 150.0;     // Would trigger conversion with default threshold
    
    OrderPtr order = createTestOrder(OrderType::LIMIT, OrderSide::BUY);
    
    OrderTypeSelection selection = selector.selectOrderType(order, mc);
    
    // Should remain a limit order due to higher threshold
    EXPECT_EQ(OrderType::LIMIT, selection.type);
}