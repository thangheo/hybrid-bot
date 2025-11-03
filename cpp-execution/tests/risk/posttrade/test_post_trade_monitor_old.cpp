#include "risk/posttrade/post_trade_monitor.hpp"
#include "risk/risk_types.hpp"
#include "risk/risk_events.hpp"
#include "risk/risk_config.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>

using namespace trading::risk;
using namespace trading::strategy;

namespace risk {
namespace test {

/**
 * Integration test fixture for PostTradeMonitor
 * Tests the actual PostTradeMonitor class without mocking internal components
 */
class PostTradeMonitorIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a valid configuration that will pass validation
        config = createValidRiskConfig();
        
        // Create PostTradeMonitor instance
        postTradeMonitor = std::make_unique<PostTradeMonitor>(config, nullptr);
        
        // Initialize (this should succeed with valid config)
        ASSERT_TRUE(postTradeMonitor->initialize()) << "PostTradeMonitor initialization should succeed with valid config";
    }

    void TearDown() override {
        if (postTradeMonitor) {
            postTradeMonitor.reset();
        }
    }

    /**
     * Create a valid RiskConfig that passes all validation checks
     */
    RiskConfig createValidRiskConfig() {
        RiskConfig config;
    MOCK_METHOD(bool, initialize, (), ());
    MOCK_METHOD(ReconciliationResult, reconcileAllPositions, (), ());
    MOCK_METHOD(ReconciliationResult, reconcileExchangePositions, (const ExchangeId&));
    MOCK_METHOD(bool, updateConfiguration, (const BalanceReconciliationConfig&));
    MOCK_METHOD(void, registerReconciliationCallback, (const std::function<void(const PositionReconciliationEvent&)>&));
};

}
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
        
        // Create test instance with proper constructor (explicit constructor)
        postTradeMonitor = std::make_unique<PostTradeMonitor>(config, nullptr);
        postTradeMonitor->initialize();
    }
    
    RiskConfig config;
    std::shared_ptr<MockCircuitBreaker> mockCircuitBreaker;
    std::shared_ptr<MockStopLossManager> mockStopLossManager;
    std::shared_ptr<MockDrawdownMonitor> mockDrawdownMonitor;
    std::shared_ptr<MockBalanceReconciliation> mockBalanceReconciliation;
    std::unique_ptr<PostTradeMonitor> postTradeMonitor;
    
    // Callback function for circuit breaker state changes
    std::function<void(CircuitBreakerState, CircuitBreakerState, const std::string&)> circuitBreakerCallback;
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
        EXPECT_EQ(event.type, RiskEventType::CIRCUIT_BREAKER_TRIGGERED);
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
    marketData.tradingPair = TradingPair::fromString("BTC/USDT");
    marketData.price = 45000.0;
    
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
    metrics.pnl = 100.0;
    metrics.maxDrawdown = 0.03;
    metrics.sharpeRatio = 1.5;
    metrics.winRate = 0.65;
    metrics.profitFactor = 1.8;
    
    // Update performance metrics
    postTradeMonitor->updatePerformanceMetrics(metrics);
}

TEST_F(PostTradeMonitorTest, StopLossTriggered) {
    // Setup stop loss event

    // StopLossEvent(
    //     Timestamp eventTime,
    //     ExchangeId exchange,
    //     TradingPair pair,
    //     StrategyId strategy,
    //     double size,
    //     double entry,
    //     double current,
    //     double lossPercent,
    //     bool liquidation,
    //     std::string desc
    // )
    StopLossEvent stopLossEvent(
        std::chrono::system_clock::now(),
        ExchangeId::fromString("binance"),
        TradingPair::fromString("BTC/USDT"),
        StrategyId("strategy1"),
        1.0,
        50000.0,
        47500.0,
        0.05,
        false,
        "Stop loss triggered for BTC/USDT"
    );
    // stopLossEvent.exchangeId = ExchangeId::fromString("binance");
    // stopLossEvent.tradingPair = TradingPair::fromString("BTC/USDT");
    // stopLossEvent.positionSize = 1.0;
    // stopLossEvent.entryPrice = 50000.0;
    // stopLossEvent.triggerPrice = 47500.0;
    // stopLossEvent.lossPercentage = 0.05;
    // stopLossEvent.timestamp = std::chrono::system_clock::now();
    
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
        if (event.type == RiskEventType::STOP_LOSS_TRIGGERED) {
            eventTriggered = true;
            EXPECT_EQ(event.description, "Stop loss triggered");
        }
    });
    
    // Process an execution report (which will trigger the stop loss check)
    ExecutionReport report;
    report.setExchangeId(ExchangeId::fromString("binance"));
    report.setSymbol(TradingPair::fromString("BTC/USDT"));

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
        if (event.type == RiskEventType::CIRCUIT_BREAKER_TRIGGERED) {
            callbackTriggered = true;
            EXPECT_EQ(event.description, "Circuit breaker triggered");
            EXPECT_TRUE(event.isCritical());
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
    auto futureResult = postTradeMonitor->reconcilePositions();
    result = futureResult.get(); // Wait for the result
    EXPECT_EQ(result.mismatchedPositions, 0);
    
    // Setup mock result for specific exchange
    ReconciliationResult exchangeResult;
    exchangeResult.success = true;
    exchangeResult.matchedPositions = 1;
    exchangeResult.mismatchedPositions = 0;
    
    // Setup expectations for specific exchange
    EXPECT_CALL(*mockBalanceReconciliation, reconcileExchangePositions(ExchangeId::fromString("binance")))
        .WillOnce(::testing::Return(exchangeResult));
    
    // Reconcile specific exchange
    auto futureExchangeResult = postTradeMonitor->reconcilePositions(ExchangeId::fromString("binance"));
    auto exchangeResult2 = futureExchangeResult.get(); // Wait for the result
    EXPECT_EQ(exchangeResult2.mismatchedPositions, 0);
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
    // Test that components are accessible (basic functionality test)
    // Note: Since PostTradeMonitor creates its own internal components,
    // we just verify they exist and are accessible
    
    // These should not be null and should return valid references
    EXPECT_NO_THROW(postTradeMonitor->getCircuitBreaker());
    EXPECT_NO_THROW(postTradeMonitor->getStopLossManager()); 
    EXPECT_NO_THROW(postTradeMonitor->getDrawdownMonitor());
    EXPECT_NO_THROW(postTradeMonitor->getBalanceReconciliation());
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