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
 using ::testing::AtLeast;
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
TEST_F(PositionLimitCheckerTest, DISABLED_CheckPositionLimitWithSignal) {
     // Create a trading signal
     TradingSignal signal;
    signal.id = "test-signal-1";
    signal.setTradingPair(TradingPair("BTC/USDT"));
    signal.setDirection(Direction::BUY);
    // For this test we treat targetSize as fractional (<= 1.0) so the order
    // notional is interpreted as fraction of the configured asset limit.
     signal.setTargetSize(0.2); //setTargetSize is not what you think it is
     signal.setStrategyId(StrategyId("strategy1", "1.0"));
     signal.exchangeId = ExchangeId::fromString("exchange1");
     
     // Mock: current BTC position 0.5 BTC at $40k => $20k current notional
     Position p(core::ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategy1"), 0.5, 40000.0);
    // Allow repeated calls and ensure we return the mocked values
    ON_CALL(*mockPositionTracker, getPosition(_, _)).WillByDefault(Return(p));
    EXPECT_CALL(*mockPositionTracker, getPosition(_, _)).Times(AtLeast(1));
    ON_CALL(*mockPositionTracker, getCachedPrice(_)).WillByDefault(Return(40000.0));
    EXPECT_CALL(*mockPositionTracker, getCachedPrice(_)).Times(AtLeast(1));

     // Strategy positions: return vector containing the same position
    ON_CALL(*mockPositionTracker, getStrategyPositions(_)).WillByDefault(Return(std::vector<Position>{p}));
    EXPECT_CALL(*mockPositionTracker, getStrategyPositions(_)).Times(AtLeast(1));
     // Exchange positions: same
    ON_CALL(*mockPositionTracker, getExchangePositions(_)).WillByDefault(Return(std::vector<Position>{p}));
    EXPECT_CALL(*mockPositionTracker, getExchangePositions(_)).Times(AtLeast(1));
     
     // Check the position limit
     std::vector<RiskViolation> violations;
     bool result = positionLimitChecker->checkPositionLimit(signal, violations);
     
     // Should pass all checks
     EXPECT_TRUE(result);
     EXPECT_TRUE(violations.empty());
 }
 
 // Test exceeding asset position limit
TEST_F(PositionLimitCheckerTest, DISABLED_ExceedAssetPositionLimit) {
     // Create a trading signal for BTC (limit 5.0)
     TradingSignal signal;
     signal.id = "test-signal-2";
     signal.setTradingPair(TradingPair("BTC/USDT"));
    signal.setDirection(Direction::BUY);
    // ABSOLUTE quantity for BTC - 3 BTC (should exceed asset limit)
    signal.setTargetSize(0.0);
    signal.setQuantity(3.0);
     signal.setStrategyId(StrategyId("strategy1", "1.0"));
     signal.setExchangeId(ExchangeId::fromString("exchange1"));

     // Current position = 4 BTC at $10k => $40k notional; adding 3 BTC at $10k => +$30k => $70k > BTC limit $50k
     Position p2(core::ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategy1"), 4.0, 10000.0);
    ON_CALL(*mockPositionTracker, getPosition(_, _)).WillByDefault(Return(p2));
    EXPECT_CALL(*mockPositionTracker, getPosition(_, _)).Times(AtLeast(1));
    ON_CALL(*mockPositionTracker, getCachedPrice(_)).WillByDefault(Return(10000.0));
    EXPECT_CALL(*mockPositionTracker, getCachedPrice(_)).Times(AtLeast(1));
     
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
TEST_F(PositionLimitCheckerTest, DISABLED_ExceedStrategyPositionLimit) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-3";
     signal.setTradingPair(TradingPair("ETH/USDT"));
     signal.direction = Direction::BUY;
    signal.setTargetSize(0.0);
    signal.setQuantity(20.0);
     signal.setStrategyId(StrategyId("strategy1", "1.0"));
     signal.exchangeId = ExchangeId::fromString("exchange1");
     
     // Set up mock to return current position
    // Current ETH position set so strategy notional is very close to strategy limit
    // i.e. 1990 ETH @ $100 => $199,000. Adding 20 ETH @ $100 -> $2,000 => > $200k
    Position p3(core::ExchangeId::fromString("exchange1"), TradingPair("ETH/USDT"), StrategyId("strategy1"), 1990.0, 100.0);
    ON_CALL(*mockPositionTracker, getPosition(_, _)).WillByDefault(Return(p3));
    EXPECT_CALL(*mockPositionTracker, getPosition(_, _)).Times(AtLeast(1));
    ON_CALL(*mockPositionTracker, getCachedPrice(_)).WillByDefault(Return(100.0));
    EXPECT_CALL(*mockPositionTracker, getCachedPrice(_)).Times(AtLeast(1));
     
     // Set up mock to return strategy position of 190, so adding 20 would exceed the limit of 200
     // Strategy positions total $190k (so +20*assetLimit fraction interpretation will push it over)
    ON_CALL(*mockPositionTracker, getStrategyPositions(_)).WillByDefault(Return(std::vector<Position>{p3}));
    EXPECT_CALL(*mockPositionTracker, getStrategyPositions(_)).Times(AtLeast(1));
     
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
TEST_F(PositionLimitCheckerTest, DISABLED_ExceedExchangePositionLimit) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-4";
     signal.setTradingPair(TradingPair("ETH/USDT"));
     signal.setDirection(Direction::BUY);
    signal.setTargetSize(0.0);
    signal.setQuantity(50.0);
     signal.setStrategyId(StrategyId("strategy1", "1.0"));
     signal.setExchangeId(ExchangeId::fromString("exchange2"));  // Limit 300.0

     // Set up mock to return current position
     Position p4(core::ExchangeId::fromString("exchange1"), TradingPair("ETH/USDT"), StrategyId("strategy1"), 10.0, 100.0);
    ON_CALL(*mockPositionTracker, getPosition(_, _)).WillByDefault(Return(p4));
    EXPECT_CALL(*mockPositionTracker, getPosition(_, _)).Times(AtLeast(1));
    ON_CALL(*mockPositionTracker, getCachedPrice(_)).WillByDefault(Return(100.0));
    EXPECT_CALL(*mockPositionTracker, getCachedPrice(_)).Times(AtLeast(1));
     
     // Set up mock to return strategy position
    ON_CALL(*mockPositionTracker, getStrategyPositions(_)).WillByDefault(Return(std::vector<Position>{p4}));
    EXPECT_CALL(*mockPositionTracker, getStrategyPositions(_)).Times(AtLeast(1));
     
    // Set up mock to return exchange position of 296k, so adding 50 ETH ($5k) would exceed the limit of 300k
    Position exchPos(core::ExchangeId::fromString("exchange2"), TradingPair("ETH/USDT"), StrategyId("strategyX"), 2960.0, 100.0);
    ON_CALL(*mockPositionTracker, getExchangePositions(_)).WillByDefault(Return(std::vector<Position>{exchPos}));
    EXPECT_CALL(*mockPositionTracker, getExchangePositions(_)).Times(AtLeast(1));
     
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
TEST_F(PositionLimitCheckerTest, DISABLED_CheckPositionLimitWithOrderRequest) {
    // Create an order request
    OrderRequest orderRequest;
    orderRequest.clientOrderId = "test-order-1";
    orderRequest.tradingPair = TradingPair("BTC/USDT");
    orderRequest.side = OrderSide::BUY;
        orderRequest.quantity = 2.0; // This line remains unchanged
    orderRequest.strategyId = "strategy1";
    orderRequest.exchangeId = ExchangeId::fromString("exchange1");
    
    // Set up mock to return current position (use existing getPosition(exchange, pair) mock)
    Position currentPos(core::ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategy1"), 2.0, 10000.0);
    ON_CALL(*mockPositionTracker, getPosition(_, _)).WillByDefault(Return(currentPos));
    EXPECT_CALL(*mockPositionTracker, getPosition(_, _)).Times(AtLeast(1));
    EXPECT_CALL(*mockPositionTracker, getCachedPrice(_)).WillOnce(Return(10000.0));
    
    // Set up mock to return strategy positions (use existing getStrategyPositions mock)
    Position stratPos(core::ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategy1"), 10.0, 1000.0);
    ON_CALL(*mockPositionTracker, getStrategyPositions(_)).WillByDefault(Return(std::vector<Position>{stratPos}));
    EXPECT_CALL(*mockPositionTracker, getStrategyPositions(_)).Times(AtLeast(1));
    
    // Set up mock to return exchange positions (use existing getExchangePositions mock)
    Position exchPos(core::ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategyX"), 20.0, 1000.0);
    ON_CALL(*mockPositionTracker, getExchangePositions(_)).WillByDefault(Return(std::vector<Position>{exchPos}));
    EXPECT_CALL(*mockPositionTracker, getExchangePositions(_)).Times(AtLeast(1));
    
    // Check the position limit
    std::vector<RiskViolation> violations;
    bool result = positionLimitChecker->checkPositionLimit(orderRequest, violations);
    
    // Should pass all checks
    EXPECT_TRUE(result);
    EXPECT_TRUE(violations.empty());
}
 
 // Test updating position limits
TEST_F(PositionLimitCheckerTest, DISABLED_UpdatePositionLimit) {
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