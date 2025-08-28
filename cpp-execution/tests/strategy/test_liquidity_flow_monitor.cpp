/**
 * @file test_liquidity_flow_monitor.cpp
 * @brief Unit tests for the LiquidityFlowMonitor class
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #include <gtest/gtest.h>
 #include "strategy/core_analysis/liquidity_flow_monitor.hpp"
 #include "data_ingestion/orderbook/order_book.hpp"
 
 using namespace trading::strategy;
 using namespace trading::data_ingestion;
 
 // Helper function to create an OrderBook with specified bids and asks
 OrderBook createOrderBook(const std::string& exchange_id, const std::string& trading_pair,
                           const std::vector<PriceLevel>& bids, const std::vector<PriceLevel>& asks,
                           uint64_t sequence_number = 1) {
     OrderBook book(exchange_id, trading_pair);
     book.applySnapshot(bids, asks, sequence_number, std::chrono::system_clock::now());
     return book;
 }
 
 // Helper to create a price level
 PriceLevel createPriceLevel(double price, double quantity) {
     return PriceLevel(price, quantity);
 }
 
 class LiquidityFlowMonitorTest : public ::testing::Test {
 protected:
     void SetUp() override {
         // Create a monitor with default parameters
         monitor = std::make_shared<LiquidityFlowMonitor>("binance", "BTC/USDT", 5.0, 60);
     }
 
     // Helper to create order book deltas
     OrderBookDelta createDelta(const std::vector<PriceLevel>& added_bids, 
                                const std::vector<PriceLevel>& removed_bids,
                                const std::vector<PriceLevel>& added_asks,
                                const std::vector<PriceLevel>& removed_asks,
                                uint64_t sequence_number = 1) {
         OrderBookDelta delta;
         delta.exchange_id = "binance";
         delta.trading_pair = "BTC/USDT";
         delta.timestamp = std::chrono::system_clock::now();
         delta.sequence_number = sequence_number;
         delta.added_bids = added_bids;
         delta.removed_bids = removed_bids;
         delta.added_asks = added_asks;
         delta.removed_asks = removed_asks;
         return delta;
     }
 
     std::shared_ptr<LiquidityFlowMonitor> monitor;
 };
 
 TEST_F(LiquidityFlowMonitorTest, ProcessDelta) {
     // Create sample delta with imbalanced liquidity
     auto delta = createDelta(
         {createPriceLevel(50000.0, 10.0)},  // added_bids
         {},                                  // removed_bids
         {},                                  // added_asks
         {createPriceLevel(50100.0, 5.0)}     // removed_asks
     );
     
     // Process the delta
     monitor->processOrderBookDelta(delta);
     
     // Check metrics
     auto metrics = monitor->getLiquidityMetrics();
     
     // Verify basic metric calculations
     EXPECT_GT(metrics.bid_add_rate, 0.0);
     EXPECT_EQ(metrics.bid_cancel_rate, 0.0);
     EXPECT_GT(metrics.ask_cancel_rate, 0.0);
     EXPECT_EQ(metrics.ask_add_rate, 0.0);
     
     // Verify direction of flow imbalance (should be positive/buy)
     EXPECT_GT(metrics.flow_imbalance, 0.0);
 }
 
 TEST_F(LiquidityFlowMonitorTest, ProcessOrderBookSnapshots) {
     // Create two consecutive order book snapshots
     std::vector<PriceLevel> bids1 = {
         createPriceLevel(50000.0, 1.0),
         createPriceLevel(49900.0, 2.0)
     };
     
     std::vector<PriceLevel> asks1 = {
         createPriceLevel(50100.0, 1.0),
         createPriceLevel(50200.0, 2.0)
     };
     
     std::vector<PriceLevel> bids2 = {
         createPriceLevel(50000.0, 2.0),  // Increased quantity
         createPriceLevel(49900.0, 2.0)
     };
     
     std::vector<PriceLevel> asks2 = {
         createPriceLevel(50100.0, 0.5),  // Decreased quantity
         createPriceLevel(50200.0, 2.0)
     };
     
     auto book1 = createOrderBook("binance", "BTC/USDT", bids1, asks1, 1);
     auto book2 = createOrderBook("binance", "BTC/USDT", bids2, asks2, 2);
     
     // Process the snapshots
     monitor->processOrderBookSnapshots(book1, book2);
     
     // Verify that a delta was created and processed
     auto metrics = monitor->getLiquidityMetrics();
     EXPECT_GT(metrics.bid_add_rate, 0.0);
     EXPECT_GT(metrics.ask_cancel_rate, 0.0);
     
     // Verify flow imbalance direction (should be positive/buy)
     EXPECT_GT(metrics.flow_imbalance, 0.0);
 }
 
 TEST_F(LiquidityFlowMonitorTest, DetectSignificantEvents) {
     // Create a large imbalance
     std::vector<PriceLevel> added_bids = {
         createPriceLevel(50000.0, 50.0)  // Large order
     };
     
     auto delta = createDelta(added_bids, {}, {}, {});
     
     // Process the delta
     monitor->processOrderBookDelta(delta);
     
     // Check if significant events are detected
     EXPECT_TRUE(monitor->hasSignificantEvents(0.5));
     
     // Get the events
     auto events = monitor->detectLiquidityEvents();
     
     // Verify that events were generated
     ASSERT_FALSE(events.events.empty());
     
     // Verify event properties
     auto first_event = events.events[0];
     EXPECT_EQ(first_event.direction, Direction::BUY);
     EXPECT_GE(first_event.significance, 0.5);
 }
 
 TEST_F(LiquidityFlowMonitorTest, LargeOrderDetection) {
     // Set a smaller large order threshold for testing
     monitor->setLargeOrderThreshold(2.0);
     
     // Create a delta with a mix of regular and large orders
     std::vector<PriceLevel> added_bids = {
         createPriceLevel(50000.0, 1.0),  // Regular order
         createPriceLevel(49900.0, 5.0)   // Large order (>2x typical)
     };
     
     auto delta = createDelta(added_bids, {}, {}, {});
     
     // Process the delta
     monitor->processOrderBookDelta(delta);
     
     // Get metrics
     auto metrics = monitor->getLiquidityMetrics();
     
     // Large order ratio should be non-zero
     EXPECT_GT(metrics.large_order_ratio, 0.0);
     
     // Detect events
     auto events = monitor->detectLiquidityEvents();
     
     // Verify large order events
     bool found_large_order_event = false;
     for (const auto& event : events.events) {
         if (event.type == LiquidityEvent::Type::LARGE_ORDER_ADDED) {
             found_large_order_event = true;
             break;
         }
     }
     
     EXPECT_TRUE(found_large_order_event);
 }
 
 TEST_F(LiquidityFlowMonitorTest, SpreadCollapseDetection) {
     // Create order books with changing spreads
     std::vector<PriceLevel> bids1 = {createPriceLevel(50000.0, 1.0)};
     std::vector<PriceLevel> asks1 = {createPriceLevel(50200.0, 1.0)}; // Spread: 200
     
     std::vector<PriceLevel> bids2 = {createPriceLevel(50000.0, 1.0)};
     std::vector<PriceLevel> asks2 = {createPriceLevel(50180.0, 1.0)}; // Spread: 180
     
     std::vector<PriceLevel> bids3 = {createPriceLevel(50000.0, 1.0)};
     std::vector<PriceLevel> asks3 = {createPriceLevel(50050.0, 1.0)}; // Spread: 50 (collapse)
     
     auto book1 = createOrderBook("binance", "BTC/USDT", bids1, asks1, 1);
     auto book2 = createOrderBook("binance", "BTC/USDT", bids2, asks2, 2);
     auto book3 = createOrderBook("binance", "BTC/USDT", bids3, asks3, 3);
     
     // Process first two books to establish baseline
     monitor->processOrderBookSnapshots(book1, book2);
     
     // Process the collapse
     monitor->processOrderBookSnapshots(book2, book3);
     
     // Detect events
     auto events = monitor->detectLiquidityEvents();
     
     // Verify spread collapse events
     bool found_spread_collapse = false;
     for (const auto& event : events.events) {
         if (event.type == LiquidityEvent::Type::SPREAD_COLLAPSE) {
             found_spread_collapse = true;
             break;
         }
     }
     
     EXPECT_TRUE(found_spread_collapse);
 }
 
 int main(int argc, char **argv) {
     ::testing::InitGoogleTest(&argc, argv);
     return RUN_ALL_TESTS();
 }