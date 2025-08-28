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
        config.circuitBreakerConfig.mandatoryHaltPeriodSeconds = 60;
        config.circuitBreakerConfig.coolingPeriodSeconds = 120;
        config.circuitBreakerConfig.maxTriggersBeforeExtendedHalt = 3;
        config.circuitBreakerConfig.extendedHaltMultiplier = 2;
        config.circuitBreakerConfig.triggerCountResetMinutes = 30;
        config.circuitBreakerConfig.gradualTradingResumption = true;
        config.circuitBreakerConfig.maxHistorySize = 100;
        
        // Setup metric thresholds
        MetricThreshold drawdownThreshold;
        drawdownThreshold.warningThreshold = 0.05;
        drawdownThreshold.criticalThreshold = 0.10;
        drawdownThreshold.isHigherBetter = false;
        
        MetricThreshold sharpeThreshold;
        sharpeThreshold.warningThreshold = 0.5;
        sharpeThreshold.criticalThreshold = 0.0;
        sharpeThreshold.isHigherBetter = true;
        
        config.circuitBreakerConfig.metricThresholds["drawdown"] = drawdownThreshold;
        config.circuitBreakerConfig.metricThresholds["sharpe_ratio"] = sharpeThreshold;
        
        // Create test instance
        circuitBreaker = std::make_unique<CircuitBreaker>(config);
        circuitBreaker->initialize();
    }
    
    RiskConfig config;
    std::unique_ptr<CircuitBreaker> circuitBreaker;
};

// Tests
TEST_F(CircuitBreakerTest, InitialStateIsOff) {
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::OFF);
    EXPECT_TRUE(circuitBreaker->isTradingAllowed());
}

TEST_F(CircuitBreakerTest, WarningStateWhenMetricExceedsWarningThreshold) {
    // Create metrics with drawdown at warning level
    std::map<std::string, double> metrics;
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
    std::map<std::string, double> metrics;
    metrics["drawdown"] = 0.12;  // Critical threshold is 0.10
    metrics["sharpe_ratio"] = 1.0;  // Above warning threshold
    
    // Setup callback to detect state change
    bool stateChangeTriggered = false;
    circuitBreaker->registerStateChangeCallback(
        [&](CircuitBreakerState oldState, CircuitBreakerState newState, const std::string& reason) {
            stateChangeTriggered = true;
            EXPECT_EQ(oldState, CircuitBreakerState::OFF);
            EXPECT_EQ(newState, CircuitBreakerState::TRIGGERED);
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
    std::map<std::string, double> metrics;
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

TEST_F(CircuitBreakerTest, ResetFromTriggeredState) {
    // Modify config for shorter halt period (for testing)
    config.circuitBreakerConfig.mandatoryHaltPeriodSeconds = 1;
    circuitBreaker->updateConfiguration(config.circuitBreakerConfig);
    
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
    
    // Seconds until reset should be less than or equal to cooling period
    int secondsUntilReset = circuitBreaker->getSecondsUntilReset();
    EXPECT_LE(secondsUntilReset, config.circuitBreakerConfig.coolingPeriodSeconds);
    
    // Wait for cooling period
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Reset again
    result = circuitBreaker->reset();
    EXPECT_TRUE(result);
    
    // Should be back to OFF state
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::OFF);
    EXPECT_TRUE(circuitBreaker->isTradingAllowed());
}

TEST_F(CircuitBreakerTest, AutoResetAfterMandatoryHalt) {
    // Modify config for shorter periods (for testing)
    config.circuitBreakerConfig.mandatoryHaltPeriodSeconds = 1;
    config.circuitBreakerConfig.coolingPeriodSeconds = 1;
    circuitBreaker->updateConfiguration(config.circuitBreakerConfig);
    
    // Trigger circuit breaker
    circuitBreaker->manualTrigger("Testing auto reset");
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
    
    // Wait for circuit breaker to auto-reset
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Evaluate empty metrics to trigger state check
    std::map<std::string, double> metrics;
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

TEST_F(CircuitBreakerTest, MultipleTriggersExtendHaltPeriod) {
    // Modify config for testing
    config.circuitBreakerConfig.mandatoryHaltPeriodSeconds = 1;
    config.circuitBreakerConfig.maxTriggersBeforeExtendedHalt = 1;
    config.circuitBreakerConfig.extendedHaltMultiplier = 2;
    circuitBreaker->updateConfiguration(config.circuitBreakerConfig);
    
    // First trigger
    circuitBreaker->manualTrigger("First trigger");
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
    
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