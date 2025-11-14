#include "execution/smart_order_router.hpp"
#include "execution/models/order.hpp"
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace trading::execution;

// Mock implementation of ExchangeInfo methods
bool ExchangeInfo::supports_order_type(OrderType type) const {
    // For test simplicity, assume all exchanges support all types except for FTX
    if (exchange_id == "ftx" && type == OrderType::ICEBERG) {
        return false;
    }
    return true;
}

bool ExchangeInfo::supports_trading_pair(const std::string& trading_pair) const {
    // For test simplicity, assume all exchanges support most pairs with exceptions
    if (exchange_id == "binance" && trading_pair == "XMR/USDT") {
        return false;  // Binance doesn't support Monero
    }
    if (exchange_id == "coinbase" && trading_pair == "ADA/USDT") {
        return false;  // Just for testing
    }
    return true;
}

class SmartOrderRouterTest : public ::testing::Test {
protected:
    SmartOrderRouter router;
    
    void SetUp() override {
        std::vector<ExchangeInfo> exchanges = {
            {"binance", 0.001, 0.9, 50.0, 0.995},    // Low fee, high liquidity, fast
            {"coinbase", 0.005, 0.85, 80.0, 0.999},  // High fee, good liquidity, high reliability
            {"ftx", 0.002, 0.75, 60.0, 0.990}        // Medium fee, lower liquidity
        };
        router.initialize(exchanges);
    }
    
    // Helper to create orders for testing
    OrderPtr createTestOrder(const std::string& pair, OrderType type) {
        OrderPtr order = std::make_shared<Order>();
        order->setTradingPair(pair);
        order->setType(type);
        order->setQuantity(1.0);
        order->setSide(OrderSide::BUY);
        return order;
    }
};

// Test initialization
TEST_F(SmartOrderRouterTest, Initialization) {
    // Check that all exchanges are available for a common pair
    auto exchanges = router.getAvailableExchanges("BTC/USDT");
    EXPECT_EQ(3, exchanges.size());
    
    // Check by individually looking for each expected exchange
    EXPECT_NE(exchanges.end(), std::find(exchanges.begin(), exchanges.end(), "binance"));
    EXPECT_NE(exchanges.end(), std::find(exchanges.begin(), exchanges.end(), "coinbase"));
    EXPECT_NE(exchanges.end(), std::find(exchanges.begin(), exchanges.end(), "ftx"));
}

// Test getting available exchanges for specific pair
TEST_F(SmartOrderRouterTest, GetAvailableExchanges) {
    // XMR/USDT not supported by Binance in our mock
        auto xmr_exchanges = router.getAvailableExchanges("XMR/USDT");
        EXPECT_EQ(2, xmr_exchanges.size());
        EXPECT_EQ(xmr_exchanges.end(), std::find(xmr_exchanges.begin(), xmr_exchanges.end(), "binance"));
        
        // ADA/USDT not supported by Coinbase in our mock
        auto ada_exchanges = router.getAvailableExchanges("ADA/USDT");
        EXPECT_EQ(2, ada_exchanges.size());
        EXPECT_EQ(ada_exchanges.end(), std::find(ada_exchanges.begin(), ada_exchanges.end(), "coinbase"));
}

// Test routing a standard order
TEST_F(SmartOrderRouterTest, StandardOrderRouting) {
    OrderPtr order = createTestOrder("BTC/USDT", OrderType::LIMIT);
    
    RoutingResult result = router.routeOrder(order);
    
    // With default factors, Binance should be chosen (lowest fee, highest liquidity)
    EXPECT_EQ("binance", result.exchange_id);
    EXPECT_GT(result.score, 0.0);
}

// Test routing with different routing factors
TEST_F(SmartOrderRouterTest, CustomRoutingFactors) {
    // Set factors to prioritize reliability over fee
    RoutingFactors factors;
    factors.fee_weight = 0.1;          // Less important
    factors.success_rate_weight = 0.6; // Most important
    factors.liquidity_weight = 0.2;
    factors.latency_weight = 0.1;
    
    router.setRoutingFactors(factors);
    
    OrderPtr order = createTestOrder("BTC/USDT", OrderType::LIMIT);
    RoutingResult result = router.routeOrder(order);
    
    // Coinbase should now be chosen due to highest reliability
    EXPECT_EQ("coinbase", result.exchange_id);
}

// Test routing to specific exchange
TEST_F(SmartOrderRouterTest, RouteToSpecificExchange) {
    OrderPtr order = createTestOrder("BTC/USDT", OrderType::LIMIT);
    
    bool success = router.routeToExchange(order, "ftx");
    
    EXPECT_TRUE(success);
    EXPECT_EQ("ftx", order->getExchangeId());
    
    // Try routing to non-existent exchange
    OrderPtr order2 = createTestOrder("BTC/USDT", OrderType::LIMIT);
    bool failure = router.routeToExchange(order2, "kraken");
    
    EXPECT_FALSE(failure);
    EXPECT_EQ("", order2->getExchangeId());
}

// Test routing for unsupported order type
TEST_F(SmartOrderRouterTest, UnsupportedOrderType) {
    // FTX doesn't support ICEBERG orders in our mock
    OrderPtr order = createTestOrder("BTC/USDT", OrderType::ICEBERG);
    
    RoutingResult result = router.routeOrder(order);
    
    // Result should not include FTX
    EXPECT_NE("ftx", result.exchange_id);
}

// Test routing for unsupported trading pair
TEST_F(SmartOrderRouterTest, UnsupportedTradingPair) {
    // XMR not supported by Binance in our mock
    OrderPtr order = createTestOrder("XMR/USDT", OrderType::LIMIT);
    
    RoutingResult result = router.routeOrder(order);
    
    // Result should not be Binance
    EXPECT_NE("binance", result.exchange_id);
}

// Test updating exchange information
TEST_F(SmartOrderRouterTest, UpdateExchangeInfo) {
    // Initially Binance should be chosen for BTC/USDT
    OrderPtr order = createTestOrder("BTC/USDT", OrderType::LIMIT);
    RoutingResult initial_result = router.routeOrder(order);
    EXPECT_EQ("binance", initial_result.exchange_id);
    
    // Update Binance to have worse metrics
    ExchangeInfo updated_binance = {"binance", 0.01, 0.5, 150.0, 0.8};
    router.updateExchangeInfo(updated_binance);
    
    // Now a different exchange should be chosen
    RoutingResult new_result = router.routeOrder(order);
    EXPECT_NE("binance", new_result.exchange_id);
}

// Test custom routing strategy
TEST_F(SmartOrderRouterTest, CustomRoutingStrategy) {
    router.setRoutingStrategy([](const OrderPtr& order, 
                                const std::vector<ExchangeInfo>& exchanges,
                                const RoutingFactors& factors) {
        // Always choose the last exchange alphabetically
        std::string last_exchange = "";
        for (const auto& exchange : exchanges) {
            if (exchange.exchange_id > last_exchange) {
                last_exchange = exchange.exchange_id;
            }
        }
        
        RoutingResult result;
        result.exchange_id = last_exchange;
        result.score = 1.0;
        result.reason = "Custom strategy: alphabetically last";
        return result;
    });
    
    OrderPtr order = createTestOrder("BTC/USDT", OrderType::LIMIT);
    RoutingResult result = router.routeOrder(order);
    
    // "ftx" is alphabetically last among our exchanges
    EXPECT_EQ("ftx", result.exchange_id);
    EXPECT_EQ("Custom strategy: alphabetically last", result.reason);
}

// Test routing when no exchanges support the pair
TEST_F(SmartOrderRouterTest, NoSupportedExchanges) {
    // Create a completely new router with empty exchanges
    SmartOrderRouter empty_router;
    
    OrderPtr order = createTestOrder("BTC/USDT", OrderType::LIMIT);
    RoutingResult result = empty_router.routeOrder(order);
    
    EXPECT_EQ("", result.exchange_id);
    EXPECT_EQ(0.0, result.score);
    EXPECT_FALSE(result.reason.empty());
}