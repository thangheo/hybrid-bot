#include "risk/posttrade/drawdown_monitor.hpp"
#include "risk/risk_types.hpp"
#include "risk/risk_events.hpp"
#include "risk/risk_config.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <chrono>

namespace risk {
namespace test {

// Test fixture
class DrawdownMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup default config
        config.drawdownConfig.portfolioWarningThreshold = 0.05;
        config.drawdownConfig.portfolioCriticalThreshold = 0.10;
        config.drawdownConfig.strategyWarningThreshold = 0.07;
        config.drawdownConfig.strategyCriticalThreshold = 0.15;
        config.drawdownConfig.lookbackPeriodMinutes = 60;
        
        // Create test instance
        drawdownMonitor = std::make_unique<DrawdownMonitor>(config);
        drawdownMonitor->initialize();
    }
    
    RiskConfig config;
    std::unique_ptr<DrawdownMonitor> drawdownMonitor;
};

// Tests
TEST_F(DrawdownMonitorTest, InitialDrawdownIsZero) {
    auto portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.drawdownPercent, 0.0);
    EXPECT_FALSE(portfolioDrawdown.warningThresholdBreached);
    EXPECT_FALSE(portfolioDrawdown.criticalThresholdBreached);
    
    auto strategyDrawdown = drawdownMonitor->getStrategyDrawdown("test_strategy");
    EXPECT_DOUBLE_EQ(strategyDrawdown.drawdownPercent, 0.0);
    EXPECT_FALSE(strategyDrawdown.warningThresholdBreached);
    EXPECT_FALSE(strategyDrawdown.criticalThresholdBreached);
}

TEST_F(DrawdownMonitorTest, HighWaterMarkTracking) {
    // Update portfolio value (initial)
    drawdownMonitor->updatePortfolioValue(100.0);
    
    // High water mark should be set to initial value
    auto portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.highWaterMark.value, 100.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue, 100.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.drawdownPercent, 0.0);
    
    // Update to higher value
    drawdownMonitor->updatePortfolioValue(110.0);
    
    // High water mark should update
    portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.highWaterMark.value, 110.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue, 110.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.drawdownPercent, 0.0);
    
    // Update to lower value
    drawdownMonitor->updatePortfolioValue(100.0);
    
    // High water mark should not change, drawdown should be calculated
    portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.highWaterMark.value, 110.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue, 100.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.drawdownPercent, 0.0909); // (110-100)/110 approx 0.0909
}

TEST_F(DrawdownMonitorTest, WarningThresholdBreach) {
    // Setup callback to detect events
    bool eventTriggered = false;
    drawdownMonitor->registerDrawdownCallback([&](const DrawdownEvent& event) {
        eventTriggered = true;
        EXPECT_EQ(event.strategyId, "PORTFOLIO");
        EXPECT_FALSE(event.isCritical);
        EXPECT_DOUBLE_EQ(event.highWaterMark, 100.0);
        EXPECT_DOUBLE_EQ(event.currentValue, 94.0);
        EXPECT_NEAR(event.drawdownPercent, 0.06, 0.001);
    });
    
    // Set high water mark
    drawdownMonitor->updatePortfolioValue(100.0);
    
    // Drop to warning level (6% drawdown, threshold is 5%)
    auto event = drawdownMonitor->updatePortfolioValue(94.0);
    
    // Event should be returned and callback triggered
    EXPECT_TRUE(event.has_value());
    EXPECT_TRUE(eventTriggered);
    
    // Check drawdown state
    auto portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.highWaterMark.value, 100.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue, 94.0);
    EXPECT_NEAR(portfolioDrawdown.drawdownPercent, 0.06, 0.001);
    EXPECT_TRUE(portfolioDrawdown.warningThresholdBreached);
    EXPECT_FALSE(portfolioDrawdown.criticalThresholdBreached);
}

TEST_F(DrawdownMonitorTest, CriticalThresholdBreach) {
    // Setup callback to detect events
    bool eventTriggered = false;
    drawdownMonitor->registerDrawdownCallback([&](const DrawdownEvent& event) {
        eventTriggered = true;
        EXPECT_EQ(event.strategyId, "PORTFOLIO");
        EXPECT_TRUE(event.isCritical);
        EXPECT_DOUBLE_EQ(event.highWaterMark, 100.0);
        EXPECT_DOUBLE_EQ(event.currentValue, 89.0);
        EXPECT_NEAR(event.drawdownPercent, 0.11, 0.001);
    });
    
    // Set high water mark
    drawdownMonitor->updatePortfolioValue(100.0);
    
    // Drop to critical level (11% drawdown, threshold is 10%)
    auto event = drawdownMonitor->updatePortfolioValue(89.0);
    
    // Event should be returned and callback triggered
    EXPECT_TRUE(event.has_value());
    EXPECT_TRUE(eventTriggered);
    
    // Check drawdown state
    auto portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.highWaterMark.value, 100.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue, 89.0);
    EXPECT_NEAR(portfolioDrawdown.drawdownPercent, 0.11, 0.001);
    EXPECT_TRUE(portfolioDrawdown.warningThresholdBreached);
    EXPECT_TRUE(portfolioDrawdown.criticalThresholdBreached);
}

TEST_F(DrawdownMonitorTest, StrategyDrawdownTracking) {
    // Update strategy value (initial)
    drawdownMonitor->updateStrategyValue("strategy1", 100.0);
    
    // Check initial state
    auto strategyDrawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    EXPECT_DOUBLE_EQ(strategyDrawdown.highWaterMark.value, 100.0);
    EXPECT_DOUBLE_EQ(strategyDrawdown.currentValue, 100.0);
    EXPECT_DOUBLE_EQ(strategyDrawdown.drawdownPercent, 0.0);
    
    // Update to trigger warning (8% drawdown, threshold is 7%)
    drawdownMonitor->updateStrategyValue("strategy1", 92.0);
    
    // Check warning state
    strategyDrawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    EXPECT_DOUBLE_EQ(strategyDrawdown.highWaterMark.value, 100.0);
    EXPECT_DOUBLE_EQ(strategyDrawdown.currentValue, 92.0);
    EXPECT_NEAR(strategyDrawdown.drawdownPercent, 0.08, 0.001);
    EXPECT_TRUE(strategyDrawdown.warningThresholdBreached);
    EXPECT_FALSE(strategyDrawdown.criticalThresholdBreached);
    
    // Update to trigger critical (16% drawdown, threshold is 15%)
    drawdownMonitor->updateStrategyValue("strategy1", 84.0);
    
    // Check critical state
    strategyDrawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    EXPECT_DOUBLE_EQ(strategyDrawdown.highWaterMark.value, 100.0);
    EXPECT_DOUBLE_EQ(strategyDrawdown.currentValue, 84.0);
    EXPECT_NEAR(strategyDrawdown.drawdownPercent, 0.16, 0.001);
    EXPECT_TRUE(strategyDrawdown.warningThresholdBreached);
    EXPECT_TRUE(strategyDrawdown.criticalThresholdBreached);
}

TEST_F(DrawdownMonitorTest, ResetHighWaterMark) {
    // Set initial values
    drawdownMonitor->updatePortfolioValue(100.0);
    drawdownMonitor->updateStrategyValue("strategy1", 100.0);
    
    // Create some drawdown
    drawdownMonitor->updatePortfolioValue(90.0);
    drawdownMonitor->updateStrategyValue("strategy1", 85.0);
    
    // Reset high water marks
    EXPECT_TRUE(drawdownMonitor->resetPortfolioHighWaterMark());
    EXPECT_TRUE(drawdownMonitor->resetHighWaterMark("strategy1"));
    
    // Check that drawdowns are reset
    auto portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.highWaterMark.value, 90.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.drawdownPercent, 0.0);
    EXPECT_FALSE(portfolioDrawdown.warningThresholdBreached);
    EXPECT_FALSE(portfolioDrawdown.criticalThresholdBreached);
    
    auto strategyDrawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    EXPECT_DOUBLE_EQ(strategyDrawdown.highWaterMark.value, 85.0);
    EXPECT_DOUBLE_EQ(strategyDrawdown.drawdownPercent, 0.0);
    EXPECT_FALSE(strategyDrawdown.warningThresholdBreached);
    EXPECT_FALSE(strategyDrawdown.criticalThresholdBreached);
}

TEST_F(DrawdownMonitorTest, UpdatePerformanceMetrics) {
    // Create performance metrics
    PerformanceMetrics metrics;
    metrics.portfolioValue = 100.0;
    metrics.strategyMetrics["strategy1"].value = 50.0;
    metrics.strategyMetrics["strategy2"].value = 50.0;
    
    // Update with metrics
    auto events = drawdownMonitor->updatePerformanceMetrics(metrics);
    
    // Should be no events yet
    EXPECT_TRUE(events.empty());
    
    // Check initial state
    auto portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue, 100.0);
    
    auto strategy1Drawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    EXPECT_DOUBLE_EQ(strategy1Drawdown.currentValue, 50.0);
    
    auto strategy2Drawdown = drawdownMonitor->getStrategyDrawdown("strategy2");
    EXPECT_DOUBLE_EQ(strategy2Drawdown.currentValue, 50.0);
    
    // Update with drawdown in portfolio and strategy1
    metrics.portfolioValue = 89.0;          // 11% drawdown (critical)
    metrics.strategyMetrics["strategy1"].value = 46.0;  // 8% drawdown (warning)
    metrics.strategyMetrics["strategy2"].value = 51.0;  // No drawdown (new high)
    
    events = drawdownMonitor->updatePerformanceMetrics(metrics);
    
    // Should have 2 events (portfolio critical and strategy1 warning)
    EXPECT_EQ(events.size(), 2);
    
    // Check updated state
    portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue, 89.0);
    EXPECT_NEAR(portfolioDrawdown.drawdownPercent, 0.11, 0.001);
    EXPECT_TRUE(portfolioDrawdown.criticalThresholdBreached);
    
    strategy1Drawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    EXPECT_DOUBLE_EQ(strategy1Drawdown.currentValue, 46.0);
    EXPECT_NEAR(strategy1Drawdown.drawdownPercent, 0.08, 0.001);
    EXPECT_TRUE(strategy1Drawdown.warningThresholdBreached);
    
    strategy2Drawdown = drawdownMonitor->getStrategyDrawdown("strategy2");
    EXPECT_DOUBLE_EQ(strategy2Drawdown.currentValue, 51.0);
    EXPECT_DOUBLE_EQ(strategy2Drawdown.highWaterMark.value, 51.0);
    EXPECT_DOUBLE_EQ(strategy2Drawdown.drawdownPercent, 0.0);
    EXPECT_FALSE(strategy2Drawdown.warningThresholdBreached);
}

} // namespace test
} // namespace risk