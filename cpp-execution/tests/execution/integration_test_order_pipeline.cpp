#include "execution/order_generator.hpp"
#include "execution/order_type_selector.hpp"
#include "execution/smart_order_router.hpp"
#include <gtest/gtest.h>
#include <memory>

        // using namespace trading::execution;
        // using namespace trading::execution::models;

 using namespace trading::execution;
 using namespace testing;
 using namespace trading::execution::models;
 using namespace trading::core;


class OrderPipelineIntegrationTest : public ::testing::Test {
protected:
    OrderGenerator generator;
    OrderTypeSelector selector;
    SmartOrderRouter router;
    
    void SetUp() override {
        // Initialize router with exchange information
        // struct ExchangeInfo {
        // core::ExchangeId exchangeId;
        // double trading_fee;
        // double liquidity_score;
        // double latency_ms; 
        // double success_rate;
        std::vector<ExchangeInfo> exchanges = {
            ExchangeInfo(ExchangeId::binance(), 0.001, 0.9, 50.0, 0.99),
            ExchangeInfo(ExchangeId::coinbase(), 0.005, 0.8, 70.0, 0.98),
            ExchangeInfo(ExchangeId::ftx(), 0.002, 0.85, 60.0, 0.97)
        };
        router.initialize(exchanges);
        
        // Set up market conditions
        MarketCondition btc_market = {
            55000.0,  // bid_price
            55050.0,  // ask_price
            50.0,     // spread
            0.01,     // volatility
            100.0,    // volume
            0.85,     // liquidity_score
            0.002     // market_impact_estimate
        };
        
        MarketCondition eth_market = {
            3500.0,   // bid_price
            3505.0,   // ask_price
            5.0,      // spread
            0.015,    // volatility
            200.0,    // volume
            0.9,      // liquidity_score
            0.001     // market_impact_estimate
        };
        
        selector.updateMarketCondition(core::Symbol::fromString("BTC/USDT"), btc_market);
        selector.updateMarketCondition(core::Symbol::fromString("ETH/USDT"), eth_market);
    }
    
    // Process an order through the entire pipeline
    OrderPtr processThroughPipeline(const OrderGenerationParams& params) {
        // Generate the order
        OrderPtr order = generator.createOrder(params);
        
        // Get market condition
        MarketCondition market = selector.getMarketCondition(params.tradingPair);
        
        // Select optimal order type
        OrderTypeSelection type_selection = selector.selectOrderType(order, market);
        
        // Apply the selection to the order
        order->setType(type_selection.type);
        if (type_selection.price > 0) {
            order->setPrice(type_selection.price);
        }
        order->setTimeInForce(type_selection.time_in_force);
        
        // Route the order to the best exchange
        RoutingResult routing = router.routeOrder(order);
        
        // Apply routing result if successful
        if (!routing.exchangeId.toString().empty()) {
            order->setExchangeId(routing.exchangeId);
        }
        
        return order;
    }
};

// Test complete pipeline for a market buy order
// TEST_F(OrderPipelineIntegrationTest, MarketBuyOrderPipeline) {
//     OrderGenerationParams params;
//     params.tradingPair = core::Symbol::fromString("BTC/USDT");
//     params.side = OrderSide::BUY;
//     params.type = OrderType::MARKET;
//     params.quantity = 0.1;
//     params.strategy_id = "test_strategy";
    
//     OrderPtr final_order = processThroughPipeline(params);
    
//     // Verify order properties
//     EXPECT_EQ("BTC/USDT", final_order->getTradingPair());
//     EXPECT_EQ(0.1, final_order->getQuantity());
    
//     // Check if order type was maintained or changed based on market conditions
//     // (This will depend on the thresholds and market conditions)
    
//     // Verify the order has an exchange assigned
//     EXPECT_FALSE(final_order->getExchangeId().empty());
// }

// Test complete pipeline for a limit sell order
TEST_F(OrderPipelineIntegrationTest, LimitSellOrderPipeline) {
    OrderGenerationParams params;
    params.tradingPair = core::Symbol::fromString("ETH/USDT");
    params.side = OrderSide::SELL;
    params.type = OrderType::LIMIT;
    params.quantity = 1.0;
    params.price = 3550.0;  // Above current ask
    params.strategy_id = core::StrategyId("test_strategy");
    
    OrderPtr final_order = processThroughPipeline(params);
    
    // Verify order properties
    EXPECT_EQ("ETH/USDT", final_order->getTradingPair().toString());
    EXPECT_EQ(OrderSide::SELL, final_order->getSide());
    EXPECT_EQ(1.0, final_order->getQuantity());
    
    // Price may have been adjusted by OrderTypeSelector
    // Check if the order has an exchange assigned
    EXPECT_FALSE(final_order->getExchangeId().toString().empty());
}

// Test pipeline with custom behavior at each stage
TEST_F(OrderPipelineIntegrationTest, CustomizedPipeline) {
    // Set custom parameter transformer
    generator.setParameterTransformer([](OrderGenerationParams& params) {
        // Add a signal ID based on trading pair
        params.signal_id = core::SignalId("SIG-" + params.tradingPair.toString());
    });
    
    // Set custom selection strategy
    selector.setSelectionStrategy([](const OrderPtr& order, const MarketCondition& mc) {
        // Always use limit orders with aggressive prices for quick fills
        OrderTypeSelection selection;
        selection.type = OrderType::LIMIT;
        if (order->getSide() == OrderSide::BUY) {
            selection.price = mc.askPrice * 1.001;  // Slightly above ask
        } else {
            selection.price = mc.bidPrice * 0.999;  // Slightly below bid
        }
        selection.time_in_force = TimeInForce::IOC;  // Immediate execution
        selection.reason = "Custom strategy: aggressive limit order";
        return selection;
    });
    
    // Set custom routing strategy
    router.setRoutingStrategy([](const OrderPtr& order, 
                               const std::vector<ExchangeInfo>& exchanges,
                               const RoutingFactors& factors) {
        // Select exchange with lowest latency
        std::string best_exchange;
        double lowest_latency = std::numeric_limits<double>::max();
        
        for (const auto& exchange : exchanges) {
            if (exchange.latency_ms < lowest_latency) {
                lowest_latency = exchange.latency_ms;
                best_exchange = exchange.exchangeId.toString();
            }
        }
        
        RoutingResult result;
        result.exchangeId = ExchangeId::fromString(best_exchange);
        result.score = 1.0;
        result.reason = "Custom strategy: lowest latency";
        return result;
    });
    
    // Process an order through the customized pipeline
    OrderGenerationParams params;
    params.tradingPair = core::Symbol::fromString("BTC/USDT");
    params.side = OrderSide::BUY;
    params.type = OrderType::MARKET;  // Should be converted to limit
    params.quantity = 0.2;
    params.strategy_id = core::StrategyId("custom_strategy");
    
    OrderPtr final_order = processThroughPipeline(params);
    
    // Verify customizations were applied
    EXPECT_EQ("SIG-BTC/USDT", final_order->getSignalId());  // From parameter transformer
    EXPECT_EQ(OrderType::LIMIT, final_order->getType());    // From selection strategy
    EXPECT_GT(final_order->getPrice(), 0.0);                // Price should be set
    EXPECT_EQ(TimeInForce::IOC, final_order->getTimeInForce());  // From selection strategy
    EXPECT_EQ("binance", final_order->getExchangeId().toString());     // Binance has lowest latency in our setup
}

// Test error handling in the pipeline
TEST_F(OrderPipelineIntegrationTest, ErrorHandlingInPipeline) {
    // Invalid order params (zero quantity)
    OrderGenerationParams invalid_params;
    invalid_params.tradingPair = core::Symbol::fromString("BTC/USDT");
    invalid_params.side = OrderSide::BUY;
    invalid_params.quantity = 0.0;  // Invalid
    
    EXPECT_THROW(processThroughPipeline(invalid_params), std::invalid_argument);
    
    // Unknown trading pair
    OrderGenerationParams unknown_pair;
    unknown_pair.tradingPair = core::Symbol::fromString("UNKNOWN/USDT");
    unknown_pair.side = OrderSide::BUY;
    unknown_pair.quantity = 1.0;
    
    // This should not throw, but will use default market conditions
    OrderPtr order = processThroughPipeline(unknown_pair);
    EXPECT_EQ("UNKNOWN/USDT", order->getTradingPair().toString());
    EXPECT_FALSE(order->getExchangeId().toString().empty());  // Should still be routed
}