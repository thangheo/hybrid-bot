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
    TestDataInterface() : DataInterface() {}

    bool initialize() override { return true; }

    // match DataInterface signature: non-const getPrice
    double getPrice(const core::Symbol& symbol) override {
        if (symbol.toString() == "BTC/USDT") return 50000.0;
        if (symbol.toString() == "ETH/USDT") return 3000.0;
        return 1.0;  // Default
    }

    // match DataInterface signature for volatility
    double getVolatility(const core::Symbol& tradingPair, const std::string& /*timeframe*/, const std::optional<std::string>& /*exchangeId*/ = std::nullopt) override {
        if (tradingPair.toString() == "BTC/USDT") return 0.2;  // 20%
        if (tradingPair.toString() == "ETH/USDT") return 0.15;  // 15%
        return 0.1;  // Default
    }

    // match DataInterface signature: return optional<MarketData>
    std::optional<MarketData> getMarketData(const core::Symbol& tradingPair, const std::optional<std::string>& /*exchangeId*/ = std::nullopt) override {
        MarketData data;
        data.tradingPair = tradingPair;
        if (tradingPair.toString() == "BTC/USDT") data.price = 50000.0;
        if (tradingPair.toString() == "ETH/USDT") data.price = 3000.0;
        return data;
    }
};
    class TestPositionTracker : public PositionTracker {
    public:
        // PositionTracker requires (const RiskConfig&, shared_ptr<DataInterface>)
        TestPositionTracker() : PositionTracker(RiskConfig(), nullptr) {}

        // Note: PositionTracker methods are not virtual in the current API,
        // so do not use 'override' here.
        bool initialize() { return true; }

        // Provide a few helper methods that PositionTracker exposes
        std::vector<Position> getAllPositions() const {
            std::vector<Position> positions;

            // Use Position constructor: (ExchangeId, TradingPair, StrategyId, quantity, avgPrice)
            positions.emplace_back(ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), StrategyId("strategy1"), 1.0, 48000.0);
            positions.emplace_back(ExchangeId::fromString("exchange1"), TradingPair("ETH/USDT"), StrategyId("strategy1"), 10.0, 2800.0);

            return positions;
        }

        std::vector<Position> getExchangePositions(const core::ExchangeId& exchangeId) const {
            std::vector<Position> all = getAllPositions();
            std::vector<Position> res;
            for (const auto& p : all) {
                if (p.getExchangeId() == exchangeId) res.push_back(p);
            }
            return res;
        }

        std::vector<Position> getPositionsByTradingPair(const TradingPair& tradingPair) const {
            std::vector<Position> all = getAllPositions();
            std::vector<Position> res;
            for (const auto& p : all) {
                if (p.getTradingPair() == tradingPair) res.push_back(p);
            }
            return res;
        }

        std::optional<Position> getPosition(const core::ExchangeId& exchangeId, const TradingPair& tradingPair) const {
            for (const auto& p : getAllPositions()) {
                if (p.getExchangeId() == exchangeId && p.getTradingPair() == tradingPair) return p;
            }
            return std::nullopt;
        }

        std::vector<Position> getStrategyPositions(const StrategyId& strategyId) const {
            std::vector<Position> res;
            for (const auto& p : getAllPositions()) {
                if (p.getStrategyId() == strategyId) res.push_back(p);
            }
            return res;
            }
        };

// Test fixture for pre-trade integration
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
         config.setPositionLimitConfig(positionLimits);
         
         // Exposure limits
         ExposureLimitConfig exposureLimits;
         exposureLimits.maxTotalExposure = 1000000.0;
         exposureLimits.maxAssetExposure = 100000.0;
         exposureLimits.maxStrategyExposure = 300000.0;
         exposureLimits.maxExchangeExposure = 600000.0;
         exposureLimits.maxAssetExposure = 150000.0; // adjust to desired value
         exposureLimits.volatilityMultiplier = 2.0;
         config.setExposureLimitConfig(exposureLimits);
         
         // Order validation config
         OrderValidationConfig validationConfig;
         validationConfig.defaultMinOrderSize = 0.001;
         validationConfig.maxOrderSize = 5.0;
         validationConfig.maxPriceDeviationPercent = 10.0;
         validationConfig.allowedOrderTypes = {
             trading::common::OrderType::MARKET,
             trading::common::OrderType::LIMIT,
             trading::common::OrderType::STOP_LIMIT
         };
         validationConfig.allowedTimeInForce = {"GTC", "IOC", "FOK"};
         validationConfig.symbolWhitelist = {"BTC/USDT", "ETH/USDT"};
         // Note: OrderValidationConfig has no 'enforceWhitelist' member; remove the invalid assignment.
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

    // Create the pre-trade risk filter using the data interface; let it initialize components
    riskFilter = std::make_unique<PreTradeRiskFilter>(config, testDataInterface);
    // initialize will set up subcomponents internally
    riskFilter->initialize();
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
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
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
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
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
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
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
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
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
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
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
     signal.setTradingPair(TradingPair("BTC/USDT"));
     signal.direction = Direction::BUY;
     signal.targetSize = 0.5;
     signal.setStrategyId(StrategyId("strategy1"));
     signal.exchangeId = ExchangeId::fromString("exchange1");
     
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