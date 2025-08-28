/**
 * @file order_lifecycle_tracker_tests.cpp
 * @brief Unit tests for OrderLifecycleTracker
 * 
 * Created on: May 6, 2025
 */

 #include "execution/order_lifecycle_tracker.hpp"
 #include <gtest/gtest.h>
 #include <chrono>
 #include <thread>
 
 using namespace trading::execution;
 
 class OrderLifecycleTrackerTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create a tracker for each test
         tracker = std::make_shared<OrderLifecycleTracker>();
     }
 
     // Helper function to create a test order
     Order createTestOrder(const std::string& client_id, const std::string& symbol, 
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
 
     std::shared_ptr<OrderLifecycleTracker> tracker;
 };
 
 // Test adding a new order
 TEST_F(OrderLifecycleTrackerTest, AddOrder) {
     Order order = createTestOrder("client-123", "BTC-USDT", OrderSide::BUY, 0.1, 50000.0);
     
     // Add the order
     bool result = tracker->addOrder(order);
     EXPECT_TRUE(result);
     
     // Try to retrieve it
     auto retrieved = tracker->getOrderByClientId("client-123");
     ASSERT_NE(retrieved, nullptr);
     EXPECT_EQ(retrieved->getClientOrderId(), "client-123");
     EXPECT_EQ(retrieved->getTradingPair(), "BTC-USDT");
     EXPECT_EQ(retrieved->getSide(), OrderSide::BUY);
     EXPECT_DOUBLE_EQ(retrieved->getQuantity(), 0.1);
     EXPECT_DOUBLE_EQ(retrieved->getPrice(), 50000.0);
 }
 
 // Test adding a duplicate order
 TEST_F(OrderLifecycleTrackerTest, AddDuplicateOrder) {
     Order order1 = createTestOrder("client-123", "BTC-USDT", OrderSide::BUY, 0.1, 50000.0);
     Order order2 = createTestOrder("client-123", "ETH-USDT", OrderSide::SELL, 1.0, 3000.0);
     
     // Add the first order
     bool result1 = tracker->addOrder(order1);
     EXPECT_TRUE(result1);
     
     // Try to add a duplicate
     bool result2 = tracker->addOrder(order2);
     EXPECT_FALSE(result2); // Should fail with duplicate client ID
 }
 
 // Test updating order state
 TEST_F(OrderLifecycleTrackerTest, UpdateOrderState) {
     Order order = createTestOrder("client-123", "BTC-USDT", OrderSide::BUY, 0.1, 50000.0);
     tracker->addOrder(order);
     
     // Update to PENDING
     bool result1 = tracker->updateOrderState("client-123", OrderState::PENDING);
     EXPECT_TRUE(result1);
     
     // Get the order and check its state
     auto order1 = tracker->getOrderByClientId("client-123");
     ASSERT_NE(order1, nullptr);
     EXPECT_EQ(order1->getState(), OrderState::PENDING);
     
     // Update to OPEN with exchange ID
     bool result2 = tracker->updateOrderState("client-123", OrderState::OPEN, "exchange-456");
     EXPECT_TRUE(result2);
     
     // Get the order and check its state and exchange ID
     auto order2 = tracker->getOrderByClientId("client-123");
     ASSERT_NE(order2, nullptr);
     EXPECT_EQ(order2->getState(), OrderState::OPEN);
     EXPECT_EQ(order2->getExchangeOrderId(), "exchange-456");
     
     // Try to get the order by exchange ID
     auto order3 = tracker->getOrderByExchangeId("exchange-456");
     ASSERT_NE(order3, nullptr);
     EXPECT_EQ(order3->getClientOrderId(), "client-123");
 }
 
 // Test updating a non-existent order
 TEST_F(OrderLifecycleTrackerTest, UpdateNonExistentOrder) {
     bool result = tracker->updateOrderState("non-existent", OrderState::PENDING);
     EXPECT_FALSE(result);
 }
 
 // Test recording a fill
 TEST_F(OrderLifecycleTrackerTest, RecordFill) {
     Order order = createTestOrder("client-123", "BTC-USDT", OrderSide::BUY, 0.1, 50000.0);
     tracker->addOrder(order);
     tracker->updateOrderState("client-123", OrderState::OPEN, "exchange-456");
     
     // Create a fill
     Fill fill;
     fill.fill_id = "fill-789";
     fill.orderId = "client-123";
     fill.quantity = 0.1;
     fill.price = 50000.0;
     fill.timestamp = std::chrono::system_clock::now();
     
     // Record the fill
     bool result = tracker->recordFill("client-123", fill);
     EXPECT_TRUE(result);
     
     // Get the order and check its state
     auto filled_order = tracker->getOrderByClientId("client-123");
     ASSERT_NE(filled_order, nullptr);
     EXPECT_EQ(filled_order->getState(), OrderState::FILLED);
     EXPECT_DOUBLE_EQ(filled_order->getFilledQuantity(), 0.1);
     EXPECT_DOUBLE_EQ(filled_order->getAverageFillPrice(), 50000.0);
 }
 
 // Test partial fills
 TEST_F(OrderLifecycleTrackerTest, PartialFills) {
     Order order = createTestOrder("client-123", "BTC-USDT", OrderSide::BUY, 0.2, 50000.0);
     tracker->addOrder(order);
     tracker->updateOrderState("client-123", OrderState::OPEN, "exchange-456");
     
     // Create first fill (50%)
     Fill fill1;
     fill1.fill_id = "fill-789";
     fill1.orderId = "client-123";
     fill1.quantity = 0.1;
     fill1.price = 50000.0;
     fill1.timestamp = std::chrono::system_clock::now();
     
     // Record the first fill
     bool result1 = tracker->recordFill("client-123", fill1);
     EXPECT_TRUE(result1);
     
     // Get the order and check its state after partial fill
     auto partial_order = tracker->getOrderByClientId("client-123");
     ASSERT_NE(partial_order, nullptr);
     EXPECT_EQ(partial_order->getState(), OrderState::PARTIALLY_FILLED);
     EXPECT_DOUBLE_EQ(partial_order->getFilledQuantity(), 0.1);
     EXPECT_DOUBLE_EQ(partial_order->getRemainingQuantity(), 0.1);
     
     // Create second fill (remaining 50%)
     Fill fill2;
     fill2.fill_id = "fill-790";
     fill2.orderId = "client-123";
     fill2.quantity = 0.1;
     fill2.price = 50010.0;  // Different price
     fill2.timestamp = std::chrono::system_clock::now();
     
     // Record the second fill
     bool result2 = tracker->recordFill("client-123", fill2);
     EXPECT_TRUE(result2);
     
     // Get the order and check its state after being fully filled
     auto filled_order = tracker->getOrderByClientId("client-123");
     ASSERT_NE(filled_order, nullptr);
     EXPECT_EQ(filled_order->getState(), OrderState::FILLED);
     EXPECT_DOUBLE_EQ(filled_order->getFilledQuantity(), 0.2);
     EXPECT_DOUBLE_EQ(filled_order->getRemainingQuantity(), 0.0);
     // Average price should be (50000*0.1 + 50010*0.1) / 0.2 = 50005
     EXPECT_DOUBLE_EQ(filled_order->getAverageFillPrice(), 50005.0);
 }
 
 // Test state change callbacks
 TEST_F(OrderLifecycleTrackerTest, StateChangeCallbacks) {
     bool callback_called = false;
     OrderState old_state_in_callback;
     OrderState new_state_in_callback;
     
     // Register a callback
     tracker->registerStateChangeCallback([&](const Order& order, OrderState old_state, OrderState new_state) {
         callback_called = true;
         old_state_in_callback = old_state;
         new_state_in_callback = new_state;
     });
     
     // Add and update an order
     Order order = createTestOrder("client-123", "BTC-USDT", OrderSide::BUY, 0.1, 50000.0);
     tracker->addOrder(order);
     tracker->updateOrderState("client-123", OrderState::PENDING);
     
     // Check if callback was triggered
     EXPECT_TRUE(callback_called);
     EXPECT_EQ(old_state_in_callback, OrderState::CREATED);
     EXPECT_EQ(new_state_in_callback, OrderState::PENDING);
 }
 
 // Test getting orders by state
 TEST_F(OrderLifecycleTrackerTest, GetOrdersByState) {
     // Add multiple orders in different states
     Order order1 = createTestOrder("client-1", "BTC-USDT", OrderSide::BUY, 0.1, 50000.0);
     Order order2 = createTestOrder("client-2", "ETH-USDT", OrderSide::SELL, 1.0, 3000.0);
     Order order3 = createTestOrder("client-3", "SOL-USDT", OrderSide::BUY, 5.0, 100.0);
     
     tracker->addOrder(order1);
     tracker->addOrder(order2);
     tracker->addOrder(order3);
     
     // Update states
     tracker->updateOrderState("client-1", OrderState::OPEN);
     tracker->updateOrderState("client-2", OrderState::OPEN);
     tracker->updateOrderState("client-3", OrderState::PENDING);
     
     // Get orders by state
     auto open_orders = tracker->getOrdersByState(OrderState::OPEN);
     auto pending_orders = tracker->getOrdersByState(OrderState::PENDING);
     auto created_orders = tracker->getOrdersByState(OrderState::CREATED);
     
     // Check results
     EXPECT_EQ(open_orders.size(), 2);
     EXPECT_EQ(pending_orders.size(), 1);
     EXPECT_EQ(created_orders.size(), 0);
 }
 
 // Test getting orders by symbol
 TEST_F(OrderLifecycleTrackerTest, GetOrdersBySymbol) {
     // Add multiple orders with different symbols
     Order order1 = createTestOrder("client-1", "BTC-USDT", OrderSide::BUY, 0.1, 50000.0);
     Order order2 = createTestOrder("client-2", "ETH-USDT", OrderSide::SELL, 1.0, 3000.0);
     Order order3 = createTestOrder("client-3", "BTC-USDT", OrderSide::BUY, 0.2, 49000.0);
     
     tracker->addOrder(order1);
     tracker->addOrder(order2);
     tracker->addOrder(order3);
     
     // Get orders by symbol
     auto btc_orders = tracker->getOrdersBySymbol("BTC-USDT");
     auto eth_orders = tracker->getOrdersBySymbol("ETH-USDT");
     auto sol_orders = tracker->getOrdersBySymbol("SOL-USDT");
     
     // Check results
     EXPECT_EQ(btc_orders.size(), 2);
     EXPECT_EQ(eth_orders.size(), 1);
     EXPECT_EQ(sol_orders.size(), 0);
 }
 
 // Test checking for timed-out orders
 TEST_F(OrderLifecycleTrackerTest, CheckForTimedOutOrders) {
     // Add an order
     Order order = createTestOrder("client-123", "BTC-USDT", OrderSide::BUY, 0.1, 50000.0);
     tracker->addOrder(order);
     tracker->updateOrderState("client-123", OrderState::PENDING);
     
     // Check for timed-out orders immediately (shouldn't find any)
     auto immediate_timeouts = tracker->checkForTimedOutOrders(5000);
     EXPECT_EQ(immediate_timeouts.size(), 0);
     
     // Wait a bit
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     // Check with a very short timeout (should find our order)
     auto short_timeout_orders = tracker->checkForTimedOutOrders(50);
     EXPECT_EQ(short_timeout_orders.size(), 1);
 }
 
 // Test purging old orders
 TEST_F(OrderLifecycleTrackerTest, PurgeOldOrders) {
     // Add orders in terminal states
     Order order1 = createTestOrder("client-1", "BTC-USDT", OrderSide::BUY, 0.1, 50000.0);
     Order order2 = createTestOrder("client-2", "ETH-USDT", OrderSide::SELL, 1.0, 3000.0);
     
     tracker->addOrder(order1);
     tracker->addOrder(order2);
     
     // Set to terminal states
     tracker->updateOrderState("client-1", OrderState::FILLED);
     tracker->updateOrderState("client-2", OrderState::CANCELED);
     
     // Wait a bit
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     // Purge with a very short age threshold
     int purged = tracker->purgeOldOrders(50);
     EXPECT_EQ(purged, 2);
     
     // Check that orders were purged
     auto order1_after = tracker->getOrderByClientId("client-1");
     auto order2_after = tracker->getOrderByClientId("client-2");
     EXPECT_EQ(order1_after, nullptr);
     EXPECT_EQ(order2_after, nullptr);
 }
 
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }