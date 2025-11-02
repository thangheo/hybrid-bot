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
 using namespace trading;
 // Mock position tracker for testing - override relevant methods
 class MockPositionTracker : public PositionTracker {
 public:
     MockPositionTracker() : PositionTracker(RiskConfig(), nullptr) {}

     MOCK_METHOD(std::optional<Position>, getPosition, (const ExchangeId&, const TradingPair&), (const));
     MOCK_METHOD(std::vector<Position>, getStrategyPositions, (const StrategyId&), (const));
     MOCK_METHOD(std::vector<Position>, getExchangePositions, (const ExchangeId&), (const));
     MOCK_METHOD(double, getCachedPrice, (const TradingPair&), (const));
 };
 
 class PositionLimitCheckerTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create a risk config with test limits
         RiskConfig config;
         PositionLimitConfig limitConfig;

         // All limits are USD notional
         limitConfig.defaultLimit = 100000.0; // $100k

         // Set specific asset limits (USD)
         limitConfig.assetLimits["BTC"] = 50000.0; // $50k
         limitConfig.assetLimits["ETH"] = 20000.0; // $20k

         // Set specific strategy limits (USD)
         limitConfig.strategyLimits["strategy1"] = 200000.0; // $200k
         limitConfig.strategyLimits["strategy2"] = 150000.0; // $150k

         // Set specific exchange limits (USD)
         limitConfig.exchangeLimits["exchange1"] = 500000.0; // $500k
         limitConfig.exchangeLimits["exchange2"] = 300000.0; // $300k
         
         config.setPositionLimitConfig(limitConfig);
         
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
     signal.setTradingPair(TradingPair("BTC/USDT"));
     signal.direction = Direction::BUY;
     signal.targetSize = 2.0;
     signal.setStrategyId(StrategyId("strategy1", "1.0"));
     signal.exchangeId = ExchangeId::fromString("exchange1");
     
     // Mock: current BTC position 0.5 BTC at $40k => $20k current notional
     Position p(core::ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategy1"), 0.5, 40000.0);
     EXPECT_CALL(*mockPositionTracker, getPosition(_, _)).WillOnce(Return(p));
     EXPECT_CALL(*mockPositionTracker, getCachedPrice(_)).WillOnce(Return(40000.0));

     // Strategy positions: return vector containing the same position
     EXPECT_CALL(*mockPositionTracker, getStrategyPositions(_)).WillOnce(Return(std::vector<Position>{p}));
     // Exchange positions: same
     EXPECT_CALL(*mockPositionTracker, getExchangePositions(_)).WillOnce(Return(std::vector<Position>{p}));
     
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
     signal.setTradingPair(TradingPair("BTC/USDT"));
     signal.setDirection(Direction::BUY);
     signal.setTargetSize(3.0);  // interpreted as absolute qty -> 3 BTC
     signal.setStrategyId(StrategyId("strategy1", "1.0"));
     signal.setExchangeId(ExchangeId::fromString("exchange1"));

     // Current position = 4 BTC at $10k => $40k notional; adding 3 BTC at $10k => +$30k => $70k > BTC limit $50k
     Position p2(core::ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategy1"), 4.0, 10000.0);
     EXPECT_CALL(*mockPositionTracker, getPosition(_, _)).WillOnce(Return(p2));
     EXPECT_CALL(*mockPositionTracker, getCachedPrice(_)).WillOnce(Return(10000.0));
     
     // Other positions don't matter since asset check will fail first
     
     // Check the position limit
     std::vector<RiskViolation> violations;
     bool result = positionLimitChecker->checkPositionLimit(signal, violations);
     
     // Should fail the asset position check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
    EXPECT_EQ(violations[0].violationType, "POSITION_LIMIT");
    EXPECT_EQ(violations[0].limitValue, 50000.0);
    // actual notional should be 70k
    EXPECT_EQ(violations[0].actualValue, 70000.0);
 }
 
 // Test exceeding strategy position limit
 TEST_F(PositionLimitCheckerTest, ExceedStrategyPositionLimit) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-3";
     signal.setTradingPair(TradingPair("ETH/USDT"));
     signal.direction = Direction::BUY;
    signal.targetSize = 20.0; // absolute qty -> 20 ETH
     signal.setStrategyId(StrategyId("strategy1", "1.0"));
     signal.exchangeId = ExchangeId::fromString("exchange1");
     
     // Set up mock to return current position
     // Current ETH position 10 ETH at $100 -> $1k
     Position p3(core::ExchangeId::fromString("exchange1"), TradingPair("ETH/USDT"), StrategyId("strategy1"), 10.0, 100.0);
     EXPECT_CALL(*mockPositionTracker, getPosition(_, _)).WillOnce(Return(p3));
     EXPECT_CALL(*mockPositionTracker, getCachedPrice(_)).WillOnce(Return(100.0));
     
     // Set up mock to return strategy position of 190, so adding 20 would exceed the limit of 200
     // Strategy positions total $190k (so +20*assetLimit fraction interpretation will push it over)
     EXPECT_CALL(*mockPositionTracker, getStrategyPositions(_)).WillOnce(Return(std::vector<Position>{p3}));
     
     // Check the position limit
     std::vector<RiskViolation> violations;
     bool result = positionLimitChecker->checkPositionLimit(signal, violations);
     
     // Should fail the strategy position check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
    EXPECT_EQ(violations[0].violationType, "POSITION_LIMIT");
    EXPECT_EQ(violations[0].limitValue, 200000.0);
 }
 
 // Test exceeding exchange position limit
 TEST_F(PositionLimitCheckerTest, ExceedExchangePositionLimit) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-4";
     signal.setTradingPair(TradingPair("ETH/USDT"));
     signal.setDirection(Direction::BUY);
     signal.setTargetSize(50.0); // 50 ETH
     signal.setStrategyId(StrategyId("strategy1", "1.0"));
     signal.setExchangeId(ExchangeId::fromString("exchange2"));  // Limit 300.0

     // Set up mock to return current position
     Position p4(core::ExchangeId::fromString("exchange1"), TradingPair("ETH/USDT"), StrategyId("strategy1"), 10.0, 100.0);
     EXPECT_CALL(*mockPositionTracker, getPosition(_, _)).WillOnce(Return(p4));
     EXPECT_CALL(*mockPositionTracker, getCachedPrice(_)).WillOnce(Return(100.0));
     
     // Set up mock to return strategy position
     EXPECT_CALL(*mockPositionTracker, getStrategyPositions(_)).WillOnce(Return(std::vector<Position>{p4}));
     
     // Set up mock to return exchange position of 260, so adding 50 would exceed the limit of 300
     // Exchange positions currently $260k
     Position exchPos(core::ExchangeId::fromString("exchange2"), TradingPair("ETH/USDT"), StrategyId("strategyX"), 2600.0, 100.0);
     EXPECT_CALL(*mockPositionTracker, getExchangePositions(_)).WillOnce(Return(std::vector<Position>{exchPos}));
     
     // Check the position limit
     std::vector<RiskViolation> violations;
     bool result = positionLimitChecker->checkPositionLimit(signal, violations);
     
     // Should fail the exchange position check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
    EXPECT_EQ(violations[0].violationType, "POSITION_LIMIT");
    EXPECT_EQ(violations[0].limitValue, 300000.0);
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
    orderRequest.exchangeId = ExchangeId::fromString("exchange1");
    
    // Set up mock to return current position (use existing getPosition(exchange, pair) mock)
    Position currentPos(core::ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategy1"), 2.0, 10000.0);
    EXPECT_CALL(*mockPositionTracker, getPosition(_, _))
        .WillOnce(Return(currentPos));
    
    // Set up mock to return strategy positions (use existing getStrategyPositions mock)
    Position stratPos(core::ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategy1"), 10.0, 1000.0);
    EXPECT_CALL(*mockPositionTracker, getStrategyPositions(_))
        .WillOnce(Return(std::vector<Position>{stratPos}));
    
    // Set up mock to return exchange positions (use existing getExchangePositions mock)
    Position exchPos(core::ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategyX"), 20.0, 1000.0);
    EXPECT_CALL(*mockPositionTracker, getExchangePositions(_))
        .WillOnce(Return(std::vector<Position>{exchPos}));
    
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
     bool result = positionLimitChecker->updatePositionLimit("BTC", core::StrategyId(), 10.0);
     EXPECT_TRUE(result);
     
     // Check that the limit was updated
     double limit = positionLimitChecker->getPositionLimit("BTC", core::StrategyId().toString());
     EXPECT_EQ(limit, 10.0);
     
     // Update position limit for a strategy
     result = positionLimitChecker->updatePositionLimit("", core::StrategyId("strategy1"), 250.0);
     EXPECT_TRUE(result);
     
     // Check that the limit was updated
     limit = positionLimitChecker->getPositionLimit("", core::StrategyId("strategy1").toString());
     EXPECT_EQ(limit, 250.0);
 }
 
 int main(int argc, char** argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }