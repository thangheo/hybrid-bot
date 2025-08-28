/**
 * Unit tests for position tracker component
 * 
 * File: test_position_tracker.cpp
 * Created: 2025-04-30
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include "portfolio/position_tracker.hpp"
 #include "../mocks/data_interface_mock.hpp"
 
 using ::testing::Return;
 using ::testing::_;
 using ::testing::NiceMock;
 
 namespace risk {
 namespace test {
 
 class PositionTrackerTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create mock data interface
         m_dataInterface = std::make_shared<NiceMock<MockDataInterface>>();
         
         // Setup default config
         m_config.positionTrackerConfig.enablePositionAging = true;
         m_config.positionTrackerConfig.positionAgingDays = 30;
         
         // Create position tracker with mock
         m_positionTracker = std::make_shared<PositionTracker>(m_config, m_dataInterface);
         
         // Setup common test data
         m_testPosition = Position("binance", "BTC/USD", "test_strategy", 1.5, 50000.0);
         m_testPosition.setCurrentPrice(52000.0);
         
         // Setup default expectations
         ON_CALL(*m_dataInterface, getMarketData(_))
             .WillByDefault(Return(std::optional<MarketData>({{"BTC/USD", 52000.0, 
                 std::chrono::system_clock::now().time_since_epoch().count()}})));
     }
     
     void TearDown() override {
         m_positionTracker->clearPositions();
     }
     
     // Helper methods
     void addTestPosition() {
         m_positionTracker->updatePosition(
             m_testPosition.getExchangeId(),
             m_testPosition.getTradingPair(),
             m_testPosition.getQuantity(),
             m_testPosition.getAverageEntryPrice()
         );
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
     std::shared_ptr<PositionTracker> m_positionTracker;
     RiskConfig m_config;
     Position m_testPosition;
 };
 
 // Test position creation and basic properties
 TEST_F(PositionTrackerTest, PositionBasicProperties) {
     Position p("binance", "ETH/USD", "test_strategy", 10.0, 2000.0);
     
     EXPECT_EQ(p.getExchangeId(), "binance");
     EXPECT_EQ(p.getTradingPair(), "ETH/USD");
     EXPECT_EQ(p.getStrategyId(), "test_strategy");
     EXPECT_DOUBLE_EQ(p.getQuantity(), 10.0);
     EXPECT_DOUBLE_EQ(p.getAverageEntryPrice(), 2000.0);
     EXPECT_DOUBLE_EQ(p.getCurrentPrice(), 2000.0); // Defaults to entry price
     
     // Test notional value calculation
     EXPECT_DOUBLE_EQ(p.getNotionalValue(), 20000.0);
     
     // Test price update
     p.setCurrentPrice(2200.0);
     EXPECT_DOUBLE_EQ(p.getCurrentPrice(), 2200.0);
     EXPECT_DOUBLE_EQ(p.getNotionalValue(), 22000.0);
     
     // Test unrealized PnL calculation
     EXPECT_DOUBLE_EQ(p.getUnrealizedPnl(), 2000.0); // (2200 - 2000) * 10
 }
 
 // Test position updates
 TEST_F(PositionTrackerTest, PositionUpdates) {
     Position p("binance", "ETH/USD", "test_strategy", 10.0, 2000.0);
     
     // Test adding to position
     p.updatePosition(5.0, 2200.0);
     EXPECT_DOUBLE_EQ(p.getQuantity(), 15.0);
     EXPECT_DOUBLE_EQ(p.getAverageEntryPrice(), 2066.67); // ((10*2000) + (5*2200)) / 15
     
     // Test reducing position
     p.updatePosition(-3.0, 2300.0);
     EXPECT_DOUBLE_EQ(p.getQuantity(), 12.0);
     EXPECT_DOUBLE_EQ(p.getAverageEntryPrice(), 2066.67); // Unchanged when reducing
     EXPECT_DOUBLE_EQ(p.getRealizedPnl(), 700.0); // (2300 - 2066.67) * 3
     
     // Test closing position
     p.updatePosition(-12.0, 2400.0);
     EXPECT_DOUBLE_EQ(p.getQuantity(), 0.0);
     EXPECT_DOUBLE_EQ(p.getRealizedPnl(), 4700.0); // 700 + (2400 - 2066.67) * 12
     
     // Test opening a new position in opposite direction
     p.updatePosition(-5.0, 2500.0);
     EXPECT_DOUBLE_EQ(p.getQuantity(), -5.0);
     EXPECT_DOUBLE_EQ(p.getAverageEntryPrice(), 2500.0);
     EXPECT_DOUBLE_EQ(p.getRealizedPnl(), 4700.0); // Unchanged
 }
 
 // Test position tracker initialization
 TEST_F(PositionTrackerTest, Initialization) {
     EXPECT_TRUE(m_positionTracker->initialize());
     EXPECT_EQ(m_positionTracker->getAllPositions().size(), 0);
 }
 
 // Test position tracker update position
 TEST_F(PositionTrackerTest, UpdatePosition) {
     EXPECT_TRUE(m_positionTracker->initialize());
     
     // Add a position
     EXPECT_TRUE(m_positionTracker->updatePosition("binance", "BTC/USD", 2.0, 50000.0));
     
     // Check position was added
     auto positions = m_positionTracker->getAllPositions();
     EXPECT_EQ(positions.size(), 1);
     EXPECT_EQ(positions[0].getExchangeId(), "binance");
     EXPECT_EQ(positions[0].getTradingPair(), "BTC/USD");
     EXPECT_DOUBLE_EQ(positions[0].getQuantity(), 2.0);
     EXPECT_DOUBLE_EQ(positions[0].getAverageEntryPrice(), 50000.0);
     
     // Update existing position
     EXPECT_TRUE(m_positionTracker->updatePosition("binance", "BTC/USD", 3.0, 52000.0));
     
     // Check position was updated
     positions = m_positionTracker->getAllPositions();
     EXPECT_EQ(positions.size(), 1);
     EXPECT_DOUBLE_EQ(positions[0].getQuantity(), 3.0);
     EXPECT_DOUBLE_EQ(positions[0].getAverageEntryPrice(), 52000.0);
 }
 
 // Test position tracker processing execution reports
 TEST_F(PositionTrackerTest, ProcessExecutionReport) {
     EXPECT_TRUE(m_positionTracker->initialize());
     
     // Process a buy execution report
     ExecutionReport buyReport = createExecutionReport("binance", "ETH/USD", "buy", 5.0, 2000.0);
     m_positionTracker->processExecutionReport(buyReport);
     
     // Check position was created
     auto positions = m_positionTracker->getAllPositions();
     EXPECT_EQ(positions.size(), 1);
     EXPECT_EQ(positions[0].getExchangeId(), "binance");
     EXPECT_EQ(positions[0].getTradingPair(), "ETH/USD");
     EXPECT_DOUBLE_EQ(positions[0].getQuantity(), 5.0);
     EXPECT_DOUBLE_EQ(positions[0].getAverageEntryPrice(), 2000.0);
     
     // Process a sell execution report
     ExecutionReport sellReport = createExecutionReport("binance", "ETH/USD", "sell", 2.0, 2200.0);
     m_positionTracker->processExecutionReport(sellReport);
     
     // Check position was updated
     positions = m_positionTracker->getAllPositions();
     EXPECT_EQ(positions.size(), 1);
     EXPECT_DOUBLE_EQ(positions[0].getQuantity(), 3.0);
     EXPECT_DOUBLE_EQ(positions[0].getAverageEntryPrice(), 2000.0); // Unchanged when reducing
     
     // Check that a new position on different exchange is created
     ExecutionReport otherExchangeReport = createExecutionReport("ftx", "ETH/USD", "buy", 2.0, 2100.0);
     m_positionTracker->processExecutionReport(otherExchangeReport);
     
     positions = m_positionTracker->getAllPositions();
     EXPECT_EQ(positions.size(), 2);
 }
 
 // Test market data updates
 TEST_F(PositionTrackerTest, MarketDataUpdates) {
     EXPECT_TRUE(m_positionTracker->initialize());
     addTestPosition();
     
     // Get initial position
     auto position = m_positionTracker->getPosition("binance", "BTC/USD");
     ASSERT_TRUE(position.has_value());
     EXPECT_DOUBLE_EQ(position->getCurrentPrice(), 52000.0);
     
     // Update market data
     MarketData updatedData;
     updatedData.tradingPair = "BTC/USD";
     updatedData.price = 55000.0;
     updatedData.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
     
     m_positionTracker->updateMarketData(updatedData);
     
     // Check position price was updated
     position = m_positionTracker->getPosition("binance", "BTC/USD");
     ASSERT_TRUE(position.has_value());
     EXPECT_DOUBLE_EQ(position->getCurrentPrice(), 55000.0);
     EXPECT_DOUBLE_EQ(position->getUnrealizedPnl(), 7500.0); // (55000 - 50000) * 1.5
 }
 
 // Test position querying
 TEST_F(PositionTrackerTest, PositionQueries) {
     EXPECT_TRUE(m_positionTracker->initialize());
     
     // Add test positions
     m_positionTracker->updatePosition("binance", "BTC/USD", 1.0, 50000.0);
     m_positionTracker->updatePosition("binance", "ETH/USD", 10.0, 2000.0);
     m_positionTracker->updatePosition("ftx", "BTC/USD", 0.5, 51000.0);
     
     // Test getting all positions
     auto allPositions = m_positionTracker->getAllPositions();
     EXPECT_EQ(allPositions.size(), 3);
     
     // Test getting positions by exchange
     auto binancePositions = m_positionTracker->getExchangePositions("binance");
     EXPECT_EQ(binancePositions.size(), 2);
     
     auto ftxPositions = m_positionTracker->getExchangePositions("ftx");
     EXPECT_EQ(ftxPositions.size(), 1);
     
     // Test getting positions by trading pair
     auto btcPositions = m_positionTracker->getPositionsByTradingPair("BTC/USD");
     EXPECT_EQ(btcPositions.size(), 2);
     
     // Test getting specific position
     auto binanceBtc = m_positionTracker->getPosition("binance", "BTC/USD");
     ASSERT_TRUE(binanceBtc.has_value());
     EXPECT_EQ(binanceBtc->getExchangeId(), "binance");
     EXPECT_EQ(binanceBtc->getTradingPair(), "BTC/USD");
     EXPECT_DOUBLE_EQ(binanceBtc->getQuantity(), 1.0);
     
     // Test non-existent position
     auto nonExistent = m_positionTracker->getPosition("binance", "XRP/USD");
     EXPECT_FALSE(nonExistent.has_value());
 }
 
 // Test portfolio metrics
 TEST_F(PositionTrackerTest, PortfolioMetrics) {
     EXPECT_TRUE(m_positionTracker->initialize());
     
     // Setup market data mockup
     ON_CALL(*m_dataInterface, getMarketData("BTC/USD"))
         .WillByDefault(Return(std::optional<MarketData>({{"BTC/USD", 50000.0, 0}})));
     ON_CALL(*m_dataInterface, getMarketData("ETH/USD"))
         .WillByDefault(Return(std::optional<MarketData>({{"ETH/USD", 2000.0, 0}})));
     
     // Add test positions
     m_positionTracker->updatePosition("binance", "BTC/USD", 1.0, 50000.0);
     m_positionTracker->updatePosition("binance", "ETH/USD", 10.0, 2000.0);
     
     // Test portfolio value calculation
     double portfolioValue = m_positionTracker->calculatePortfolioValue();
     EXPECT_DOUBLE_EQ(portfolioValue, 70000.0); // (1 * 50000) + (10 * 2000)
     
     // Test total exposure calculation
     double totalExposure = m_positionTracker->calculateTotalExposure();
     EXPECT_DOUBLE_EQ(totalExposure, 70000.0);
     
     // Add a short position
     m_positionTracker->updatePosition("ftx", "BTC/USD", -0.5, 51000.0);
     
     // Test updated portfolio value
     portfolioValue = m_positionTracker->calculatePortfolioValue();
     EXPECT_DOUBLE_EQ(portfolioValue, 45000.0); // (1 * 50000) + (10 * 2000) - (0.5 * 50000)
     
     // Test updated total exposure (uses abs value for shorts)
     totalExposure = m_positionTracker->calculateTotalExposure();
     EXPECT_DOUBLE_EQ(totalExposure, 95000.0); // (1 * 50000) + (10 * 2000) + (0.5 * 50000)
 }
 
 // Test callbacks
 TEST_F(PositionTrackerTest, PositionCallbacks) {
     EXPECT_TRUE(m_positionTracker->initialize());
     
     // Setup callback tracking
     bool callbackInvoked = false;
     Position callbackPosition("", "", "", 0, 0);
     std::string callbackType;
     
     m_positionTracker->registerPositionUpdateCallback(
         [&callbackInvoked, &callbackPosition, &callbackType](const Position& position, const std::string& updateType) {
             callbackInvoked = true;
             callbackPosition = position;
             callbackType = updateType;
         }
     );
     
     // Test callback on position update
     m_positionTracker->updatePosition("binance", "BTC/USD", 1.0, 50000.0);
     
     EXPECT_TRUE(callbackInvoked);
     EXPECT_EQ(callbackPosition.getExchangeId(), "binance");
     EXPECT_EQ(callbackPosition.getTradingPair(), "BTC/USD");
     EXPECT_DOUBLE_EQ(callbackPosition.getQuantity(), 1.0);
     EXPECT_EQ(callbackType, "new_reconciliation");
     
     // Reset tracking
     callbackInvoked = false;
     
     // Test callback on execution report
     ExecutionReport report = createExecutionReport("binance", "BTC/USD", "buy", 0.5, 52000.0);
     m_positionTracker->processExecutionReport(report);
     
     EXPECT_TRUE(callbackInvoked);
     EXPECT_EQ(callbackPosition.getExchangeId(), "binance");
     EXPECT_EQ(callbackPosition.getTradingPair(), "BTC/USD");
     EXPECT_DOUBLE_EQ(callbackPosition.getQuantity(), 1.5);
     EXPECT_EQ(callbackType, "modified");
 }
 
 } // namespace test
 } // namespace risk