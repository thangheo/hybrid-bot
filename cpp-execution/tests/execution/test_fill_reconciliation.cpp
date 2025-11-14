#include <gtest/gtest.h>
#include "execution/fill_reconciliation.hpp"
#include "execution/models/order.hpp"
#include <chrono>

using namespace trading::execution;
using namespace trading::execution::models;
using namespace trading::core;
using namespace trading;
static core::ExchangeId dummyExchange() {
    return core::ExchangeId::binance();
}

// Helper to create a Fill
static Fill makeFill(const std::string& fid, double qty, double price) {
    Fill f;
    f.fill_id = fid;
    f.orderId = "order-1";
    f.exchangeOrderId = "ex-ord-1";
    f.exchangeId = dummyExchange();
    f.quantity = qty;
    f.price = price;
    f.fee = 0.0;
    f.fee_currency = "USD";
    f.timestamp = std::chrono::system_clock::now();
    return f;
}

TEST(FillReconciliationTest, ReconcilesExactFill) {
    FillReconciliation fr;
    fr.initialize();

    auto expected = makeFill("f1", 1.0, 100.0);
    fr.recordExpectedFill("order-1", expected);

    auto actual = makeFill("f1", 1.0, 100.0);
    bool reconciled = fr.recordActualFill("order-1", actual);

    EXPECT_TRUE(reconciled);

    auto rec = fr.getReconciledFills("order-1");
    ASSERT_EQ(rec.size(), 1);
    EXPECT_EQ(rec[0].fill_id, "f1");
}

TEST(FillReconciliationTest, DetectsExtraFill) {
    FillReconciliation fr;
    fr.initialize();

    auto actual = makeFill("ax1", 2.0, 50.0);
    bool reconciled = fr.recordActualFill("order-unknown", actual);

    EXPECT_FALSE(reconciled);

    auto unreconciled = fr.getUnreconciledActualFills("order-unknown");
    ASSERT_EQ(unreconciled.size(), 1);
    EXPECT_EQ(unreconciled[0].fill_id, "ax1");
}

TEST(FillReconciliationTest, DetectsMissingFillAfterTimeout) {
    FillReconciliation fr;
    fr.initialize();

    auto expected = makeFill("m1", 3.0, 10.0);
    fr.recordExpectedFill("ord-missing", expected);

    // Sleep a short while to ensure timestamp ages (use small timeout)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto missing = fr.checkForMissingFills(5); // 5 ms timeout
    ASSERT_FALSE(missing.empty());
    EXPECT_EQ(missing[0].discrepancy_type, "MISSING");
}

TEST(FillReconciliationTest, DetectsPriceDiscrepancy) {
    FillReconciliation fr;
    fr.initialize();

    auto expected = makeFill("p1", 1.0, 100.0);
    fr.recordExpectedFill("order-price", expected);

    // Actual has same quantity but different price
    auto actual = makeFill("a_p1", 1.0, 101.0);
    bool reconciled = fr.recordActualFill("order-price", actual);

    EXPECT_FALSE(reconciled);

    auto unreconcActual = fr.getUnreconciledActualFills("order-price");
    ASSERT_EQ(unreconcActual.size(), 1);
}

TEST(FillReconciliationTest, DetectsQuantityDiscrepancy) {
    FillReconciliation fr;
    fr.initialize();

    auto expected = makeFill("q1", 5.0, 20.0);
    fr.recordExpectedFill("order-qty", expected);

    // Actual has same price but different quantity
    auto actual = makeFill("a_q1", 4.5, 20.0);
    bool reconciled = fr.recordActualFill("order-qty", actual);

    EXPECT_FALSE(reconciled);

    auto unreconcActual = fr.getUnreconciledActualFills("order-qty");
    ASSERT_EQ(unreconcActual.size(), 1);
}

TEST(FillReconciliationTest, PurgesOldData) {
    FillReconciliation fr;
    fr.initialize();

    auto expected = makeFill("old1", 1.0, 1.0);
    fr.recordExpectedFill("ord-old", expected);

    // Use negative threshold to ensure current records are treated as old and purged
    int purged = fr.purgeOldReconciliationData(-1);
    EXPECT_GE(purged, 1);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
