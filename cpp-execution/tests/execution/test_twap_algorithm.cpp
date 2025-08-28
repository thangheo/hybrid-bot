// tests/execution/test_twap_algorithm.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <chrono>
#include <thread>
#include "execution/algorithms/twap_algorithm.hpp"

using namespace trading::execution;
using namespace trading::execution::algorithms;
using namespace testing;

// Mock Order for testing
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

class TwapAlgorithmTest : public ::testing::Test {
protected:
    void SetUp() override {
        algorithm_ = std::make_unique<TwapAlgorithm>();
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
        config_.duration = std::chrono::seconds(300); // 5 minutes
        config_.slice_interval = std::chrono::seconds(60); // 1 minute
        config_.price_limit = 0.0; // Market orders
        config_.allow_partial = true;
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

    std::unique_ptr<TwapAlgorithm> algorithm_;
    TwapConfig config_;
    bool order_callback_called_;
    bool completion_callback_called_;
    bool completion_success_;
    std::string completion_message_;
    models::OrderPtr last_order_;
};

// Basic initialization test
TEST_F(TwapAlgorithmTest, InitializeWithValidConfig) {
    setupBasicConfig();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_FALSE(algorithm_->isRunning());
    EXPECT_FALSE(algorithm_->isComplete());
    EXPECT_EQ(algorithm_->getTotalQuantity(), 100.0);
    EXPECT_EQ(algorithm_->getExecutedQuantity(), 0.0);
    EXPECT_EQ(algorithm_->getRemainingQuantity(), 100.0);
}

// Test with invalid config
TEST_F(TwapAlgorithmTest, InitializeWithInvalidConfig) {
    // Setup config with invalid values
    config_.total_quantity = -100.0; // Invalid negative quantity
    config_.duration = std::chrono::seconds(300);
    config_.slice_interval = std::chrono::seconds(60);
    
    setupCompletionCallback();
    
    EXPECT_FALSE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(completion_callback_called_);
    EXPECT_FALSE(completion_success_);
}

// Test starting the algorithm
TEST_F(TwapAlgorithmTest, StartAlgorithm) {
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
    EXPECT_EQ(last_order_->getType(), models::OrderType::MARKET);
    
    // Check that the quantity is approximately correct
    // For 5 slices over 5 minutes, each slice should be around 20% of total
    EXPECT_NEAR(last_order_->getQuantity(), 20.0, 1.0);
}

// Test pausing and resuming
TEST_F(TwapAlgorithmTest, PauseAndResume) {
    setupBasicConfig();
    setupOrderCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    EXPECT_TRUE(algorithm_->isRunning());
    
    // Pause algorithm
    EXPECT_TRUE(algorithm_->pause());
    EXPECT_FALSE(algorithm_->isRunning());
    
    // Resume algorithm
    EXPECT_TRUE(algorithm_->resume());
    EXPECT_TRUE(algorithm_->isRunning());
}

// Test cancellation
TEST_F(TwapAlgorithmTest, Cancel) {
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

// Test order updates
TEST_F(TwapAlgorithmTest, HandleOrderUpdate) {
    setupBasicConfig();
    setupOrderCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    
    // Store the order created by start
    auto first_order = std::dynamic_pointer_cast<MockOrder>(last_order_);
    ASSERT_NE(first_order, nullptr);
    
    // Simulate a fill
    first_order->simulateFill(first_order->getQuantity(), 50000.0);
    
    // Update the algorithm with the filled order
    algorithm_->handleOrderUpdate(first_order);
    
    // Check that executed quantity is updated
    EXPECT_NEAR(algorithm_->getExecutedQuantity(), first_order->getQuantity(), 0.001);
    EXPECT_NEAR(algorithm_->getAverageExecutionPrice(), 50000.0, 0.001);
}

// Test completion when all orders are filled
TEST_F(TwapAlgorithmTest, CompleteWhenAllOrdersFilled) {
    // Use a shorter duration for testing
    config_.total_quantity = 100.0;
    config_.duration = std::chrono::seconds(10); // 10 seconds
    config_.slice_interval = std::chrono::seconds(2); // 2 seconds
    
    setupOrderCallback();
    setupCompletionCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    
    // Get all child orders over time
    auto start_time = std::chrono::steady_clock::now();
    std::vector<models::OrderPtr> orders;
    
    // Run until completion or timeout
    const auto timeout = std::chrono::seconds(15);
    while (!algorithm_->isComplete() && 
           std::chrono::steady_clock::now() - start_time < timeout) {
        if (order_callback_called_) {
            orders.push_back(last_order_);
            order_callback_called_ = false;
            
            // Simulate immediate fill
            auto mock_order = std::dynamic_pointer_cast<MockOrder>(last_order_);
            if (mock_order) {
                mock_order->simulateFill(mock_order->getQuantity(), 50000.0);
                algorithm_->handleOrderUpdate(mock_order);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Check that the algorithm completed
    EXPECT_TRUE(algorithm_->isComplete());
    EXPECT_TRUE(completion_callback_called_);
    EXPECT_TRUE(completion_success_);
    
    // Check that executed quantity matches total quantity
    EXPECT_NEAR(algorithm_->getExecutedQuantity(), config_.total_quantity, 0.001);
    
    // Check that we got at least a few orders
    EXPECT_GE(orders.size(), 3);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}