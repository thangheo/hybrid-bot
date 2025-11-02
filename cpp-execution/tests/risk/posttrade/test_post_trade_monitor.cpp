#include "risk/posttrade/post_trade_monitor.hpp"
#include "risk/risk_types.hpp"
#include "risk/risk_events.hpp"
#include "risk/risk_config.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>

namespace risk {
namespace test {

// Mock sub-components for testing
class MockCircuitBreaker {
public:
    MOCK_METHOD(bool, initialize, (), ());
    MOCK_METHOD(CircuitBreakerState, evaluateMetrics, (const std::map<std::string, double>&), ());
    MOCK_METHOD(CircuitBreakerState, updateMetric, (const std::string&, double), ());
    MOCK_METHOD(CircuitBreakerState, getState, (), (const));
    MOCK_METHOD(bool, manualTrigger, (const std::string&), ());
    MOCK_METHOD(bool, reset, (), ());
    MOCK_METHOD(bool, isTradingAllowed, (), (const));
    MOCK_METHOD(void, registerStateChangeCallback, (const std::function<void(CircuitBreakerState, CircuitBreakerState, const std::string&)>&), ());
    MOCK_METHOD(bool, updateConfiguration, (const CircuitBreakerConfig&), ());
};

class MockStopLossManager {
public:
    MOCK_METHOD(bool, initialize, (), ());
    MOCK_METHOD(void, processExecutionReport, (const ExecutionReport&), ());
    MOCK_METHOD(void, updateMarketData, (const MarketData&), ());
    MOCK_METHOD(std::vector<StopLossEvent>, checkStopLosses, (), ());
    MOCK_METHOD(bool, updateConfiguration, (const StopLossConfig&), ());
    MOCK_METHOD(void, registerStopLossCallback, (const std::function<void(const StopLossEvent&)>&), ());
};

class MockDrawdownMonitor {
public:
    MOCK_METHOD(bool, initialize, (), ());
    MOCK_METHOD(std::vector<DrawdownEvent>, updatePerformanceMetrics, (const PerformanceMetrics&), ());
    MOCK_METHOD(bool, updateConfiguration, (const DrawdownConfig&), ());
    MOCK_METHOD(void, registerDrawdownCallback, (const std::function<void(const DrawdownEvent&)>&), ());
};

class MockBalanceReconciliation {
public:
    MOCK_METHOD(bool, initialize, (), ());
    MOCK_METHOD(ReconciliationResult, reconcileAllPositions, (), ());
    MOCK_METHOD(ReconciliationResult, reconcileExchangePositions, (const ExchangeId&), ());
    MOCK_METHOD(bool, updateConfiguration, (const BalanceReconciliationConfig&), ());
    MOCK_METHOD(void, registerReconciliationCallback, (const std::function<void(const PositionReconciliationEvent&)>&), ());
};

// Test fixture with dependency injection for mocks
class PostTradeMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup default config
        config = RiskConfig();
        
        // Setup mock components
        mockCircuitBreaker = std::make_shared<MockCircuitBreaker>();
        mockStopLossManager = std::make_shared<MockStopLossManager>();
        mockDrawdownMonitor = std::make_shared<MockDrawdownMonitor>();
        mockBalanceReconciliation = std::make_shared<MockBalanceReconciliation>();
        
        // Expect initialization calls when monitor is created
        EXPECT_CALL(*mockCircuitBreaker, initialize())
            .WillOnce(::testing::Return(true));
        EXPECT_CALL(*mockStopLossManager, initialize())
            .WillOnce(::testing::Return(true));
        EXPECT_CALL(*mockDrawdownMonitor, initialize())
            .WillOnce(::testing::Return(true));
        EXPECT_CALL(*mockBalanceReconciliation, initialize())
            .WillOnce(::testing::Return(true));
        
        // Expect callback registration
        EXPECT_CALL(*mockCircuitBreaker, registerStateChangeCallback(::testing::_));
        EXPECT_CALL(*mockStopLossManager, registerStopLossCallback(::testing::_));
        EXPECT_CALL(*mockDrawdownMonitor, registerDrawdownCallback(::testing::_));
        EXPECT_CALL(*mockBalanceReconciliation, registerReconciliationCallback(::testing::_));
        
        // Create test instance with injection
        postTradeMonitor = std::make_unique<PostTradeMonitor>(
            config, mockCircuitBreaker, mockStopLossManager, mockDrawdownMonitor, mockBalanceReconciliation);
        postTradeMonitor->initialize();
    }
    
    RiskConfig config;
    std::shared_ptr<MockCircuitBreaker> mockCircuitBreaker;
    std::shared_ptr<MockStopLossManager> mockStopLossManager;
    std::shared_ptr<MockDrawdownMonitor> mockDrawdownMonitor;
    std::shared_ptr<MockBalanceReconciliation> mockBalanceReconciliation;
    std::unique_ptr<PostTradeMonitor> postTradeMonitor;
};

// Tests
TEST_F(PostTradeMonitorTest, ProcessExecutionReport) {
    // Setup expectations
    EXPECT_CALL(*mockCircuitBreaker, isTradingAllowed())
        .WillOnce(::testing::Return(true));
    
    EXPECT_CALL(*mockStopLossManager, processExecutionReport(::testing::_));
    
    EXPECT_CALL(*mockStopLossManager, checkStopLosses())
        .WillOnce(::testing::Return(std::vector<StopLossEvent>{}));
    
    EXPECT_CALL(*mockCircuitBreaker, getState())
        .WillOnce(::testing::Return(CircuitBreakerState::OFF));
    
    // Create execution report
    ExecutionReport report;
    report.setExchangeId(ExchangeId::fromString("binance"));
    report.setSymbol(TradingPair::fromString("BTC/USDT"));
    
    // Process report
    postTradeMonitor->processExecutionReport(report);
}
TEST_F(PostTradeMonitorTest, ProcessExecutionReportWhenCircuitBreakerTriggered) {
    // Setup expectations - trading not allowed
    EXPECT_CALL(*mockCircuitBreaker, isTradingAllowed())
        .WillOnce(::testing::Return(false));
    
    // Should not process execution when circuit breaker is triggered
    EXPECT_CALL(*mockStopLossManager, processExecutionReport(::testing::_))
        .Times(0);
    
    // Create execution report
    ExecutionReport report;
    report.setExchangeId(ExchangeId::fromString("binance"));
    report.setSymbol(TradingPair::fromString("BTC/USDT"));

    // Register a callback to detect risk events
    bool eventTriggered = false;
    postTradeMonitor->registerRiskEventCallback([&](const RiskEvent& event) {
        eventTriggered = true;
        EXPECT_EQ(event.type, RiskEventType::TRADING_BLOCKED);
    });
    
    // Process report
    postTradeMonitor->processExecutionReport(report);
    
    // Event should be triggered
    EXPECT_TRUE(eventTriggered);
}

TEST_F(PostTradeMonitorTest, UpdateMarketData) {
    // Setup expectations
    EXPECT_CALL(*mockStopLossManager, updateMarketData(::testing::_));
    
    EXPECT_CALL(*mockStopLossManager, checkStopLosses())
        .WillOnce(::testing::Return(std::vector<StopLossEvent>{}));
    
    EXPECT_CALL(*mockCircuitBreaker, getState())
        .WillOnce(::testing::Return(CircuitBreakerState::OFF));
    
    // Create market data
    MarketData marketData;
    marketData.exchangeId = ExchangeId::fromString("binance");
    marketData.symbol = "BTC/USDT";
    marketData.lastPrice = 50000.0;
    
    // Update market data
    postTradeMonitor->updateMarketData(marketData);
}

TEST_F(PostTradeMonitorTest, UpdatePerformanceMetrics) {
    // Setup expectations
    EXPECT_CALL(*mockDrawdownMonitor, updatePerformanceMetrics(::testing::_))
        .WillOnce(::testing::Return(std::vector<DrawdownEvent>{}));
    
    EXPECT_CALL(*mockCircuitBreaker, updateMetric(::testing::_, ::testing::_))
        .Times(::testing::AtLeast(1))
        .WillRepeatedly(::testing::Return(CircuitBreakerState::OFF));
    
    EXPECT_CALL(*mockStopLossManager, checkStopLosses())
        .WillOnce(::testing::Return(std::vector<StopLossEvent>{}));
    
    EXPECT_CALL(*mockCircuitBreaker, getState())
        .WillOnce(::testing::Return(CircuitBreakerState::OFF));
    
    // Create performance metrics
    PerformanceMetrics metrics;
    metrics.portfolioValue = 100.0;
    metrics.strategyMetrics["strategy1"].value = 50.0;
    metrics.riskMetrics["drawdown"] = 0.03;
    metrics.riskMetrics["sharpe_ratio"] = 1.5;
    
    // Update performance metrics
    postTradeMonitor->updatePerformanceMetrics(metrics);
}

TEST_F(PostTradeMonitorTest, StopLossTriggered) {
    // Setup stop loss event
    StopLossEvent stopLossEvent;
    stopLossEvent.exchangeId = ExchangeId::fromString("binance");
    stopLossEvent.tradingPair = TradingPair::fromString("BTC/USDT");
    stopLossEvent.positionSize = 1.0;
    stopLossEvent.entryPrice = 50000.0;
    stopLossEvent.triggerPrice = 47500.0;
    stopLossEvent.lossPercentage = 0.05;
    stopLossEvent.timestamp = std::chrono::system_clock::now();
    
    std::vector<StopLossEvent> stopLossEvents = {stopLossEvent};
    
    // Setup expectations
    EXPECT_CALL(*mockCircuitBreaker, isTradingAllowed())
        .WillOnce(::testing::Return(true));
    
    EXPECT_CALL(*mockStopLossManager, processExecutionReport(::testing::_));
    
    EXPECT_CALL(*mockStopLossManager, checkStopLosses())
        .WillOnce(::testing::Return(stopLossEvents));
    
    EXPECT_CALL(*mockCircuitBreaker, getState())
        .WillOnce(::testing::Return(CircuitBreakerState::OFF));
    
    // Register a callback to detect risk events
    bool eventTriggered = false;
    postTradeMonitor->registerRiskEventCallback([&](const RiskEvent& event) {
        if (event.type == RiskEventType::STOP_LOSS) {
            eventTriggered = true;
            EXPECT_EQ(event.stopLossEvent->exchangeId, "binance");
            EXPECT_EQ(event.stopLossEvent->tradingPair, "BTC/USDT");
        }
    });
    
    // Process an execution report (which will trigger the stop loss check)
    ExecutionReport report;
    report.exchangeId = ExchangeId::fromString("binance");
    report.tradingPair = TradingPair::fromString("BTC/USDT");
    
    postTradeMonitor->processExecutionReport(report);
    
    // Event should be triggered
    EXPECT_TRUE(eventTriggered);
}

TEST_F(PostTradeMonitorTest, CircuitBreakerTriggered) {
    // Setup expectation for circuit breaker state change callback
    bool callbackTriggered = false;
    
    // This tests the callback registration by simulating what happens when circuit breaker triggers
    EXPECT_CALL(*mockCircuitBreaker, registerStateChangeCallback(::testing::_))
        .WillOnce(::testing::SaveArg<0>(&circuitBreakerCallback));
    
    // Re-initialize to set up callback capture
    postTradeMonitor->initialize();
    
    // Register a callback to detect risk events
    postTradeMonitor->registerRiskEventCallback([&](const RiskEvent& event) {
        if (event.type == RiskEventType::CIRCUIT_BREAKER) {
            callbackTriggered = true;
            EXPECT_EQ(event.circuitBreakerEvent->oldState, CircuitBreakerState::OFF);
            EXPECT_EQ(event.circuitBreakerEvent->newState, CircuitBreakerState::TRIGGERED);
        }
    });
    
    // Simulate circuit breaker triggering by calling the captured callback
    if (circuitBreakerCallback) {
        circuitBreakerCallback(
            CircuitBreakerState::OFF, 
            CircuitBreakerState::TRIGGERED, 
            "Test circuit breaker trigger"
        );
    }
    
    // Event should be triggered
    EXPECT_TRUE(callbackTriggered);
    
    // Mode should be set to EMERGENCY
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::EMERGENCY);
}

TEST_F(PostTradeMonitorTest, ReconcilePositions) {
    // Setup mock results
    ReconciliationResult result;
    result.success = true;
    result.matchedPositions = 2;
    result.mismatchedPositions = 0;
    
    // Setup expectations
    EXPECT_CALL(*mockBalanceReconciliation, reconcileAllPositions())
        .WillOnce(::testing::Return(result));
    
    // Reconcile all positions
    bool success = postTradeMonitor->reconcilePositions();
    EXPECT_TRUE(success);
    
    // Setup mock result for specific exchange
    ReconciliationResult exchangeResult;
    exchangeResult.success = true;
    exchangeResult.matchedPositions = 1;
    exchangeResult.mismatchedPositions = 0;
    
    // Setup expectations for specific exchange
    EXPECT_CALL(*mockBalanceReconciliation, reconcileExchangePositions("binance"))
        .WillOnce(::testing::Return(exchangeResult));
    
    // Reconcile specific exchange
    success = postTradeMonitor->reconcilePositions("binance");
    EXPECT_TRUE(success);
}

TEST_F(PostTradeMonitorTest, UpdateConfiguration) {
    // Setup expectations
    EXPECT_CALL(*mockCircuitBreaker, updateConfiguration(::testing::_))
        .WillOnce(::testing::Return(true));
    EXPECT_CALL(*mockStopLossManager, updateConfiguration(::testing::_))
        .WillOnce(::testing::Return(true));
    EXPECT_CALL(*mockDrawdownMonitor, updateConfiguration(::testing::_))
        .WillOnce(::testing::Return(true));
    EXPECT_CALL(*mockBalanceReconciliation, updateConfiguration(::testing::_))
        .WillOnce(::testing::Return(true));
    
    // Update configuration
    RiskConfig newConfig;
    bool result = postTradeMonitor->updateConfiguration(newConfig);
    
    // Should succeed if all components succeed
    EXPECT_TRUE(result);
}

TEST_F(PostTradeMonitorTest, GetComponents) {
    // Test getting component references
    EXPECT_EQ(&postTradeMonitor->getCircuitBreaker(), mockCircuitBreaker.get());
    EXPECT_EQ(&postTradeMonitor->getStopLossManager(), mockStopLossManager.get());
    EXPECT_EQ(&postTradeMonitor->getDrawdownMonitor(), mockDrawdownMonitor.get());
    EXPECT_EQ(&postTradeMonitor->getBalanceReconciliation(), mockBalanceReconciliation.get());
}

TEST_F(PostTradeMonitorTest, RiskModeTransitions) {
    // Test mode transitions
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::NORMAL);
    
    // Register a callback to detect mode changes
    bool modeChangeDetected = false;
    postTradeMonitor->registerRiskEventCallback([&](const RiskEvent& event) {
        if (event.type == RiskEventType::RISK_MODE_CHANGE) {
            modeChangeDetected = true;
        }
    });
    
    // Change mode to HEIGHTENED_RISK
    postTradeMonitor->setMode(RiskManagerMode::HEIGHTENED_RISK);
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::HEIGHTENED_RISK);
    EXPECT_TRUE(modeChangeDetected);
    
    // Reset flag and change to a different mode
    modeChangeDetected = false;
    postTradeMonitor->setMode(RiskManagerMode::EMERGENCY);
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::EMERGENCY);
    EXPECT_TRUE(modeChangeDetected);
    
    // No event if mode doesn't change
    modeChangeDetected = false;
    postTradeMonitor->setMode(RiskManagerMode::EMERGENCY);
    EXPECT_FALSE(modeChangeDetected);
}

} // namespace test
} // namespace risk