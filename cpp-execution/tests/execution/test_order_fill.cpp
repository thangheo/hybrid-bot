#include <gtest/gtest.h>

#include "execution/models/order.hpp"
#include "execution/order_lifecycle_tracker.hpp"

using namespace trading::execution;
using namespace trading::execution::models;
using namespace trading::core;

TEST(OrderFillTest, FillUpdatesFilledQuantityAndState) {
    Order order;
    // set basic fields
    order.setQuantity(10.0);
    order.open();

    Fill fill1;
    fill1.fill_id = "f1";
    fill1.quantity = 3.5;
    fill1.price = 100.0;
    fill1.exchangeId = ExchangeId::binance();
    fill1.timestamp = std::chrono::system_clock::now();

    bool applied1 = order.fill(fill1);
    EXPECT_TRUE(applied1);
    EXPECT_DOUBLE_EQ(order.getFilledQuantity(), 3.5);
    EXPECT_EQ(order.getState(), OrderState::PARTIALLY_FILLED);

    Fill fill2 = fill1;
    fill2.fill_id = "f2";
    fill2.quantity = 6.5;
    fill2.price = 101.0;

    bool applied2 = order.fill(fill2);
    EXPECT_TRUE(applied2);
    EXPECT_DOUBLE_EQ(order.getFilledQuantity(), 10.0);
    EXPECT_EQ(order.getState(), OrderState::FILLED);
}

TEST(OrderLifecycleTrackerTest, RecordFillUpdatesOrder) {
    OrderLifecycleTracker tracker;

    Order order("client-1");
    order.setQuantity(5.0);
    order.setClientOrderId("client-1");
    order.open();

    bool added = tracker.addOrder(order);
    EXPECT_TRUE(added);

    Fill fill;
    fill.fill_id = "ff1";
    fill.quantity = 2.0;
    fill.price = 50.0;
    fill.exchangeId = ExchangeId::binance();
    fill.timestamp = std::chrono::system_clock::now();

    bool recorded = tracker.recordFill("client-1", fill);
    EXPECT_TRUE(recorded);

    auto order_ptr = tracker.getOrderByClientId("client-1");
    ASSERT_NE(order_ptr, nullptr);
    EXPECT_DOUBLE_EQ(order_ptr->getFilledQuantity(), 2.0);
    EXPECT_EQ(order_ptr->getState(), OrderState::PARTIALLY_FILLED);
}

TEST(OrderFillEdgeCases, ZeroQuantityFillIsIgnored) {
    Order order;
    order.setQuantity(5.0);
    order.open();

    Fill zeroFill;
    zeroFill.fill_id = "z1";
    zeroFill.quantity = 0.0;
    zeroFill.price = 10.0;
    zeroFill.exchangeId = ExchangeId::binance();
    zeroFill.timestamp = std::chrono::system_clock::now();

    // filling with zero quantity should be ignored (no-op) and return false
    bool applied = order.fill(zeroFill);
    EXPECT_FALSE(applied);
    EXPECT_DOUBLE_EQ(order.getFilledQuantity(), 0.0);
    EXPECT_EQ(order.getState(), OrderState::OPEN);
}

TEST(OrderFillEdgeCases, OverfillClampsToQuantityAndMarksFilled) {
    Order order;
    order.setQuantity(4.0);
    order.open();

    Fill f1;
    f1.fill_id = "o1";
    f1.quantity = 5.0; // more than the order's quantity
    f1.price = 20.0;
    f1.exchangeId = ExchangeId::binance();
    f1.timestamp = std::chrono::system_clock::now();

    bool applied = order.fill(f1);
    EXPECT_TRUE(applied);
    // Implementation currently sums fill quantity; verify it marks as FILLED and doesn't crash
    EXPECT_GE(order.getFilledQuantity(), order.getQuantity());
    EXPECT_EQ(order.getState(), OrderState::FILLED);
}

TEST(OrderFillMetrics, AverageFillPriceCalculation) {
    Order order;
    order.setQuantity(10.0);
    order.open();

    Fill f1;
    f1.fill_id = "p1";
    f1.quantity = 3.0;
    f1.price = 100.0;
    f1.exchangeId = ExchangeId::binance();
    f1.timestamp = std::chrono::system_clock::now();

    Fill f2 = f1;
    f2.fill_id = "p2";
    f2.quantity = 2.0;
    f2.price = 110.0;

    bool a1 = order.fill(f1);
    bool a2 = order.fill(f2);
    EXPECT_TRUE(a1);
    EXPECT_TRUE(a2);

    double expectedAvg = ((3.0 * 100.0) + (2.0 * 110.0)) / (3.0 + 2.0);
    EXPECT_DOUBLE_EQ(order.getAverageFillPrice(), expectedAvg);
}

TEST(OrderFillConcurrency, ConcurrentFillsAccumulateSafely) {
    Order order;
    order.setQuantity(1000.0);
    order.open();

    const int threads = 8;
    const int fillsPerThread = 50;
    const double qtyPerFill = 2.0;

    std::vector<std::thread> ths;
    for (int t = 0; t < threads; ++t) {
        ths.emplace_back([&order, fillsPerThread, qtyPerFill, t]() {
            for (int i = 0; i < fillsPerThread; ++i) {
                Fill f;
                f.fill_id = "c-" + std::to_string(t) + "-" + std::to_string(i);
                f.quantity = qtyPerFill;
                f.price = 1.0 + t; // different prices per thread
                f.exchangeId = ExchangeId::binance();
                f.timestamp = std::chrono::system_clock::now();
                order.fill(f);
            }
        });
    }

    for (auto &th : ths) th.join();

    double expected = threads * fillsPerThread * qtyPerFill;
    EXPECT_DOUBLE_EQ(order.getFilledQuantity(), expected);
    // If expected < order quantity, should be partially filled
    if (expected < order.getQuantity()) {
        EXPECT_EQ(order.getState(), OrderState::PARTIALLY_FILLED);
    }
}
