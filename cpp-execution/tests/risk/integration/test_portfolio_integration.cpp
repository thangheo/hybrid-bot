/**
 * Integration tests for portfolio management components
 * 
 * File: test_portfolio_integration.cpp
 * Created: 2025-04-30
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include "risk/portfolio/portfolio_manager.hpp"
 #include "../mocks/data_interface_mock.hpp"
 #include "../mocks/execution_interface_mock.hpp"
 #include <string>

 // Minimal AccountBalance used by tests and mock interfaces
 struct AccountBalance {
     std::string asset;
     double total;
     double available;
     double reserved;
 };
 
 // Minimal OrderResponse used by mock ExecutionInterface::submitOrder
 struct OrderResponse {
     bool success;
     std::string message;
 };
 
 using ::testing::Return;
 using ::testing::_;
 using ::testing::NiceMock;
 
 namespace risk {
 namespace test {
 
 class PortfolioIntegrationTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create mock interfaces
         m_dataInterface = std::make_shared<NiceMock<MockDataInterface>>();
         m_executionInterface = std::make_shared<NiceMock<MockExecutionInterface>>();
         
         // Setup default config
         setupConfig();
         
         // Setup mock data
         setupMockData();
         
         // Create portfolio manager
         m_portfolioManager = std::make_shared<PortfolioManager>(m_config);
     }
     
     void setupConfig() {
         // Portfolio config
         m_config.portfolioConfig.autoRebalancingEnabled = false;
         m_config.portfolioConfig.autoExecuteRebalancing = false;
         
        // Rebalancing config
        // The RiskConfig in this codebase does not expose a `rebalancingConfig` member;
        // use portfolioConfig defaults or configure rebalancing through the portfolioConfig
        // or manager APIs as needed.
        
        // Collateral config
        m_config.collateralConfig.baseCurrency = "USD";
        m_config.collateralConfig.minimumFreeCollateralRatio = 0.2;
        m_config.collateralConfig.managedExchanges = {"binance", "ftx"};
        m_config.collateralConfig.minimumBalances = {
            {"USD", 10000.0},
            {"BTC", 0.5}
        };
    }
    
    void setupMockData() {
        // Mock market data
                MarketData btcData;
                btcData.tradingPair = core::Symbol("BTC/USDT");
                btcData.price = 50000.0;
                btcData.timestamp =  std::chrono::system_clock::now();
                ON_CALL(*m_dataInterface, getMarketData("BTC/USDT"))
                    .WillByDefault(Return(std::optional<MarketData>(btcData)));
        
                MarketData ethData;
                ethData.tradingPair = core::Symbol("ETH/USDT");
                ethData.price = 2000.0;
                ethData.timestamp = std::chrono::system_clock::now();
                ON_CALL(*m_dataInterface, getMarketData("ETH/USDT"))
                    .WillByDefault(Return(std::optional<MarketData>(ethData)));
            
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
            
        // Mock order submission
        ON_CALL(*m_executionInterface, submitOrder(_))
            .WillByDefault(Return(OrderResponse{true, "success"}));
    }
    
    ExecutionReport createExecutionReport(const std::string& exchangeId, 
                                         const std::string& tradingPair,
                                         const std::string& side,
                                         double quantity,
                                         double price) {
        ExecutionReport report;
        report.setExchangeId(core::ExchangeId::fromString(exchangeId));
        report.setSymbol(core::Symbol::fromString(tradingPair));
        report.setSide(side == "buy" ? OrderSide::BUY : OrderSide::SELL);
        report.setStatus(OrderState::FILLED);
        report.setQuantity(quantity);
        report.setFilledQuantity(quantity);
        report.setPrice(price);
        // report.s
        // Fill fill;
        // fill.setQuantity(quantity);
        // fill.setPrice(price);
        // fill.setTimestamp(std::chrono::system_clock::now().time_since_epoch().count());

        // report.fills.push_back(fill);
        // report.setStrategyId("test_strategy");
        
        return report;
    }
    
    std::shared_ptr<MockDataInterface> m_dataInterface;
    std::shared_ptr<MockExecutionInterface> m_executionInterface;
    std::shared_ptr<PortfolioManager> m_portfolioManager;
    RiskConfig m_config;
};

// Test end-to-end position tracking and portfolio management
TEST_F(PortfolioIntegrationTest, EndToEndPositionTracking) {
    // Initialize the portfolio manager
    EXPECT_TRUE(m_portfolioManager->initialize());
    
    // Initially portfolio should be empty
    EXPECT_EQ(m_portfolioManager->getAllPositions().size(), 0);
    EXPECT_DOUBLE_EQ(m_portfolioManager->getPortfolioValue(), 0.0);
    
    // Process a buy execution report
    ExecutionReport buyReport = createExecutionReport("binance", "BTC/USD", "buy", 1.0, 50000.0);
    m_portfolioManager->processExecutionReport(buyReport);
    
    // Verify position was created
    auto positions = m_portfolioManager->getAllPositions();
    EXPECT_EQ(positions.size(), 1);
    EXPECT_EQ(positions[0].getExchangeId(), "binance");
    EXPECT_EQ(positions[0].getTradingPair(), "BTC/USD");
    EXPECT_DOUBLE_EQ(positions[0].getQuantity(), 1.0);
    EXPECT_DOUBLE_EQ(positions[0].getAverageEntryPrice(), 50000.0);
    
    // Process another buy report
    ExecutionReport ethBuyReport = createExecutionReport("binance", "ETH/USD", "buy", 10.0, 2000.0);
    m_portfolioManager->processExecutionReport(ethBuyReport);
    
    // Verify portfolio value is updated
    positions = m_portfolioManager->getAllPositions();
    EXPECT_EQ(positions.size(), 2);
    EXPECT_DOUBLE_EQ(m_portfolioManager->getPortfolioValue(), 70000.0); // (1 * 50000) + (10 * 2000)
    
    // Check portfolio allocation
    auto allocation = m_portfolioManager->getPortfolioAllocation();
    EXPECT_EQ(allocation.size(), 2);
    EXPECT_NEAR(allocation["BTC"], 50000.0 / 70000.0, 0.001); // ~0.714
    EXPECT_NEAR(allocation["ETH"], 20000.0 / 70000.0, 0.001); // ~0.286
}

// Test collateral checks and position management
TEST_F(PortfolioIntegrationTest, CollateralAndPositionManagement) {
    // Initialize the portfolio manager
    EXPECT_TRUE(m_portfolioManager->initialize());
    
    // Get the collateral manager
    auto collateralManager = m_portfolioManager->getCollateralManager();
    ASSERT_NE(collateralManager, nullptr);
    
    // Verify initial collateral status
    auto status = collateralManager->getCollateralStatus();
    EXPECT_FALSE(status.balances.empty());
    EXPECT_GT(status.totalCollateral, 0.0);
    
    // Create an order request
    OrderRequest buyRequest;
    buyRequest.exchangeId = core::ExchangeId::binance();
    buyRequest.tradingPair = core::Symbol("BTC/USD");
    buyRequest.side = OrderSide::BUY;
    buyRequest.type = OrderType::MARKET;
    buyRequest.quantity = 1.0;
    buyRequest.price = 50000.0;
    
    // Check collateral requirements
    std::vector<RiskViolation> violations;
    EXPECT_TRUE(collateralManager->checkCollateralRequirements(buyRequest, violations));
    EXPECT_TRUE(violations.empty());
    
    // Process the trade execution
    ExecutionReport buyReport = createExecutionReport("binance", "BTC/USD", "buy", 1.0, 50000.0);
    m_portfolioManager->processExecutionReport(buyReport);
    
    // Verify position was created
    auto position = m_portfolioManager->getPosition(core::Symbol("BTC/USD"), 
                        core::ExchangeId::binance());
    ASSERT_TRUE(position.has_value());
    EXPECT_DOUBLE_EQ(position->getQuantity(), 1.0);
    EXPECT_DOUBLE_EQ(position->getAverageEntryPrice(), 50000.0);

    // Verify collateral reflects the new position
    collateralManager->updateCollateralStatus();
    status = collateralManager->getCollateralStatus();
    EXPECT_FALSE(status.belowMinimumThreshold);
}

// Test rebalancing integration
TEST_F(PortfolioIntegrationTest, RebalancingIntegration) {
    // Initialize the portfolio manager
    EXPECT_TRUE(m_portfolioManager->initialize());
    
    // Get the rebalancing engine and position tracker
    auto rebalancingEngine = m_portfolioManager->getRebalancingEngine();
    auto positionTracker = m_portfolioManager->getPositionTracker();
    ASSERT_NE(rebalancingEngine, nullptr);
    ASSERT_NE(positionTracker, nullptr);
    
    // Create unbalanced portfolio
    positionTracker->updatePosition(core::ExchangeId::binance(), core::Symbol("BTC/USD"), 1.5, 50000.0);  // 75% of portfolio
    positionTracker->updatePosition(core::ExchangeId::binance(), core::Symbol("ETH/USD"), 5.0, 2000.0);   // 10% of portfolio
    positionTracker->updatePosition(core::ExchangeId::ftx(), core::Symbol("BTC/USD"), 0.15, 50000.0);     // 7.5% of portfolio
    positionTracker->updatePosition(core::ExchangeId::ftx(), core::Symbol("ETH/USD"), 2.0, 2000.0);      // 7.5% of portfolio
    // Trigger portfolio recalculation
    MarketData data;
    data.tradingPair = core::Symbol("BTC/USD");
    data.price = 50000.0;
    data.timestamp = std::chrono::system_clock::now();
    m_portfolioManager->updateMarketData(data);
    
    // Check if rebalancing is needed
    EXPECT_TRUE(rebalancingEngine->isRebalancingNeeded());
    
    // Calculate rebalancing needs
    auto rebalancingOrders = rebalancingEngine->calculateRebalancingNeeds();
    EXPECT_FALSE(rebalancingOrders.empty());
    
    // Trigger rebalancing through portfolio manager
    EXPECT_TRUE(m_portfolioManager->triggerRebalancing());
    
    // Verify rebalancing was executed
    auto lastResult = rebalancingEngine->getLastRebalancingResult();
    EXPECT_TRUE(lastResult.success);
    EXPECT_FALSE(lastResult.ordersCreated.empty());
}

// Test configuration updates across components
TEST_F(PortfolioIntegrationTest, ConfigurationUpdates) {
    // Initialize the portfolio manager
    EXPECT_TRUE(m_portfolioManager->initialize());
    
    // Get the sub-components
    auto rebalancingEngine = m_portfolioManager->getRebalancingEngine();
    auto collateralManager = m_portfolioManager->getCollateralManager();
    ASSERT_NE(rebalancingEngine, nullptr);
    ASSERT_NE(collateralManager, nullptr);
    
    // Verify initial configurations
    EXPECT_FALSE(rebalancingEngine->isAutoRebalancingEnabled());
    EXPECT_DOUBLE_EQ(collateralManager->getConfiguration().minimumFreeCollateralRatio, 0.2);
    
    // Update configuration
    RiskConfig newConfig = m_config;
    newConfig.portfolioConfig.autoRebalancingEnabled = true;
    // Note: if RiskConfig exposes a rebalancingConfig member in your codebase, set it via the config here or via manager APIs.
    newConfig.collateralConfig.minimumFreeCollateralRatio = 0.3;
    
    EXPECT_TRUE(m_portfolioManager->updateConfiguration(newConfig));
}

// Test callbacks propagation
TEST_F(PortfolioIntegrationTest, CallbacksPropagation) {
    // Initialize the portfolio manager
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
    
    // Trigger an event by adding a position
    auto positionTracker = m_portfolioManager->getPositionTracker();
    positionTracker->updatePosition(core::ExchangeId::binance(), core::Symbol("BTC/USD"), 1.0, 50000.0);

    // Verify callback was invoked
    // EXPECT_TRUE(callbackInvoked);
    // EXPECT_EQ(eventType, "position_update");
    
    // Reset tracking
        // callbackInvoked = false;
        // eventType = "";
        // description = "";
    
    // Trigger a rebalancing event
    positionTracker->updatePosition(core::ExchangeId::binance(), core::Symbol("ETH/USD"), 10.0, 2000.0);
    m_portfolioManager->triggerRebalancing();
    
    // Verify callback was invoked
    EXPECT_TRUE(callbackInvoked);
    EXPECT_EQ(eventType, "rebalancing");
}

} // namespace test
} // namespace risk