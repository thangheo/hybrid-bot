/**
 * Unit tests for portfolio manager component
 * 
 * File: test_portfolio_manager.cpp
 * Created: 2025-04-30
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include "portfolio/portfolio_manager.hpp"
 #include "../mocks/data_interface_mock.hpp"
 #include "../mocks/execution_interface_mock.hpp"
 
 using ::testing::Return;
 using ::testing::_;
 using ::testing::NiceMock;
 
 namespace risk {
 namespace test {
 
 class PortfolioManagerTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create mock interfaces
         m_dataInterface = std::make_shared<NiceMock<MockDataInterface>>();
         m_executionInterface = std::make_shared<NiceMock<MockExecutionInterface>>();
         
         // Setup default config
         m_config.portfolioConfig.autoRebalancingEnabled = false;
         m_config.portfolioConfig.autoExecuteRebalancing = false;
         
         // Setup rebalancing config
         m_config.rebalancingConfig.autoRebalancingEnabled = false;
         m_config.rebalancingConfig.rebalancingIntervalMinutes = 60;
         m_config.rebalancingConfig.allocationDeviationThreshold = 0.05;
         m_config.rebalancingConfig.minimumRebalancingValue = 1000.0;
         m_config.rebalancingConfig.targetAllocations = {
             {"binance:BTC/USD", 0.6},
             {"binance:ETH/USD", 0.3},
             {"ftx:BTC/USD", 0.1}
         };
         
         // Setup collateral config
         m_config.collateralConfig.baseCurrency = "USD";
         m_config.collateralConfig.minimumFreeCollateralRatio = 0.2;
         m_config.collateralConfig.managedExchanges = {"binance", "ftx"};
         
         // Setup mocked data
         setupMockData();
         
         // Create portfolio manager
         m_portfolioManager = std::make_shared<PortfolioManager>(m_config);
     }
     
     void setupMockData() {
         // Mock market data
         ON_CALL(*m_dataInterface, getMarketData("BTC/USD"))
             .WillByDefault(Return(std::optional<MarketData>({{"BTC/USD", 50000.0, 0}})));
         ON_CALL(*m_dataInterface, getMarketData("ETH/USD"))
             .WillByDefault(Return(std::optional<MarketData>({{"ETH/USD", 2000.0, 0}})));
             
         // Mock account balances
         std::vector<AccountBalance> binanceBalances = {
             {"USD", 100000.0, 100000.0, 0.0},
             {"BTC", 2.0, 2.0, 0.0},
             {"ETH", 20.0, 20.0, 0.0}
         };
         
         std::vector<AccountBalance> ftxBalances = {
             {"USD", 50000.0, 50000.0, 0.0},
             {"BTC", 1.0, 1.0, 0.0},
             {"ETH", 10.0, 10.0, 0.0}
         };
         
         ON_CALL(*m_executionInterface, getAccountBalances("binance"))
             .WillByDefault(Return(binanceBalances));
         ON_CALL(*m_executionInterface, getAccountBalances("ftx"))
             .WillByDefault(Return(ftxBalances));
     }
     
     ExecutionReport createExecutionReport(const core::ExchangeId& exchangeId, 
                                          const std::string& tradingPair,
                                          const std::string& side,
                                          double quantity,
                                          double price) {
         ExecutionReport report;
         report.exchangeId = exchangeId;
         report.tradingPair = tradingPair;
         report.side = side;
         
         Fill fill;
         fill.quantity = quantity;
         fill.price = price;
         fill.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
         
         report.fills.push_back(fill);
         report.strategyId = "test_strategy";
         
         return report;
     }
     
     std::shared_ptr<MockDataInterface> m_dataInterface;
     std::shared_ptr<MockExecutionInterface> m_executionInterface;
     std::shared_ptr<PortfolioManager> m_portfolioManager;
     RiskConfig m_config;
 };
 
 // Test initialization
 TEST_F(PortfolioManagerTest, Initialization) {
     // Test successful initialization
     EXPECT_TRUE(m_portfolioManager->initialize());
     
     // Verify sub-components were created
     EXPECT_NE(m_portfolioManager->getPositionTracker(), nullptr);
     EXPECT_NE(m_portfolioManager->getRebalancingEngine(), nullptr);
     EXPECT_NE(m_portfolioManager->getCollateralManager(), nullptr);
 }
 
 // Test processing execution reports
 TEST_F(PortfolioManagerTest, ProcessExecutionReport) {
     EXPECT_TRUE(m_portfolioManager->initialize());
     
     // Process an execution report
     ExecutionReport report = createExecutionReport("binance", "BTC/USD", "buy", 1.0, 50000.0);
     m_portfolioManager->processExecutionReport(report);
     
     // Verify position was created
     auto positions = m_portfolioManager->getPositions();
     EXPECT_EQ(positions.size(), 1);
     EXPECT_EQ(positions[0].getExchangeId(), "binance");
     EXPECT_EQ(positions[0].getTradingPair(), "BTC/USD");
     EXPECT_DOUBLE_EQ(positions[0].getQuantity(), 1.0);
     
     // Process another report
     report = createExecutionReport("binance", "ETH/USD", "buy", 10.0, 2000.0);
     m_portfolioManager->processExecutionReport(report);
     
     // Verify both positions exist
     positions = m_portfolioManager->getPositions();
     EXPECT_EQ(positions.size(), 2);
 }
 
 // Test portfolio value calculation
 TEST_F(PortfolioManagerTest, PortfolioValue) {
     EXPECT_TRUE(m_portfolioManager->initialize());
     
     // Initially zero
     EXPECT_DOUBLE_EQ(m_portfolioManager->getPortfolioValue(), 0.0);
     
     // Add positions
     auto positionTracker = m_portfolioManager->getPositionTracker();
     positionTracker->updatePosition("binance", "BTC/USD", 1.0, 50000.0);
     positionTracker->updatePosition("binance", "ETH/USD", 10.0, 2000.0);
     
     // Trigger recalculation by processing a market data update
     MarketData data;
     data.tradingPair = "BTC/USD";
     data.price = 50000.0;
     data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
     m_portfolioManager->updateMarketData(data);
     
     // Verify portfolio value
     EXPECT_DOUBLE_EQ(m_portfolioManager->getPortfolioValue(), 70000.0); // (1 * 50000) + (10 * 2000)
 }
 
 // Test portfolio allocation
 TEST_F(PortfolioManagerTest, PortfolioAllocation) {
     EXPECT_TRUE(m_portfolioManager->initialize());
     
     // Add positions
     auto positionTracker = m_portfolioManager->getPositionTracker();
     positionTracker->updatePosition("binance", "BTC/USD", 1.0, 50000.0);
     positionTracker->updatePosition("binance", "ETH/USD", 10.0, 2000.0);
     
     // Trigger recalculation
     MarketData data;
     data.tradingPair = "BTC/USD";
     data.price = 50000.0;
     data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
     m_portfolioManager->updateMarketData(data);
     
     // Verify allocations
     auto allocations = m_portfolioManager->getPortfolioAllocation();
     EXPECT_EQ(allocations.size(), 2);
     EXPECT_NEAR(allocations["BTC"], 50000.0 / 70000.0, 0.001); // ~0.714
     EXPECT_NEAR(allocations["ETH"], 20000.0 / 70000.0, 0.001); // ~0.286
 }
 
 // Test position queries
 TEST_F(PortfolioManagerTest, PositionQueries) {
     EXPECT_TRUE(m_portfolioManager->initialize());
     
     // Add positions
     auto positionTracker = m_portfolioManager->getPositionTracker();
     positionTracker->updatePosition("binance", "BTC/USD", 1.0, 50000.0);
     positionTracker->updatePosition("ftx", "BTC/USD", 0.5, 51000.0);
     
     // Test all positions
     auto allPositions = m_portfolioManager->getPositions();
     EXPECT_EQ(allPositions.size(), 2);
     
     // Test positions by exchange
     auto binancePositions = m_portfolioManager->getPositions("binance");
     EXPECT_EQ(binancePositions.size(), 1);
     EXPECT_EQ(binancePositions[0].getExchangeId(), "binance");
     
     // Test position by trading pair
     auto btcPosition = m_portfolioManager->getPosition("BTC/USD");
     ASSERT_TRUE(btcPosition.has_value());
     EXPECT_EQ(btcPosition->getTradingPair(), "BTC/USD");
     
     // For aggregated position, quantity should be sum across exchanges
     EXPECT_DOUBLE_EQ(btcPosition->getQuantity(), 1.5);
     
     // Test position by trading pair and exchange
     auto binanceBtc = m_portfolioManager->getPosition("BTC/USD", "binance");
     ASSERT_TRUE(binanceBtc.has_value());
     EXPECT_EQ(binanceBtc->getExchangeId(), "binance");
     EXPECT_DOUBLE_EQ(binanceBtc->getQuantity(), 1.0);
 }
 
 // Test configuration updates
 TEST_F(PortfolioManagerTest, ConfigurationUpdates) {
     EXPECT_TRUE(m_portfolioManager->initialize());
     
     // Update configuration
     RiskConfig newConfig = m_config;
     newConfig.portfolioConfig.autoRebalancingEnabled = true;
     newConfig.portfolioConfig.autoExecuteRebalancing = true;
     
     EXPECT_TRUE(m_portfolioManager->updateConfiguration(newConfig));
     
     // Verify auto-rebalancing is enabled in rebalancing engine
     auto rebalancingEngine = m_portfolioManager->getRebalancingEngine();
     EXPECT_TRUE(rebalancingEngine->isAutoRebalancingEnabled());
 }
 
 // Test callbacks
 TEST_F(PortfolioManagerTest, PortfolioCallbacks) {
     EXPECT_TRUE(m_portfolioManager->initialize());
     
     // Setup callback tracking
     bool callbackInvoked = false;
     std::string eventType;
     std::string description;
     
     m_portfolioManager->registerPortfolioCallback(
         [&callbackInvoked, &eventType, &description](const std::string& type, const std::string& desc) {
             callbackInvoked = true;
             eventType = type;
             description = desc;
         }
     );
     
     // Add a position to trigger the callback
     auto positionTracker = m_portfolioManager->getPositionTracker();
     positionTracker->updatePosition("binance", "BTC/USD", 1.0, 50000.0);
     
     // Verify callback was invoked
     EXPECT_TRUE(callbackInvoked);
     EXPECT_EQ(eventType, "position_update");
 }
 
 // Test rebalancing trigger
 TEST_F(PortfolioManagerTest, RebalancingTrigger) {
     EXPECT_TRUE(m_portfolioManager->initialize());
     
     // Set up positions that will need rebalancing
     auto positionTracker = m_portfolioManager->getPositionTracker();
     positionTracker->updatePosition("binance", "BTC/USD", 1.5, 50000.0); // 75% of portfolio
     positionTracker->updatePosition("binance", "ETH/USD", 5.0, 2000.0);  // 10% of portfolio
     
     // This deviates from the target allocation in the config (60% BTC, 30% ETH)
     
     // Trigger portfolio recalculation
     MarketData data;
     data.tradingPair = "BTC/USD";
     data.price = 50000.0;
     data.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
     m_portfolioManager->updateMarketData(data);
     
     // Trigger rebalancing
     bool result = m_portfolioManager->triggerRebalancing();
     
     // Should succeed and generate rebalancing orders
     EXPECT_TRUE(result);
     
     // Verify rebalancing orders were created
     auto rebalancingEngine = m_portfolioManager->getRebalancingEngine();
     auto lastResult = rebalancingEngine->getLastRebalancingResult();
     
     EXPECT_TRUE(lastResult.success);
     EXPECT_GT(lastResult.ordersCreated.size(), 0);
 }
 
 } // namespace test
 } // namespace risk