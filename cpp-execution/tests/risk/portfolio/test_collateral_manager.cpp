/**
 * Unit tests for collateral manager component
 * 
 * File: test_collateral_manager.cpp
 * Created: 2025-04-30
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 #include "portfolio/collateral_manager.hpp"
 #include "../mocks/data_interface_mock.hpp"
 #include "../mocks/execution_interface_mock.hpp"
 #include "../mocks/position_tracker_mock.hpp"
 
 using ::testing::Return;
 using ::testing::_;
 using ::testing::NiceMock;
 
 namespace risk {
 namespace test {
 
 class CollateralManagerTest : public ::testing::Test {
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
         
         // Create collateral manager
         m_collateralManager = std::make_shared<CollateralManager>(
             m_config, m_positionTracker, m_executionInterface, m_dataInterface);
     }
     
     void setupConfig() {
         // Collateral config
         m_config.collateralConfig.baseCurrency = "USD";
         m_config.collateralConfig.minimumFreeCollateralRatio = 0.2;
         m_config.collateralConfig.emergencyFundingThreshold = 0.1;
         m_config.collateralConfig.emergencyFundingBuffer = 0.2;
         m_config.collateralConfig.autoInitiateEmergencyFunding = false;
         m_config.collateralConfig.emergencyFundingSource = "reserve";
         m_config.collateralConfig.managedExchanges = {"binance", "ftx"};
         
         // Minimum balances
         m_config.collateralConfig.minimumBalances = {
             {"USD", 10000.0},
             {"BTC", 0.5},
             {"ETH", 5.0}
         };
         
         // Critical asset minimums
         m_config.collateralConfig.criticalAssetMinimums = {
             {"USD", 5000.0},
             {"BTC", 0.2}
         };
         
         // Exchange margin rates
         m_config.collateralConfig.exchangeMarginRates["binance"]["default"] = 0.1;
         m_config.collateralConfig.exchangeMarginRates["ftx"]["default"] = 0.05;
         
         // Asset conversion rates
         m_config.collateralConfig.assetConversionRates["BTC"] = 50000.0;
         m_config.collateralConfig.assetConversionRates["ETH"] = 2000.0;
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
             
         // Mock position data
         std::vector<Position> positions = {
             Position("binance", "BTC/USD", "test_strategy", 1.0, 50000.0),
             Position("binance", "ETH/USD", "test_strategy", 10.0, 2000.0),
             Position("ftx", "BTC/USD", "test_strategy", 0.5, 51000.0)
         };
         
         ON_CALL(*m_positionTracker, getAllPositions())
             .WillByDefault(Return(positions));
     }
     
     OrderRequest createOrderRequest(const core::ExchangeId& exchangeId, 
                                    const std::string& tradingPair,
                                    const std::string& side,
                                    double quantity,
                                    double price) {
         OrderRequest request;
         request.exchangeId = exchangeId;
         request.tradingPair = tradingPair;
         request.side = side;
         request.quantity = quantity;
         request.price = price;
         request.strategyId = "test_strategy";
         
         return request;
     }
     
     std::shared_ptr<MockDataInterface> m_dataInterface;
     std::shared_ptr<MockExecutionInterface> m_executionInterface;
     std::shared_ptr<MockPositionTracker> m_positionTracker;
     std::shared_ptr<CollateralManager> m_collateralManager;
     RiskConfig m_config;
 };
 
 // Test initialization
 TEST_F(CollateralManagerTest, Initialization) {
     EXPECT_TRUE(m_collateralManager->initialize());
     
     // Balances should be fetched during initialization
     auto balances = m_collateralManager->getExchangeBalances();
     EXPECT_FALSE(balances.empty());
 }
 
 // Test collateral status updates
 TEST_F(CollateralManagerTest, CollateralStatusUpdates) {
     EXPECT_TRUE(m_collateralManager->initialize());
     
     // Update collateral status
     EXPECT_TRUE(m_collateralManager->updateCollateralStatus());
     
     // Verify status was updated
     auto status = m_collateralManager->getCollateralStatus();
     EXPECT_GT(status.totalCollateral, 0.0);
     EXPECT_GT(status.freeCollateral, 0.0);
     EXPECT_GT(status.freeCollateralRatio, 0.0);
     EXPECT_FALSE(status.belowMinimumThreshold);
     EXPECT_FALSE(status.emergencyFundingNeeded);
 }
 
 // Test collateral requirements for orders
 TEST_F(CollateralManagerTest, CollateralRequirements) {
     EXPECT_TRUE(m_collateralManager->initialize());
     
     // Test successful collateral check
     std::vector<RiskViolation> violations;
     OrderRequest buyRequest = createOrderRequest("binance", "BTC/USD", "buy", 1.0, 50000.0);
     
     EXPECT_TRUE(m_collateralManager->checkCollateralRequirements(buyRequest, violations));
     EXPECT_TRUE(violations.empty());
     
     // Test insufficient collateral for large order
     violations.clear();
     OrderRequest largeBuyRequest = createOrderRequest("binance", "BTC/USD", "buy", 1000.0, 50000.0);
     
     // Override balance for this test
     ON_CALL(*m_executionInterface, getAccountBalances("binance"))
         .WillByDefault(Return(std::vector<AccountBalance>{
             {"USD", 1000.0, 1000.0, 0.0}
         }));
     
     m_collateralManager->updateCollateralStatus(); // Refresh balances
     
     EXPECT_FALSE(m_collateralManager->checkCollateralRequirements(largeBuyRequest, violations));
     EXPECT_FALSE(violations.empty());
     EXPECT_EQ(violations[0].violation_type, "INSUFFICIENT_COLLATERAL");
     
     // Test sell order with insufficient asset
     violations.clear();
     OrderRequest sellRequest = createOrderRequest("binance", "BTC/USD", "sell", 10.0, 50000.0);
     
     // Override balance for this test
     ON_CALL(*m_executionInterface, getAccountBalances("binance"))
         .WillByDefault(Return(std::vector<AccountBalance>{
             {"BTC", 0.1, 0.1, 0.0}
         }));
     
     m_collateralManager->updateCollateralStatus(); // Refresh balances
     
     EXPECT_FALSE(m_collateralManager->checkCollateralRequirements(sellRequest, violations));
     EXPECT_FALSE(violations.empty());
     EXPECT_EQ(violations[0].violation_type, "INSUFFICIENT_ASSET");
 }
 
 // Test balance retrieval
 TEST_F(CollateralManagerTest, BalanceRetrieval) {
     EXPECT_TRUE(m_collateralManager->initialize());
     
     // Get all balances
     auto allBalances = m_collateralManager->getExchangeBalances();
     EXPECT_FALSE(allBalances.empty());
     
     // Get balances for a specific exchange
     auto binanceBalances = m_collateralManager->getExchangeBalances("binance");
     EXPECT_FALSE(binanceBalances.empty());
     EXPECT_EQ(binanceBalances[0].exchangeId, "binance");
 }
 
 // Test sufficient collateral checks
 TEST_F(CollateralManagerTest, SufficientCollateralChecks) {
     EXPECT_TRUE(m_collateralManager->initialize());
     
     // Test sufficient collateral
     EXPECT_TRUE(m_collateralManager->hasSufficientCollateral("binance", "USD", 50000.0));
     
     // Test insufficient collateral
     EXPECT_FALSE(m_collateralManager->hasSufficientCollateral("binance", "USD", 200000.0));
     
     // Test non-existent asset
     EXPECT_FALSE(m_collateralManager->hasSufficientCollateral("binance", "XRP", 1.0));
 }
 
 // Test margin requirements
 TEST_F(CollateralManagerTest, MarginRequirements) {
     EXPECT_TRUE(m_collateralManager->initialize());
     
     // Test margin calculation for binance
     double binanceMargin = m_collateralManager->calculateMarginRequirement(
         "binance", "BTC/USD", 1.0);
     
     // Should be 10% of notional value (50000 * 0.1 = 5000)
     EXPECT_DOUBLE_EQ(binanceMargin, 5000.0);
     
     // Test margin calculation for ftx
     double ftxMargin = m_collateralManager->calculateMarginRequirement(
         "ftx", "BTC/USD", 1.0);
     
     // Should be 5% of notional value (50000 * 0.05 = 2500)
     EXPECT_DOUBLE_EQ(ftxMargin, 2500.0);
 }
 
 // Test configuration updates
 TEST_F(CollateralManagerTest, ConfigurationUpdates) {
     EXPECT_TRUE(m_collateralManager->initialize());
     
     // Update configuration
     CollateralConfig newConfig = m_config.collateralConfig;
     newConfig.minimumFreeCollateralRatio = 0.3;
     newConfig.emergencyFundingThreshold = 0.15;
     
     EXPECT_TRUE(m_collateralManager->updateConfiguration(newConfig));
     
     // Verify config was updated
     auto currentConfig = m_collateralManager->getConfiguration();
     EXPECT_DOUBLE_EQ(currentConfig.minimumFreeCollateralRatio, 0.3);
     EXPECT_DOUBLE_EQ(currentConfig.emergencyFundingThreshold, 0.15);
 }
 
 // Test emergency funding
 TEST_F(CollateralManagerTest, EmergencyFunding) {
     EXPECT_TRUE(m_collateralManager->initialize());
     
     // Setup low balances to trigger emergency funding
     ON_CALL(*m_executionInterface, getAccountBalances("binance"))
         .WillByDefault(Return(std::vector<AccountBalance>{
             {"USD", 1000.0, 1000.0, 0.0}, // Below critical minimum
             {"BTC", 0.1, 0.1, 0.0}        // Below critical minimum
         }));
     
     m_collateralManager->updateCollateralStatus();
     
     // Initiate emergency funding
     EXPECT_TRUE(m_collateralManager->initiateEmergencyFunding());
     
     // Verify funding history was updated
     auto fundingHistory = m_collateralManager->getFundingHistory();
     EXPECT_FALSE(fundingHistory.empty());
 }
 
 // Test callbacks
 TEST_F(CollateralManagerTest, CollateralCallbacks) {
     EXPECT_TRUE(m_collateralManager->initialize());
     
     // Setup callback tracking
     bool callbackInvoked = false;
     std::string eventType;
     std::string description;
     
     m_collateralManager->registerCollateralCallback(
         [&callbackInvoked, &eventType, &description](const std::string& type, const std::string& desc) {
             callbackInvoked = true;
             eventType = type;
             description = desc;
         }
     );
     
     // Setup low balances to trigger a callback
     ON_CALL(*m_executionInterface, getAccountBalances("binance"))
         .WillByDefault(Return(std::vector<AccountBalance>{
             {"USD", 1000.0, 1000.0, 0.0} // Below minimum
         }));
     
     m_collateralManager->updateCollateralStatus();
     
     // Verify callback was invoked
     EXPECT_TRUE(callbackInvoked);
     EXPECT_FALSE(eventType.empty());
     EXPECT_FALSE(description.empty());
 }
 
 } // namespace test
 } // namespace risk