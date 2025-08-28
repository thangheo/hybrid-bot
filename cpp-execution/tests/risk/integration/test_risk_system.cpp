#include <gtest/gtest.h>
#include "risk/risk_manager.hpp"
#include "risk/pretrade/pre_trade_risk_filter.hpp"
#include "risk/pretrade/position_limit_checker.hpp"
#include "risk/pretrade/exposure_calculator.hpp"
#include "risk/pretrade/order_validator.hpp"
#include "risk/pretrade/fat_finger_protection.hpp"
#include "risk/portfolio/position_tracker.hpp"
#include "risk/portfolio/portfolio_manager.hpp"
#include "risk/utils/limit_manager.hpp"
#include <memory>

class RiskSystemIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up real components
        limitManager = std::make_shared<LimitManager>();
        limitManager->setAssetLimit("BTC-USDT", 2.0);
        limitManager->setAssetLimit("ETH-USDT", 20.0);
        limitManager->setStrategyLimit("strategy1", 100000.0);
        limitManager->setExchangeLimit("binance", 200000.0);
        
        positionTracker = std::make_shared<PositionTracker>();
        positionTracker->updatePosition("binance", "BTC-USDT", "strategy1", 1.0, 45000.0);
        
        portfolioManager = std::make_shared<PortfolioManager>(positionTracker);
        
        // Set up pre-trade risk components
        positionLimitChecker = std::make_shared<PositionLimitChecker>(positionTracker, limitManager);
        exposureCalculator = std::make_shared<ExposureCalculator>(positionTracker, limitManager);
        orderValidator = std::make_shared<OrderValidator>();
        fatFingerProtection = std::make_shared<FatFingerProtection>();
        
        // Set current prices for exposure calculation
        std::map<std::string, double> currentPrices = {
            {"BTC-USDT", 50000.0},
            {"ETH-USDT", 3000.0}
        };
        positionLimitChecker->setCurrentPrices(currentPrices);
        exposureCalculator->setCurrentPrices(currentPrices);
        
        // Create pre-trade risk filter with all components
        preTradeFilter = std::make_shared<PreTradeRiskFilter>(
            positionLimitChecker,
            exposureCalculator,
            orderValidator,
            fatFingerProtection
        );
        
        // Create post-trade monitor
        postTradeMonitor = std::make_shared<PostTradeMonitor>();
        
        // Create risk manager with all components
        riskManager = std::make_unique<RiskManager>(
            preTradeFilter,
            postTradeMonitor,
            portfolioManager
        );
    }

    std::shared_ptr<LimitManager> limitManager;
    std::shared_ptr<PositionTracker> positionTracker;
    std::shared_ptr<PortfolioManager> portfolioManager;
    std::shared_ptr<PositionLimitChecker> positionLimitChecker;
    std::shared_ptr<ExposureCalculator> exposureCalculator;
    std::shared_ptr<OrderValidator> orderValidator;
    std::shared_ptr<FatFingerProtection> fatFingerProtection;
    std::shared_ptr<PreTradeRiskFilter> preTradeFilter;
    std::shared_ptr<PostTradeMonitor> postTradeMonitor;
    std::unique_ptr<RiskManager> riskManager;
};

TEST_F(RiskSystemIntegrationTest, CompleteOrderFlowTest) {
    // 1. Create and validate a signal
    TradingSignal signal;
    signal.id = "signal1";
    signal.exchangeId = "binance";
    signal.tradingPair = "BTC-USDT";
    signal.strategy = "strategy1";
    signal.direction = "buy";
    signal.strength = 0.7;
    signal.targetSize = 0.5;
    
    ValidationResult signalResult = riskManager->validateSignal(signal);
    EXPECT_TRUE(signalResult.approved);
    
    // 2. Create and validate an order
    OrderRequest order;
    order.orderId = "order1";
    order.exchangeId = "binance";
    order.tradingPair = "BTC-USDT";
    order.strategy = "strategy1";
    order.side = OrderSide::BUY;
    order.quantity = 0.5;
    order.price = 50000.0;
    order.signalId = "signal1";
    
    ValidationResult orderResult = riskManager->validateOrder(order);
    EXPECT_TRUE(orderResult.approved);
    
    // 3. Process a partial fill
    ExecutionReport partialFill;
    partialFill.orderId = "order1";
    partialFill.exchangeId = "binance";
    partialFill.tradingPair = "BTC-USDT";
    partialFill.strategyId = "strategy1";
    partialFill.side = OrderSide::BUY;
    partialFill.quantity = 0.2;
    partialFill.filledQuantity = 0.2;
    partialFill.price = 50000.0;
    partialFill.status = OrderStatus::PARTIALLY_FILLED;
    
    riskManager->processExecution(partialFill);
    
    // 4. Check position after partial fill
    Position positionAfterPartial = riskManager->getPosition("binance", "BTC-USDT", "strategy1");
    EXPECT_DOUBLE_EQ(positionAfterPartial.quantity, 1.2); // 1.0 initial + 0.2 filled
    
    // 5. Complete the fill
    ExecutionReport completeFill;
    completeFill.orderId = "order1";
    completeFill.exchangeId = "binance";
    completeFill.tradingPair = "BTC-USDT";
    completeFill.strategyId = "strategy1";
    completeFill.side = OrderSide::BUY;
    completeFill.quantity = 0.5;
    completeFill.filledQuantity = 0.5;
    completeFill.price = 50000.0;
    completeFill.status = OrderStatus::FILLED;
    
    riskManager->processExecution(completeFill);
    
    // 6. Check position after complete fill
    Position positionAfterComplete = riskManager->getPosition("binance", "BTC-USDT", "strategy1");
    EXPECT_DOUBLE_EQ(positionAfterComplete.quantity, 1.5); // 1.0 initial + 0.5 filled
    
    // 7. Try to place an order that would exceed position limit
    OrderRequest largeOrder;
    largeOrder.orderId = "order2";
    largeOrder.exchangeId = "binance";
    largeOrder.tradingPair = "BTC-USDT";
    largeOrder.strategy = "strategy1";
    largeOrder.side = OrderSide::BUY;
    largeOrder.quantity = 1.0; // Would bring position to 2.5, exceeding 2.0 limit
    largeOrder.price = 50000.0;
    
    ValidationResult largeOrderResult = riskManager->validateOrder(largeOrder);
    EXPECT_FALSE(largeOrderResult.approved);
    
    // 8. Place a sell order
    OrderRequest sellOrder;
    sellOrder.orderId = "order3";
    sellOrder.exchangeId = "binance";
    sellOrder.tradingPair = "BTC-USDT";
    sellOrder.strategy = "strategy1";
    sellOrder.side = OrderSide::SELL;
    sellOrder.quantity = 0.5;
    sellOrder.price = 50000.0;
    
    ValidationResult sellOrderResult = riskManager->validateOrder(sellOrder);
    EXPECT_TRUE(sellOrderResult.approved);
    
    // 9. Process sell order fill
    ExecutionReport sellFill;
    sellFill.orderId = "order3";
    sellFill.exchangeId = "binance";
    sellFill.tradingPair = "BTC-USDT";
    sellFill.strategyId = "strategy1";
    sellFill.side = OrderSide::SELL;
    sellFill.quantity = 0.5;
    sellFill.filledQuantity = 0.5;
    sellFill.price = 50000.0;
    sellFill.status = OrderStatus::FILLED;
    
    riskManager->processExecution(sellFill);
    
    // 10. Check final position
    Position finalPosition = riskManager->getPosition("binance", "BTC-USDT", "strategy1");
    EXPECT_DOUBLE_EQ(finalPosition.quantity, 1.0); // Back to initial position
}