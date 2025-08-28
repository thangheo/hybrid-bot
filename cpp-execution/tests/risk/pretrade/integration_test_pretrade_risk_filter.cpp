/**
 * Integration tests for pre-trade risk controls
 *
 * File: integration_test_pretrade_risk_filter.cpp
 * Created: 2025-05-05
 */

 #include <gtest/gtest.h>
 #include <memory>
 
 #include "risk/pretrade/pre_trade_risk_filter.hpp"
 #include "risk/risk_types.hpp"
 #include "risk/interfaces/data_interface.hpp"
 #include "risk/portfolio/position_tracker.hpp"
 
 using namespace trading::risk;
 
 // Test DataInterface implementation for integration testing
 class TestDataInterface : public DataInterface {
 public:
     TestDataInterface() : DataInterface(RiskConfig()) {}
     
     bool initialize() override { return true; }
     
     double getPrice(const std::string& symbol) const override {
         if (symbol == "BTC/USDT") return 50000.0;
         if (symbol == "ETH/USDT") return 3000.0;
         return 1.0;  // Default
     }
     
     double getVolatility(const std::string& symbol) const override {
         if (symbol == "BTC/USDT") return 0.2;  // 20%
         if (symbol == "ETH/USDT") return 0.15;  // 15%
         return 0.1;  // Default
     }
     
     MarketData getMarketData() const override {
         MarketData data;
         data.prices["BTC/USDT"] = 50000.0;
         data.prices["ETH/USDT"] = 3000.0;
         data.volatilities["BTC/USDT"] = 0.2;
         data.volatilities["ETH/USDT"] = 0.15;
         return data;
     }
 };
 
 // Test PositionTracker implementation for integration testing
 class TestPositionTracker : public PositionTracker {
 public:
     TestPositionTracker() : PositionTracker(RiskConfig()) {}
     
     bool initialize() override { return true; }
     
     double getPosition(const std::string& asset) const override {
         if (asset == "BTC") return 1.0;
         if (asset == "ETH") return 10.0;
         return 0.0;
     }
     
     double getTotalPositionForStrategy(const std::string& strategyId) const override {
         if (strategyId == "strategy1") return 100000.0;  // In USD equivalent
         return 0.0;
     }
     
     double getTotalPositionForExchange(const core::ExchangeId& exchangeId) const override {
         if (exchangeId == "exchange1") return 200000.0;  // In USD equivalent
         return 0.0;
     }
     
     std::vector<Position> getAllPositions() const override {
         std::vector<Position> positions;
         
         Position btcPosition;
         btcPosition.asset = "BTC";
         btcPosition.tradingPair = TradingPair("BTC/USDT");
         btcPosition.quantity = 1.0;
         btcPosition.averageEntryPrice = 48000.0;
         btcPosition.exchangeId = "exchange1";
         btcPosition.strategyId = "strategy1";
         
         Position ethPosition;
         ethPosition.asset = "ETH";
         ethPosition.tradingPair = TradingPair("ETH/USDT");
         ethPosition.quantity = 10.0;
         ethPosition.averageEntryPrice = 2800.0;
         ethPosition.exchangeId = "exchange1";
         ethPosition.strategyId = "strategy1";
         
         positions.push_back(btcPosition);
         positions.push_back(ethPosition);
         
         return positions;
     }
     
     std::vector<Position> getPositionsForAsset(const std::string& asset) const override {
         std::vector<Position> positions = getAllPositions();
         std::vector<Position> result;
         
         for (const auto& position : positions) {
             if (position.asset == asset) {
                 result.push_back(position);
             }
         }
         
         return result;
     }
     
     std::vector<Position> getPositionsForStrategy(const std::string& strategyId) const override {
         std::vector<Position> positions = getAllPositions();
         std::vector<Position> result;
         
         for (const auto& position : positions) {
             if (position.strategyId == strategyId) {
                 result.push_back(position);
             }
         }
         
         return result;
     }
     
     std::vector<Position> getPositionsForExchange(const core::ExchangeId& exchangeId) const override {
         std::vector<Position> positions = getAllPositions();
         std::vector<Position> result;
         
         for (const auto& position : positions) {
             if (position.exchangeId == exchangeId) {
                 result.push_back(position);
             }
         }
         
         return result;
     }
 };
 
 class PreTradeRiskIntegrationTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create a comprehensive risk config for integration testing
         RiskConfig config;
         
         // Position limits
         PositionLimitConfig positionLimits;
         positionLimits.defaultLimit = 100.0;
         positionLimits.assetLimits["BTC"] = 2.0;
         positionLimits.assetLimits["ETH"] = 20.0;
         positionLimits.strategyLimits["strategy1"] = 300000.0;
         positionLimits.exchangeLimits["exchange1"] = 500000.0;
         config.setPositionLimits(positionLimits);
         
         // Exposure limits
         ExposureLimitConfig exposureLimits;
         exposureLimits.totalExposureLimit = 1000000.0;
         exposureLimits.defaultAssetExposureLimit = 100000.0;
         exposureLimits.defaultStrategyExposureLimit = 300000.0;
         exposureLimits.defaultExchangeExposureLimit = 600000.0;
         exposureLimits.assetExposureLimits["BTC"] = 150000.0;
         exposureLimits.riskFactor = 2.0;
         config.setExposureLimits(exposureLimits);
         
         // Order validation config
         OrderValidationConfig validationConfig;
         validationConfig.defaultMinOrderSize = 0.001;
         validationConfig.maxOrderSize = 5.0;
         validationConfig.maxPriceDeviationPercent = 10.0;
         validationConfig.allowedOrderTypes = {
             OrderType::MARKET, OrderType::LIMIT, OrderType::STOP_LIMIT
         };
         validationConfig.allowedTimeInForce = {"GTC", "IOC", "FOK"};
         validationConfig.symbolWhitelist = {"BTC/USDT", "ETH/USDT"};
         validationConfig.enforceWhitelist = true;
         config.setOrderValidationConfig(validationConfig);
         
         // Fat finger config
         FatFingerConfig ffConfig;
         ffConfig.priceDeviationThreshold = 0.1;  // 10%
         ffConfig.sizeAnomalyThreshold = 5.0;     // 5x typical
         ffConfig.notionalValueThreshold = 250000.0;  // $250k
         ffConfig.defaultTypicalSize = 0.1;
         ffConfig.rejectHighNotionalOrders = false;
         config.setFatFingerConfig(ffConfig);
         
         // Create test data interface and position tracker
         testDataInterface = std::make_shared<TestDataInterface>();
         testPositionTracker = std::make_shared<TestPositionTracker>();
         
         // Create the pre-trade risk filter
         riskFilter = std::make_unique<PreTradeRiskFilter>(config);
         
         // Initialize manually with our test implementations
         riskFilter->m_positionLimitChecker = std::make_unique<PositionLimitChecker>(config, testPositionTracker);
         riskFilter->m_positionLimitChecker->initialize();
         
         riskFilter->m_exposureCalculator = std::make_unique<ExposureCalculator>(config, testPositionTracker, testDataInterface);
         riskFilter->m_exposureCalculator->initialize();
         
         riskFilter->m_orderValidator = std::make_unique<OrderValidator>(config, testDataInterface);
         riskFilter->m_orderValidator->initialize();
         
         riskFilter->m_fatFingerProtection = std::make_unique<FatFingerProtection>(config, testDataInterface);
         riskFilter->m_fatFingerProtection->initialize();
     }
     
     std::unique_ptr<PreTradeRiskFilter> riskFilter;
     std::shared_ptr<TestDataInterface> testDataInterface;
     std::shared_ptr<TestPositionTracker> testPositionTracker;
 };
 
 // Test a valid BTC order within all limits
 TEST_F(PreTradeRiskIntegrationTest, ValidBtcOrder) {
     // Create a valid BTC order
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "integration-test-1";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 0.5;  // Below position limit
     orderRequest.price = 49000.0;  // Within 10% of market price
     orderRequest.timeInForce = "GTC";
     orderRequest.exchangeId = "exchange1";
     orderRequest.strategyId = "strategy1";
     
     // Validate the order
     ValidationResult result = riskFilter->validateOrder(orderRequest);
     
     // Should be approved
     EXPECT_TRUE(result.approved);
     EXPECT_TRUE(result.violations.empty());
     EXPECT_EQ(result.signalId, orderRequest.clientOrderId);
 }
 
 // Test a BTC order that exceeds position limit
 TEST_F(PreTradeRiskIntegrationTest, ExceedsPositionLimit) {
     // Create a BTC order that would exceed position limit
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "integration-test-2";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 1.5;  // Would result in position of 1.0 + 1.5 = 2.5 > limit of 2.0
     orderRequest.price = 49000.0;
     orderRequest.timeInForce = "GTC";
     orderRequest.exchangeId = "exchange1";
     orderRequest.strategyId = "strategy1";
     
     // Validate the order
     ValidationResult result = riskFilter->validateOrder(orderRequest);
     
     // Should be rejected due to position limit
     EXPECT_FALSE(result.approved);
     EXPECT_EQ(result.violations.size(), 1);
     EXPECT_EQ(result.violations[0].violationType, "POSITION_LIMIT_EXCEEDED");
 }
 
 // Test an order with price deviation too large
 TEST_F(PreTradeRiskIntegrationTest, PriceDeviationTooLarge) {
     // Create an ETH order with price too far from market
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "integration-test-3";
     orderRequest.tradingPair = TradingPair("ETH/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 2.0;  // Valid position
     orderRequest.price = 2500.0;  // More than 10% below market price of 3000
     orderRequest.timeInForce = "GTC";
     orderRequest.exchangeId = "exchange1";
     orderRequest.strategyId = "strategy1";
     
     // Validate the order
     ValidationResult result = riskFilter->validateOrder(orderRequest);
     
     // Should be rejected due to price deviation
     EXPECT_FALSE(result.approved);
     EXPECT_EQ(result.violations.size(), 1);
     EXPECT_EQ(result.violations[0].violationType, "PRICE_DEVIATION_EXCEEDED");
 }
 
 // Test an order with non-whitelisted symbol
 TEST_F(PreTradeRiskIntegrationTest, NonWhitelistedSymbol) {
     // Create an order with non-whitelisted symbol
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "integration-test-4";
     orderRequest.tradingPair = TradingPair("XRP/USDT");  // Not in whitelist
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 100.0;
     orderRequest.price = 0.5;
     orderRequest.timeInForce = "GTC";
     orderRequest.exchangeId = "exchange1";
     orderRequest.strategyId = "strategy1";
     
     // Validate the order
     ValidationResult result = riskFilter->validateOrder(orderRequest);
     
     // Should be rejected due to whitelist
     EXPECT_FALSE(result.approved);
     EXPECT_EQ(result.violations.size(), 1);
     EXPECT_EQ(result.violations[0].violationType, "SYMBOL_NOT_WHITELISTED");
 }
 
 // Test an order with high notional value
 TEST_F(PreTradeRiskIntegrationTest, HighNotionalValue) {
     // Create a BTC order with high notional value
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "integration-test-5";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 5.0;  // $250k at price of 50k
     orderRequest.price = 50000.0;
     orderRequest.timeInForce = "GTC";
     orderRequest.exchangeId = "exchange1";
     orderRequest.strategyId = "strategy1";
     
     // Validate the order
     ValidationResult result = riskFilter->validateOrder(orderRequest);
     
     // Should flag for additional verification but not reject
     EXPECT_TRUE(result.violations.size() > 0);
     
     bool hasVerificationFlag = false;
     for (const auto& violation : result.violations) {
         if (violation.violationType == "REQUIRES_ADDITIONAL_VERIFICATION") {
             hasVerificationFlag = true;
             break;
         }
     }
     
     EXPECT_TRUE(hasVerificationFlag);
 }
 
 // Test using a trading signal
 TEST_F(PreTradeRiskIntegrationTest, ValidateTradingSignal) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "integration-signal-1";
     signal.tradingPair = TradingPair("BTC/USDT");
     signal.direction = Direction::BUY;
     signal.targetSize = 0.5;
     signal.strategyId = "strategy1";
     signal.exchangeId = "exchange1";
     
     // Validate the signal
     ValidationResult result = riskFilter->validateSignal(signal);
     
     // Should be approved
     EXPECT_TRUE(result.approved);
     EXPECT_TRUE(result.violations.empty());
     EXPECT_EQ(result.signalId, signal.id);
 }
 
 int main(int argc, char** argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }