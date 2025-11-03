#include "risk/posttrade/drawdown_monitor.hpp"
#include "risk/risk_types.hpp"
#include "risk/risk_events.hpp"
#include "risk/risk_config.hpp"
#include "strategy/types/performance_metrics.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <chrono>

namespace trading::risk {
namespace test {

// Test fixture
class DrawdownMonitorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup default config
        DrawdownConfig drawdownConfig;
        drawdownConfig.warningThreshold = 0.05;
        drawdownConfig.criticalThreshold = 0.10;
        drawdownConfig.lookbackPeriod = std::chrono::hours(1);
        drawdownConfig.enableAutoRiskReduction = true;
        drawdownConfig.riskReductionFactor = 0.5;
        config.setDrawdownConfig(drawdownConfig);
        
        // Create test instance
        drawdownMonitor = std::make_unique<DrawdownMonitor>(config);
        drawdownMonitor->initialize();
    }
    
    RiskConfig config;
    std::unique_ptr<DrawdownMonitor> drawdownMonitor;
};

// Tests
TEST_F(DrawdownMonitorTest, InitialDrawdownIsZero) {
    const auto& portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.drawdownPercent.load(), 0.0);
    EXPECT_FALSE(portfolioDrawdown.warningThresholdBreached.load());
    EXPECT_FALSE(portfolioDrawdown.criticalThresholdBreached.load());
    
    const auto* strategyDrawdown = drawdownMonitor->getStrategyDrawdown("test_strategy");
    if (strategyDrawdown) {
        EXPECT_DOUBLE_EQ(strategyDrawdown->drawdownPercent.load(), 0.0);
        EXPECT_FALSE(strategyDrawdown->warningThresholdBreached.load());
        EXPECT_FALSE(strategyDrawdown->criticalThresholdBreached.load());
    } else {
        // Strategy doesn't exist yet, which is expected
        EXPECT_TRUE(true);
    }
}

TEST_F(DrawdownMonitorTest, HighWaterMarkTracking) {
    // Update portfolio value (initial)
    drawdownMonitor->updatePortfolioValue(100.0);
    
    // High water mark should be set to initial value
    const auto& portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.highWaterMark.value.load(), 100.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue.load(), 100.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.drawdownPercent.load(), 0.0);
    
    // Update to higher value
    drawdownMonitor->updatePortfolioValue(110.0);
    
    // High water mark should update
    const auto& portfolioDrawdown2 = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown2.highWaterMark.value.load(), 110.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown2.currentValue.load(), 110.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown2.drawdownPercent.load(), 0.0);
    
    // Update to lower value
    drawdownMonitor->updatePortfolioValue(100.0);
    
    // High water mark should not change, drawdown should be calculated
    const auto& portfolioDrawdown3 = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown3.highWaterMark.value.load(), 110.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown3.currentValue.load(), 100.0);
    EXPECT_NEAR(portfolioDrawdown3.drawdownPercent.load(), 0.0909, 0.001); // (110-100)/110 approx 0.0909
}

TEST_F(DrawdownMonitorTest, WarningThresholdBreach) {
    // Setup callback to detect events
    bool eventTriggered = false;
    drawdownMonitor->registerDrawdownCallback([&](const DrawdownEvent& event) {
        eventTriggered = true;
        EXPECT_EQ(event.strategyId.toString(), "PORTFOLIO:1.0");
        EXPECT_FALSE(event.isCritical());
        EXPECT_DOUBLE_EQ(event.highWatermark, 100.0);
        EXPECT_DOUBLE_EQ(event.currentValue, 94.0);
        EXPECT_NEAR(event.drawdownPercentage, 0.06, 0.001);
    });
    
    // Set high water mark
    drawdownMonitor->updatePortfolioValue(100.0);
    
    // Drop to warning level (6% drawdown, threshold is 5%)
    auto event = drawdownMonitor->updatePortfolioValue(94.0);
    
    // Event should be returned and callback triggered
    EXPECT_TRUE(event.has_value());
    EXPECT_TRUE(eventTriggered);
    
    // Check drawdown state
    const auto& portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.highWaterMark.value.load(), 100.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue.load(), 94.0);
    EXPECT_NEAR(portfolioDrawdown.drawdownPercent.load(), 0.06, 0.001);
    EXPECT_TRUE(portfolioDrawdown.warningThresholdBreached.load());
    EXPECT_FALSE(portfolioDrawdown.criticalThresholdBreached.load());
}

TEST_F(DrawdownMonitorTest, CriticalThresholdBreach) {
    // Setup callback to detect events
    bool eventTriggered = false;
    drawdownMonitor->registerDrawdownCallback([&](const DrawdownEvent& event) {
        eventTriggered = true;
        EXPECT_EQ(event.strategyId.toString(), "PORTFOLIO:1.0");
        EXPECT_TRUE(event.isCritical());
        EXPECT_DOUBLE_EQ(event.highWatermark, 100.0);
        EXPECT_DOUBLE_EQ(event.currentValue, 89.0);
        EXPECT_NEAR(event.drawdownPercentage, 0.11, 0.001);
    });
    
    // Set high water mark
    drawdownMonitor->updatePortfolioValue(100.0);
    
    // Drop to critical level (11% drawdown, threshold is 10%)
    auto event = drawdownMonitor->updatePortfolioValue(89.0);
    
    // Event should be returned and callback triggered
    EXPECT_TRUE(event.has_value());
    EXPECT_TRUE(eventTriggered);
    
    // Check drawdown state
    const auto& portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.highWaterMark.value.load(), 100.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue.load(), 89.0);
    EXPECT_NEAR(portfolioDrawdown.drawdownPercent.load(), 0.11, 0.001);
    EXPECT_TRUE(portfolioDrawdown.warningThresholdBreached.load());
    EXPECT_TRUE(portfolioDrawdown.criticalThresholdBreached.load());
}

TEST_F(DrawdownMonitorTest, StrategyDrawdownTracking) {
    // Update strategy value (initial)
    drawdownMonitor->updateStrategyValue("strategy1", 100.0);
    
    // Check initial state
    const auto* strategyDrawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    ASSERT_NE(strategyDrawdown, nullptr);
    EXPECT_DOUBLE_EQ(strategyDrawdown->highWaterMark.value.load(), 100.0);
    EXPECT_DOUBLE_EQ(strategyDrawdown->currentValue.load(), 100.0);
    EXPECT_DOUBLE_EQ(strategyDrawdown->drawdownPercent.load(), 0.0);
    
    // Update to trigger warning (8% drawdown, threshold is 7%)
    drawdownMonitor->updateStrategyValue("strategy1", 92.0);
    
    // Check warning state
    strategyDrawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    ASSERT_NE(strategyDrawdown, nullptr);
    EXPECT_DOUBLE_EQ(strategyDrawdown->highWaterMark.value.load(), 100.0);
    EXPECT_DOUBLE_EQ(strategyDrawdown->currentValue.load(), 92.0);
    EXPECT_NEAR(strategyDrawdown->drawdownPercent.load(), 0.08, 0.001);
    EXPECT_TRUE(strategyDrawdown->warningThresholdBreached.load());
    EXPECT_FALSE(strategyDrawdown->criticalThresholdBreached.load());
    
    // Update to trigger critical (16% drawdown, threshold is 15%)
    drawdownMonitor->updateStrategyValue("strategy1", 84.0);
    
    // Check critical state
    strategyDrawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    ASSERT_NE(strategyDrawdown, nullptr);
    EXPECT_DOUBLE_EQ(strategyDrawdown->highWaterMark.value.load(), 100.0);
    EXPECT_DOUBLE_EQ(strategyDrawdown->currentValue.load(), 84.0);
    EXPECT_NEAR(strategyDrawdown->drawdownPercent.load(), 0.16, 0.001);
    EXPECT_TRUE(strategyDrawdown->warningThresholdBreached.load());
    EXPECT_TRUE(strategyDrawdown->criticalThresholdBreached.load());
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
    EXPECT_TRUE(drawdownMonitor->resetStrategyHighWaterMark("strategy1"));
    
    // Check that drawdowns are reset
    const auto& portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.highWaterMark.value.load(), 90.0);
    EXPECT_DOUBLE_EQ(portfolioDrawdown.drawdownPercent.load(), 0.0);
    EXPECT_FALSE(portfolioDrawdown.warningThresholdBreached.load());
    EXPECT_FALSE(portfolioDrawdown.criticalThresholdBreached.load());
    
    const auto* strategyDrawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    ASSERT_NE(strategyDrawdown, nullptr);
    EXPECT_DOUBLE_EQ(strategyDrawdown->highWaterMark.value.load(), 85.0);
    EXPECT_DOUBLE_EQ(strategyDrawdown->drawdownPercent.load(), 0.0);
    EXPECT_FALSE(strategyDrawdown->warningThresholdBreached.load());
    EXPECT_FALSE(strategyDrawdown->criticalThresholdBreached.load());
}

TEST_F(DrawdownMonitorTest, MultipleStrategyTracking) {
    // Update multiple strategies
    drawdownMonitor->updateStrategyValue("strategy1", 100.0);
    drawdownMonitor->updateStrategyValue("strategy2", 200.0);
    drawdownMonitor->updatePortfolioValue(300.0);
    
    // Check initial state
    const auto& portfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(portfolioDrawdown.currentValue.load(), 300.0);
    
    const auto* strategy1Drawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    ASSERT_NE(strategy1Drawdown, nullptr);
    EXPECT_DOUBLE_EQ(strategy1Drawdown->currentValue.load(), 100.0);
    
    const auto* strategy2Drawdown = drawdownMonitor->getStrategyDrawdown("strategy2");
    ASSERT_NE(strategy2Drawdown, nullptr);
    EXPECT_DOUBLE_EQ(strategy2Drawdown->currentValue.load(), 200.0);
    
    // Update with different drawdowns
    drawdownMonitor->updatePortfolioValue(270.0);        // 10% drawdown (critical)
    drawdownMonitor->updateStrategyValue("strategy1", 92.0);  // 8% drawdown (warning)
    drawdownMonitor->updateStrategyValue("strategy2", 210.0); // No drawdown (new high)
    
    // Check updated state
    const auto& updatedPortfolioDrawdown = drawdownMonitor->getPortfolioDrawdown();
    EXPECT_DOUBLE_EQ(updatedPortfolioDrawdown.currentValue.load(), 270.0);
    EXPECT_NEAR(updatedPortfolioDrawdown.drawdownPercent.load(), 0.10, 0.001);
    EXPECT_TRUE(updatedPortfolioDrawdown.criticalThresholdBreached.load());
    
    strategy1Drawdown = drawdownMonitor->getStrategyDrawdown("strategy1");
    ASSERT_NE(strategy1Drawdown, nullptr);
    EXPECT_DOUBLE_EQ(strategy1Drawdown->currentValue.load(), 92.0);
    EXPECT_NEAR(strategy1Drawdown->drawdownPercent.load(), 0.08, 0.001);
    EXPECT_TRUE(strategy1Drawdown->warningThresholdBreached.load());
    
    strategy2Drawdown = drawdownMonitor->getStrategyDrawdown("strategy2");
    ASSERT_NE(strategy2Drawdown, nullptr);
    EXPECT_DOUBLE_EQ(strategy2Drawdown->currentValue.load(), 210.0);
    EXPECT_DOUBLE_EQ(strategy2Drawdown->highWaterMark.value.load(), 210.0);
    EXPECT_DOUBLE_EQ(strategy2Drawdown->drawdownPercent.load(), 0.0);
    EXPECT_FALSE(strategy2Drawdown->warningThresholdBreached.load());
}

} // namespace test
} // namespace trading::risk