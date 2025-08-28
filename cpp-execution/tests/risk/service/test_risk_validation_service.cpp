/**
 * Unit tests for RiskValidationService
 *
 * File: test_risk_validation_service.cpp
 * Created: 2025-04-30
 */

 #include "service/risk_validation_service.hpp"
 #include "risk_manager.hpp"
 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include <memory>
 #include <chrono>
 #include <thread>
 
 namespace trading {
 namespace risk {
 namespace test {
 
 // Mock RiskManager for testing
 class MockRiskManager : public RiskManager {
 public:
     explicit MockRiskManager(const RiskConfig& config) : RiskManager(config) {}
     
     MOCK_METHOD(ValidationResult, validateSignal, (const TradingSignal&), (override));
     MOCK_METHOD(RiskParameters, getRiskParameters, (const ExchangeId&, const TradingPair&, const StrategyId&), (const, override));
     MOCK_METHOD(bool, updateRiskParameters, (const RiskParameters&), (override));
     MOCK_METHOD(RiskManagerMode, getMode, (), (const, override));
     MOCK_METHOD(void, setMode, (RiskManagerMode), (override));
 };
 
 class RiskValidationServiceTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create a simple config for testing
         RiskConfig config;
         config.loadFromString("{\"validation.timeout_ms\": 100}");
         
         // Create mock risk manager
         mockRiskManager = std::make_shared<::testing::NiceMock<MockRiskManager>>(config);
         
         // Create validation service with mock risk manager
         validationService = std::make_unique<RiskValidationService>(config, mockRiskManager);
         validationService->initialize();
     }
     
     void TearDown() override {
         validationService.reset();
         mockRiskManager.reset();
     }
     
     // Helper method to create a sample trading signal
     TradingSignal createSampleSignal() {
         TradingSignal signal;
         signal.id = "test-signal-1";
         signal.exchangeId = "binance";
         signal.tradingPair = "BTC/USDT";
         signal.direction = Direction::BUY;
         signal.strength = 0.8;
         signal.targetSize = 1.0;
         signal.strategyId = "order-imbalance";
         signal.signalTtlMs = 1000;
         return signal;
     }
     
     std::unique_ptr<RiskValidationService> validationService;
     std::shared_ptr<MockRiskManager> mockRiskManager;
 };
 
 // Test successful signal validation
 TEST_F(RiskValidationServiceTest, ValidateSignalSuccess) {
     // Create a sample signal
     TradingSignal signal = createSampleSignal();
     
     // Create an expected result
     ValidationResult expectedResult;
     expectedResult.signalId = signal.id;
     expectedResult.approved = true;
     expectedResult.timestamp = 123456789;
     
     // Configure mock to return the expected result
     EXPECT_CALL(*mockRiskManager, validateSignal(::testing::_))
         .WillOnce(::testing::Return(expectedResult));
     
     // Call the service and verify the result
     ValidationResult result = validationService->validateSignal(signal);
     
     EXPECT_EQ(result.signalId, expectedResult.signalId);
     EXPECT_EQ(result.approved, expectedResult.approved);
     EXPECT_EQ(result.timestamp, expectedResult.timestamp);
 }
 
 // Test validation with violations
 TEST_F(RiskValidationServiceTest, ValidateSignalWithViolations) {
     // Create a sample signal
     TradingSignal signal = createSampleSignal();
     
     // Create an expected result with violations
     ValidationResult expectedResult;
     expectedResult.signalId = signal.id;
     expectedResult.approved = false;
     expectedResult.timestamp = 123456789;
     
     RiskViolation violation;
     violation.violationType = "POSITION_LIMIT";
     violation.description = "Position limit exceeded";
     violation.limitValue = 5.0;
     violation.actualValue = 6.0;
     expectedResult.violations.push_back(violation);
     
     // Configure mock to return the expected result
     EXPECT_CALL(*mockRiskManager, validateSignal(::testing::_))
         .WillOnce(::testing::Return(expectedResult));
     
     // Call the service and verify the result
     ValidationResult result = validationService->validateSignal(signal);
     
     EXPECT_EQ(result.signalId, expectedResult.signalId);
     EXPECT_EQ(result.approved, expectedResult.approved);
     EXPECT_EQ(result.violations.size(), 1);
     EXPECT_EQ(result.violations[0].violationType, "POSITION_LIMIT");
 }
 
 // Test validation timeout
 TEST_F(RiskValidationServiceTest, ValidateSignalTimeout) {
     // Create a sample signal
     TradingSignal signal = createSampleSignal();
     
     // Configure mock to sleep longer than the timeout
     EXPECT_CALL(*mockRiskManager, validateSignal(::testing::_))
         .WillOnce([](const TradingSignal&) {
             std::this_thread::sleep_for(std::chrono::milliseconds(200)); // Longer than timeout
             ValidationResult result;
             result.approved = true;
             return result;
         });
     
     // Call the service with a short timeout
     validationService->setValidationTimeout(50); // ms
     ValidationResult result = validationService->validateSignal(signal);
     
     // Verify the result is a timeout result
     EXPECT_FALSE(result.approved);
     EXPECT_EQ(result.violations.size(), 1);
     EXPECT_EQ(result.violations[0].violationType, "TIMEOUT");
 }
 
 // Test get risk parameters
 TEST_F(RiskValidationServiceTest, GetRiskParameters) {
     // Create expected parameters
     RiskParameters expectedParams;
     expectedParams.exchangeId = "binance";
     expectedParams.tradingPair = "BTC/USDT";
     expectedParams.maxPositionSize = 10.0;
     
     // Configure mock to return the expected parameters
     EXPECT_CALL(*mockRiskManager, getRiskParameters(::testing::_, ::testing::_, ::testing::_))
         .WillOnce(::testing::Return(expectedParams));
     
     // Call the service and verify the result
     RiskParameters params = validationService->getRiskParameters("binance", "BTC/USDT", "order-imbalance");
     
     EXPECT_EQ(params.exchangeId, expectedParams.exchangeId);
     EXPECT_EQ(params.tradingPair, expectedParams.tradingPair);
     EXPECT_EQ(params.maxPositionSize, expectedParams.maxPositionSize);
 }
 
 // Test validation callbacks
 TEST_F(RiskValidationServiceTest, ValidationCallbacks) {
     // Create a sample signal
     TradingSignal signal = createSampleSignal();
     
     // Create a result
     ValidationResult expectedResult;
     expectedResult.signalId = signal.id;
     expectedResult.approved = true;
     
     // Configure mock to return the expected result
     EXPECT_CALL(*mockRiskManager, validateSignal(::testing::_))
         .WillOnce(::testing::Return(expectedResult));
     
     // Set up callback tracking
     bool callbackCalled = false;
     ValidationResult callbackResult;
     
     // Register validation callback
     validationService->registerValidationCallback(
         [&callbackCalled, &callbackResult](const TradingSignal&, const ValidationResult& result) {
             callbackCalled = true;
             callbackResult = result;
         }
     );
     
     // Call the service
     ValidationResult result = validationService->validateSignal(signal);
     
     // Verify callback was called with correct result
     EXPECT_TRUE(callbackCalled);
     EXPECT_EQ(callbackResult.signalId, expectedResult.signalId);
     EXPECT_EQ(callbackResult.approved, expectedResult.approved);
 }
 
 } // namespace test
 } // namespace risk
 } // namespace trading
 
 // Main function to run the tests
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }