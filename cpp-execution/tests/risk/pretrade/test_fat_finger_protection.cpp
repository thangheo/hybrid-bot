/**
 * Unit tests for fat finger protection
 *
 * File: test_fat_finger_protection.cpp
 * Created: 2025-05-05
 */

 #include <gtest/gtest.h>
 #include <gmock/gmock.h>
 
 #include "risk/pretrade/fat_finger_protection.hpp"
 #include "risk/interfaces/data_interface.hpp"
 #include "risk/risk_types.hpp"
 
 using namespace risk;
 using ::testing::Return;
 using ::testing::_;
 
 // Mock data interface for testing
 class MockDataInterface : public DataInterface {
 public:
     MockDataInterface() : DataInterface(RiskConfig()) {}
     
     MOCK_METHOD(double, getPrice, (const std::string&), (const));
 };
 
 class FatFingerProtectionTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create a risk config with test settings
         RiskConfig config;
         FatFingerConfig ffConfig;
         
         // Set thresholds
         ffConfig.priceDeviationThreshold = 0.1;  // 10%
         ffConfig.sizeAnomalyThreshold = 5.0;  // 5x typical size
         ffConfig.notionalValueThreshold = 250000.0;  // $250k
         ffConfig.defaultTypicalSize = 0.1;  // Default for new symbols
         ffConfig.rejectHighNotionalOrders = false;  // Flag for additional verification
         
         config.setFatFingerConfig(ffConfig);
         
         // Create mock data interface
         mockDataInterface = std::make_shared<MockDataInterface>();
         
         // Create fat finger protection
         fatFingerProtection = std::make_unique<FatFingerProtection>(config, mockDataInterface);
         fatFingerProtection->initialize();
         
         // Add some historical orders
         OrderRequest order1;
         order1.tradingPair = TradingPair("BTC/USDT");
         order1.quantity = 0.1;
         
         OrderRequest order2;
         order2.tradingPair = TradingPair("BTC/USDT");
         order2.quantity = 0.15;
         
         OrderRequest order3;
         order3.tradingPair = TradingPair("BTC/USDT");
         order3.quantity = 0.08;
         
         fatFingerProtection->recordOrder(order1);
         fatFingerProtection->recordOrder(order2);
         fatFingerProtection->recordOrder(order3);
         
         OrderRequest ethOrder1;
         ethOrder1.tradingPair = TradingPair("ETH/USDT");
         ethOrder1.quantity = 1.0;
         
         OrderRequest ethOrder2;
         ethOrder2.tradingPair = TradingPair("ETH/USDT");
         ethOrder2.quantity = 1.5;
         
         fatFingerProtection->recordOrder(ethOrder1);
         fatFingerProtection->recordOrder(ethOrder2);
     }
     
     std::shared_ptr<MockDataInterface> mockDataInterface;
     std::unique_ptr<FatFingerProtection> fatFingerProtection;
 };
 
 // Test with a normal order request
 TEST_F(FatFingerProtectionTest, NormalOrderRequest) {
     // Create a normal order
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-1";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 0.2;  // 2x typical size, but below threshold
     orderRequest.price = 50000.0;
     
     // Set up mock to return current market price
     EXPECT_CALL(*mockDataInterface, getPrice("BTC/USDT"))
         .WillOnce(Return(51000.0));  // Within 10% of order price
     
     // Check for fat finger issues
     std::vector<RiskViolation> violations;
     bool result = fatFingerProtection->checkFatFinger(orderRequest, violations);
     
     // Should pass all checks
     EXPECT_TRUE(result);
     EXPECT_TRUE(violations.empty());
 }
 
 // Test with excessive price deviation
 TEST_F(FatFingerProtectionTest, ExcessivePriceDeviation) {
     // Create an order with price far from market
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-2";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 0.1;
     orderRequest.price = 40000.0;  // 20% below market
     
     // Set up mock to return current market price
     EXPECT_CALL(*mockDataInterface, getPrice("BTC/USDT"))
         .WillOnce(Return(50000.0));
     
     // Check for fat finger issues
     std::vector<RiskViolation> violations;
     bool result = fatFingerProtection->checkFatFinger(orderRequest, violations);
     
     // Should fail price deviation check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     EXPECT_EQ(violations[0].violationType, "PRICE_DEVIATION_TOO_LARGE");
     EXPECT_EQ(violations[0].limitValue, 10.0);  // 10% threshold
     EXPECT_NEAR(violations[0].actualValue, 20.0, 0.1);  // 20% deviation
 }
 
 // Test with abnormally large order size
 TEST_F(FatFingerProtectionTest, AbnormallyLargeSize) {
     // Create an order with size much larger than typical
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-3";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::MARKET;
     orderRequest.quantity = 0.8;  // 8x typical size (threshold is 5x)
     
     // Check for fat finger issues
     std::vector<RiskViolation> violations;
     bool result = fatFingerProtection->checkFatFinger(orderRequest, violations);
     
     // Should fail size anomaly check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     EXPECT_EQ(violations[0].violationType, "ORDER_SIZE_ANOMALY");
     EXPECT_NEAR(violations[0].limitValue, 0.5, 0.01);  // 5x typical size of ~0.1
     EXPECT_EQ(violations[0].actualValue, 0.8);
 }
 
 // Test with high notional value
 TEST_F(FatFingerProtectionTest, HighNotionalValue) {
     // Create an order with high notional value
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-4";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 6.0;  // 6 BTC
     orderRequest.price = 50000.0;  // $300k total (> $250k threshold)
     
     // Set up mock to return current market price
     EXPECT_CALL(*mockDataInterface, getPrice("BTC/USDT"))
         .WillOnce(Return(50000.0));
     
     // Check for fat finger issues
     std::vector<RiskViolation> violations;
     bool result = fatFingerProtection->checkFatFinger(orderRequest, violations);
     
     // Should flag for additional verification but not reject
     EXPECT_TRUE(result);
     EXPECT_EQ(violations.size(), 1);
     EXPECT_EQ(violations[0].violationType, "REQUIRES_ADDITIONAL_VERIFICATION");
     EXPECT_EQ(violations[0].limitValue, 250000.0);
     EXPECT_EQ(violations[0].actualValue, 300000.0);
 }
 
 // Test with trading signal
 TEST_F(FatFingerProtectionTest, CheckTradingSignal) {
     // Create a trading signal with large size
     TradingSignal signal;
     signal.id = "test-signal-1";
     signal.tradingPair = TradingPair("BTC/USDT");
     signal.direction = Direction::BUY;
     signal.targetSize = 0.8;  // 8x typical size
     signal.strategyId = "strategy1";
     signal.exchangeId = "exchange1";
     
     // Check for fat finger issues
     std::vector<RiskViolation> violations;
     bool result = fatFingerProtection->checkFatFinger(signal, violations);
     
     // Should fail size anomaly check
     EXPECT_FALSE(result);
     EXPECT_EQ(violations.size(), 1);
     EXPECT_EQ(violations[0].violationType, "ORDER_SIZE_ANOMALY");
 }
 
 // Test recording orders and typical size calculation
 TEST_F(FatFingerProtectionTest, TypicalSizeCalculation) {
     // Get typical size before adding more orders
     double typicalSizeBefore = fatFingerProtection->getTypicalOrderSize(TradingPair("BTC/USDT"));
     
     // Add several more orders with larger sizes
     for (int i = 0; i < 10; i++) {
         OrderRequest order;
         order.tradingPair = TradingPair("BTC/USDT");
         order.getQuantity() = 0.3;  // Larger than previous orders
         fatFingerProtection->recordOrder(order);
     }
     
     // Get typical size after adding orders
     double typicalSizeAfter = fatFingerProtection->getTypicalOrderSize(TradingPair("BTC/USDT"));
     
     // Typical size should have increased
     EXPECT_GT(typicalSizeAfter, typicalSizeBefore);
 }
 
 // Test updating settings
 TEST_F(FatFingerProtectionTest, UpdateSettings) {
     // Update fat finger protection settings
     FatFingerConfig newConfig;
     newConfig.priceDeviationThreshold = 0.15;  // 15% instead of 10%
     newConfig.sizeAnomalyThreshold = 10.0;  // 10x instead of 5x
     newConfig.notionalValueThreshold = 500000.0;  // $500k instead of $250k
     newConfig.rejectHighNotionalOrders = true;  // Reject instead of verify
     
     bool result = fatFingerProtection->updateSettings(newConfig);
     EXPECT_TRUE(result);
     
     // Test with an order that would have failed before but passes now
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-5";
     orderRequest.tradingPair = TradingPair("BTC/USDT");
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::LIMIT;
     orderRequest.quantity = 0.8;  // 8x typical (now below new threshold of 10x)
     orderRequest.price = 45000.0;  // 10% below market (now below new threshold of 15%)
     
     // Set up mock to return current market price
     EXPECT_CALL(*mockDataInterface, getPrice("BTC/USDT"))
         .WillOnce(Return(50000.0));
     
     // Check for fat finger issues
     std::vector<RiskViolation> violations;
     result = fatFingerProtection->checkFatFinger(orderRequest, violations);
     
     // Should pass all checks with the new thresholds
     EXPECT_TRUE(result);
     EXPECT_TRUE(violations.empty());
 }
 
 // Test with completely new symbol (no history)
 TEST_F(FatFingerProtectionTest, NewSymbol) {
     // Create an order for a new symbol
     OrderRequest orderRequest;
     orderRequest.clientOrderId = "test-order-6";
     orderRequest.tradingPair = TradingPair("SOL/USDT");  // No history for this
     orderRequest.side = OrderSide::BUY;
     orderRequest.type = OrderType::MARKET;
     orderRequest.quantity = 0.3;  // 3x default typical size (0.1)
     
     // Check for fat finger issues
     std::vector<RiskViolation> violations;
     bool result = fatFingerProtection->checkFatFinger(orderRequest, violations);
     
     // Should pass (below 5x threshold)
     EXPECT_TRUE(result);
     EXPECT_TRUE(violations.empty());
 }
 
 int main(int argc, char** argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }