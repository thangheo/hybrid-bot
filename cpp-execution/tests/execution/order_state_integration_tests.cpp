/**
 * @file order_state_integration_tests.cpp
 * @brief Integration tests for Order State Management components
 * 
 * Created on: May 6, 2025
 */

 #include "execution/order_lifecycle_tracker.hpp"
 #include "execution/fill_reconciliation.hpp"
 #include <gtest/gtest.h>
 #include <memory>
 #include <vector>
 #include <string>
 
 using namespace trading::execution;
 using namespace testing;
 using namespace trading::execution::models;
 using namespace trading::core;
//  using namespace trading::core::models;
using namespace trading;

 class OrderStateIntegrationTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create components for each test
         lifecycle_tracker = std::make_shared<OrderLifecycleTracker>();
         fill_reconciliation = std::make_shared<FillReconciliation>();
         
         // Set up event recording for testing
         state_changes.clear();
         fill_discrepancies.clear();
         
         // Register callbacks
         lifecycle_tracker->registerStateChangeCallback(
             [this](const Order& order, OrderState old_state, OrderState new_state) {
                 recordStateChange(order.getClientOrderId(), old_state, new_state);
             });
             
         fill_reconciliation->registerDiscrepancyCallback(
             [this](const FillDiscrepancy& discrepancy) {
                 recordDiscrepancy(discrepancy);
             });
     }
     
     // Helper function to create a test order
     Order createTestOrder(const std::string& client_id, const core::Symbol& symbol, 
                           OrderSide side, double qty, double price) {
         Order order;
         order.setClientOrderId(client_id);
         order.setTradingPair(symbol);
         order.setSide(side);
         order.setQuantity(qty);
         order.setPrice(price);
         order.setType(OrderType::LIMIT);
         return order;
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
         fill.fee = qty * price * 0.001; // 0.1% fee for testing
         fill.fee_currency = "USDT";
         fill.timestamp = std::chrono::system_clock::now();
         return fill;
     }
     
     // Record state changes for testing
     void recordStateChange(const std::string& order_id, OrderState old_state, OrderState new_state) {
         state_changes.push_back({order_id, old_state, new_state});
     }
     
     // Record discrepancies for testing
     void recordDiscrepancy(const FillDiscrepancy& discrepancy) {
         fill_discrepancies.push_back(discrepancy);
     }
     
     std::shared_ptr<OrderLifecycleTracker> lifecycle_tracker;
     std::shared_ptr<FillReconciliation> fill_reconciliation;
     
     // Event recording for testing
     struct StateChangeEvent {
         std::string order_id;
         OrderState old_state;
         OrderState new_state;
     };
     
     std::vector<StateChangeEvent> state_changes;
     std::vector<FillDiscrepancy> fill_discrepancies;
 };
 
 // Test the complete order lifecycle with fill reconciliation
 TEST_F(OrderStateIntegrationTest, CompleteOrderLifecycle) {
     // 1. Create an order
     Order order = createTestOrder("client-123", core::Symbol("BTC-USDT"), OrderSide::BUY, 0.1, 50000.0);
     
     // 2. Add to lifecycle tracker
     lifecycle_tracker->addOrder(order);
     
     // 3. Progress the order through states
     lifecycle_tracker->updateOrderState("client-123", OrderState::PENDING);
     lifecycle_tracker->updateOrderState("client-123", OrderState::OPEN, "exchange-456");
     
     // 4. Create expected fill
     Fill expected_fill = createTestFill("fill-789", "client-123", "exchange-456", 0.1, 50000.0);
     
     // 5. Record expected fill in reconciliation
     fill_reconciliation->recordExpectedFill("client-123", expected_fill);
     
     // 6. Create actual fill from exchange
     Fill actual_fill = createTestFill("fill-789", "client-123", "exchange-456", 0.1, 50000.0);
     
     // 7. Record the actual fill in reconciliation
     bool reconciled = fill_reconciliation->recordActualFill("client-123", actual_fill);
     EXPECT_TRUE(reconciled);
     
     // 8. Record the fill in lifecycle tracker
     bool fill_recorded = lifecycle_tracker->recordFill("client-123", actual_fill);
     EXPECT_TRUE(fill_recorded);
     
     // 9. Check order state
     auto order_ptr = lifecycle_tracker->getOrderByClientId("client-123");
     ASSERT_NE(order_ptr, nullptr);
     EXPECT_EQ(order_ptr->getState(), OrderState::FILLED);
     
     // 10. Check fill reconciliation
     auto reconciled_fills = fill_reconciliation->getReconciledFills("client-123");
     EXPECT_EQ(reconciled_fills.size(), 1);
     
     // 11. Check state change events
     ASSERT_EQ(state_changes.size(), 3);
     EXPECT_EQ(state_changes[0].old_state, OrderState::CREATED);
     EXPECT_EQ(state_changes[0].new_state, OrderState::PENDING);
     EXPECT_EQ(state_changes[1].old_state, OrderState::PENDING);
     EXPECT_EQ(state_changes[1].new_state, OrderState::OPEN);
     EXPECT_EQ(state_changes[2].old_state, OrderState::OPEN);
     EXPECT_EQ(state_changes[2].new_state, OrderState::FILLED);
     
     // 12. Check discrepancies (should be none)
     EXPECT_EQ(fill_discrepancies.size(), 0);
 }
 
 // Test partial fills and reconciliation
 TEST_F(OrderStateIntegrationTest, PartialFillsAndReconciliation) {
     // Create an order for 0.3 BTC
     Order order = createTestOrder("client-123", core::Symbol("BTC-USDT"), OrderSide::BUY, 0.3, 50000.0);
     lifecycle_tracker->addOrder(order);
     lifecycle_tracker->updateOrderState("client-123", OrderState::OPEN, "exchange-456");
     
     // Create three expected fills (0.1 BTC each)
     Fill expected_fill1 = createTestFill("fill-1", "client-123", "exchange-456", 0.1, 50000.0);
     Fill expected_fill2 = createTestFill("fill-2", "client-123", "exchange-456", 0.1, 50010.0);
     Fill expected_fill3 = createTestFill("fill-3", "client-123", "exchange-456", 0.1, 50020.0);
     
     // Record expected fills
     fill_reconciliation->recordExpectedFill("client-123", expected_fill1);
     fill_reconciliation->recordExpectedFill("client-123", expected_fill2);
     fill_reconciliation->recordExpectedFill("client-123", expected_fill3);
     
     // Process first fill
     Fill actual_fill1 = createTestFill("fill-1", "client-123", "exchange-456", 0.1, 50000.0);
     bool reconciled1 = fill_reconciliation->recordActualFill("client-123", actual_fill1);
     bool recorded1 = lifecycle_tracker->recordFill("client-123", actual_fill1);
     
     EXPECT_TRUE(reconciled1);
     EXPECT_TRUE(recorded1);
     
     // Check order state after first fill
     auto order_after_fill1 = lifecycle_tracker->getOrderByClientId("client-123");
     ASSERT_NE(order_after_fill1, nullptr);
     EXPECT_EQ(order_after_fill1->getState(), OrderState::PARTIALLY_FILLED);
     EXPECT_DOUBLE_EQ(order_after_fill1->getFilledQuantity(), 0.1);
     
     // Process second fill
     Fill actual_fill2 = createTestFill("fill-2", "client-123", "exchange-456", 0.1, 50010.0);
     bool reconciled2 = fill_reconciliation->recordActualFill("client-123", actual_fill2);
     bool recorded2 = lifecycle_tracker->recordFill("client-123", actual_fill2);
     
     EXPECT_TRUE(reconciled2);
     EXPECT_TRUE(recorded2);
     
     // Check order state after second fill
     auto order_after_fill2 = lifecycle_tracker->getOrderByClientId("client-123");
     ASSERT_NE(order_after_fill2, nullptr);
     EXPECT_EQ(order_after_fill2->getState(), OrderState::PARTIALLY_FILLED);
     EXPECT_DOUBLE_EQ(order_after_fill2->getFilledQuantity(), 0.2);
     
     // Process third fill (should complete the order)
     Fill actual_fill3 = createTestFill("fill-3", "client-123", "exchange-456", 0.1, 50020.0);
     bool reconciled3 = fill_reconciliation->recordActualFill("client-123", actual_fill3);
     bool recorded3 = lifecycle_tracker->recordFill("client-123", actual_fill3);
     
     EXPECT_TRUE(reconciled3);
     EXPECT_TRUE(recorded3);
     
     // Check order state after third fill
     auto order_after_fill3 = lifecycle_tracker->getOrderByClientId("client-123");
     ASSERT_NE(order_after_fill3, nullptr);
     EXPECT_EQ(order_after_fill3->getState(), OrderState::FILLED);
     EXPECT_DOUBLE_EQ(order_after_fill3->getFilledQuantity(), 0.3);
     
     // Average price should be (50000*0.1 + 50010*0.1 + 50020*0.1) / 0.3 = 50010.0
     EXPECT_DOUBLE_EQ(order_after_fill3->getAverageFillPrice(), 50010.0);
     
     // Check state changes
     ASSERT_EQ(state_changes.size(), 4);  // CREATED->OPEN, OPEN->PARTIALLY_FILLED, PARTIALLY_FILLED->PARTIALLY_FILLED, PARTIALLY_FILLED->FILLED
     
     // Check reconciliation
     auto reconciled_fills = fill_reconciliation->getReconciledFills("client-123");
     EXPECT_EQ(reconciled_fills.size(), 3);
 }
 
 // Test handling of out-of-order fills
 TEST_F(OrderStateIntegrationTest, OutOfOrderFills) {
     // Create an order for 0.3 BTC
     Order order = createTestOrder("client-123", core::Symbol("BTC-USDT"), OrderSide::BUY, 0.3, 50000.0);
     lifecycle_tracker->addOrder(order);
     lifecycle_tracker->updateOrderState("client-123", OrderState::OPEN, "exchange-456");
     
     // Create three expected fills
     Fill expected_fill1 = createTestFill("fill-1", "client-123", "exchange-456", 0.1, 50000.0);
     Fill expected_fill2 = createTestFill("fill-2", "client-123", "exchange-456", 0.1, 50010.0);
     Fill expected_fill3 = createTestFill("fill-3", "client-123", "exchange-456", 0.1, 50020.0);
     
     // Record expected fills
     fill_reconciliation->recordExpectedFill("client-123", expected_fill1);
     fill_reconciliation->recordExpectedFill("client-123", expected_fill2);
     fill_reconciliation->recordExpectedFill("client-123", expected_fill3);
     
     // Process fills out of order (3, 1, 2)
     Fill actual_fill3 = createTestFill("fill-3", "client-123", "exchange-456", 0.1, 50020.0);
     bool reconciled3 = fill_reconciliation->recordActualFill("client-123", actual_fill3);
     bool recorded3 = lifecycle_tracker->recordFill("client-123", actual_fill3);
     
     EXPECT_TRUE(reconciled3);
     EXPECT_TRUE(recorded3);
     
     Fill actual_fill1 = createTestFill("fill-1", "client-123", "exchange-456", 0.1, 50000.0);
     bool reconciled1 = fill_reconciliation->recordActualFill("client-123", actual_fill1);
     bool recorded1 = lifecycle_tracker->recordFill("client-123", actual_fill1);
     
     EXPECT_TRUE(reconciled1);
     EXPECT_TRUE(recorded1);
     
     Fill actual_fill2 = createTestFill("fill-2", "client-123", "exchange-456", 0.1, 50010.0);
     bool reconciled2 = fill_reconciliation->recordActualFill("client-123", actual_fill2);
     bool recorded2 = lifecycle_tracker->recordFill("client-123", actual_fill2);
     
     EXPECT_TRUE(reconciled2);
     EXPECT_TRUE(recorded2);
     
     // Order should be FILLED in the end
     auto final_order = lifecycle_tracker->getOrderByClientId("client-123");
     ASSERT_NE(final_order, nullptr);
     EXPECT_EQ(final_order->getState(), OrderState::FILLED);
     EXPECT_DOUBLE_EQ(final_order->getFilledQuantity(), 0.3);
     
     // All fills should be reconciled
     auto reconciled_fills = fill_reconciliation->getReconciledFills("client-123");
     EXPECT_EQ(reconciled_fills.size(), 3);
 }
 
 // Test discrepancy handling
 TEST_F(OrderStateIntegrationTest, DiscrepancyHandling) {
     // Create an order
     Order order = createTestOrder("client-123", core::Symbol("BTC-USDT"), OrderSide::BUY, 0.1, 50000.0);
     lifecycle_tracker->addOrder(order);
     lifecycle_tracker->updateOrderState("client-123", OrderState::OPEN, "exchange-456");
     
     // Create expected fill
     Fill expected_fill = createTestFill("fill-1", "client-123", "exchange-456", 0.1, 50000.0);
     fill_reconciliation->recordExpectedFill("client-123", expected_fill);
     
     // Create actual fill with a discrepancy in quantity
     Fill actual_fill = createTestFill("fill-1", "client-123", "exchange-456", 0.2, 50000.0);
     
     // Record the actual fill - should detect discrepancy
     bool reconciled = fill_reconciliation->recordActualFill("client-123", actual_fill);
     EXPECT_FALSE(reconciled);
     
     // Process fill in lifecycle tracker anyway
     bool recorded = lifecycle_tracker->recordFill("client-123", actual_fill);
     EXPECT_TRUE(recorded);
     
     // Order should be FILLED
     auto order_ptr = lifecycle_tracker->getOrderByClientId("client-123");
     ASSERT_NE(order_ptr, nullptr);
     EXPECT_EQ(order_ptr->getState(), OrderState::FILLED);
     
     // Check that discrepancy was recorded
     ASSERT_EQ(fill_discrepancies.size(), 1);
     EXPECT_EQ(fill_discrepancies[0].discrepancy_type, "QUANTITY");
     EXPECT_DOUBLE_EQ(fill_discrepancies[0].expected_quantity, 0.1);
     EXPECT_DOUBLE_EQ(fill_discrepancies[0].actual_quantity, 0.2);
 }
 
 // Test cancelled order
 TEST_F(OrderStateIntegrationTest, CancelledOrder) {
     // Create an order
     Order order = createTestOrder("client-123", core::Symbol("BTC-USDT"), OrderSide::BUY, 0.1, 50000.0);
     lifecycle_tracker->addOrder(order);
     lifecycle_tracker->updateOrderState("client-123", OrderState::OPEN, "exchange-456");
     
     // Create expected fill
     Fill expected_fill = createTestFill("fill-1", "client-123", "exchange-456", 0.1, 50000.0);
     fill_reconciliation->recordExpectedFill("client-123", expected_fill);
     
     // Cancel the order
     lifecycle_tracker->updateOrderState("client-123", OrderState::CANCELED);
     
     // Check missing fills
     auto missing_fills = fill_reconciliation->checkForMissingFills(0);
     EXPECT_EQ(missing_fills.size(), 1);
     
     // Order should be CANCELED
     auto order_ptr = lifecycle_tracker->getOrderByClientId("client-123");
     ASSERT_NE(order_ptr, nullptr);
     EXPECT_EQ(order_ptr->getState(), OrderState::CANCELED);
     
     // State changes should reflect cancellation
     ASSERT_GE(state_changes.size(), 2);
     EXPECT_EQ(state_changes.back().old_state, OrderState::OPEN);
     EXPECT_EQ(state_changes.back().new_state, OrderState::CANCELED);
 }
 
 // Test rejected order
 TEST_F(OrderStateIntegrationTest, RejectedOrder) {
     // Create an order
     Order order = createTestOrder("client-123", core::Symbol("BTC-USDT"), OrderSide::BUY, 0.1, 50000.0);
     lifecycle_tracker->addOrder(order);
     
     // Update to PENDING
     lifecycle_tracker->updateOrderState("client-123", OrderState::PENDING);
     
     // Reject the order
     lifecycle_tracker->updateOrderState("client-123", OrderState::REJECTED);
     
     // Order should be REJECTED
     auto order_ptr = lifecycle_tracker->getOrderByClientId("client-123");
     ASSERT_NE(order_ptr, nullptr);
     EXPECT_EQ(order_ptr->getState(), OrderState::REJECTED);
     
     // State changes should reflect rejection
     ASSERT_EQ(state_changes.size(), 2);
     EXPECT_EQ(state_changes[0].old_state, OrderState::CREATED);
     EXPECT_EQ(state_changes[0].new_state, OrderState::PENDING);
     EXPECT_EQ(state_changes[1].old_state, OrderState::PENDING);
     EXPECT_EQ(state_changes[1].new_state, OrderState::REJECTED);
 }
 
 // Test purging old data
 TEST_F(OrderStateIntegrationTest, PurgingOldData) {
     // Create several orders
     for (int i = 1; i <= 5; i++) {
         std::string client_id = "client-" + std::to_string(i);
         Order order = createTestOrder(client_id, core::Symbol("BTC-USDT"), OrderSide::BUY, 0.1, 50000.0);
         lifecycle_tracker->addOrder(order);
         
         // Set different states
         if (i <= 2) {
             // Set to terminal state
             lifecycle_tracker->updateOrderState(client_id, OrderState::FILLED);
             
             // Create fills for reconciliation
             Fill expected_fill = createTestFill("fill-" + std::to_string(i), client_id, "exchange-" + std::to_string(i), 0.1, 50000.0);
             fill_reconciliation->recordExpectedFill(client_id, expected_fill);
             
             Fill actual_fill = createTestFill("fill-" + std::to_string(i), client_id, "exchange-" + std::to_string(i), 0.1, 50000.0);
             fill_reconciliation->recordActualFill(client_id, actual_fill);
         } else {
             // Keep active
             lifecycle_tracker->updateOrderState(client_id, OrderState::OPEN, "exchange-" + std::to_string(i));
         }
     }
     
     // Wait a bit
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     // Purge old data with short timeout
     int purged_orders = lifecycle_tracker->purgeOldOrders(50);
     int purged_fills = fill_reconciliation->purgeOldReconciliationData(50);
     
     // Should have purged first 2 orders and their fills
     EXPECT_EQ(purged_orders, 2);
     EXPECT_GE(purged_fills, 2);
     
     // Check that active orders remain
     for (int i = 1; i <= 5; i++) {
         std::string client_id = "client-" + std::to_string(i);
         auto order = lifecycle_tracker->getOrderByClientId(client_id);
         
         if (i <= 2) {
             // Should be purged
             EXPECT_EQ(order, nullptr);
         } else {
             // Should remain
             ASSERT_NE(order, nullptr);
             EXPECT_EQ(order->getState(), OrderState::OPEN);
         }
     }
 }
 
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }