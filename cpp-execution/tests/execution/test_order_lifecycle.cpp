/**
 * @file test_order_lifecycle.cpp
 * @brief Unit tests for order lifecycle tracking with state transitions
 * @date 2025-04-29
 * 
 * Order Imbalance Strategy - Execution Engine
 */

#include <gtest/gtest.h>

#include "execution/models/order.hpp"
#include "execution/order_lifecycle_tracker.hpp"

using namespace trading::execution;
using namespace trading::execution::models;
using namespace trading::core;

TEST(OrderLifecycleBasic, AddAndOpenOrder) {
	OrderLifecycleTracker tracker;

	Order order("cli-100");
	order.setClientOrderId("cli-100");
	order.setQuantity(2.0);

	bool added = tracker.addOrder(order);
	EXPECT_TRUE(added);

	// Update to OPEN via tracker
	bool opened = tracker.updateOrderState("cli-100", OrderState::OPEN, "", "");
	EXPECT_TRUE(opened);

	auto op = tracker.getOrderByClientId("cli-100");
	ASSERT_NE(op, nullptr);
	EXPECT_EQ(op->getState(), OrderState::OPEN);
}

TEST(OrderLifecycleExchangeIdMapping, UpdateExchangeIdAndLookup) {
	OrderLifecycleTracker tracker;

	Order order("cli-200");
	order.setClientOrderId("cli-200");
	order.setQuantity(1.0);
	tracker.addOrder(order);

	// assign exchange id via updateOrderWithExchangeId
	auto op = tracker.getOrderByClientId("cli-200");
	ASSERT_NE(op, nullptr);
	tracker.updateOrderWithExchangeId(*op, "exch-123");

	// lookup by exchange id should return the same client id
	auto found = tracker.getOrderByExchangeId("exch-123");
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->getClientOrderId(), "cli-200");
}

TEST(OrderLifecycleRecordFillAndCallback, RecordFillInvokesCallbackAndChangesState) {
	OrderLifecycleTracker tracker;

	Order order("cli-300");
	order.setClientOrderId("cli-300");
	order.setQuantity(5.0);
	order.open();
	tracker.addOrder(order);

	bool callbackCalled = false;
	tracker.registerStateChangeCallback([&callbackCalled](const models::Order& o, models::OrderState old_s, models::OrderState new_s){
		callbackCalled = true;
	});

	Fill f;
	f.fill_id = "ff-1";
	f.quantity = 2.0;
	f.price = 10.0;
	f.exchangeId = ExchangeId::binance();
	f.timestamp = std::chrono::system_clock::now();

	bool rec = tracker.recordFill("cli-300", f);
	EXPECT_TRUE(rec);

	auto op = tracker.getOrderByClientId("cli-300");
	ASSERT_NE(op, nullptr);
	EXPECT_EQ(op->getState(), OrderState::PARTIALLY_FILLED);
	EXPECT_TRUE(callbackCalled);
}

TEST(OrderLifecycleRevert, RevertFromPendingCancelToPreviousState) {
	OrderLifecycleTracker tracker;

	Order order("cli-400");
	order.setClientOrderId("cli-400");
	order.setQuantity(3.0);
	order.open();
	tracker.addOrder(order);

	// set to pending cancel (tracker will store previous state)
	bool pending = tracker.updateOrderState("cli-400", OrderState::PENDING_CANCEL, "", "");
	EXPECT_TRUE(pending);

	// revert to previous state (should be OPEN)
	bool reverted = tracker.revertToPreviousState("cli-400", OrderState::OPEN);
	EXPECT_TRUE(reverted);

	auto op = tracker.getOrderByClientId("cli-400");
	ASSERT_NE(op, nullptr);
	EXPECT_EQ(op->getState(), OrderState::OPEN);
}
