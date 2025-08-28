// tests/execution/test_iceberg_algorithm.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <thread>
#include "execution/algorithms/iceberg_algorithm.hpp"

using namespace trading::execution;
using namespace trading::execution::algorithms;
using namespace testing;

// Mock Order for testing - same as in previous tests
class MockOrder : public models::Order {
public:
    MockOrder() : models::Order() {
        setOrderId("mock-order-id");
    }
    
    void simulateFill(double quantity, double price) {
        models::Fill fill;
        fill.quantity = quantity;
        fill.price = price;
        fill.timestamp = std::chrono::system_clock::now();
        
        // Update fill state
        setState(models::OrderState::FILLED);
        addFill(fill);
    }
    
    void simulatePartialFill(double quantity, double price) {
        models::Fill fill;
        fill.quantity = quantity;
        fill.price = price;
        fill.timestamp = std::chrono::system_clock::now();
        
        // Update fill state
        setState(models::OrderState::PARTIALLY_FILLED);
        addFill(fill);
    }
    
    void simulateCancel() {
        setState(models::OrderState::CANCELED);
    }
    
    void simulateReject() {
        setState(models::OrderState::REJECTED);
    }
    
private:
    // Add a fill to the order
    void addFill(const models::Fill& fill) {
        fills_.push_back(fill);
        filled_quantity_ += fill.quantity;
        average_fill_price_ = (average_fill_price_ * (filled_quantity_ - fill.quantity) + 
                              fill.price * fill.quantity) / filled_quantity_;
    }
    
    // Set order state
    void setState(models::OrderState state) {
        state_ = state;
    }
    
    // Order state
    models::OrderState state_ = models::OrderState::CREATED;
    // Fill information
    std::vector<models::Fill> fills_;
    double filled_quantity_ = 0.0;
    double average_fill_price_ = 0.0;
};

class IcebergAlgorithmTest : public ::testing::Test {
protected:
    void SetUp() override {
        algorithm_ = std::make_unique<IcebergAlgorithm>();
        order_callback_called_ = false;
        completion_callback_called_ = false;
        completion_success_ = false;
    }

    void TearDown() override {
        algorithm_.reset();
    }

    // Helper methods
    void setupBasicConfig() {
        config_.total_quantity = 100.0;
        config_.visible_quantity = 10.0;
        config_.price = 50000.0;
        config_.price_delta = 0.0;
        config_.adjust_price_on_fill = false;
        config_.dynamic_visible_qty = false;
    }
    
    void setupOrderCallback() {
        algorithm_->setOrderCallback([this](const models::OrderPtr& order) {
            order_callback_called_ = true;
            last_order_ = order;
        });
    }
    
    void setupCompletionCallback() {
        algorithm_->setCompletionCallback([this](bool success, const std::string& message) {
            completion_callback_called_ = true;
            completion_success_ = success;
            completion_message_ = message;
        });
    }

    std::unique_ptr<IcebergAlgorithm> algorithm_;
    IcebergConfig config_;
    bool order_callback_called_;
    bool completion_callback_called_;
    bool completion_success_;
    std::string completion_message_;
    models::OrderPtr last_order_;
};

// Basic initialization test
TEST_F(IcebergAlgorithmTest, InitializeWithValidConfig) {
    setupBasicConfig();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_FALSE(algorithm_->isRunning());
    EXPECT_FALSE(algorithm_->isComplete());
    EXPECT_EQ(algorithm_->getTotalQuantity(), 100.0);
    EXPECT_EQ(algorithm_->getExecutedQuantity(), 0.0);
    EXPECT_EQ(algorithm_->getRemainingQuantity(), 100.0);
    EXPECT_EQ(algorithm_->getCurrentPrice(), 50000.0);
}

// Test with invalid config
TEST_F(IcebergAlgorithmTest, InitializeWithInvalidConfig) {
    // Setup config with invalid values
    config_.total_quantity = 100.0;
    config_.visible_quantity = 0.0; // Invalid zero quantity
    config_.price = 50000.0;
    
    setupCompletionCallback();
    
    EXPECT_FALSE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(completion_callback_called_);
    EXPECT_FALSE(completion_success_);
}

// Test with visible quantity > total quantity
TEST_F(IcebergAlgorithmTest, InitializeWithVisibleGreaterThanTotal) {
    config_.total_quantity = 100.0;
    config_.visible_quantity = 150.0; // Invalid: greater than total
    config_.price = 50000.0;
    
    setupCompletionCallback();
    
    EXPECT_FALSE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(completion_callback_called_);
    EXPECT_FALSE(completion_success_);
}

// Test starting the algorithm
TEST_F(IcebergAlgorithmTest, StartAlgorithm) {
    setupBasicConfig();
    setupOrderCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    EXPECT_TRUE(algorithm_->isRunning());
    
    // The algorithm should create and send the first slice immediately
    EXPECT_TRUE(order_callback_called_);
    ASSERT_NE(last_order_, nullptr);
    EXPECT_EQ(last_order_->getTradingPair(), "BTC-USD");
    EXPECT_EQ(last_order_->getSide(), models::OrderSide::BUY);
    EXPECT_EQ(last_order_->getType(), models::OrderType::LIMIT);
    EXPECT_EQ(last_order_->getPrice(), 50000.0);
    EXPECT_EQ(last_order_->getQuantity(), 10.0); // Visible quantity
    
    // Check that the current order is set
    ASSERT_NE(algorithm_->getCurrentOrder(), nullptr);
    EXPECT_EQ(algorithm_->getCurrentOrder()->getOrderId(), last_order_->getOrderId());
}

// Test with dynamic visible quantity
TEST_F(IcebergAlgorithmTest, DynamicVisibleQuantity) {
    setupBasicConfig();
    config_.dynamic_visible_qty = true;
    config_.rand_factor = 0.2; // 20% randomization
    setupOrderCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    
    // The visible quantity should be randomized within ±20% of 10.0
    EXPECT_TRUE(order_callback_called_);
    ASSERT_NE(last_order_, nullptr);
    EXPECT_GE(last_order_->getQuantity(), 8.0);  // Lower bound
    EXPECT_LE(last_order_->getQuantity(), 12.0); // Upper bound
}

// Test with price delta
TEST_F(IcebergAlgorithmTest, PriceDelta) {
    setupBasicConfig();
    config_.price_delta = 100.0; // $100 adjustment per slice
    setupOrderCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    
    // First slice should be at the base price
    EXPECT_TRUE(order_callback_called_);
    ASSERT_NE(last_order_, nullptr);
    EXPECT_EQ(last_order_->getPrice(), 50000.0);
    
    // Simulate a fill and handle the update
    auto first_order = std::dynamic_pointer_cast<MockOrder>(last_order_);
    ASSERT_NE(first_order, nullptr);
    first_order->simulateFill(first_order->getQuantity(), first_order->getPrice());
    algorithm_->handleOrderUpdate(first_order);
    
    // Reset the flag
    order_callback_called_ = false;
    
    // Wait for the second slice to be created
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Second slice should have adjusted price
    EXPECT_TRUE(order_callback_called_);
    ASSERT_NE(last_order_, nullptr);
    
    // For buy order with positive delta, price should decrease
    // For sell order with positive delta, price would increase
    if (last_order_->getSide() == models::OrderSide::BUY) {
        EXPECT_EQ(last_order_->getPrice(), 49900.0); // 50000 - 100
    } else {
        EXPECT_EQ(last_order_->getPrice(), 50100.0); // 50000 + 100
    }
}

// Test pausing and resuming
TEST_F(IcebergAlgorithmTest, PauseAndResume) {
    setupBasicConfig();
    setupOrderCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    EXPECT_TRUE(algorithm_->isRunning());
    
    // Pause algorithm
    EXPECT_TRUE(algorithm_->pause());
    EXPECT_FALSE(algorithm_->isRunning());
    
    // Reset the flag
    order_callback_called_ = false;
    
    // Resume algorithm
    EXPECT_TRUE(algorithm_->resume());
    EXPECT_TRUE(algorithm_->isRunning());
    
    // If we had a filled order when pausing, resuming should create a new order
    if (last_order_ && last_order_->getState() == models::OrderState::FILLED) {
        EXPECT_TRUE(order_callback_called_);
    }
}

// Test cancellation
TEST_F(IcebergAlgorithmTest, Cancel) {
    setupBasicConfig();
    setupCompletionCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    EXPECT_TRUE(algorithm_->isRunning());
    
    // Cancel algorithm
    EXPECT_TRUE(algorithm_->cancel());
    EXPECT_FALSE(algorithm_->isRunning());
    EXPECT_TRUE(algorithm_->isCancelled());
    
    // Check that completion callback was called with failure
    EXPECT_TRUE(completion_callback_called_);
    EXPECT_FALSE(completion_success_);
}

// Test completion with multiple slices
TEST_F(IcebergAlgorithmTest, CompleteWithMultipleSlices) {
    setupBasicConfig();
    setupOrderCallback();
    setupCompletionCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    
    // Execute until complete
    while (algorithm_->isRunning() && !algorithm_->isComplete()) {
        // If we have a current order, simulate a fill
        auto current_order = algorithm_->getCurrentOrder();
        if (current_order) {
            auto mock_order = std::dynamic_pointer_cast<MockOrder>(current_order);
            if (mock_order) {
                mock_order->simulateFill(mock_order->getQuantity(), mock_order->getPrice());
                algorithm_->handleOrderUpdate(mock_order);
            }
        }
        
        // Small delay to avoid tight loop
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Check that the algorithm completed successfully
    EXPECT_TRUE(algorithm_->isComplete());
    EXPECT_TRUE(completion_callback_called_);
    EXPECT_TRUE(completion_success_);
    
    // Check that all quantity was executed
    EXPECT_NEAR(algorithm_->getExecutedQuantity(), config_.total_quantity, 0.001);
    EXPECT_NEAR(algorithm_->getRemainingQuantity(), 0.0, 0.001);
    
    // Check that we've executed multiple slices
    EXPECT_GE(algorithm_->getChildOrders().size(), config_.total_quantity / config_.visible_quantity);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}