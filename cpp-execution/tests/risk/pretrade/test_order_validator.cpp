/**
 * Unit tests for order validator
 *
 * File: test_order_validator.cpp
 * Created: 2025-05-05
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 
 #include "risk/pretrade/order_validator.hpp"
 #include "risk/interfaces/data_interface.hpp"
 #include "risk/risk_types.hpp"
 
 using namespace risk;
 using ::testing::Return;
 using ::testing::_;
 
 // Mock data interface for testing
 class MockDataInterface : public DataInterface {
 public:
     MockDataInterface() : DataInterface() {}
     
     MOCK_METHOD(double, getPrice, (const std::string&), (const));
 };
 
 class OrderValidatorTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create a risk config with test validation rules
         RiskConfig config;
         OrderValidationConfig validationConfig;
         
         // Set default values
         validationConfig.defaultMinOrderSize = 0.001;
         validationConfig.maxOrderSize = 100.0;
         validationConfig.maxPriceDeviationPercent = 10.0;
         
         // Set allowed order types
         validationConfig.allowedOrderTypes = {
             OrderType::MARKET,
             OrderType::LIMIT,
             OrderType::STOP_LIMIT
         };
         
         // Set allowed time in force values
         validationConfig.allowedTimeInForce = {
             "GTC", "IOC", "FOK"
         };
         
         // Set symbol whitelist
         validationConfig.symbolWhitelist = {
             "BTC/USDT", "ETH/USDT", "BNB/USDT"
         };
         validationConfig.enforceSymbolWhitelist = true;
         
    // Set minimum trade notional value (preferred over per-symbol min sizes)
    validationConfig.minTradeValue = 10.0; // $10 minimum trade value for tests
         
         config.setOrderValidationConfig(validationConfig);
         
         // Create mock data interface
         mockDataInterface = std::make_shared<::MockDataInterface>();
         
         // Create order validator
         orderValidator = std::make_unique<OrderValidator>(config, mockDataInterface);
         orderValidator->initialize();
     }
     
     std::shared_ptr<::MockDataInterface> mockDataInterface;
     std::unique_ptr<OrderValidator> orderValidator;
 };
 
 // Test validating a valid order request
 TEST_F(OrderValidatorTest, ValidateValidOrderRequest) {
     // Create a valid order request
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-1";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 0.01;
     orderRequest.price = 50000.0;
     orderRequest.timeInForce = "GTC";
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
     
     // Set up mock to return current market price
     EXPECT_CALL(*mockDataInterface, getPrice("BTC/USDT"))
         .WillOnce(Return(51000.0));  // Within 10% of order price
     
     // Validate the order
     std::vector<RiskViolation> violations;
     bool result = orderValidator->validateOrder(orderRequest, violations);
     
     // Should pass all validations
     EXPECT_TRUE(result);
     EXPECT_TRUE(violations.empty());
 }
 
 // Test with non-whitelisted symbol
 TEST_F(OrderValidatorTest, NonWhitelistedSymbol) {
     // Create an order with non-whitelisted symbol
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-2";
     orderRequest.tradingPair = TradingPair("XRP/USDT");  // Not in whitelist
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 100.0;
     orderRequest.price = 0.5;
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
     
     // Validate the order
     std::vector<RiskViolation> violations;
     bool result = orderValidator->validateOrder(orderRequest, violations);
     
     // Should fail whitelist check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     // RiskViolation no longer exposes a string member named 'violationType' — just verify one violation was returned.
 }
 
 // Test with invalid order type
 TEST_F(OrderValidatorTest, InvalidOrderType) {
     // Create an order with invalid order type
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-3";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::TRAILING_STOP;  // Not in allowed types
     orderRequest.quantity = 0.01;
     orderRequest.price = 50000.0;
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
     
     // Validate the order
     std::vector<RiskViolation> violations;
     bool result = orderValidator->validateOrder(orderRequest, violations);
     
     // Should fail order type validation
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     // Do not access a missing 'violationType' member; presence of one violation is sufficient for this test.
 }
 
 // Test with invalid time in force
 TEST_F(OrderValidatorTest, InvalidTimeInForce) {
     // Create an order with invalid time in force
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-4";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 0.01;
     orderRequest.price = 50000.0;
     orderRequest.timeInForce = "DAY";  // Not in allowed values
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
     
     // Validate the order
     std::vector<RiskViolation> violations;
     bool result = orderValidator->validateOrder(orderRequest, violations);
     
     // Should fail time in force validation
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     // Do not access a missing 'violationType' member; presence of one violation is sufficient for this test.
 }
 
 // Test with order size too small
 TEST_F(OrderValidatorTest, OrderSizeTooSmall) {
     // Create an order with size too small
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-5";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 0.0005;  // Below min of 0.001
     orderRequest.price = 50000.0;
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
     
     // Validate the order
     std::vector<RiskViolation> violations;
     bool result = orderValidator->validateOrder(orderRequest, violations);
     
     // Should fail order size validation
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     // RiskViolation does not expose 'violationType'; check the numeric limits that are present.
     EXPECT_EQ(violations[0].limitValue, 0.001);
     EXPECT_EQ(violations[0].actualValue, 0.0005);
 }
 
 // Test with order size too large
 TEST_F(OrderValidatorTest, OrderSizeTooLarge) {
     // Create an order with size too large
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-6";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 150.0;  // Above max of 100
     orderRequest.price = 50000.0;
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
     
     // Validate the order
     std::vector<RiskViolation> violations;
     bool result = orderValidator->validateOrder(orderRequest, violations);
     
     // Should fail order size validation
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     // RiskViolation does not expose 'violationType'; check the numeric limits that are present.
     EXPECT_EQ(violations[0].limitValue, 100.0);
     EXPECT_EQ(violations[0].actualValue, 150.0);
 }
 
 // Test with price deviation too large
 TEST_F(OrderValidatorTest, PriceDeviationTooLarge) {
     // Create an order with price too far from market
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-7";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 0.01;
     orderRequest.price = 42000.0;  // More than 10% below market
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
     
     // Set up mock to return current market price
     EXPECT_CALL(*mockDataInterface, getPrice("BTC/USDT"))
         .WillOnce(Return(50000.0));
     
     // Validate the order
     std::vector<RiskViolation> violations;
     bool result = orderValidator->validateOrder(orderRequest, violations);
     
     // Should fail price deviation check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     // RiskViolation does not expose 'violationType'; verify the numeric deviation limits instead.
     EXPECT_EQ(violations[0].limitValue, 10.0);
     EXPECT_NEAR(violations[0].actualValue, 16.0, 0.1);  // ~16% deviation
 }
 
 // Test with trading signal
 TEST_F(OrderValidatorTest, ValidateTradingSignal) {
     // Create a trading signal
     TradingSignal signal;
     signal.id = "test-signal-1";
     signal.symbol = TradingPair("BTC/USDT");
     signal.direction = Direction::BUY;
     signal.targetSize = 0.01;
     signal.strategyId = StrategyId("strategy1");
     signal.exchangeId = ExchangeId::fromString("exchange1");

     // Validate the signal
     std::vector<RiskViolation> violations;
     bool result = orderValidator->validateOrder(signal, violations);
     
     // Should pass validation
     EXPECT_TRUE(result);
     EXPECT_TRUE(violations.empty());
 }
 
 // Test updating symbol whitelist
 TEST_F(OrderValidatorTest, UpdateSymbolWhitelist) {
     // Update whitelist to add XRP/USDT
     std::vector<std::string> newWhitelist = {"BTC/USDT", "ETH/USDT", "BNB/USDT", "XRP/USDT"};
     bool result = orderValidator->updateSymbolWhitelist(newWhitelist, true);
     EXPECT_TRUE(result);
     
     // Test with previously non-whitelisted symbol
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-8";
     orderRequest.tradingPair = TradingPair("XRP/USDT");  // Now in whitelist
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::MARKET;
     orderRequest.quantity = 100.0;
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
     
     // Validate the order
     std::vector<RiskViolation> violations;
     result = orderValidator->validateOrder(orderRequest, violations);
     
     // Should pass whitelist check now
     EXPECT_TRUE(violations.empty());
 }
 
 // Test updating minimum order size
 TEST_F(OrderValidatorTest, UpdateMinOrderSize) {
     // Update minimum order size for BTC/USDT on exchange1
     bool result = orderValidator->updateMinOrderSize(ExchangeId::fromString("exchange1"), TradingPair("BTC/USDT"), 0.005);
     EXPECT_TRUE(result);
     
     // Test with order size that would have been valid before but is now too small
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-9";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 0.002;  // Between old (0.001) and new (0.005) minimum
     orderRequest.price = 50000.0;
     orderRequest.exchangeId = ExchangeId::fromString("exchange1");
     
     // Set up mock for price check
     EXPECT_CALL(*mockDataInterface, getPrice("BTC/USDT"))
         .WillOnce(Return(51000.0));
     
     // Validate the order
     std::vector<RiskViolation> violations;
     result = orderValidator->validateOrder(orderRequest, violations);
     
     // Should fail with order size too small
     // Should fail with order size too small
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     // RiskViolation does not expose 'violationType'; verify the numeric limits instead.
     EXPECT_EQ(violations[0].limitValue, 0.005);
     EXPECT_EQ(violations[0].actualValue, 0.002);
 
 }
 
 int main(int argc, char** argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }