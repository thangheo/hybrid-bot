#include "risk/posttrade/circuit_breaker.hpp"
#include "risk/risk_types.hpp"
#include "risk/risk_events.hpp"
#include "risk/risk_config.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <chrono>
#include <thread>
#include <map>

namespace trading::risk::test {

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
        config.setCircuitBreakerConfig(cbConfig);
        circuitBreaker = std::make_unique<CircuitBreaker>(config);
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
    cbConfig.mandatoryHaltPeriodSeconds = 1;
    cbConfig.coolingPeriodSeconds = 1;
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
    
    // Wait for cooling period to complete
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
    circuitBreaker->updateConfiguration(cbConfig);
    
    // Trigger circuit breaker so the auto-reset path is exercised
    circuitBreaker->manualTrigger("Testing auto reset");
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
    
    // Wait for circuit breaker to auto-reset to cooling state
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Evaluate empty metrics to trigger state check
    CircuitBreaker::MetricMap metrics;
    auto state = circuitBreaker->evaluateMetrics(metrics);
    
    // Should be in cooling state
    EXPECT_EQ(state, CircuitBreakerState::COOLING);
    
    // Wait for cooling period to complete
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
    
    // First trigger
    circuitBreaker->manualTrigger("First trigger");
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
    
    // Wait for halt period
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Reset
    circuitBreaker->reset();
    
    // Wait for cooling to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    
    // Reset to OFF
    circuitBreaker->reset();
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
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::COOLING);
}

TEST_F(CircuitBreakerTest, MetricUpdateDuringWarningState) {
    // Put circuit breaker in warning state
    CircuitBreaker::MetricMap metrics;
    metrics["drawdown"] = 0.06;  // Above warning but below critical
    metrics["sharpe_ratio"] = 1.0;
    
    auto state = circuitBreaker->evaluateMetrics(metrics);
    EXPECT_EQ(state, CircuitBreakerState::WARNING);
    
    // Now update to all metrics below warning
    metrics["drawdown"] = 0.03;  // Below warning
    metrics["sharpe_ratio"] = 1.5;  // Well above warning
    
    state = circuitBreaker->evaluateMetrics(metrics);
    
    // Should return to OFF state
    EXPECT_EQ(state, CircuitBreakerState::OFF);
}

TEST_F(CircuitBreakerTest, TriggerCountReset) {
    // Modify config for very short reset period
    cbConfig.triggerCountResetMinutes = 1;
    cbConfig.mandatoryHaltPeriodSeconds = 1;
    circuitBreaker->updateConfiguration(cbConfig);
    
    // Trigger multiple times
    circuitBreaker->manualTrigger("First trigger");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    circuitBreaker->reset();
    
    // Get history to confirm trigger count
    auto history = circuitBreaker->getTriggerHistory();
    EXPECT_GE(history.size(), 1);
    
    // Wait for trigger count reset time
    std::this_thread::sleep_for(std::chrono::seconds(61));
    
    // Trigger again - should be considered first trigger after reset
    circuitBreaker->manualTrigger("Trigger after reset");
    
    // Verify circuit breaker is still functional
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
}

TEST_F(CircuitBreakerTest, ConcurrentMetricUpdates) {
    // Test thread safety with concurrent updates
    std::vector<std::thread> threads;
    
    // Start multiple threads updating different metrics
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([this, i]() {
            for (int j = 0; j < 100; ++j) {
                std::string metricName = "metric_" + std::to_string(i);
                double value = 0.01 * j;  // Values from 0.0 to 0.99
                circuitBreaker->updateMetric(metricName, value);
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
        });
    }
    
    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }
    
    // Circuit breaker should still be in a valid state
    auto state = circuitBreaker->getState();
    EXPECT_TRUE(state == CircuitBreakerState::OFF || 
                state == CircuitBreakerState::WARNING || 
                state == CircuitBreakerState::TRIGGERED ||
                state == CircuitBreakerState::COOLING);
}

TEST_F(CircuitBreakerTest, HealthCheck) {
    // Test isHealthy() method
    EXPECT_TRUE(circuitBreaker->isHealthy());  // Initially OFF
    
    // Put in warning state
    CircuitBreaker::MetricMap metrics;
    metrics["drawdown"] = 0.06;
    circuitBreaker->evaluateMetrics(metrics);
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::WARNING);
    EXPECT_TRUE(circuitBreaker->isHealthy());  // WARNING is still healthy
    
    // Trigger circuit breaker
    metrics["drawdown"] = 0.12;
    circuitBreaker->evaluateMetrics(metrics);
    EXPECT_EQ(circuitBreaker->getState(), CircuitBreakerState::TRIGGERED);
    EXPECT_FALSE(circuitBreaker->isHealthy());  // TRIGGERED is not healthy
}

TEST_F(CircuitBreakerTest, DetailedStatusOutput) {
    // Add some metrics
    CircuitBreaker::MetricMap metrics;
    metrics["drawdown"] = 0.04;
    metrics["sharpe_ratio"] = 1.2;
    circuitBreaker->evaluateMetrics(metrics);
    
    // Get detailed status
    std::string status = circuitBreaker->getDetailedStatus();
    
    // Verify status contains expected information
    EXPECT_TRUE(status.find("CircuitBreaker Status:") != std::string::npos);
    EXPECT_TRUE(status.find("State:") != std::string::npos);
    EXPECT_TRUE(status.find("Trading Allowed:") != std::string::npos);
    EXPECT_TRUE(status.find("Current Metrics:") != std::string::npos);
    EXPECT_TRUE(status.find("drawdown") != std::string::npos);
    EXPECT_TRUE(status.find("sharpe_ratio") != std::string::npos);
}

} // namespace trading::risk::test
