#include "risk/posttrade/circuit_breaker.hpp"
#include "risk/risk_types.hpp"
#include "risk/risk_events.hpp"
#include "risk/risk_config.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <chrono>
#include <map>

namespace risk {
namespace test {

// Test fixture
class CircuitBreakerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup default config
        cbConfig.mandatoryHaltPeriodSeconds = 60;
        cbConfig.coolingPeriodSeconds = 120;
        cbConfig.maxTriggersBeforeExtendedHalt = 3;
        cbConfig.extendedHaltMultiplier = 2;
        cbConfig.triggerCountResetMinutes = 30;
        cbConfig.gradualTradingResumption = true;
        cbConfig.maxHistorySize = 100;
        
        // Setup metric thresholds
        MetricThreshold drawdownThreshold;
        drawdownThreshold.warningThreshold = 0.05;
        drawdownThreshold.criticalThreshold = 0.10;
        drawdownThreshold.isHigherBetter = false;
        
        MetricThreshold sharpeThreshold;
        sharpeThreshold.warningThreshold = 0.5;
        sharpeThreshold.criticalThreshold = 0.0;
        sharpeThreshold.isHigherBetter = true;
        
        cbConfig.metricThresholds["drawdown"] = drawdownThreshold;
        cbConfig.metricThresholds["sharpe_ratio"] = sharpeThreshold;
        
        // Create test instance
        circuitBreaker = std::make_unique<CircuitBreaker>(config);
        circuitBreaker->updateConfiguration(cbConfig);
        circuitBreaker->initialize();
    }
    RiskConfig config;
    CircuitBreakerConfig cbConfig;
    std::unique_ptr<CircuitBreaker> circuitBreaker;
};

// Tests
TEST_F(CircuitBreakerTest, InitialStateIsOff) {
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::OFF);
    EXPECT_TRUE(circuitBreaker->isTradingAllowed());
}

TEST_F(CircuitBreakerTest, WarningStateWhenMetricExceedsWarningThreshold) {
    // Create metrics with drawdown at warning level
    CircuitBreaker::MetricMap metrics;
    metrics["drawdown"] = 0.06;  // Warning threshold is 0.05
    metrics["sharpe_ratio"] = 1.0;  // Above warning threshold
    
    // Evaluate metrics
    auto state = circuitBreaker->evaluateMetrics(metrics);
    
    // Should be in warning state
    EXPECT_EQ(state, CircuitBreakerState::WARNING);
    EXPECT_TRUE(circuitBreaker->isTradingAllowed());  // Trading still allowed in warning state
}

TEST_F(CircuitBreakerTest, TriggeredStateWhenMetricExceedsCriticalThreshold) {
    // Create metrics with drawdown at critical level
    CircuitBreaker::MetricMap metrics;
    metrics["drawdown"] = 0.12;  // Critical threshold is 0.10
    metrics["sharpe_ratio"] = 1.0;  // Above warning threshold
    
    // Setup callback to detect state change
    bool stateChangeTriggered = false;
    //  if (circuitBreaker_) {
    //     circuitBreaker_->registerStateChangeCallback([this](CircuitBreakerState oldState, 
    //                                                        CircuitBreakerState newState, 
    //                                                        std::string_view reason) {
    //         handleCircuitBreakerEvent(oldState, newState, reason);
    //     });
    // }
    circuitBreaker->registerStateChangeCallback(
        [&stateChangeTriggered](CircuitBreakerState oldState, 
                               CircuitBreakerState newState, 
                               std::string_view reason) {
            stateChangeTriggered = true;
        });
    
    // Evaluate metrics
    auto state = circuitBreaker->evaluateMetrics(metrics);
    
    // Should be in triggered state
    EXPECT_EQ(state, CircuitBreakerState::TRIGGERED);
    EXPECT_TRUE(stateChangeTriggered);
    EXPECT_FALSE(circuitBreaker->isTradingAllowed());  // Trading not allowed in triggered state
    
    // Get trigger history
    auto history = circuitBreaker->getTriggerHistory();
    EXPECT_EQ(history.size(), 1);
    if (!history.empty()) {
        EXPECT_EQ(history[0].oldState, CircuitBreakerState::OFF);
        EXPECT_EQ(history[0].newState, CircuitBreakerState::TRIGGERED);
        EXPECT_EQ(history[0].triggerMetric, "drawdown");
        EXPECT_DOUBLE_EQ(history[0].metricValue, 0.12);
        EXPECT_DOUBLE_EQ(history[0].metricThreshold, 0.10);
    }
}

TEST_F(CircuitBreakerTest, LowSharpeRatioTriggersCritical) {
    // Create metrics with sharpe ratio below critical threshold
    CircuitBreaker::MetricMap metrics;
    metrics["drawdown"] = 0.03;  // Below warning threshold
    metrics["sharpe_ratio"] = -0.2;  // Below critical threshold (0.0)
    
    // Evaluate metrics
    auto state = circuitBreaker->evaluateMetrics(metrics);
    
    // Should be in triggered state
    EXPECT_EQ(state, CircuitBreakerState::TRIGGERED);
    EXPECT_FALSE(circuitBreaker->isTradingAllowed());
}

TEST_F(CircuitBreakerTest, ManualTrigger) {
    // Trigger manually
    bool result = circuitBreaker->manualTrigger("Testing manual trigger");
    EXPECT_TRUE(result);
    
    // Should be in triggered state
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
    EXPECT_FALSE(circuitBreaker->isTradingAllowed());
    
    // Get trigger history
    auto history = circuitBreaker->getTriggerHistory();
    EXPECT_EQ(history.size(), 1);
    if (!history.empty()) {
        EXPECT_EQ(history[0].oldState, CircuitBreakerState::OFF);
        EXPECT_EQ(history[0].newState, CircuitBreakerState::TRIGGERED);
        EXPECT_EQ(history[0].triggerMetric, "MANUAL");
    }
}
TEST_F(CircuitBreakerTest, ResetAfterHaltPeriod) {
    // Modify config for shorter halt period (for testing)
    // Modify config for shorter halt period (for testing)
    cbConfig.mandatoryHaltPeriodSeconds = 1;
    circuitBreaker->updateConfiguration(cbConfig);
    // Trigger circuit breaker
    circuitBreaker->manualTrigger("Testing reset");
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
    
    // Wait for mandatory halt period
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Reset
    bool result = circuitBreaker->reset();
    EXPECT_TRUE(result);
    
    // Should be in cooling state
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::COOLING);
    
    // There's no public API to query remaining seconds until reset; rely on state checks and timing instead.
    // Ensure we are still in COOLING state before waiting for completion.
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::COOLING);
    
    // Wait for cooling period
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Reset again
    result = circuitBreaker->reset();
    EXPECT_TRUE(result);
    
    // Should be back to OFF state
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::OFF);
    EXPECT_TRUE(circuitBreaker->isTradingAllowed());
}
TEST_F(CircuitBreakerTest, AutoResetAfterCoolingPeriod) {
    // Modify config for shorter periods (for testing)
    cbConfig.mandatoryHaltPeriodSeconds = 1;
    cbConfig.coolingPeriodSeconds = 1;
    // Modify config for shorter periods (for testing)
    cbConfig.mandatoryHaltPeriodSeconds = 1;
    cbConfig.coolingPeriodSeconds = 1;
    circuitBreaker->updateConfiguration(cbConfig);
    // Trigger circuit breaker so the auto-reset path is exercised
    circuitBreaker->manualTrigger("Testing auto reset");
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
    
    // Wait for circuit breaker to auto-reset
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Evaluate empty metrics to trigger state check
    CircuitBreaker::MetricMap metrics;
    auto state = circuitBreaker->evaluateMetrics(metrics);
    
    // Should be in cooling state
    EXPECT_EQ(state, CircuitBreakerState::COOLING);
    
    // Wait for cooling period
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Evaluate empty metrics again
    state = circuitBreaker->evaluateMetrics(metrics);
    
    // Should be back to OFF state
    EXPECT_EQ(state, CircuitBreakerState::OFF);
    EXPECT_TRUE(circuitBreaker->isTradingAllowed());
}

TEST_F(CircuitBreakerTest, UpdateMetric) {
    // Update single metric
    auto state = circuitBreaker->updateMetric("drawdown", 0.12);
    
    // Should be triggered
    EXPECT_EQ(state, CircuitBreakerState::TRIGGERED);
    EXPECT_FALSE(circuitBreaker->isTradingAllowed());
}
TEST_F(CircuitBreakerTest, ExtendedHaltAfterMultipleTriggers) {
    // Modify config for testing
    cbConfig.mandatoryHaltPeriodSeconds = 1;
    cbConfig.maxTriggersBeforeExtendedHalt = 1;
    cbConfig.extendedHaltMultiplier = 2;
    circuitBreaker->updateConfiguration(cbConfig);
    // Modify config for testing
    cbConfig.mandatoryHaltPeriodSeconds = 1;
    cbConfig.maxTriggersBeforeExtendedHalt = 1;
    cbConfig.extendedHaltMultiplier = 2;
    circuitBreaker->updateConfiguration(cbConfig);
    // Wait for halt period
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Reset
    circuitBreaker->reset();
    
    // Should be in OFF state now
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::OFF);
    
    // Second trigger (should have extended halt period)
    circuitBreaker->manualTrigger("Second trigger");
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
    
    // Wait for normal halt period (not enough for extended)
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Try to reset
    bool result = circuitBreaker->reset();
    EXPECT_FALSE(result);  // Should not reset yet
    
    // Should still be in TRIGGERED state
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
    
    // Wait for extended halt period
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Now should be able to reset
    result = circuitBreaker->reset();
    EXPECT_TRUE(result);
}

} // namespace test
} // namespace risk