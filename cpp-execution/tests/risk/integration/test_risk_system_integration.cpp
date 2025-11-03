/**
 * Integration tests for the Risk Management System
 *
 * File: test_risk_system_integration.cpp
 * Created: 2025-04-30
 */

 #include "risk/risk_manager.hpp"
 #include "risk/service/risk_service.hpp"
 #include "risk/service/risk_validation_service.hpp"
 #include <gtest/gtest.h>
 #include <memory>
 #include <chrono>
 #include <thread>
 namespace trading {
 namespace risk {
 namespace test {
  using namespace trading;
    using namespace trading::core;
    using namespace trading::execution;
 class RiskSystemIntegrationTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create real components for integration testing
         
         // Load configuration from file
         config = std::make_shared<RiskConfig>();
         config->loadFromFile("/home/e8/works/hybrid-bot/cpp-execution/build/config/risk_config.json");
         
         // Create the risk manager
         riskManager = std::make_shared<RiskManager>(*config);
         
         // Initialize the manager
         ASSERT_TRUE(riskManager->initialize());
         
         // Create validation service
         validationService = std::make_shared<RiskValidationService>(*config, riskManager);
         ASSERT_TRUE(validationService->initialize());
         
         // Create risk service
         riskService = std::make_unique<RiskService>(*config);
         riskService->setRiskValidationService(validationService);
         ASSERT_TRUE(riskService->initialize());
         ASSERT_TRUE(riskService->start());
     }
     
     void TearDown() override {
         // Clean shutdown
         if (riskService) {
             riskService->stop();
         }
         
         // Clear all components
         riskService.reset();
         validationService.reset();
         riskManager.reset();
         config.reset();
     }
     
     // Helper method to create a sample trading signal
     TradingSignal createSampleSignal() {
         TradingSignal signal;
         signal.id = "test-signal-1";
         signal.exchangeId = ExchangeId::fromString("binance");
         signal.setTradingPair(core::Symbol("BTC/USDT"));
         signal.direction = Direction::BUY;
         signal.strength = 0.8;
         signal.targetSize = 1.0;
         signal.setStrategyId(StrategyId("test-strategy", "1.0"));
         signal.setTtl(std::chrono::milliseconds(10000)); // 10 seconds TTL
         return signal;
     }
     
    // Helper method to create a sample execution report
    ExecutionReport createSampleExecutionReport() {
        ExecutionReport report;
        // report.orderId = core::OrderId("order-12345");
        report.setOrderId("order-12345");
        report.setExchangeId(ExchangeId::fromString("binance"));
        report.setSymbol(TradingPair::fromString("BTC/USDT"));
        report.setSide(models::OrderSide::BUY);
        report.setType(models::OrderType::LIMIT);
        report.setQuantity(1.0);
        report.setStatus(models::OrderState::FILLED);
        report.setFilledQuantity(1.0);
        report.setPrice(50000.0);
        return report;
    }
     
     std::shared_ptr<RiskConfig> config;
     std::shared_ptr<RiskManager> riskManager;
     std::shared_ptr<RiskValidationService> validationService;
     std::unique_ptr<RiskService> riskService;
 };
 
 // Test end-to-end signal validation flow
 TEST_F(RiskSystemIntegrationTest, EndToEndSignalValidation) {
     // Create a signal that should pass validation
     TradingSignal signal = createSampleSignal();
     
     // Validate through the service
     ValidationResult result = riskService->validateSignal(signal);
     
     // Validate through the validation service directly
     ValidationResult directResult = validationService->validateSignal(signal);
     
     // Results should match
     EXPECT_EQ(result.signalId, directResult.signalId);
     EXPECT_EQ(result.approved, directResult.approved);
     EXPECT_EQ(result.violations.size(), directResult.violations.size());
 }
 
 // Test risk parameters flow
 TEST_F(RiskSystemIntegrationTest, RiskParametersFlow) {
     // Get parameters through the service
     RiskParameters params = riskService->getRiskParameters(ExchangeId::fromString("binance"), TradingPair::fromString("BTC/USDT"), core::StrategyId("order-imbalance"));
     
     // Update parameters
     params.maxPositionSize = params.maxPositionSize * 1.1; // Increase by 10%
     
     // Update through the service
     bool updateResult = riskService->updateRiskParameters(params);
     EXPECT_TRUE(updateResult);
     
     // Get updated parameters
     RiskParameters updatedParams = riskService->getRiskParameters(ExchangeId::fromString("binance"), TradingPair::fromString("BTC/USDT"), core::StrategyId("order-imbalance"));
     
     // Verify the update was applied
     EXPECT_EQ(updatedParams.maxPositionSize, params.maxPositionSize);
 }
 
 // Test execution processing flow
 TEST_F(RiskSystemIntegrationTest, ExecutionProcessingFlow) {
     // Create an execution report
     ExecutionReport report = createSampleExecutionReport();
     
     // Process the execution
     riskManager->processExecutionReport(report);
     
     // Check if position was updated correctly
     // This is dependent on the implementation of PortfolioManager
     // We might need to add methods to get position information for testing
     
     // As a simplified test, we can check that getting risk parameters still works
     // after processing an execution
     // Use the same exchange and symbol used when creating the sample report
     RiskParameters params = riskService->getRiskParameters(ExchangeId::fromString("binance"), TradingPair::fromString("BTC/USDT"), core::StrategyId("order-imbalance"));
     EXPECT_FALSE(params.exchangeId.toString().empty());
 }
 
 // Test risk event propagation
 TEST_F(RiskSystemIntegrationTest, RiskEventPropagation) {
     // Set up event tracking
     bool serviceEventReceived = false;
     
     // Register event callbacks
     riskService->registerRiskEventCallback([&serviceEventReceived](const RiskEvent& event) {
         serviceEventReceived = true;
     });
     
     // Trigger an event (emergency action)
     bool emergencyResult = riskManager->emergencyAction("Integration test emergency");
     EXPECT_TRUE(emergencyResult);
     
     // Allow time for event propagation
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     // Verify event was received
     EXPECT_TRUE(serviceEventReceived);
     
     // Check mode was changed correctly
     EXPECT_EQ(riskService->getMode(), RiskManagerMode::EMERGENCY);
 }
 
 // Test concurrent requests
 TEST_F(RiskSystemIntegrationTest, ConcurrentRequests) {
     // Number of concurrent requests
     const int numRequests = 10;
     
     // Create signals
     std::vector<TradingSignal> signals;
     for (int i = 0; i < numRequests; ++i) {
         TradingSignal signal = createSampleSignal();
         signal.id = "test-signal-" + std::to_string(i);
         signals.push_back(signal);
     }
     
     // Vector to hold results
     std::vector<ValidationResult> results(numRequests);
     
     // Create threads to validate signals concurrently
     std::vector<std::thread> threads;
     for (int i = 0; i < numRequests; ++i) {
         threads.emplace_back([this, i, &signals, &results]() {
             results[i] = riskService->validateSignal(signals[i]);
         });
     }
     
     // Wait for all threads to complete
     for (auto& thread : threads) {
         thread.join();
     }
     
     // Verify all results
     for (int i = 0; i < numRequests; ++i) {
         EXPECT_EQ(results[i].signalId, signals[i].id);
     }
 }
 
 // Test system mode transitions
 TEST_F(RiskSystemIntegrationTest, SystemModeTransitions) {
     // Verify initial mode
     EXPECT_EQ(riskService->getMode(), RiskManagerMode::NORMAL);
     
     // Change to heightened risk
     EXPECT_TRUE(riskService->setMode(RiskManagerMode::HEIGHTENED_RISK));
     EXPECT_EQ(riskService->getMode(), RiskManagerMode::HEIGHTENED_RISK);
     
     // Change to emergency
     EXPECT_TRUE(riskService->setMode(RiskManagerMode::EMERGENCY));
     EXPECT_EQ(riskService->getMode(), RiskManagerMode::EMERGENCY);
     
     // Change to maintenance
     EXPECT_TRUE(riskService->setMode(RiskManagerMode::MAINTENANCE));
     EXPECT_EQ(riskService->getMode(), RiskManagerMode::MAINTENANCE);
     
     // Change back to normal
     EXPECT_TRUE(riskService->setMode(RiskManagerMode::NORMAL));
     EXPECT_EQ(riskService->getMode(), RiskManagerMode::NORMAL);
 }
 
 } // namespace test
 } // namespace risk
 } // namespace trading
 
 // Main function to run the tests
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }