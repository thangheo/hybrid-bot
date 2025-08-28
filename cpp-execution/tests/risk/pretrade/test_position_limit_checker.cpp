/**
 * Unit tests for position limit checker
 *
 * File: test_position_limit_checker.cpp
 * Created: 2025-05-05
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 
 #include "risk/pretrade/position_limit_checker.hpp"
 #include "risk/portfolio/position_tracker.hpp"
 #include "risk/risk_types.hpp"
 
 using namespace trading::risk;
 using ::testing::Return;
 using ::testing::_;
 
 // Mock position tracker for testing
 class MockPositionTracker : public PositionTracker {
 public:
     MockPositionTracker() : PositionTracker(RiskConfig()) {}
     
     MOCK_METHOD(double, getPosition, (const std::string&), (const));
     MOCK_METHOD(double, getTotalPositionForStrategy, (const std::string&), (const));
     MOCK_METHOD(double, getTotalPositionForExchange, (const std::string&), (const));
 };
 
 class PositionLimitCheckerTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create a risk config with test limits
         RiskConfig config;
         PositionLimitConfig limitConfig;
         
         // Set default limit
         limitConfig.defaultLimit = 100.0;
         
         // Set specific asset limits
         limitConfig.assetLimits["BTC"] = 5.0;
         limitConfig.assetLimits["ETH"] = 50.0;
         
         // Set specific strategy limits
         limitConfig.strategyLimits["strategy1"] = 200.0;
         limitConfig.strategyLimits["strategy2"] = 150.0;
         
         // Set specific exchange limits
         limitConfig.exchangeLimits["exchange1"] = 500.0;
         limitConfig.exchangeLimits["exchange2"] = 300.0;
         
         config.setPositionLimits(limitConfig);
         
         // Create mock position tracker
         mockPositionTracker = std::make_shared<MockPositionTracker>();
         
         // Create position limit checker with mock tracker
         positionLimitChecker = std::make_unique<PositionLimitChecker>(config, mockPositionTracker);
         positionLimitChecker->initialize();
     }
     
     std::shared_ptr<MockPositionTracker> mockPositionTracker;
     std::unique_ptr<PositionLimitChecker> positionLimitChecker;
 };
 
 // Test checking position limits with trading signal
 TEST_F(PositionLimitCheckerTest, CheckPositionLimitWithSignal) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-1";
     signal.tradingPair = TradingPair("BTC/USDT");
     signal.direction = Direction::BUY;
     signal.targetSize = 2.0;
     signal.strategyId = "strategy1";
     signal.exchangeId = "exchange1";
     
     // Set up mock to return current position
     EXPECT_CALL(*mockPositionTracker, getPosition("BTC"))
         .WillOnce(Return(2.0));
     
     // Set up mock to return strategy position
     EXPECT_CALL(*mockPositionTracker, getTotalPositionForStrategy("strategy1"))
         .WillOnce(Return(50.0));
     
     // Set up mock to return exchange position
     EXPECT_CALL(*mockPositionTracker, getTotalPositionForExchange("exchange1"))
         .WillOnce(Return(100.0));
     
     // Check the position limit
     std::vector<RiskViolation> violations;
     bool result = positionLimitChecker->checkPositionLimit(signal, violations);
     
     // Should pass all checks
     EXPECT_TRUE(result);
     EXPECT_TRUE(violations.empty());
 }
 
 // Test exceeding asset position limit
 TEST_F(PositionLimitCheckerTest, ExceedAssetPositionLimit) {
     // Create a trading signal for BTC (limit 5.0)
     TradingSignal signal;
     signal.id = "test-signal-2";
     signal.tradingPair = TradingPair("BTC/USDT");
     signal.direction = Direction::BUY;
     signal.targetSize = 3.0;  // New position would be 4 + 3 = 7, exceeding limit of 5
     signal.strategyId = "strategy1";
     signal.exchangeId = "exchange1";
     
     // Set up mock to return current position of 4 BTC
     EXPECT_CALL(*mockPositionTracker, getPosition("BTC"))
         .WillOnce(Return(4.0));
     
     // Other positions don't matter since asset check will fail first
     
     // Check the position limit
     std::vector<RiskViolation> violations;
     bool result = positionLimitChecker->checkPositionLimit(signal, violations);
     
     // Should fail the asset position check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     EXPECT_EQ(violations[0].violationType, "POSITION_LIMIT_EXCEEDED");
     EXPECT_EQ(violations[0].limitValue, 5.0);
     EXPECT_EQ(violations[0].actualValue, 7.0);
 }
 
 // Test exceeding strategy position limit
 TEST_F(PositionLimitCheckerTest, ExceedStrategyPositionLimit) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-3";
     signal.tradingPair = TradingPair("ETH/USDT");
     signal.direction = Direction::BUY;
     signal.targetSize = 20.0;
     signal.strategyId = "strategy1";  // Limit 200.0
     signal.exchangeId = "exchange1";
     
     // Set up mock to return current position
     EXPECT_CALL(*mockPositionTracker, getPosition("ETH"))
         .WillOnce(Return(10.0));  // Well under the ETH limit of 50
     
     // Set up mock to return strategy position of 190, so adding 20 would exceed the limit of 200
     EXPECT_CALL(*mockPositionTracker, getTotalPositionForStrategy("strategy1"))
         .WillOnce(Return(190.0));
     
     // Check the position limit
     std::vector<RiskViolation> violations;
     bool result = positionLimitChecker->checkPositionLimit(signal, violations);
     
     // Should fail the strategy position check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     EXPECT_EQ(violations[0].violationType, "STRATEGY_POSITION_LIMIT_EXCEEDED");
     EXPECT_EQ(violations[0].limitValue, 200.0);
     EXPECT_EQ(violations[0].actualValue, 210.0);
 }
 
 // Test exceeding exchange position limit
 TEST_F(PositionLimitCheckerTest, ExceedExchangePositionLimit) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-4";
     signal.tradingPair = TradingPair("ETH/USDT");
     signal.direction = Direction::BUY;
     signal.targetSize = 50.0;
     signal.strategyId = "strategy1";
     signal.exchangeId = "exchange2";  // Limit 300.0
     
     // Set up mock to return current position
     EXPECT_CALL(*mockPositionTracker, getPosition("ETH"))
         .WillOnce(Return(10.0));  // Well under the ETH limit of 50
     
     // Set up mock to return strategy position
     EXPECT_CALL(*mockPositionTracker, getTotalPositionForStrategy("strategy1"))
         .WillOnce(Return(100.0));  // Well under the strategy1 limit of 200
     
     // Set up mock to return exchange position of 260, so adding 50 would exceed the limit of 300
     EXPECT_CALL(*mockPositionTracker, getTotalPositionForExchange("exchange2"))
         .WillOnce(Return(260.0));
     
     // Check the position limit
     std::vector<RiskViolation> violations;
     bool result = positionLimitChecker->checkPositionLimit(signal, violations);
     
     // Should fail the exchange position check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     EXPECT_EQ(violations[0].violationType, "EXCHANGE_POSITION_LIMIT_EXCEEDED");
     EXPECT_EQ(violations[0].limitValue, 300.0);
     EXPECT_EQ(violations[0].actualValue, 310.0);
 }
 
 // Test with OrderRequest instead of TradingSignal
 TEST_F(PositionLimitCheckerTest, CheckPositionLimitWithOrderRequest) {
     // Create an order request
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-1";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.quantity = 2.0;
     orderRequest.strategyId = "strategy1";
     orderRequest.exchangeId = "exchange1";
     
     // Set up mock to return current position
     EXPECT_CALL(*mockPositionTracker, getPosition("BTC"))
         .WillOnce(Return(2.0));
     
     // Set up mock to return strategy position
     EXPECT_CALL(*mockPositionTracker, getTotalPositionForStrategy("strategy1"))
         .WillOnce(Return(50.0));
     
     // Set up mock to return exchange position
     EXPECT_CALL(*mockPositionTracker, getTotalPositionForExchange("exchange1"))
         .WillOnce(Return(100.0));
     
     // Check the position limit
     std::vector<RiskViolation> violations;
     bool result = positionLimitChecker->checkPositionLimit(orderRequest, violations);
     
     // Should pass all checks
     EXPECT_TRUE(result);
     EXPECT_TRUE(violations.empty());
 }
 
 // Test updating position limits
 TEST_F(PositionLimitCheckerTest, UpdatePositionLimit) {
     // Update position limit for BTC
     bool result = positionLimitChecker->updatePositionLimit("BTC", "", 10.0);
     EXPECT_TRUE(result);
     
     // Check that the limit was updated
     double limit = positionLimitChecker->getPositionLimit("BTC", "");
     EXPECT_EQ(limit, 10.0);
     
     // Update position limit for a strategy
     result = positionLimitChecker->updatePositionLimit("", "strategy1", 250.0);
     EXPECT_TRUE(result);
     
     // Check that the limit was updated
     limit = positionLimitChecker->getPositionLimit("", "strategy1");
     EXPECT_EQ(limit, 250.0);
 }
 
 int main(int argc, char** argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }