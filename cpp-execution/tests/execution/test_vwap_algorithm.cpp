// tests/execution/test_vwap_algorithm.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <chrono>
#include <thread>
#include <vector>
#include "execution/algorithms/vwap_algorithm.hpp"

using namespace trading::execution;
using namespace trading::execution::algorithms;
using namespace testing;

// Mock Order for testing - same as in TWAP tests
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

class VwapAlgorithmTest : public ::testing::Test {
protected:
    void SetUp() override {
        algorithm_ = std::make_unique<VwapAlgorithm>();
        order_callback_called_ = false;
        completion_callback_called_ = false;
        completion_success_ = false;
        
        // Default volume profile - hourly distribution
        volume_profile_ = {0.03, 0.04, 0.05, 0.06, 0.07, 0.08, 0.09, 0.1, 0.09, 0.08, 0.07, 0.06,
                          0.05, 0.04, 0.03, 0.02, 0.02, 0.01, 0.01, 0.02, 0.02, 0.01, 0.01, 0.02};
    }

    void TearDown() override {
        algorithm_.reset();
    }

    // Helper methods
    void setupBasicConfig() {
        config_.total_quantity = 100.0;
        config_.duration = std::chrono::seconds(120); // 2 minutes for testing
        config_.volume_profile = volume_profile_;
        config_.price_limit = 0.0; // Market orders
        config_.max_participation_rate = 0.1;
        config_.dynamic_sizing = true;
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
    
    void setupVolumeCallback() {
        algorithm_->setVolumeCallback([this](const std::string& trading_pair, std::chrono::minutes minutes) {
            // Simple mock that returns higher volume later in the day
            // This is just for testing - in a real implementation this would query market data
            return 1000.0 * minutes.count(); // Just a simple function for testing
        });
    }

    std::unique_ptr<VwapAlgorithm> algorithm_;
    VwapConfig config_;
    std::vector<double> volume_profile_;
    bool order_callback_called_;
    bool completion_callback_called_;
    bool completion_success_;
    std::string completion_message_;
    models::OrderPtr last_order_;
};

// Basic initialization test
TEST_F(VwapAlgorithmTest, InitializeWithValidConfig) {
    setupBasicConfig();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_FALSE(algorithm_->isRunning());
    EXPECT_FALSE(algorithm_->isComplete());
    EXPECT_EQ(algorithm_->getTotalQuantity(), 100.0);
    EXPECT_EQ(algorithm_->getExecutedQuantity(), 0.0);
    EXPECT_EQ(algorithm_->getRemainingQuantity(), 100.0);
}

// Test with invalid config
TEST_F(VwapAlgorithmTest, InitializeWithInvalidConfig) {
    // Setup config with invalid values
    config_.total_quantity = -100.0; // Invalid negative quantity
    config_.duration = std::chrono::seconds(300);
    config_.volume_profile = volume_profile_;
    
    setupCompletionCallback();
    
    EXPECT_FALSE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(completion_callback_called_);
    EXPECT_FALSE(completion_success_);
}

// Test with empty volume profile
TEST_F(VwapAlgorithmTest, InitializeWithEmptyVolumeProfile) {
    config_.total_quantity = 100.0;
    config_.duration = std::chrono::seconds(300);
    config_.volume_profile.clear(); // Empty profile
    
    setupCompletionCallback();
    
    EXPECT_FALSE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(completion_callback_called_);
    EXPECT_FALSE(completion_success_);
}

// Test setting volume profile
TEST_F(VwapAlgorithmTest, SetVolumeProfile) {
    setupBasicConfig();
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    
    // Update with a new volume profile
    std::vector<double> new_profile = {0.25, 0.25, 0.25, 0.25};
    algorithm_->setVolumeProfile(new_profile);
    
    // There's no direct way to verify this in the test, but we can 
    // check that it doesn't crash and that the algorithm is still initialized
    EXPECT_FALSE(algorithm_->isRunning());
    EXPECT_FALSE(algorithm_->isComplete());
}

// Test starting the algorithm
TEST_F(VwapAlgorithmTest, StartAlgorithm) {
    setupBasicConfig();
    setupOrderCallback();
    setupVolumeCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    EXPECT_TRUE(algorithm_->isRunning());
    
    // The algorithm should create time buckets and schedule processing
    // Eventually we should see an order being created
    
    // Wait for a short time to allow for the order to be created
    auto start_time = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(5);
    
    while (!order_callback_called_ && 
           std::chrono::steady_clock::now() - start_time < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    EXPECT_TRUE(order_callback_called_);
    ASSERT_NE(last_order_, nullptr);
    EXPECT_EQ(last_order_->getTradingPair(), "BTC-USD");
    EXPECT_EQ(last_order_->getSide(), models::OrderSide::BUY);
    EXPECT_EQ(last_order_->getType(), models::OrderType::MARKET);
    
    // Quantity should be positive but we can't predict the exact value
    // as it depends on the bucket sizing and volume callback
    EXPECT_GT(last_order_->getQuantity(), 0.0);
}

// Test pausing and resuming
TEST_F(VwapAlgorithmTest, PauseAndResume) {
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
TEST_F(VwapAlgorithmTest, Cancel) {
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
TEST_F(VwapAlgorithmTest, HandleOrderUpdate) {
    setupBasicConfig();
    setupOrderCallback();
    setupVolumeCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    
    // Wait for an order to be created
    auto start_time = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(5);
    
    while (!order_callback_called_ && 
           std::chrono::steady_clock::now() - start_time < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    EXPECT_TRUE(order_callback_called_);
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

// Test VWAP deviation calculation
TEST_F(VwapAlgorithmTest, VwapDeviation) {
    setupBasicConfig();
    setupOrderCallback();
    
    EXPECT_TRUE(algorithm_->initialize("BTC-USD", models::OrderSide::BUY, config_));
    EXPECT_TRUE(algorithm_->start());
    
    // Wait for an order to be created
    auto start_time = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(5);
    
    while (!order_callback_called_ && 
           std::chrono::steady_clock::now() - start_time < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    auto first_order = std::dynamic_pointer_cast<MockOrder>(last_order_);
    ASSERT_NE(first_order, nullptr);
    
    // Simulate a fill
    first_order->simulateFill(first_order->getQuantity(), 50000.0);
    
    // Update the algorithm with the filled order
    algorithm_->handleOrderUpdate(first_order);
    
    // In our implementation, getVwapDeviationPercent will return 0
    // because we don't have a real market VWAP to compare against
    EXPECT_EQ(algorithm_->getVwapDeviationPercent(), 0.0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}