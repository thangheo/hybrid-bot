#include "risk/posttrade/balance_reconciliation.hpp"
#include "risk/portfolio/position_tracker.hpp"
#include "risk/interfaces/data_interface.hpp"
#include "risk/interfaces/execution_interface.hpp"
#include "risk/risk_types.hpp"
#include "risk/risk_events.hpp"
#include "risk/risk_config.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <map>
#include <vector>

using ::testing::Return;
using ::testing::_;
using ::testing::NiceMock;

namespace risk {
namespace test {

// Mock classes for testing
class MockPositionTracker : public PositionTracker {
public:
    MOCK_METHOD(std::vector<Position>, getAllPositions, (), (override));
    MOCK_METHOD(std::vector<std::string>, getTrackingExchanges, (), (override));
    MOCK_METHOD(std::vector<Position>, getExchangePositions, (const ExchangeId&), (override));
    MOCK_METHOD(bool, overridePosition, (const ExchangeId&, const TradingPair&, double), (override));
};

class MockDataInterface : public DataInterface {
public:
    MOCK_METHOD(std::map<std::string, double>, getExchangePositions, (const ExchangeId&), (override));
};

class MockExecutionInterface : public ExecutionInterface {
public:
    MOCK_METHOD(bool, submitOrder, (const Order&), (override));
};

// Test fixture
class BalanceReconciliationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup default config
        config.balanceReconciliationConfig.enableScheduledReconciliation = false;
        config.balanceReconciliationConfig.reconciliationIntervalMinutes = 60;
        config.balanceReconciliationConfig.positionDiscrepancyTolerance = 0.001;
        config.balanceReconciliationConfig.enableAutoCorrection = true;
        config.balanceReconciliationConfig.autoCorrectThresholdPercent = 5.0;
        config.balanceReconciliationConfig.criticalDiscrepancyPercent = 10.0;
        
        // Setup mocks
        positionTracker = std::make_shared<NiceMock<MockPositionTracker>>();
        dataInterface = std::make_shared<NiceMock<MockDataInterface>>();
        executionInterface = std::make_shared<NiceMock<MockExecutionInterface>>();
        
        // Create test instance
        reconciliation = std::make_unique<BalanceReconciliation>(
            config, positionTracker, dataInterface, executionInterface);
        reconciliation->initialize();
    }
    
    RiskConfig config;
    std::shared_ptr<MockPositionTracker> positionTracker;
    std::shared_ptr<MockDataInterface> dataInterface;
    std::shared_ptr<MockExecutionInterface> executionInterface;
    std::unique_ptr<BalanceReconciliation> reconciliation;
};

// Tests
TEST_F(BalanceReconciliationTest, ReconcileWithNoDiscrepancies) {
    // Setup mock data
    EXPECT_CALL(*positionTracker, getTrackingExchanges())
        .WillOnce(Return(std::vector<std::string>{"binance"}));
    
    std::vector<Position> expectedPositions = {
        {"binance", "BTC/USDT", 1.0, 50000.0, 0.0, 0.0, std::chrono::system_clock::now()},
        {"binance", "ETH/USDT", 10.0, 3000.0, 0.0, 0.0, std::chrono::system_clock::now()}
    };
    
    EXPECT_CALL(*positionTracker, getExchangePositions("binance"))
        .WillOnce(Return(expectedPositions));
    
    std::map<std::string, double> actualPositions = {
        {"BTC/USDT", 1.0},
        {"ETH/USDT", 10.0}
    };
    
    EXPECT_CALL(*dataInterface, getExchangePositions("binance"))
        .WillOnce(Return(actualPositions));
    
    // Perform reconciliation
    auto result = reconciliation->reconcileAllPositions();
    
    // Should be successful with no discrepancies
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.matchedPositions, 2);
    EXPECT_EQ(result.mismatchedPositions, 0);
    EXPECT_TRUE(result.discrepancies.empty());
}

TEST_F(BalanceReconciliationTest, ReconcileWithDiscrepancies) {
    // Setup mock data
    EXPECT_CALL(*positionTracker, getTrackingExchanges())
        .WillOnce(Return(std::vector<std::string>{"binance"}));
    
    std::vector<Position> expectedPositions = {
        {"binance", "BTC/USDT", 1.0, 50000.0, 0.0, 0.0, std::chrono::system_clock::now()},
        {"binance", "ETH/USDT", 10.0, 3000.0, 0.0, 0.0, std::chrono::system_clock::now()}
    };
    
    EXPECT_CALL(*positionTracker, getExchangePositions("binance"))
        .WillOnce(Return(expectedPositions));
    
    std::map<std::string, double> actualPositions = {
        {"BTC/USDT", 0.95},  // 5% discrepancy
        {"ETH/USDT", 11.0}   // 10% discrepancy
    };
    
    EXPECT_CALL(*dataInterface, getExchangePositions("binance"))
        .WillOnce(Return(actualPositions));
    
    // Expect position override calls
    EXPECT_CALL(*positionTracker, overridePosition("binance", "BTC/USDT", 0.95))
        .WillOnce(Return(true));
    EXPECT_CALL(*positionTracker, overridePosition("binance", "ETH/USDT", 11.0))
        .WillOnce(Return(true));
    
    // Setup callback to detect events
    int callbackCount = 0;
    reconciliation->registerReconciliationCallback([&](const PositionReconciliationEvent& event) {
        callbackCount++;
        if (event.tradingPair == "BTC/USDT") {
            EXPECT_DOUBLE_EQ(event.expectedQuantity, 1.0);
            EXPECT_DOUBLE_EQ(event.actualQuantity, 0.95);
            EXPECT_DOUBLE_EQ(event.discrepancy, 0.05);
            EXPECT_NEAR(event.discrepancyPercent, 5.0, 0.1);
            EXPECT_TRUE(event.autoCorrect);
        } else if (event.tradingPair == "ETH/USDT") {
            EXPECT_DOUBLE_EQ(event.expectedQuantity, 10.0);
            EXPECT_DOUBLE_EQ(event.actualQuantity, 11.0);
            EXPECT_DOUBLE_EQ(event.discrepancy, -1.0);
            EXPECT_NEAR(event.discrepancyPercent, 10.0, 0.1);
            EXPECT_TRUE(event.autoCorrect);
        }
    });
    
    // Perform reconciliation
    auto result = reconciliation->reconcileAllPositions();
    
    // Should find discrepancies
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.matchedPositions, 0);
    EXPECT_EQ(result.mismatchedPositions, 2);
    EXPECT_EQ(result.discrepancies.size(), 2);
    
    // Callbacks should be triggered
    EXPECT_EQ(callbackCount, 2);
}

TEST_F(BalanceReconciliationTest, ReconcileWithMissingPositions) {
    // Setup mock data
    EXPECT_CALL(*positionTracker, getTrackingExchanges())
        .WillOnce(Return(std::vector<std::string>{"binance"}));
    
    std::vector<Position> expectedPositions = {
        {"binance", "BTC/USDT", 1.0, 50000.0, 0.0, 0.0, std::chrono::system_clock::now()},
        {"binance", "ETH/USDT", 10.0, 3000.0, 0.0, 0.0, std::chrono::system_clock::now()}
    };
    
    EXPECT_CALL(*positionTracker, getExchangePositions("binance"))
        .WillOnce(Return(expectedPositions));
    
    std::map<std::string, double> actualPositions = {
        {"BTC/USDT", 1.0},
        // ETH/USDT is missing
        {"LTC/USDT", 5.0}  // Extra position not in tracker
    };
    
    EXPECT_CALL(*dataInterface, getExchangePositions("binance"))
        .WillOnce(Return(actualPositions));
    
    // Expect position override calls
    EXPECT_CALL(*positionTracker, overridePosition("binance", "ETH/USDT", 0.0))
        .WillOnce(Return(true));
    EXPECT_CALL(*positionTracker, overridePosition("binance", "LTC/USDT", 5.0))
        .WillOnce(Return(true));
    
    // Perform reconciliation
    auto result = reconciliation->reconcileAllPositions();
    
    // Should find discrepancies
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.matchedPositions, 1);  // BTC/USDT matches
    EXPECT_EQ(result.mismatchedPositions, 2);  // ETH/USDT missing, LTC/USDT extra
    EXPECT_EQ(result.discrepancies.size(), 2);
    
    // Check specific discrepancies
    for (const auto& discrepancy : result.discrepancies) {
        if (discrepancy.tradingPair == "ETH/USDT") {
            EXPECT_DOUBLE_EQ(discrepancy.expectedQuantity, 10.0);
            EXPECT_DOUBLE_EQ(discrepancy.actualQuantity, 0.0);
            EXPECT_DOUBLE_EQ(discrepancy.discrepancy, 10.0);
        } else if (discrepancy.tradingPair == "LTC/USDT") {
            EXPECT_DOUBLE_EQ(discrepancy.expectedQuantity, 0.0);
            EXPECT_DOUBLE_EQ(discrepancy.actualQuantity, 5.0);
            EXPECT_DOUBLE_EQ(discrepancy.discrepancy, -5.0);
        }
    }
}

TEST_F(BalanceReconciliationTest, AutoCorrectionDisabled) {
    // Disable auto-correction
    config.balanceReconciliationConfig.enableAutoCorrection = false;
    reconciliation->updateConfiguration(config.balanceReconciliationConfig);
    
    // Setup mock data
    EXPECT_CALL(*positionTracker, getTrackingExchanges())
        .WillOnce(Return(std::vector<std::string>{"binance"}));
    
    std::vector<Position> expectedPositions = {
        {"binance", "BTC/USDT", 1.0, 50000.0, 0.0, 0.0, std::chrono::system_clock::now()}
    };
    
    EXPECT_CALL(*positionTracker, getExchangePositions("binance"))
        .WillOnce(Return(expectedPositions));
    
    std::map<std::string, double> actualPositions = {
        {"BTC/USDT", 0.95}  // 5% discrepancy
    };
    
    EXPECT_CALL(*dataInterface, getExchangePositions("binance"))
        .WillOnce(Return(actualPositions));
    
    // Should NOT get override calls
    EXPECT_CALL(*positionTracker, overridePosition(_, _, _))
        .Times(0);
    
    // Perform reconciliation
    auto result = reconciliation->reconcileAllPositions();
    
    // Should find discrepancy
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.matchedPositions, 0);
    EXPECT_EQ(result.mismatchedPositions, 1);
    
    // Check discrepancy has auto-correct disabled
    EXPECT_FALSE(result.discrepancies[0].autoCorrect);
}

TEST_F(BalanceReconciliationTest, ManualCorrection) {
    // Setup expectation
    EXPECT_CALL(*positionTracker, overridePosition("binance", "BTC/USDT", 0.95))
        .WillOnce(Return(true));
    
    // Manually correct position
    bool result = reconciliation->correctDiscrepancy("binance", "BTC/USDT", 0.95);
    
    // Should succeed
    EXPECT_TRUE(result);
}

TEST_F(BalanceReconciliationTest, ReconciliationFailedToFetchPositions) {
    // Setup mock data
    EXPECT_CALL(*positionTracker, getTrackingExchanges())
        .WillOnce(Return(std::vector<std::string>{"binance"}));
    
    // Mock data interface to fail
    EXPECT_CALL(*dataInterface, getExchangePositions("binance"))
        .WillOnce(::testing::Throw(std::runtime_error("Failed to fetch positions")));
    
    // Perform reconciliation
    auto result = reconciliation->reconcileExchangePositions("binance");
    
    // Should fail
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.matchedPositions, 0);
    EXPECT_EQ(result.mismatchedPositions, 0);
    EXPECT_TRUE(result.discrepancies.empty());
}

TEST_F(BalanceReconciliationTest, UpdateConfiguration) {
    // New configuration
    BalanceReconciliationConfig newConfig;
    newConfig.enableScheduledReconciliation = true;
    newConfig.reconciliationIntervalMinutes = 30;
    newConfig.positionDiscrepancyTolerance = 0.002;
    newConfig.enableAutoCorrection = false;
    newConfig.autoCorrectThresholdPercent = 2.0;
    newConfig.criticalDiscrepancyPercent = 5.0;
    
    // Update configuration
    bool result = reconciliation->updateConfiguration(newConfig);
    EXPECT_TRUE(result);
    
    // Check updated configuration
    auto updatedConfig = reconciliation->getConfiguration();
    EXPECT_EQ(updatedConfig.enableScheduledReconciliation, true);
    EXPECT_EQ(updatedConfig.reconciliationIntervalMinutes, 30);
    EXPECT_DOUBLE_EQ(updatedConfig.positionDiscrepancyTolerance, 0.002);
    EXPECT_EQ(updatedConfig.enableAutoCorrection, false);
    EXPECT_DOUBLE_EQ(updatedConfig.autoCorrectThresholdPercent, 2.0);
    EXPECT_DOUBLE_EQ(updatedConfig.criticalDiscrepancyPercent, 5.0);
}

} // namespace test
} // namespace risk