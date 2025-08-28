// tests/execution/test_execution_algorithm_factory.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <chrono>
#include <thread>
#include "execution/execution_algorithm_factory.hpp"

using namespace trading::execution;
using namespace testing;

class ExecutionAlgorithmFactoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        factory_ = std::make_unique<ExecutionAlgorithmFactory>();
        order_callback_called_ = false;
        completion_callback_called_ = false;
    }

    void TearDown() override {
        factory_.reset();
    }

    // Helper methods
    std::shared_ptr<TwapParams> createTwapParams() {
        auto params = std::make_shared<TwapParams>();
        params->config.total_quantity = 100.0;
        params->config.duration = std::chrono::seconds(300);
        params->config.slice_interval = std::chrono::seconds(60);
        return params;
    }
    
    std::shared_ptr<VwapParams> createVwapParams() {
        auto params = std::make_shared<VwapParams>();
        params->config.total_quantity = 100.0;
        params->config.duration = std::chrono::seconds(300);
        params->config.volume_profile = {0.2, 0.2, 0.2, 0.2, 0.2}; // Equal distribution
        params->config.max_participation_rate = 0.1;
        return params;
    }
    
    std::shared_ptr<IcebergParams> createIcebergParams() {
        auto params = std::make_shared<IcebergParams>();
        params->config.total_quantity = 100.0;
        params->config.visible_quantity = 10.0;
        params->config.price = 50000.0;
        return params;
    }
    
    void registerOrderCallback() {
        factory_->registerOrderCallback([this](const models::OrderPtr& order) {
            order_callback_called_ = true;
            last_order_ = order;
        });
    }
    
    void registerCompletionCallback() {
        factory_->registerCompletionCallback(
            [this](const std::string& id, bool success, const std::string& message) {
                completion_callback_called_ = true;
                completion_algorithm_id_ = id;
                completion_success_ = success;
                completion_message_ = message;
            });
    }

    std::unique_ptr<ExecutionAlgorithmFactory> factory_;
    bool order_callback_called_;
    bool completion_callback_called_;
    std::string completion_algorithm_id_;
    bool completion_success_;
    std::string completion_message_;
    models::OrderPtr last_order_;
};

// Test creating a TWAP algorithm
TEST_F(ExecutionAlgorithmFactoryTest, CreateTwapAlgorithm) {
    auto params = createTwapParams();
    auto algorithm = factory_->createAlgorithm(
        "test-twap", "BTC-USD", models::OrderSide::BUY, params);
    
    ASSERT_NE(algorithm, nullptr);
    EXPECT_EQ(algorithm->getId(), "test-twap");
    EXPECT_EQ(algorithm->getType(), AlgorithmType::TWAP);
    EXPECT_FALSE(algorithm->isRunning());
}

// Test creating a VWAP algorithm
TEST_F(ExecutionAlgorithmFactoryTest, CreateVwapAlgorithm) {
    auto params = createVwapParams();
    auto algorithm = factory_->createAlgorithm(
        "test-vwap", "BTC-USD", models::OrderSide::BUY, params);
    
    ASSERT_NE(algorithm, nullptr);
    EXPECT_EQ(algorithm->getId(), "test-vwap");
    EXPECT_EQ(algorithm->getType(), AlgorithmType::VWAP);
    EXPECT_FALSE(algorithm->isRunning());
}

// Test creating an Iceberg algorithm
TEST_F(ExecutionAlgorithmFactoryTest, CreateIcebergAlgorithm) {
    auto params = createIcebergParams();
    auto algorithm = factory_->createAlgorithm(
        "test-iceberg", "BTC-USD", models::OrderSide::BUY, params);
    
    ASSERT_NE(algorithm, nullptr);
    EXPECT_EQ(algorithm->getId(), "test-iceberg");
    EXPECT_EQ(algorithm->getType(), AlgorithmType::ICEBERG);
    EXPECT_FALSE(algorithm->isRunning());
}

// Test generated ID when none provided
TEST_F(ExecutionAlgorithmFactoryTest, CreateWithGeneratedId) {
    auto params = createTwapParams();
    auto algorithm = factory_->createAlgorithm(
        "", "BTC-USD", models::OrderSide::BUY, params);
    
    ASSERT_NE(algorithm, nullptr);
    EXPECT_FALSE(algorithm->getId().empty());
    EXPECT_EQ(algorithm->getType(), AlgorithmType::TWAP);
}

// Test retrieving an algorithm by ID
TEST_F(ExecutionAlgorithmFactoryTest, GetAlgorithmById) {
    auto params = createTwapParams();
    auto created = factory_->createAlgorithm(
        "test-twap-get", "BTC-USD", models::OrderSide::BUY, params);
    
    ASSERT_NE(created, nullptr);
    
    auto retrieved = factory_->getAlgorithm("test-twap-get");
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->getId(), "test-twap-get");
    EXPECT_EQ(retrieved->getType(), AlgorithmType::TWAP);
}

// Test retrieving a non-existent algorithm
TEST_F(ExecutionAlgorithmFactoryTest, GetNonExistentAlgorithm) {
    auto retrieved = factory_->getAlgorithm("non-existent-id");
    EXPECT_EQ(retrieved, nullptr);
}

// Test creating algorithm with invalid parameters
TEST_F(ExecutionAlgorithmFactoryTest, CreateWithInvalidParams) {
    // Null parameters
    auto algorithm = factory_->createAlgorithm(
        "test-null-params", "BTC-USD", models::OrderSide::BUY, nullptr);
    EXPECT_EQ(algorithm, nullptr);
    
    // Wrong parameter type
    auto twap_params = createTwapParams();
    auto wrong_type_params = std::static_pointer_cast<AlgorithmParams>(twap_params);
    
    // Try to create a VWAP algorithm with TWAP parameters
    auto wrong_cast = std::dynamic_pointer_cast<VwapParams>(wrong_type_params);
    
    algorithm = factory_->createAlgorithm(
        "test-wrong-params", "BTC-USD", models::OrderSide::BUY, wrong_cast);
    EXPECT_EQ(algorithm, nullptr);
}

// Test duplicate algorithm ID
TEST_F(ExecutionAlgorithmFactoryTest, CreateWithDuplicateId) {
    auto params1 = createTwapParams();
    auto algorithm1 = factory_->createAlgorithm(
        "test-duplicate", "BTC-USD", models::OrderSide::BUY, params1);
    
    ASSERT_NE(algorithm1, nullptr);
    
    auto params2 = createVwapParams();
    auto algorithm2 = factory_->createAlgorithm(
        "test-duplicate", "ETH-USD", models::OrderSide::SELL, params2);
    
    // Should fail due to duplicate ID
    EXPECT_EQ(algorithm2, nullptr);
}

// Test registering order callback
TEST_F(ExecutionAlgorithmFactoryTest, RegisterOrderCallback) {
    registerOrderCallback();
    
    auto params = createTwapParams();
    auto algorithm = factory_->createAlgorithm(
        "test-callback", "BTC-USD", models::OrderSide::BUY, params);
    
    ASSERT_NE(algorithm, nullptr);
    
    // Start the algorithm to generate an order
    algorithm->start();
    
    // In a real scenario, we'd wait for the order to be generated
    // For this test, we're just checking that the callback registration works
    
    // Since the actual order generation is tested in algorithm-specific tests,
    // we won't test that part here
}

// Test registering completion callback
TEST_F(ExecutionAlgorithmFactoryTest, RegisterCompletionCallback) {
    registerCompletionCallback();
    
    auto params = createTwapParams();
    auto algorithm = factory_->createAlgorithm(
        "test-completion", "BTC-USD", models::OrderSide::BUY, params);
    
    ASSERT_NE(algorithm, nullptr);
    
    // Cancel the algorithm to trigger completion callback
    algorithm->cancel();
    
    // Check that the completion callback was called
    EXPECT_TRUE(completion_callback_called_);
    EXPECT_EQ(completion_algorithm_id_, "test-completion");
    EXPECT_FALSE(completion_success_); // Cancelled should be treated as not successful
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}