/**
 * Unit tests for rebalancing engine component
 * 
 * File: test_rebalancing_engine.cpp
 * Created: 2025-04-30
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include "portfolio/rebalancing_engine.hpp"
 #include "../mocks/data_interface_mock.hpp"
 #include "../mocks/execution_interface_mock.hpp"
 #include "../mocks/position_tracker_mock.hpp"
 
 using ::testing::Return;
 using ::testing::_;
 using ::testing::NiceMock;
 
 namespace risk {
 namespace test {
 
 class RebalancingEngineTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create mock interfaces
         m_dataInterface = std::make_shared<NiceMock<MockDataInterface>>();
         m_executionInterface = std::make_shared<NiceMock<MockExecutionInterface>>();
         m_positionTracker = std::make_shared<NiceMock<MockPositionTracker>>();
         
         // Setup default config
         setupConfig();
         
         // Setup mock data
         setupMockData();
         
         // Create rebalancing engine
         m_rebalancingEngine = std::make_shared<RebalancingEngine>(
             m_config, m_positionTracker, m_executionInterface, m_dataInterface);
     }
     
     void setupConfig() {
         // Rebalancing config
         m_config.rebalancingConfig.autoRebalancingEnabled = false;
         m_config.rebalancingConfig.autoExecuteRebalancing = false;
         m_config.rebalancingConfig.rebalancingIntervalMinutes = 60;
         m_config.rebalancingConfig.allocationDeviationThreshold = 0.05;
         m_config.rebalancingConfig.minimumRebalancingValue = 1000.0;
         m_config.rebalancingConfig.minimumOrderSize = 500.0;
         m_config.rebalancingConfig.parallelExecution = false;
         m_config.rebalancingConfig.defaultTransferFeeRate = 0.001;
         
         // Target allocations
         m_config.rebalancingConfig.targetAllocations = {
             {"binance:BTC/USD", 0.6},
             {"binance:ETH/USD", 0.3},
             {"ftx:BTC/USD", 0.1}
         };
         
         // Preferred exchanges
         m_config.rebalancingConfig.preferredExchanges = {"binance", "ftx"};
         
         // Fixed prices for testing
         m_config.rebalancingConfig.fixedPrices = {
             {"BTC/USD", 50000.0},
             {"ETH/USD", 2000.0}
         };
     }
     
     void setupMockData() {
         // Mock market data
         ON_CALL(*m_dataInterface, getMarketData("BTC/USD"))
             .WillByDefault(Return(std::optional<MarketData>({{"BTC/USD", 50000.0, 0}})));
         ON_CALL(*m_dataInterface, getMarketData("ETH/USD"))
             .WillByDefault(Return(std::optional<MarketData>({{"ETH/USD", 2000.0, 0}})));
             
         // Setup default balanced portfolio for position tracker
         std::vector<Position> positions = {
             Position("binance", "BTC/USD", "test_strategy", 1.2, 50000.0),  // 60% of portfolio
             Position("binance", "ETH/USD", "test_strategy", 15.0, 2000.0),  // 30% of portfolio
             Position("ftx", "BTC/USD", "test_strategy", 0.2, 50000.0)       // 10% of portfolio
         };
         
         // Update current prices for accurate valuation
         positions[0].setCurrentPrice(50000.0);
         positions[1].setCurrentPrice(2000.0);
         positions[2].setCurrentPrice(50000.0);
         
         ON_CALL(*m_positionTracker, getAllPositions())
             .WillByDefault(Return(positions));
             
         // Portfolio value (1.2 * 50000) + (15 * 2000) + (0.2 * 50000) = 100000
         ON_CALL(*m_positionTracker, calculatePortfolioValue())
             .WillByDefault(Return(100000.0));
             
         // Mock order submission
         ON_CALL(*m_executionInterface, submitOrder(_))
             .WillByDefault(Return(OrderResponse{true, "success"}));
     }
     
     void setupUnbalancedPortfolio() {
         // Setup an unbalanced portfolio for rebalancing tests
         std::vector<Position> positions = {
             Position("binance", "BTC/USD", "test_strategy", 1.5, 50000.0),  // 75% of portfolio
             Position("binance", "ETH/USD", "test_strategy", 5.0, 2000.0),   // 10% of portfolio
             Position("ftx", "BTC/USD", "test_strategy", 0.15, 50000.0)      // 7.5% of portfolio
         };
         
         // Update current prices for accurate valuation
         positions[0].setCurrentPrice(50000.0);
         positions[1].setCurrentPrice(2000.0);
         positions[2].setCurrentPrice(50000.0);
         
         ON_CALL(*m_positionTracker, getAllPositions())
             .WillByDefault(Return(positions));
             
         // Portfolio value (1.5 * 50000) + (5 * 2000) + (0.15 * 50000) = 92500
         ON_CALL(*m_positionTracker, calculatePortfolioValue())
             .WillByDefault(Return(92500.0));
     }
     
     std::shared_ptr<MockDataInterface> m_dataInterface;
     std::shared_ptr<MockExecutionInterface> m_executionInterface;
     std::shared_ptr<MockPositionTracker> m_positionTracker;
     std::shared_ptr<RebalancingEngine> m_rebalancingEngine;
     RiskConfig m_config;
 };
 
 // Test initialization
 TEST_F(RebalancingEngineTest, Initialization) {
     EXPECT_TRUE(m_rebalancingEngine->initialize());
     EXPECT_FALSE(m_rebalancingEngine->isAutoRebalancingEnabled());
 }
 
 // Test rebalancing need detection
 TEST_F(RebalancingEngineTest, RebalancingNeeded) {
     EXPECT_TRUE(m_rebalancingEngine->initialize());
     
     // With balanced portfolio, no rebalancing needed
     EXPECT_FALSE(m_rebalancingEngine->isRebalancingNeeded());
     
     // Setup unbalanced portfolio
     setupUnbalancedPortfolio();
     
     // Now rebalancing should be needed
     EXPECT_TRUE(m_rebalancingEngine->isRebalancingNeeded());
 }
 
 // Test rebalancing order calculation
 TEST_F(RebalancingEngineTest, CalculateRebalancingNeeds) {
     EXPECT_TRUE(m_rebalancingEngine->initialize());
     
     // With balanced portfolio, no rebalancing orders needed
     auto balancedOrders = m_rebalancingEngine->calculateRebalancingNeeds();
     EXPECT_TRUE(balancedOrders.empty());
     
     // Setup unbalanced portfolio
     setupUnbalancedPortfolio();
     
     // Now rebalancing orders should be generated
     auto unbalancedOrders = m_rebalancingEngine->calculateRebalancingNeeds();
     EXPECT_FALSE(unbalancedOrders.empty());
     
     // Verify orders are properly structured
     for (const auto& order : unbalancedOrders) {
         EXPECT_FALSE(order.sourceExchangeId.empty());
         EXPECT_FALSE(order.targetExchangeId.empty());
         EXPECT_FALSE(order.tradingPair.empty());
         EXPECT_GT(order.quantity, 0.0);
         EXPECT_GT(order.estimatedPrice, 0.0);
     }
 }
 
 // Test rebalancing execution
 TEST_F(RebalancingEngineTest, ExecuteRebalancing) {
     EXPECT_TRUE(m_rebalancingEngine->initialize());
     
     // Setup unbalanced portfolio
     setupUnbalancedPortfolio();
     
     // Execute rebalancing
     auto result = m_rebalancingEngine->executeRebalancing();
     
     // Should succeed
     EXPECT_TRUE(result.success);
     EXPECT_FALSE(result.ordersCreated.empty());
     
     // Verify last rebalancing result is updated
     auto lastResult = m_rebalancingEngine->getLastRebalancingResult();
     EXPECT_TRUE(lastResult.success);
     EXPECT_FALSE(lastResult.ordersCreated.empty());
 }
 
 // Test rebalancing with provided orders
 TEST_F(RebalancingEngineTest, ExecuteProvidedRebalancingOrders) {
     EXPECT_TRUE(m_rebalancingEngine->initialize());
     
     // Create test orders
     std::vector<RebalancingOrder> orders = {
         {
             "binance", "ftx", "BTC/USD", 0.2, Direction::Sell, 50000.0, "Test rebalancing"
         },
         {
             "ftx", "binance", "ETH/USD", 5.0, Direction::Buy, 2000.0, "Test rebalancing"
         }
     };
     
     // Execute rebalancing with provided orders
     auto result = m_rebalancingEngine->executeRebalancing(orders);
     
     // Should succeed
     EXPECT_TRUE(result.success);
     EXPECT_EQ(result.ordersCreated.size(), orders.size());
 }
 
 // Test auto-rebalancing scheduling
 TEST_F(RebalancingEngineTest, AutoRebalancingScheduling) {
     EXPECT_TRUE(m_rebalancingEngine->initialize());
     
     // Initially auto-rebalancing should be disabled
     EXPECT_FALSE(m_rebalancingEngine->isAutoRebalancingEnabled());
     
     // Enable auto-rebalancing
     EXPECT_TRUE(m_rebalancingEngine->setAutoRebalancing(true));
     
     // Now auto-rebalancing should be enabled
     EXPECT_TRUE(m_rebalancingEngine->isAutoRebalancingEnabled());
     
     // Disable auto-rebalancing
     EXPECT_TRUE(m_rebalancingEngine->setAutoRebalancing(false));
     
     // Now auto-rebalancing should be disabled again
     EXPECT_FALSE(m_rebalancingEngine->isAutoRebalancingEnabled());
 }
 
 // Test configuration updates
 TEST_F(RebalancingEngineTest, ConfigurationUpdates) {
     EXPECT_TRUE(m_rebalancingEngine->initialize());
     
     // Update configuration
     RebalancingConfig newConfig = m_config.rebalancingConfig;
     newConfig.allocationDeviationThreshold = 0.1;
     newConfig.rebalancingIntervalMinutes = 120;
     newConfig.autoRebalancingEnabled = true;
     
     EXPECT_TRUE(m_rebalancingEngine->updateConfiguration(newConfig));
     
     // Verify config was updated
     auto currentConfig = m_rebalancingEngine->getConfiguration();
     EXPECT_DOUBLE_EQ(currentConfig.allocationDeviationThreshold, 0.1);
     EXPECT_EQ(currentConfig.rebalancingIntervalMinutes, 120);
     EXPECT_TRUE(currentConfig.autoRebalancingEnabled);
     
     // Auto-rebalancing should now be enabled
     EXPECT_TRUE(m_rebalancingEngine->isAutoRebalancingEnabled());
 }
 
 // Test callbacks
 TEST_F(RebalancingEngineTest, RebalancingCallbacks) {
     EXPECT_TRUE(m_rebalancingEngine->initialize());
     
     // Setup callback tracking
     bool callbackInvoked = false;
     RebalancingResult callbackResult;
     
     m_rebalancingEngine->registerRebalancingCallback(
         [&callbackInvoked, &callbackResult](const RebalancingResult& result) {
             callbackInvoked = true;
             callbackResult = result;
         }
     );
     
     // Setup unbalanced portfolio
     setupUnbalancedPortfolio();
     
     // Execute rebalancing to trigger callback
     m_rebalancingEngine->executeRebalancing();
     
     // Verify callback was invoked
     EXPECT_TRUE(callbackInvoked);
     EXPECT_TRUE(callbackResult.success);
     EXPECT_FALSE(callbackResult.ordersCreated.empty());
 }
 
 // Test edge case: empty portfolio
 TEST_F(RebalancingEngineTest, EmptyPortfolio) {
     EXPECT_TRUE(m_rebalancingEngine->initialize());
     
     // Setup empty portfolio
     ON_CALL(*m_positionTracker, getAllPositions())
         .WillByDefault(Return(std::vector<Position>{}));
     ON_CALL(*m_positionTracker, calculatePortfolioValue())
         .WillByDefault(Return(0.0));
     
     // No rebalancing needed for empty portfolio
     EXPECT_FALSE(m_rebalancingEngine->isRebalancingNeeded());
     
     // No rebalancing orders for empty portfolio
     auto orders = m_rebalancingEngine->calculateRebalancingNeeds();
     EXPECT_TRUE(orders.empty());
     
     // Executing rebalancing should succeed but with no orders
     auto result = m_rebalancingEngine->executeRebalancing();
     EXPECT_TRUE(result.success);
     EXPECT_TRUE(result.ordersCreated.empty());
     EXPECT_FALSE(result.errors.empty()); // Should have a message about no orders needed
 }
 
 // Test edge case: missing target allocation
 TEST_F(RebalancingEngineTest, MissingTargetAllocation) {
     // Create config with missing allocation
     RiskConfig modifiedConfig = m_config;
     modifiedConfig.rebalancingConfig.targetAllocations.clear();
     modifiedConfig.rebalancingConfig.targetAllocations = {
         {"binance:BTC/USD", 0.6},
         // ETH allocation missing
         {"ftx:BTC/USD", 0.1}
     };
     
     // Create rebalancing engine with modified config
     auto rebalancingEngine = std::make_shared<RebalancingEngine>(
         modifiedConfig, m_positionTracker, m_executionInterface, m_dataInterface);
     
     EXPECT_TRUE(rebalancingEngine->initialize());
     
     // Setup portfolio with ETH position that has no target allocation
     std::vector<Position> positions = {
         Position("binance", "BTC/USD", "test_strategy", 1.2, 50000.0),  // 60% of portfolio
         Position("binance", "ETH/USD", "test_strategy", 15.0, 2000.0),  // 30% of portfolio - no target
         Position("ftx", "BTC/USD", "test_strategy", 0.2, 50000.0)       // 10% of portfolio
     };
     
     positions[0].setCurrentPrice(50000.0);
     positions[1].setCurrentPrice(2000.0);
     positions[2].setCurrentPrice(50000.0);
     
     ON_CALL(*m_positionTracker, getAllPositions())
         .WillByDefault(Return(positions));
     
     // Should detect rebalancing need due to missing allocation
     EXPECT_TRUE(rebalancingEngine->isRebalancingNeeded());
     
     // Should generate rebalancing orders
     auto orders = rebalancingEngine->calculateRebalancingNeeds();
     EXPECT_FALSE(orders.empty());
 }
 
 } // namespace test
 } // namespace risk