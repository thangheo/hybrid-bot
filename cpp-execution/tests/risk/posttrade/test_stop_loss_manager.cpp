#include "risk/posttrade/stop_loss_manager.hpp"
#include "risk/portfolio/position_tracker.hpp"
#include "risk/interfaces/data_interface.hpp"
#include "risk/interfaces/execution_interface.hpp"
#include "risk/risk_types.hpp"
#include "risk/risk_events.hpp"
#include "risk/risk_config.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
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
    MOCK_METHOD(std::optional<Position>, getPosition, (const ExchangeId&, const TradingPair&), (override));
    MOCK_METHOD(std::vector<Position>, getExchangePositions, (const ExchangeId&), (override));
    MOCK_METHOD(bool, overridePosition, (const ExchangeId&, const TradingPair&, double), (override));
};

class MockDataInterface : public DataInterface {
public:
    MOCK_METHOD(std::optional<double>, getLastPrice, (const ExchangeId&, const TradingPair&), (override));
    MOCK_METHOD(std::map<std::string, double>, getExchangePositions, (const ExchangeId&), (override));
};

class MockExecutionInterface : public ExecutionInterface {
public:
    MOCK_METHOD(bool, submitOrder, (const Order&), (override));
    MOCK_METHOD(bool, cancelOrder, (const std::string&), (override));
};

// Test fixture
class StopLossManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup default config
        config.stopLossConfig.enableDefaultStopLoss = true;
        config.stopLossConfig.defaultStopLossPercent = 0.05;
        config.stopLossConfig.useTrailingStopByDefault = false;
        config.stopLossConfig.defaultTrailingDistance = 0.02;
        config.stopLossConfig.autoLiquidateOnTrigger = true;
        config.stopLossConfig.criticalLossThreshold = 0.10;
        
        // Setup mocks
        positionTracker = std::make_shared<NiceMock<MockPositionTracker>>();
        dataInterface = std::make_shared<NiceMock<MockDataInterface>>();
        executionInterface = std::make_shared<NiceMock<MockExecutionInterface>>();
        
        // Create test instance
        stopLossManager = std::make_unique<StopLossManager>(
            config, positionTracker, dataInterface, executionInterface);
    }
    
    RiskConfig config;
    std::shared_ptr<MockPositionTracker> positionTracker;
    std::shared_ptr<MockDataInterface> dataInterface;
    std::shared_ptr<MockExecutionInterface> executionInterface;
    std::unique_ptr<StopLossManager> stopLossManager;
};

// Tests
TEST_F(StopLossManagerTest, InitializeCreatesDefaultStopLosses) {
    // Setup mock positions
    std::vector<Position> positions = {
        {"binance", "BTC/USDT", 1.0, 50000.0, 0.0, 0.0, std::chrono::system_clock::now()},
        {"ftx", "ETH/USDT", 10.0, 3000.0, 0.0, 0.0, std::chrono::system_clock::now()}
    };
    
    EXPECT_CALL(*positionTracker, getAllPositions())
        .WillOnce(Return(positions));
    
    // For the first position (BTC/USDT)
    Position btcPosition = positions[0];
    EXPECT_CALL(*positionTracker, getPosition("binance", "BTC/USDT"))
        .WillOnce(Return(btcPosition));
    
    // For price fetching
    EXPECT_CALL(*dataInterface, getLastPrice("binance", "BTC/USDT"))
        .WillOnce(Return(50000.0));
    
    // For the second position (ETH/USDT)
    Position ethPosition = positions[1];
    EXPECT_CALL(*positionTracker, getPosition("ftx", "ETH/USDT"))
        .WillOnce(Return(ethPosition));
    
    EXPECT_CALL(*dataInterface, getLastPrice("ftx", "ETH/USDT"))
        .WillOnce(Return(3000.0));
    
    // Initialize and check
    EXPECT_TRUE(stopLossManager->initialize());
    
    // Verify stop losses were created with correct values
    auto btcStopLoss = stopLossManager->getStopLoss("binance", "BTC/USDT");
    EXPECT_TRUE(btcStopLoss.has_value());
    if (btcStopLoss) {
        // Default 5% below entry price
        EXPECT_DOUBLE_EQ(btcStopLoss->stopPrice, 50000.0 * 0.95);
        EXPECT_FALSE(btcStopLoss->isTrailing);
    }
    
    auto ethStopLoss = stopLossManager->getStopLoss("ftx", "ETH/USDT");
    EXPECT_TRUE(ethStopLoss.has_value());
    if (ethStopLoss) {
        // Default 5% below entry price
        EXPECT_DOUBLE_EQ(ethStopLoss->stopPrice, 3000.0 * 0.95);
        EXPECT_FALSE(ethStopLoss->isTrailing);
    }
}

TEST_F(StopLossManagerTest, SetAndRetrieveStopLoss) {
    // Setup mock position
    Position position = {"binance", "BTC/USDT", 1.0, 50000.0, 0.0, 0.0, std::chrono::system_clock::now()};
    
    EXPECT_CALL(*positionTracker, getPosition("binance", "BTC/USDT"))
        .WillOnce(Return(position));
    
    EXPECT_CALL(*dataInterface, getLastPrice("binance", "BTC/USDT"))
        .WillOnce(Return(50000.0));
    
    // Set stop loss
    bool result = stopLossManager->setStopLoss("binance", "BTC/USDT", 45000.0, true, 0.02);
    EXPECT_TRUE(result);
    
    // Retrieve stop loss
    auto stopLoss = stopLossManager->getStopLoss("binance", "BTC/USDT");
    EXPECT_TRUE(stopLoss.has_value());
    if (stopLoss) {
        EXPECT_DOUBLE_EQ(stopLoss->stopPrice, 45000.0);
        EXPECT_TRUE(stopLoss->isTrailing);
        EXPECT_DOUBLE_EQ(stopLoss->trailingDistance, 0.02);
        EXPECT_TRUE(stopLoss->active);
    }
}

TEST_F(StopLossManagerTest, RemoveStopLoss) {
    // Setup mock position
    Position position = {"binance", "BTC/USDT", 1.0, 50000.0, 0.0, 0.0, std::chrono::system_clock::now()};
    
    EXPECT_CALL(*positionTracker, getPosition("binance", "BTC/USDT"))
        .WillRepeatedly(Return(position));
    
    EXPECT_CALL(*dataInterface, getLastPrice("binance", "BTC/USDT"))
        .WillOnce(Return(50000.0));
    
    // Set stop loss
    stopLossManager->setStopLoss("binance", "BTC/USDT", 45000.0);
    
    // Remove stop loss
    bool result = stopLossManager->removeStopLoss("binance", "BTC/USDT");
    EXPECT_TRUE(result);
    
    // Verify stop loss was removed
    auto stopLoss = stopLossManager->getStopLoss("binance", "BTC/USDT");
    EXPECT_FALSE(stopLoss.has_value());
}

TEST_F(StopLossManagerTest, StopLossTriggeredForLongPosition) {
    // Setup mock position - long position
    Position position = {"binance", "BTC/USDT", 1.0, 50000.0, 0.0, 0.0, std::chrono::system_clock::now()};
    
    EXPECT_CALL(*positionTracker, getPosition("binance", "BTC/USDT"))
        .WillRepeatedly(Return(position));
    
    EXPECT_CALL(*dataInterface, getLastPrice("binance", "BTC/USDT"))
        .WillOnce(Return(50000.0))   // For initial set
        .WillOnce(Return(47000.0));  // For check (above stop loss)
    
    // Set stop loss at 5% below entry
    stopLossManager->setStopLoss("binance", "BTC/USDT", 47500.0);
    
    // First check - price above stop level
    auto events = stopLossManager->checkStopLosses();
    EXPECT_TRUE(events.empty());
    
    // Now price drops below stop level
    EXPECT_CALL(*dataInterface, getLastPrice("binance", "BTC/USDT"))
        .WillOnce(Return(47000.0));
    
    // Stop loss should be triggered
    events = stopLossManager->checkStopLosses();
    EXPECT_EQ(events.size(), 1);
    
    if (!events.empty()) {
        auto& event = events[0];
        EXPECT_EQ(event.exchangeId, "binance");
        EXPECT_EQ(event.tradingPair, "BTC/USDT");
        EXPECT_DOUBLE_EQ(event.positionSize, 1.0);
        EXPECT_DOUBLE_EQ(event.entryPrice, 50000.0);
        EXPECT_DOUBLE_EQ(event.triggerPrice, 47000.0);
    }
}

TEST_F(StopLossManagerTest, TrailingStopUpdatesForLongPosition) {
    // Setup mock position - long position
    Position position = {"binance", "BTC/USDT", 1.0, 50000.0, 0.0, 0.0, std::chrono::system_clock::now()};
    
    EXPECT_CALL(*positionTracker, getPosition("binance", "BTC/USDT"))
        .WillRepeatedly(Return(position));
    
    EXPECT_CALL(*dataInterface, getLastPrice("binance", "BTC/USDT"))
        .WillOnce(Return(50000.0));  // For initial set
    
    // Set trailing stop loss at 5% below current price
    stopLossManager->setStopLoss("binance", "BTC/USDT", 47500.0, true, 0.05);
    
    // Initial state
    auto initialStopLoss = stopLossManager->getStopLoss("binance", "BTC/USDT");
    EXPECT_TRUE(initialStopLoss.has_value());
    if (initialStopLoss) {
        EXPECT_DOUBLE_EQ(initialStopLoss->stopPrice, 47500.0);
        EXPECT_DOUBLE_EQ(initialStopLoss->highestPrice, 50000.0);
    }
    
    // Price increases
    MarketData marketData;
    marketData.symbol = "BTC/USDT";
    marketData.lastPrice = 52000.0;
    stopLossManager->updateMarketData(marketData);
    
    // Check that trailing stop updated
    auto updatedStopLoss = stopLossManager->getStopLoss("binance", "BTC/USDT");
    EXPECT_TRUE(updatedStopLoss.has_value());
    if (updatedStopLoss) {
        EXPECT_DOUBLE_EQ(updatedStopLoss->highestPrice, 52000.0);
        EXPECT_DOUBLE_EQ(updatedStopLoss->stopPrice, 52000.0 * 0.95);
    }
    
    // Price decreases, but not enough to trigger
    marketData.lastPrice = 51000.0;
    stopLossManager->updateMarketData(marketData);
    
    // Check that trailing stop didn't change
    updatedStopLoss = stopLossManager->getStopLoss("binance", "BTC/USDT");
    EXPECT_TRUE(updatedStopLoss.has_value());
    if (updatedStopLoss) {
        EXPECT_DOUBLE_EQ(updatedStopLoss->highestPrice, 52000.0);
        EXPECT_DOUBLE_EQ(updatedStopLoss->stopPrice, 52000.0 * 0.95);
    }
}

TEST_F(StopLossManagerTest, AutoLiquidateOnTrigger) {
    // Setup mock position
    Position position = {"binance", "BTC/USDT", 1.0, 50000.0, 0.0, 0.0, std::chrono::system_clock::now()};
    
    EXPECT_CALL(*positionTracker, getPosition("binance", "BTC/USDT"))
        .WillRepeatedly(Return(position));
    
    EXPECT_CALL(*dataInterface, getLastPrice("binance", "BTC/USDT"))
        .WillOnce(Return(50000.0))   // For initial set
        .WillOnce(Return(47000.0));  // For check (below stop loss)
    
    // Set stop loss
    stopLossManager->setStopLoss("binance", "BTC/USDT", 47500.0);
    
    // Expect order submission for liquidation
    EXPECT_CALL(*executionInterface, submitOrder(_))
        .WillOnce([](const Order& order) {
            EXPECT_EQ(order.exchangeId, "binance");
            EXPECT_EQ(order.tradingPair, "BTC/USDT");
            EXPECT_EQ(order.side, "SELL");  // Selling long position
            EXPECT_DOUBLE_EQ(order.quantity, 1.0);
            EXPECT_EQ(order.orderType, "MARKET");
            return true;
        });
    
    // Check stop losses (will trigger and auto-liquidate)
    auto events = stopLossManager->checkStopLosses();
    EXPECT_EQ(events.size(), 1);
}

} // namespace test
} // namespace risk