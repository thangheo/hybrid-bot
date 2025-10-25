/**
 * Unit tests for pre-trade risk filter
 *
 * File: test_pretrade_risk_filter.cpp
 * Created: 2025-05-05
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 
 #include "risk/pretrade/pre_trade_risk_filter.hpp"
 #include "risk/pretrade/position_limit_checker.hpp"
 #include "risk/pretrade/exposure_calculator.hpp"
 #include "risk/pretrade/order_validator.hpp"
 #include "risk/pretrade/fat_finger_protection.hpp"
 #include "risk/risk_types.hpp"
 
 using namespace trading::risk;
 using ::testing::Return;
 using ::testing::_;
 
 // Mock position limit checker
 class MockPositionLimitChecker : public PositionLimitChecker {
 public:
     MockPositionLimitChecker() : PositionLimitChecker(RiskConfig(), nullptr) {}
     
     MOCK_METHOD(bool, checkPositionLimit, (const TradingSignal&, std::vector<RiskViolation>&), (override));
     MOCK_METHOD(bool, checkPositionLimit, (const OrderRequest&, std::vector<RiskViolation>&), (override));
     MOCK_METHOD(bool, initialize, (), (override));
     MOCK_METHOD(double, getPositionLimit, (const std::string&, const std::string&), (const, override));
 };
 
 // Mock exposure calculator
 class MockExposureCalculator : public ExposureCalculator {
 public:
     MockExposureCalculator() : ExposureCalculator(RiskConfig(), nullptr, nullptr) {}
     
     MOCK_METHOD(bool, checkExposureLimit, (const TradingSignal&, std::vector<RiskViolation>&), (override));
     MOCK_METHOD(bool, checkExposureLimit, (const OrderRequest&, std::vector<RiskViolation>&), (override));
     MOCK_METHOD(bool, initialize, (), (override));
     MOCK_METHOD(ExposureLimitConfig, getExposureLimits, (), (const, override));
    // New mocks matching the current interface used by PreTradeRiskFilter
    MOCK_METHOD(ExposureValidationResult, validateExposure, (const TradingSignal&), (const, override));
    MOCK_METHOD(ExposureValidationResult, validateExposure, (const OrderRequest&), (const, override));
    MOCK_METHOD(std::vector<RiskViolation>, getViolationDetails, (const TradingSignal&), (const, override));
 };
 
 // Mock order validator
 class MockOrderValidator : public OrderValidator {
 public:
     MockOrderValidator() : OrderValidator(RiskConfig(), nullptr) {}
     
     MOCK_METHOD(bool, validateOrder, (const TradingSignal&, std::vector<RiskViolation>&), (override));
     MOCK_METHOD(bool, validateOrder, (const OrderRequest&, std::vector<RiskViolation>&), (override));
     MOCK_METHOD(bool, initialize, (), (override));
 };
 
 // Mock fat finger protection
 class MockFatFingerProtection : public FatFingerProtection {
 public:
     MockFatFingerProtection() : FatFingerProtection(RiskConfig(), nullptr) {}
     
     MOCK_METHOD(bool, checkFatFinger, (const TradingSignal&, std::vector<RiskViolation>&), (override));
     MOCK_METHOD(bool, checkFatFinger, (const OrderRequest&, std::vector<RiskViolation>&), (override));
     MOCK_METHOD(bool, initialize, (), (override));
 };
 
 class PreTradeRiskFilterTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create a risk config
         RiskConfig config;
         
         // Create the pre-trade risk filter
         riskFilter = std::make_unique<PreTradeRiskFilter>(config);
         
         // Create and inject mocks
         mockPositionLimitChecker = new MockPositionLimitChecker();
         mockExposureCalculator = new MockExposureCalculator();
         mockOrderValidator = new MockOrderValidator();
         mockFatFingerProtection = new MockFatFingerProtection();
         
         // Inject the mocks into the risk filter
         riskFilter->m_positionLimitChecker.reset(mockPositionLimitChecker);
         riskFilter->m_exposureCalculator.reset(mockExposureCalculator);
         riskFilter->m_orderValidator.reset(mockOrderValidator);
         riskFilter->m_fatFingerProtection.reset(mockFatFingerProtection);
         
         // Set up mocks to initialize successfully
         EXPECT_CALL(*mockPositionLimitChecker, initialize())
             .WillOnce(Return(true));
         EXPECT_CALL(*mockExposureCalculator, initialize())
             .WillOnce(Return(true));
         EXPECT_CALL(*mockOrderValidator, initialize())
             .WillOnce(Return(true));
         EXPECT_CALL(*mockFatFingerProtection, initialize())
             .WillOnce(Return(true));
         
         // Initialize the risk filter
         riskFilter->initialize();
     }
     
     std::unique_ptr<PreTradeRiskFilter> riskFilter;
     MockPositionLimitChecker* mockPositionLimitChecker;
     MockExposureCalculator* mockExposureCalculator;
     MockOrderValidator* mockOrderValidator;
     MockFatFingerProtection* mockFatFingerProtection;
 };
 
 // Test validating a signal that passes all checks
 TEST_F(PreTradeRiskFilterTest, ValidateSignalAllPass) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-1";
     signal.tradingPair = TradingPair("BTC/USDT");
     signal.direction = Direction::BUY;
     signal.targetSize = 0.1;
     signal.exchangeId = "exchange1";
     signal.strategyId = "strategy1";
     
     // Set up mocks to pass all checks
     EXPECT_CALL(*mockPositionLimitChecker, checkPositionLimit(_, _))
         .WillOnce(Return(true));
     EXPECT_CALL(*mockExposureCalculator, checkExposureLimit(_, _))
         .WillOnce(Return(true));
     EXPECT_CALL(*mockOrderValidator, validateOrder(_, _))
         .WillOnce(Return(true));
     EXPECT_CALL(*mockFatFingerProtection, checkFatFinger(_, _))
         .WillOnce(Return(true));
     
     // Validate the signal
     ValidationResult result = riskFilter->validateSignal(signal);
     
     // Should be approved
     EXPECT_TRUE(result.approved);
     EXPECT_TRUE(result.violations.empty());
     EXPECT_EQ(result.signalId, signal.id);
     EXPECT_EQ(result.adjustedSize, signal.targetSize);
 }
 
 // Test validating a signal that fails position limit check
 TEST_F(PreTradeRiskFilterTest, ValidateSignalFailPositionLimit) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-2";
     signal.tradingPair = TradingPair("BTC/USDT");
     signal.direction = Direction::BUY;
     signal.targetSize = 0.1;
     signal.exchangeId = "exchange1";
     signal.strategyId = "strategy1";
     
     // Set up mock to fail position limit check
     EXPECT_CALL(*mockPositionLimitChecker, checkPositionLimit(_, _))
         .WillOnce([](const TradingSignal& signal, std::vector<RiskViolation>& violations) {
             RiskViolation violation;
             violation.violationType = "POSITION_LIMIT_EXCEEDED";
             violation.description = "Asset position limit exceeded";
             violation.limitValue = 5.0;
             violation.actualValue = 6.0;
             violations.push_back(violation);
             return false;
         });
     
     // Other checks would pass if called
     EXPECT_CALL(*mockExposureCalculator, checkExposureLimit(_, _))
         .WillOnce(Return(true));
     EXPECT_CALL(*mockOrderValidator, validateOrder(_, _))
         .WillOnce(Return(true));
     EXPECT_CALL(*mockFatFingerProtection, checkFatFinger(_, _))
         .WillOnce(Return(true));
     
     // Validate the signal
     ValidationResult result = riskFilter->validateSignal(signal);
     
     // Should be rejected
     EXPECT_FALSE(result.approved);
     EXPECT_EQ(result.violations.size(), 1);
     EXPECT_EQ(result.violations[0].violationType, "POSITION_LIMIT_EXCEEDED");
     EXPECT_EQ(result.signalId, signal.id);
 }
 
 // Test validating a signal in emergency mode
 TEST_F(PreTradeRiskFilterTest, ValidateSignalInEmergencyMode) {
     // Set risk filter to emergency mode
     riskFilter->setMode(RiskManagerMode::EMERGENCY);
     
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-3";
     signal.tradingPair = TradingPair("BTC/USDT");
     signal.direction = Direction::BUY;
     signal.targetSize = 0.1;
     signal.exchangeId = "exchange1";
     signal.strategyId = "strategy1";
     
     // No checks should be called in emergency mode
     EXPECT_CALL(*mockPositionLimitChecker, checkPositionLimit(_, _))
         .Times(0);
     EXPECT_CALL(*mockExposureCalculator, checkExposureLimit(_, _))
         .Times(0);
     EXPECT_CALL(*mockOrderValidator, validateOrder(_, _))
         .Times(0);
     EXPECT_CALL(*mockFatFingerProtection, checkFatFinger(_, _))
         .Times(0);
     
     // Validate the signal
     ValidationResult result = riskFilter->validateSignal(signal);
     
     // Should be rejected due to emergency mode
     EXPECT_FALSE(result.approved);
     EXPECT_EQ(result.violations.size(), 1);
     EXPECT_EQ(result.violations[0].violationType, "EMERGENCY_MODE_ACTIVE");
     EXPECT_EQ(result.signalId, signal.id);
 }
 
 // Test validating an order that passes all checks
//  TEST_F(PreTradeRiskFilterTest, ValidateOrderAllPass) {
//      // Set risk filter to normal mode
//      riskFilter->setMode(RiskManagerMode::NORMAL);
     
//      // Create an order request
//      OrderRequest orderRequest;
//      orderRequest.clientOrderId = "test-order-1";
//      orderRequest.tradingPair = TradingPair("BTC/USDT");
//      orderRequest.side = OrderSide::BUY;
//      orderRequest.type = OrderType::LIMIT;
//      orderRequest.quantity = 0.1;
//      orderRequest.price = 50000.0;
//      orderRequest.exchangeId = "exchange1";
//      orderRequest.strategyId = "strategy1";
     
//      // Set up mocks to pass all checks
//      EXPECT_CALL(*mockPositionLimitChecker, checkPosit
// Test validating an order that passes all checks
TEST_F(PreTradeRiskFilterTest, ValidateOrderAllPass) {
    // Set risk filter to normal mode
    riskFilter->setMode(RiskManagerMode::NORMAL);
    
    // Create an order request
    OrderRequest orderRequest;
    orderRequest.clientOrderId = "test-order-1";
    orderRequest.tradingPair = TradingPair("BTC/USDT");
    orderRequest.side = OrderSide::BUY;
    orderRequest.type = OrderType::LIMIT;
    orderRequest.quantity = 0.1;
    orderRequest.price = 50000.0;
    orderRequest.exchangeId = "exchange1";
    orderRequest.strategyId = "strategy1";
    
    // Set up mocks to pass all checks
    EXPECT_CALL(*mockPositionLimitChecker, checkPositionLimit(_, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockExposureCalculator, checkExposureLimit(_, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockOrderValidator, validateOrder(_, _))
        .WillOnce(Return(true));
    EXPECT_CALL(*mockFatFingerProtection, checkFatFinger(_, _))
        .WillOnce(Return(true));
    
    // Validate the order
    ValidationResult result = riskFilter->validateOrder(orderRequest);
    
    // Should be approved
    EXPECT_TRUE(result.approved);
    EXPECT_TRUE(result.violations.empty());
    EXPECT_EQ(result.signalId, orderRequest.clientOrderId);
    EXPECT_EQ(result.adjustedSize, orderRequest.quantity);
}

// Test validating an order that fails multiple checks
TEST_F(PreTradeRiskFilterTest, ValidateOrderMultipleFailures) {
    // Create an order request
    OrderRequest orderRequest;
    orderRequest.clientOrderId = "test-order-2";
    orderRequest.tradingPair = TradingPair("BTC/USDT");
    orderRequest.side = OrderSide::BUY;
    orderRequest.type = OrderType::LIMIT;
    orderRequest.quantity = 10.0;  // Very large
    orderRequest.price = 40000.0;  // Far from market
    orderRequest.exchangeId = "exchange1";
    orderRequest.strategyId = "strategy1";
    
    // Set up mocks to fail exposure and fat finger checks
    EXPECT_CALL(*mockPositionLimitChecker, checkPositionLimit(_, _))
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mockExposureCalculator, checkExposureLimit(_, _))
        .WillOnce([](const OrderRequest& order, std::vector<RiskViolation>& violations) {
            RiskViolation violation;
            violation.violationType = "EXCHANGE_EXPOSURE_LIMIT_EXCEEDED";
            violation.description = "Exchange exposure limit exceeded";
            violation.limitValue = 500000.0;
            violation.actualValue = 600000.0;
            violations.push_back(violation);
            return false;
        });
    
    EXPECT_CALL(*mockOrderValidator, validateOrder(_, _))
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mockFatFingerProtection, checkFatFinger(_, _))
        .WillOnce([](const OrderRequest& order, std::vector<RiskViolation>& violations) {
            RiskViolation violation;
            violation.violationType = "PRICE_DEVIATION_TOO_LARGE";
            violation.description = "Price deviates significantly from market";
            violation.limitValue = 10.0;
            violation.actualValue = 20.0;
            violations.push_back(violation);
            return false;
        });
    
    // Validate the order
    ValidationResult result = riskFilter->validateOrder(orderRequest);
    
    // Should be rejected with multiple violations
    EXPECT_FALSE(result.approved);
    EXPECT_EQ(result.violations.size(), 2);
    EXPECT_EQ(result.signalId, orderRequest.clientOrderId);
}

// Test with invalid order request (basic validation failure)
TEST_F(PreTradeRiskFilterTest, InvalidOrderRequest) {
    // Create an invalid order request (missing client order ID)
    OrderRequest orderRequest;
    // clientOrderId is intentionally left empty
    orderRequest.tradingPair = TradingPair("BTC/USDT");
    orderRequest.side = OrderSide::BUY;
    orderRequest.type = OrderType::LIMIT;
    orderRequest.quantity = 0.1;
    orderRequest.price = 50000.0;
    orderRequest.exchangeId = "exchange1";
    
    // No component checks should be called for invalid order
    EXPECT_CALL(*mockPositionLimitChecker, checkPositionLimit(_, _))
        .Times(0);
    EXPECT_CALL(*mockExposureCalculator, checkExposureLimit(_, _))
        .Times(0);
    EXPECT_CALL(*mockOrderValidator, validateOrder(_, _))
        .Times(0);
    EXPECT_CALL(*mockFatFingerProtection, checkFatFinger(_, _))
        .Times(0);
    
    // Validate the order
    ValidationResult result = riskFilter->validateOrder(orderRequest);
    
    // Should be rejected due to basic validation failure
    EXPECT_FALSE(result.approved);
    EXPECT_EQ(result.violations.size(), 1);
    EXPECT_EQ(result.violations[0].violationType, "MISSING_REQUIRED_FIELD");
}

// Test updating risk parameters
TEST_F(PreTradeRiskFilterTest, UpdateRiskParameters) {
    // Create risk parameters to update
    RiskParameters params;
    params.exchangeId = "exchange1";
    params.tradingPair = TradingPair("BTC/USDT");
    params.strategyId = "strategy1";
    params.maxPositionSize = 10.0;
    params.maxExposure = 500000.0;
    params.maxOrderSize = 1.0;
    params.minOrderSize = 0.001;
    
    // Set up expectations for updating parameters
    EXPECT_CALL(*mockPositionLimitChecker, updatePositionLimit("BTC", "strategy1", 10.0))
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mockExposureCalculator, getExposureLimits())
        .WillOnce([]() {
            ExposureLimitConfig config;
            config.totalExposureLimit = 1000000.0;
            config.defaultAssetExposureLimit = 100000.0;
            config.defaultStrategyExposureLimit = 200000.0;
            config.defaultExchangeExposureLimit = 500000.0;
            return config;
        });
    
    EXPECT_CALL(*mockExposureCalculator, updateExposureLimits(_))
        .WillOnce(Return(true));
    
    EXPECT_CALL(*mockOrderValidator, updateMinOrderSize("exchange1", _, 0.001))
        .WillOnce(Return(true));
    
    // Update parameters
    bool result = riskFilter->updateRiskParameters(params);
    
    // Should be successful
    EXPECT_TRUE(result);
}

// Test getting risk parameters
TEST_F(PreTradeRiskFilterTest, GetRiskParameters) {
    // Set up expectations for getting parameters
    EXPECT_CALL(*mockPositionLimitChecker, getPositionLimit("BTC", "strategy1"))
        .WillOnce(Return(5.0));
    
    EXPECT_CALL(*mockExposureCalculator, getExposureLimits())
        .WillOnce([]() {
            ExposureLimitConfig config;
            config.totalExposureLimit = 1000000.0;
            config.defaultAssetExposureLimit = 100000.0;
            config.defaultStrategyExposureLimit = 200000.0;
            config.defaultExchangeExposureLimit = 500000.0;
            config.strategyExposureLimits["strategy1"] = 300000.0;
            return config;
        });
    
    // Get parameters
    RiskParameters params = riskFilter->getRiskParameters(
        "exchange1", TradingPair("BTC/USDT"), "strategy1");
    
    // Check returned parameters
    EXPECT_EQ(params.exchangeId, "exchange1");
    EXPECT_EQ(params.tradingPair.getSymbol(), "BTC/USDT");
    EXPECT_EQ(params.strategyId, "strategy1");
    EXPECT_EQ(params.maxPositionSize, 5.0);
    EXPECT_EQ(params.maxExposure, 300000.0);
}

// Test changing risk manager mode
TEST_F(PreTradeRiskFilterTest, ChangeRiskManagerMode) {
    // Initial mode should be NORMAL
    EXPECT_EQ(riskFilter->getMode(), RiskManagerMode::NORMAL);
    
    // Change to HEIGHTENED mode
    riskFilter->setMode(RiskManagerMode::HEIGHTENED);
    EXPECT_EQ(riskFilter->getMode(), RiskManagerMode::HEIGHTENED);
    
    // Change to EMERGENCY mode
    riskFilter->setMode(RiskManagerMode::EMERGENCY);
    EXPECT_EQ(riskFilter->getMode(), RiskManagerMode::EMERGENCY);
    
    // Change back to NORMAL mode
    riskFilter->setMode(RiskManagerMode::NORMAL);
    EXPECT_EQ(riskFilter->getMode(), RiskManagerMode::NORMAL);
}

// Test SELL capping to available balance (no shorting)
TEST_F(PreTradeRiskFilterTest, SellCappedToAvailableBalance) {
    // Prepare a SELL signal larger than available position
    TradingSignal signal;
    signal.id = "sell-cap-1";
    signal.setTradingPair("BTC/USDT");
    signal.direction = Direction::SELL;
    signal.targetSize = 5.0; // request to sell 5
    signal.exchangeId = "exchange1";
    signal.strategyId = "strategy1";

    // Exposure validator will return not approved
    EXPECT_CALL(*mockExposureCalculator, validateExposure(_))
        .WillOnce(Return(ExposureValidationResult::TOTAL_LIMIT_EXCEEDED));

    // Provide some existing position via position tracker mock by replacing it
    // Create a mock position tracker with a single long position of 2.0
    // We'll set the position tracker directly on the filter
    // Use existing Position class to craft a Position
    Position pos("exchange1", TradingPair("BTC/USDT"), StrategyId("strategy1"), 2.0, 50000.0);
    // Replace position tracker with a mock that returns the pos
    // Create a small lambda-backed mock via existing MockPositionTracker in tests/mocks
    using risk::test::MockPositionTracker;
    auto *mockPosTracker = new risk::test::MockPositionTracker();
    EXPECT_CALL(*mockPosTracker, getPosition(_, _))
        .WillOnce(Return(std::optional<Position>(pos)));
    // Other methods can be no-ops
    EXPECT_CALL(*mockPosTracker, initialize()).WillOnce(Return(true)).RetiresOnSaturation();

    riskFilter->m_positionTracker.reset(mockPosTracker);

    // The rest of checks (order validator, fat finger) won't be reached after exposure handling
    EXPECT_CALL(*mockOrderValidator, validateOrder(_, _)).Times(AtMost(1));
    EXPECT_CALL(*mockFatFingerProtection, checkFatFinger(_, _)).Times(AtMost(1));

    // Validate
    ValidationResult res = riskFilter->validateSignal(signal);

    // Expect approved and adjusted size capped to available (2.0)
    EXPECT_TRUE(res.approved);
    EXPECT_DOUBLE_EQ(res.adjustedSize, 2.0);
}

// Test SELL rejected when no available balance
TEST_F(PreTradeRiskFilterTest, SellRejectedWhenNoBalance) {
    TradingSignal signal;
    signal.id = "sell-cap-2";
    signal.setTradingPair("BTC/USDT");
    signal.direction = Direction::SELL;
    signal.targetSize = 1.0; // request to sell 1
    signal.exchangeId = "exchange1";
    signal.strategyId = "strategy1";

    EXPECT_CALL(*mockExposureCalculator, validateExposure(_))
        .WillOnce(Return(ExposureValidationResult::TOTAL_LIMIT_EXCEEDED));

    // Position tracker returns no position
    using risk::test::MockPositionTracker;
    auto *mockPosTracker = new risk::test::MockPositionTracker();
    EXPECT_CALL(*mockPosTracker, getPosition(_, _))
        .WillOnce(Return(std::optional<Position>()));
    EXPECT_CALL(*mockPosTracker, initialize()).WillOnce(Return(true)).RetiresOnSaturation();
    riskFilter->m_positionTracker.reset(mockPosTracker);

    ValidationResult res = riskFilter->validateSignal(signal);

    // Expect rejected (no available balance to sell)
    EXPECT_FALSE(res.approved);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}