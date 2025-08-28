/**
 * Unit tests for RiskManager
 *
 * File: test_risk_manager.cpp
 * Created: 2025-04-30
 */

 #include "risk_manager.hpp"
 #include "pretrade/pre_trade_risk_filter.hpp"
 #include "posttrade/post_trade_monitor.hpp"
 #include "portfolio/portfolio_manager.hpp"
 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include <memory>
 
 namespace trading {
 namespace risk {
 namespace test {
 
 // Mock classes for dependencies
 class MockPreTradeRiskFilter : public PreTradeRiskFilter {
 public:
     MockPreTradeRiskFilter(const RiskConfig& config, PortfolioManager* portfolioManager)
         : PreTradeRiskFilter(config, portfolioManager) {}
         
     MOCK_METHOD(bool, initialize, (), (override));
     MOCK_METHOD(ValidationResult, validateSignal, (const TradingSignal&), (override));
     MOCK_METHOD(RiskParameters, getRiskParameters, (const ExchangeId&, const TradingPair&, const StrategyId&), (const, override));
     MOCK_METHOD(bool, updateRiskParameters, (const RiskParameters&), (override));
     MOCK_METHOD(bool, isHealthy, (), (const, override));
 };
 
 class MockPostTradeMonitor : public PostTradeMonitor {
 public:
     MockPostTradeMonitor(const RiskConfig& config, PortfolioManager* portfolioManager)
         : PostTradeMonitor(config, portfolioManager) {}
         
     MOCK_METHOD(bool, initialize, (), (override));
     MOCK_METHOD(void, processExecutionReport, (const ExecutionReport&), (override));
     MOCK_METHOD(void, updateMarketData, (const MarketData&), (override));
     MOCK_METHOD(PerformanceMetrics, getPerformanceMetrics, (), (const, override));
     MOCK_METHOD(bool, isHealthy, (), (const, override));
     MOCK_METHOD(void, registerRiskEventCallback, (const RiskEventCallback&), (override));
 };
 
 class MockPortfolioManager : public PortfolioManager {
 public:
     explicit MockPortfolioManager(const RiskConfig& config)
         : PortfolioManager(config) {}
         
     MOCK_METHOD(bool, initialize, (), (override));
     MOCK_METHOD(void, processExecution, (const ExecutionReport&), (override));
     MOCK_METHOD(void, updateMarketData, (const MarketData&), (override));
     MOCK_METHOD(bool, reconcilePositions, (const std::optional<ExchangeId>&), (override));
     MOCK_METHOD(bool, isHealthy, (), (const, override));
 };
 
 class MockStrategyInterface : public StrategyInterface {
 public:
     explicit MockStrategyInterface(const RiskConfig& config)
         : StrategyInterface(config) {}
         
     MOCK_METHOD(bool, initialize, (), (override));
     MOCK_METHOD(bool, isConnected, (), (const, override));
     MOCK_METHOD(void, notifyEmergencyState, (const std::string&), (override));
 };
 
 class MockExecutionInterface : public ExecutionInterface {
 public:
     explicit MockExecutionInterface(const RiskConfig& config)
         : ExecutionInterface(config) {}
         
     MOCK_METHOD(bool, initialize, (), (override));
     MOCK_METHOD(bool, isConnected, (), (const, override));
     MOCK_METHOD(bool, cancelAllOrders, (const std::string&), (override));
 };
 
 class MockDataInterface : public DataInterface {
 public:
     explicit MockDataInterface(const RiskConfig& config)
         : DataInterface(config) {}
         
     MOCK_METHOD(bool, initialize, (), (override));
     MOCK_METHOD(bool, isConnected, (), (const, override));
 };
 
 // Test fixture for RiskManager tests
 class RiskManagerTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create configuration
         config = std::make_shared<RiskConfig>();
         config->loadFromString("{}");
         
         // Create mocks through dependency injection
         mockPortfolioManager = new ::testing::NiceMock<MockPortfolioManager>(*config);
         mockPreTradeFilter = new ::testing::NiceMock<MockPreTradeRiskFilter>(*config, mockPortfolioManager);
         mockPostTradeMonitor = new ::testing::NiceMock<MockPostTradeMonitor>(*config, mockPortfolioManager);
         mockStrategyInterface = new ::testing::NiceMock<MockStrategyInterface>(*config);
         mockExecutionInterface = new ::testing::NiceMock<MockExecutionInterface>(*config);
         mockDataInterface = new ::testing::NiceMock<MockDataInterface>(*config);
         
         // Create RiskManager (use a special constructor for testing that takes mocks)
         riskManager = createTestRiskManager();
     }
     
     void TearDown() override {
         riskManager.reset();
         // Note: mocks are owned by riskManager so we don't delete them here
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
     
     // Helper method to create a sample execution report
     ExecutionReport createSampleExecutionReport() {
         ExecutionReport report;
         report.orderId = "test-order-1";
         report.exchangeOrderId = "binance-12345";
         report.exchangeId = "binance";
         report.tradingPair = "BTC/USDT";
         report.side = "buy";
         report.status = "filled";
         report.filledQuantity = 1.0;
         report.averagePrice = 50000.0;
         return report;
     }
     
     // Helper method to create a sample market data update
     MarketData createSampleMarketData() {
         MarketData data;
         data.exchangeId = "binance";
         data.tradingPair = "BTC/USDT";
         data.lastPrice = 50000.0;
         data.bidPrice = 49995.0;
         data.askPrice = 50005.0;
         data.volume24h = 1000.0;
         return data;
     }
     
     // For testing, we need a special version of RiskManager that allows us to inject mocks
     std::unique_ptr<RiskManager> createTestRiskManager() {
         // This is a simplification. In a real implementation, you might use a factory
         // or a special constructor for testing that allows dependency injection.
         // For now, we're assuming we have ways to inject mocks into RiskManager
         
         auto manager = std::make_unique<RiskManager>(*config);
         
         // Inject mocks (in a real implementation, you'd need accessors or a testing subclass)
         // This is pseudo-code showing the intent
         /* 
         manager->m_portfolioManager.reset(mockPortfolioManager);
         manager->m_preTradeFilter.reset(mockPreTradeFilter);
         manager->m_postTradeMonitor.reset(mockPostTradeMonitor);
         manager->m_strategyInterface.reset(mockStrategyInterface);
         manager->m_executionInterface.reset(mockExecutionInterface);
         manager->m_dataInterface.reset(mockDataInterface);
         */
         
         return manager;
     }
     
     std::shared_ptr<RiskConfig> config;
     std::unique_ptr<RiskManager> riskManager;
     
     // Mock objects (owned by riskManager after setup)
     ::testing::NiceMock<MockPortfolioManager>* mockPortfolioManager;
     ::testing::NiceMock<MockPreTradeRiskFilter>* mockPreTradeFilter;
     ::testing::NiceMock<MockPostTradeMonitor>* mockPostTradeMonitor;
     ::testing::NiceMock<MockStrategyInterface>* mockStrategyInterface;
     ::testing::NiceMock<MockExecutionInterface>* mockExecutionInterface;
     ::testing::NiceMock<MockDataInterface>* mockDataInterface;
 };
 
 // Test initialization
 TEST_F(RiskManagerTest, Initialize) {
     // Set up expectations for initialize calls
     EXPECT_CALL(*mockPortfolioManager, initialize())
         .WillOnce(::testing::Return(true));
     
     EXPECT_CALL(*mockPreTradeFilter, initialize())
         .WillOnce(::testing::Return(true));
     
     EXPECT_CALL(*mockPostTradeMonitor, initialize())
         .WillOnce(::testing::Return(true));
     
     EXPECT_CALL(*mockStrategyInterface, initialize())
         .WillOnce(::testing::Return(true));
     
     EXPECT_CALL(*mockExecutionInterface, initialize())
         .WillOnce(::testing::Return(true));
     
     EXPECT_CALL(*mockDataInterface, initialize())
         .WillOnce(::testing::Return(true));
     
     // Call initialize
     bool result = riskManager->initialize();
     
     // Verify the result
     EXPECT_TRUE(result);
 }
 
 // Test validate signal
 TEST_F(RiskManagerTest, ValidateSignal) {
     // Set up expected signal and result
     TradingSignal signal = createSampleSignal();
     
     ValidationResult expectedResult;
     expectedResult.signalId = signal.id;
     expectedResult.approved = true;
     
     // Set up expectations
     EXPECT_CALL(*mockPreTradeFilter, validateSignal(::testing::_))
         .WillOnce(::testing::Return(expectedResult));
     
     // Call the method
     ValidationResult result = riskManager->validateSignal(signal);
     
     // Verify the result
     EXPECT_EQ(result.signalId, expectedResult.signalId);
     EXPECT_EQ(result.approved, expectedResult.approved);
 }
 
 // Test validate signal with system not in trading mode
 TEST_F(RiskManagerTest, ValidateSignalNonTradingMode) {
     // Set up signal
     TradingSignal signal = createSampleSignal();
     
     // Set system mode to emergency (non-trading mode)
     riskManager->setMode(RiskManagerMode::EMERGENCY);
     
     // Call the method
     ValidationResult result = riskManager->validateSignal(signal);
     
     // Verify the result indicates rejection due to system mode
     EXPECT_FALSE(result.approved);
     EXPECT_EQ(result.violations.size(), 1);
     EXPECT_EQ(result.violations[0].violationType, "SYSTEM_MODE");
 }
 
 // Test process execution report
 TEST_F(RiskManagerTest, ProcessExecutionReport) {
     // Set up expected execution report
     ExecutionReport report = createSampleExecutionReport();
     
     // Set up expectations
     EXPECT_CALL(*mockPortfolioManager, processExecution(::testing::_))
         .Times(1);
     
     EXPECT_CALL(*mockPostTradeMonitor, processExecutionReport(::testing::_))
         .Times(1);
     
     // Call the method
     riskManager->processExecutionReport(report);
     
     // Verification is done via EXPECT_CALL
 }
 
 // Test update market data
 TEST_F(RiskManagerTest, UpdateMarketData) {
     // Set up expected market data
     MarketData data = createSampleMarketData();
     
     // Set up expectations
     EXPECT_CALL(*mockPortfolioManager, updateMarketData(::testing::_))
         .Times(1);
     
     EXPECT_CALL(*mockPostTradeMonitor, updateMarketData(::testing::_))
         .Times(1);
     
     // Call the method
     riskManager->updateMarketData(data);
     
     // Verification is done via EXPECT_CALL
 }
 
 // Test get risk parameters
 TEST_F(RiskManagerTest, GetRiskParameters) {
     // Set up expected parameters
     RiskParameters expectedParams;
     expectedParams.exchangeId = "binance";
     expectedParams.tradingPair = "BTC/USDT";
     expectedParams.maxPositionSize = 10.0;
     
     // Set up expectations
     EXPECT_CALL(*mockPreTradeFilter, getRiskParameters(::testing::_, ::testing::_, ::testing::_))
         .WillOnce(::testing::Return(expectedParams));
     
     // Call the method
     RiskParameters params = riskManager->getRiskParameters("binance", "BTC/USDT", "order-imbalance");
     
     // Verify the result
     EXPECT_EQ(params.exchangeId, expectedParams.exchangeId);
     EXPECT_EQ(params.tradingPair, expectedParams.tradingPair);
     EXPECT_EQ(params.maxPositionSize, expectedParams.maxPositionSize);
 }
 
 // Test update risk parameters
 TEST_F(RiskManagerTest, UpdateRiskParameters) {
     // Set up parameters to update
     RiskParameters params;
     params.exchangeId = "binance";
     params.tradingPair = "BTC/USDT";
     params.maxPositionSize = 15.0;
     
     // Set up expectations
     EXPECT_CALL(*mockPreTradeFilter, updateRiskParameters(::testing::_))
         .WillOnce(::testing::Return(true));
     
     // Call the method
     bool result = riskManager->updateRiskParameters(params);
     
     // Verify the result
     EXPECT_TRUE(result);
 }
 
 // Test mode management
 TEST_F(RiskManagerTest, ModeManagement) {
     // Initially should be in NORMAL mode
     EXPECT_EQ(riskManager->getMode(), RiskManagerMode::NORMAL);
     
     // Change to HEIGHTENED_RISK
     riskManager->setMode(RiskManagerMode::HEIGHTENED_RISK);
     EXPECT_EQ(riskManager->getMode(), RiskManagerMode::HEIGHTENED_RISK);
     
     // Change to EMERGENCY mode (should trigger emergency action)
     EXPECT_CALL(*mockExecutionInterface, cancelAllOrders(::testing::_))
         .WillOnce(::testing::Return(true));
     
     EXPECT_CALL(*mockStrategyInterface, notifyEmergencyState(::testing::_))
         .Times(1);
     
     riskManager->setMode(RiskManagerMode::EMERGENCY);
     EXPECT_EQ(riskManager->getMode(), RiskManagerMode::EMERGENCY);
 }
 
 // Test reconcilePositions
 TEST_F(RiskManagerTest, ReconcilePositions) {
     // Set up expectations
     EXPECT_CALL(*mockPortfolioManager, reconcilePositions(::testing::_))
         .WillOnce(::testing::Return(true));
     
     // Call the method with no specific exchange
     bool result = riskManager->reconcilePositions();
     
     // Verify the result
     EXPECT_TRUE(result);
     
     // Test with specific exchange
     EXPECT_CALL(*mockPortfolioManager, reconcilePositions(::testing::_))
         .WillOnce(::testing::Return(true));
     
     result = riskManager->reconcilePositions(std::optional<ExchangeId>("binance"));
     
     EXPECT_TRUE(result);
 }
 
 // Test getPerformanceMetrics
 TEST_F(RiskManagerTest, GetPerformanceMetrics) {
     // Set up expected metrics
     PerformanceMetrics expectedMetrics;
     expectedMetrics.totalPnl = 1000.0;
     expectedMetrics.sharpeRatio = 2.5;
     
     // Set up expectations
     EXPECT_CALL(*mockPostTradeMonitor, getPerformanceMetrics())
         .WillOnce(::testing::Return(expectedMetrics));
     
     // Call the method
     PerformanceMetrics metrics = riskManager->getPerformanceMetrics();
     
     // Verify the result
     EXPECT_EQ(metrics.totalPnl, expectedMetrics.totalPnl);
     EXPECT_EQ(metrics.sharpeRatio, expectedMetrics.sharpeRatio);
 }
 
 // Test emergency action
 TEST_F(RiskManagerTest, EmergencyAction) {
     // Set up expectations
     EXPECT_CALL(*mockExecutionInterface, cancelAllOrders(::testing::_))
         .WillOnce(::testing::Return(true));
     
     EXPECT_CALL(*mockStrategyInterface, notifyEmergencyState(::testing::_))
         .Times(1);
     
     // Call the method
     bool result = riskManager->emergencyAction("Test emergency");
     
     // Verify the result
     EXPECT_TRUE(result);
     
     // Verify mode was changed to EMERGENCY
     EXPECT_EQ(riskManager->getMode(), RiskManagerMode::EMERGENCY);
 }
 
 // Test risk event callbacks
 TEST_F(RiskManagerTest, RiskEventCallbacks) {
     // Set up callback tracking
     bool callbackCalled = false;
     RiskEvent capturedEvent;
     
     // Register callback
     riskManager->registerRiskEventCallback([&callbackCalled, &capturedEvent](const RiskEvent& event) {
         callbackCalled = true;
         capturedEvent = event;
     });
     
     // Trigger an event via emergency action
     EXPECT_CALL(*mockExecutionInterface, cancelAllOrders(::testing::_))
         .WillOnce(::testing::Return(true));
     
     EXPECT_CALL(*mockStrategyInterface, notifyEmergencyState(::testing::_))
         .Times(1);
     
     riskManager->emergencyAction("Test emergency");
     
     // Verify callback was called
     EXPECT_TRUE(callbackCalled);
     EXPECT_EQ(capturedEvent.type, RiskEventType::EMERGENCY_ACTION);
     EXPECT_EQ(capturedEvent.severity, RiskEventSeverity::CRITICAL);
     EXPECT_EQ(capturedEvent.description, "Emergency action triggered: Test emergency");
 }
 
 } // namespace test
 } // namespace risk
 } // namespace trading
 
 // Main function to run the tests
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }