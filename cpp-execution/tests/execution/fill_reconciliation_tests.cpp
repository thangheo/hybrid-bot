/**
 * @file fill_reconciliation_tests.cpp
 * @brief Unit tests for FillReconciliation
 * 
 * Created on: May 6, 2025
 */

 #include "execution/fill_reconciliation.hpp"
 #include <gtest/gtest.h>
 #include <chrono>
 #include <thread>
 #include <functional>
 
 using namespace trading::execution;
 
 class FillReconciliationTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create a reconciliation service for each test
         reconciliation = std::make_shared<FillReconciliation>();
     }
 
     // Helper function to create a test fill
     Fill createTestFill(const std::string& fill_id, const std::string& order_id, 
                         const std::string& exchange_order_id, double qty, double price) {
         Fill fill;
         fill.fill_id = fill_id;
         fill.orderId = order_id;
         fill.exchangeOrderId = exchange_order_id;
         fill.quantity = qty;
         fill.price = price;
         fill.fee = 0.0;  // Not testing fees here
         fill.fee_currency = "USDT";
         fill.timestamp = std::chrono::system_clock::now();
         return fill;
     }
 
     std::shared_ptr<FillReconciliation> reconciliation;
 };
 
 // Test recording and matching fills
 TEST_F(FillReconciliationTest, RecordAndMatchFills) {
     // Create expected fill
     Fill expected = createTestFill("fill-123", "order-123", "exchange-123", 0.1, 50000.0);
     
     // Record expected fill
     reconciliation->recordExpectedFill("order-123", expected);
     
     // Create matching actual fill
     Fill actual = createTestFill("fill-123", "order-123", "exchange-123", 0.1, 50000.0);
     
     // Record actual fill - should match and reconcile
     bool result = reconciliation->recordActualFill("order-123", actual);
     EXPECT_TRUE(result);
     
     // Check reconciled fills
     auto reconciled = reconciliation->getReconciledFills("order-123");
     EXPECT_EQ(reconciled.size(), 1);
     
     // Check unreconciled fills (should be empty)
     auto unrec_expected = reconciliation->getUnreconciledExpectedFills("order-123");
     auto unrec_actual = reconciliation->getUnreconciledActualFills("order-123");
     EXPECT_EQ(unrec_expected.size(), 0);
     EXPECT_EQ(unrec_actual.size(), 0);
 }
 
 // Test price discrepancy detection
 TEST_F(FillReconciliationTest, PriceDiscrepancy) {
     bool discrepancy_detected = false;
     std::string discrepancy_type;
     
     // Register callback to detect discrepancies
     reconciliation->registerDiscrepancyCallback([&](const FillDiscrepancy& discrepancy) {
         discrepancy_detected = true;
         discrepancy_type = discrepancy.discrepancy_type;
     });
     
     // Create expected fill
     Fill expected = createTestFill("fill-123", "order-123", "exchange-123", 0.1, 50000.0);
     
     // Record expected fill
     reconciliation->recordExpectedFill("order-123", expected);
     
     // Create actual fill with price discrepancy (same quantity, different price)
     Fill actual = createTestFill("fill-123", "order-123", "exchange-123", 0.1, 50100.0);
     
     // Record actual fill - should not reconcile due to price discrepancy
     bool result = reconciliation->recordActualFill("order-123", actual);
     EXPECT_FALSE(result);
     
     // Check if discrepancy was detected
     EXPECT_TRUE(discrepancy_detected);
     EXPECT_EQ(discrepancy_type, "PRICE");
     
     // Check unreconciled fills (both should still be unreconciled)
     auto unrec_expected = reconciliation->getUnreconciledExpectedFills("order-123");
     auto unrec_actual = reconciliation->getUnreconciledActualFills("order-123");
     EXPECT_EQ(unrec_expected.size(), 1);
     EXPECT_EQ(unrec_actual.size(), 1);
 }
 
 // Test quantity discrepancy detection
 TEST_F(FillReconciliationTest, QuantityDiscrepancy) {
     bool discrepancy_detected = false;
     std::string discrepancy_type;
     
     // Register callback to detect discrepancies
     reconciliation->registerDiscrepancyCallback([&](const FillDiscrepancy& discrepancy) {
         discrepancy_detected = true;
         discrepancy_type = discrepancy.discrepancy_type;
     });
     
     // Create expected fill
     Fill expected = createTestFill("fill-123", "order-123", "exchange-123", 0.1, 50000.0);
     
     // Record expected fill
     reconciliation->recordExpectedFill("order-123", expected);
     
     // Create actual fill with quantity discrepancy (same price, different quantity)
     Fill actual = createTestFill("fill-123", "order-123", "exchange-123", 0.2, 50000.0);
     
     // Record actual fill - should not reconcile due to quantity discrepancy
     bool result = reconciliation->recordActualFill("order-123", actual);
     EXPECT_FALSE(result);
     
     // Check if discrepancy was detected
     EXPECT_TRUE(discrepancy_detected);
     EXPECT_EQ(discrepancy_type, "QUANTITY");
 }
 
 // Test extra fill detection
 TEST_F(FillReconciliationTest, ExtraFillDetection) {
     bool discrepancy_detected = false;
     std::string discrepancy_type;
     
     // Register callback to detect discrepancies
     reconciliation->registerDiscrepancyCallback([&](const FillDiscrepancy& discrepancy) {
         discrepancy_detected = true;
         discrepancy_type = discrepancy.discrepancy_type;
     });
     
     // Record actual fill without any expected fill
     Fill actual = createTestFill("fill-123", "order-123", "exchange-123", 0.1, 50000.0);
     bool result = reconciliation->recordActualFill("order-123", actual);
     
     // Should not reconcile
     EXPECT_FALSE(result);
     
     // Check if discrepancy was detected
     EXPECT_TRUE(discrepancy_detected);
     EXPECT_EQ(discrepancy_type, "EXTRA");
     
     // Check extra fills
     auto extra_fills = reconciliation->checkForExtraFills();
     EXPECT_EQ(extra_fills.size(), 1);
     EXPECT_EQ(extra_fills[0].discrepancy_type, "EXTRA");
 }
 
 // Test missing fill detection
 TEST_F(FillReconciliationTest, MissingFillDetection) {
     // Create expected fill
     Fill expected = createTestFill("fill-123", "order-123", "exchange-123", 0.1, 50000.0);
     
     // Record expected fill
     reconciliation->recordExpectedFill("order-123", expected);
     
     // Check for missing fills immediately (shouldn't find any with long timeout)
     auto immediate_check = reconciliation->checkForMissingFills(5000);
     EXPECT_EQ(immediate_check.size(), 0);
     
     // Wait a bit
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     // Check with a very short timeout (should find our fill)
     auto missing_fills = reconciliation->checkForMissingFills(50);
     EXPECT_EQ(missing_fills.size(), 1);
     EXPECT_EQ(missing_fills[0].discrepancy_type, "MISSING");
 }
 
 // Test fill ID matching
 TEST_F(FillReconciliationTest, FillIdMatching) {
     // Create expected fill
     Fill expected = createTestFill("fill-123", "order-123", "exchange-123", 0.1, 50000.0);
     
     // Record expected fill
     reconciliation->recordExpectedFill("order-123", expected);
     
     // Create actual fill with different price and quantity but same fill ID
     Fill actual = createTestFill("fill-123", "order-123", "exchange-123", 0.2, 50100.0);
     
     // Record actual fill - should match based on fill ID despite discrepancies
     bool result = reconciliation->recordActualFill("order-123", actual);
     EXPECT_TRUE(result);
     
     // Check reconciled fills
     auto reconciled = reconciliation->getReconciledFills("order-123");
     EXPECT_EQ(reconciled.size(), 1);
 }
 
 // Test multiple fills for same order
 TEST_F(FillReconciliationTest, MultipleOrderFills) {
     // Create and record three expected fills for the same order
     Fill expected1 = createTestFill("fill-1", "order-123", "exchange-123", 0.1, 50000.0);
     Fill expected2 = createTestFill("fill-2", "order-123", "exchange-123", 0.2, 50010.0);
     Fill expected3 = createTestFill("fill-3", "order-123", "exchange-123", 0.3, 50020.0);
     
     reconciliation->recordExpectedFill("order-123", expected1);
     reconciliation->recordExpectedFill("order-123", expected2);
     reconciliation->recordExpectedFill("order-123", expected3);
     
     // Record matching actual fills, but in different order
     Fill actual2 = createTestFill("fill-2", "order-123", "exchange-123", 0.2, 50010.0);
     Fill actual1 = createTestFill("fill-1", "order-123", "exchange-123", 0.1, 50000.0);
     Fill actual3 = createTestFill("fill-3", "order-123", "exchange-123", 0.3, 50020.0);
     
     bool result1 = reconciliation->recordActualFill("order-123", actual2);
     bool result2 = reconciliation->recordActualFill("order-123", actual1);
     bool result3 = reconciliation->recordActualFill("order-123", actual3);
     
     EXPECT_TRUE(result1);
     EXPECT_TRUE(result2);
     EXPECT_TRUE(result3);
     
     // Check reconciled fills
     auto reconciled = reconciliation->getReconciledFills("order-123");
     EXPECT_EQ(reconciled.size(), 3);
 }
 
 // Test purging old reconciliation data
 TEST_F(FillReconciliationTest, PurgeOldData) {
     // Create and record fills
     Fill expected = createTestFill("fill-1", "order-123", "exchange-123", 0.1, 50000.0);
     reconciliation->recordExpectedFill("order-123", expected);
     
     Fill actual = createTestFill("fill-1", "order-123", "exchange-123", 0.1, 50000.0);
     reconciliation->recordActualFill("order-123", actual);
     
     // Wait a bit
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     // Purge with a very short age threshold
     int purged = reconciliation->purgeOldReconciliationData(50);
     EXPECT_EQ(purged, 1); // One fill record purged
     
     // Check that data was purged
     auto reconciled = reconciliation->getReconciledFills("order-123");
     EXPECT_EQ(reconciled.size(), 0);
 }
 
 // Test multiple discrepancy callbacks
 TEST_F(FillReconciliationTest, MultipleCallbacks) {
     int callback1_count = 0;
     int callback2_count = 0;
     
     // Register multiple callbacks
     reconciliation->registerDiscrepancyCallback([&](const FillDiscrepancy&) {
         callback1_count++;
     });
     
     reconciliation->registerDiscrepancyCallback([&](const FillDiscrepancy&) {
         callback2_count++;
     });
     
     // Create a scenario that will trigger a discrepancy
     Fill expected = createTestFill("fill-1", "order-123", "exchange-123", 0.1, 50000.0);
     reconciliation->recordExpectedFill("order-123", expected);
     
     Fill actual = createTestFill("fill-1", "order-123", "exchange-123", 0.2, 50000.0);
     reconciliation->recordActualFill("order-123", actual);
     
     // Both callbacks should have been called
     EXPECT_EQ(callback1_count, 1);
     EXPECT_EQ(callback2_count, 1);
 }
 
 // Test discrepancy toString
 TEST_F(FillReconciliationTest, DiscrepancyToString) {
     // Create a discrepancy
     FillDiscrepancy discrepancy;
     discrepancy.orderId = "order-123";
     discrepancy.exchange_id = "exchange-123";
     discrepancy.trading_pair = "BTC-USDT";
     discrepancy.expected_quantity = 0.1;
     discrepancy.actual_quantity = 0.2;
     discrepancy.expected_price = 50000.0;
     discrepancy.actual_price = 50000.0;
     discrepancy.discrepancy_type = "QUANTITY";
     discrepancy.detectionTime = std::chrono::system_clock::now();
     
     // Convert to string
     std::string str = discrepancy.toString();
     
     // Check that the string contains relevant information
     EXPECT_TRUE(str.find("order-123") != std::string::npos);
     EXPECT_TRUE(str.find("QUANTITY") != std::string::npos);
     EXPECT_TRUE(str.find("0.1") != std::string::npos);
     EXPECT_TRUE(str.find("0.2") != std::string::npos);
 }
 
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }