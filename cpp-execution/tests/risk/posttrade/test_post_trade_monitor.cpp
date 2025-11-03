#include "risk/posttrade/post_trade_monitor.hpp"
#include "risk/risk_types.hpp"
#include "risk/risk_events.hpp"
#include "risk/risk_config.hpp"

#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>
#include <atomic>

using namespace trading::risk;
using namespace trading::strategy;
using namespace trading::core;

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
        initResult = postTradeMonitor->initialize();
        
        // Setup event tracking
        riskEventReceived = false;
        lastRiskEvent = std::nullopt;
        
        // Register callback to capture risk events
        postTradeMonitor->registerRiskEventCallback([this](const RiskEvent& event) {
            riskEventReceived = true;
            lastRiskEvent = event;
        });
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
        
        // Configure drawdown monitoring
        DrawdownConfig drawdownConfig;
        drawdownConfig.warningThreshold = 0.05;   // 5% warning
        drawdownConfig.criticalThreshold = 0.10;  // 10% critical
        drawdownConfig.lookbackPeriod = std::chrono::hours(24);
        drawdownConfig.enableAutoRiskReduction = true;
        drawdownConfig.riskReductionFactor = 0.5;
        
        config.setDrawdownConfig(drawdownConfig);
        
        // Configure circuit breaker with proper metric thresholds
        CircuitBreakerConfig cbConfig;
        
        // Add metric thresholds using the correct API
        MetricThreshold errorThreshold(0.05, 0.10, false); // 5% warning, 10% critical, lower is better
        MetricThreshold sharpeThreshold(1.5, 1.0, true); // warning above 1.5, critical above 1.0, higher is better
        cbConfig.metricThresholds["error_rate"] = errorThreshold;
        cbConfig.metricThresholds["sharpe_ratio"] = sharpeThreshold;
        
        cbConfig.coolingPeriod = std::chrono::seconds(300);
        cbConfig.autoResume = true;
        
        config.setCircuitBreakerConfig(cbConfig);
        
        // Configure stop loss
        StopLossConfig slConfig;
        slConfig.maxLossPercentage = 0.05;
        slConfig.enableTrailingStop = true;
        slConfig.trailingStopDistance = 0.02;
        
        config.setStopLossConfig(slConfig);
        
        return config;
    }

    // Test data
    RiskConfig config;
    std::unique_ptr<PostTradeMonitor> postTradeMonitor;
    bool initResult;
    
    // Event tracking
    std::atomic<bool> riskEventReceived;
    std::optional<RiskEvent> lastRiskEvent;
};

// =============================================================================
// BASIC FUNCTIONALITY TESTS
// =============================================================================

TEST_F(PostTradeMonitorIntegrationTest, InitializationWithValidConfig) {
    // Verify initialization succeeded
    EXPECT_TRUE(initResult) << "PostTradeMonitor should initialize successfully with valid config";
    
    // Verify initial state
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::NORMAL);
}

TEST_F(PostTradeMonitorIntegrationTest, GetComponentReferences) {
    ASSERT_TRUE(initResult);
    
    // Test that components are accessible and return valid references
    // These should not throw exceptions and should return valid component references
    EXPECT_NO_THROW(postTradeMonitor->getCircuitBreaker());
    EXPECT_NO_THROW(postTradeMonitor->getStopLossManager()); 
    EXPECT_NO_THROW(postTradeMonitor->getDrawdownMonitor());
    EXPECT_NO_THROW(postTradeMonitor->getBalanceReconciliation());
    
    // Verify circuit breaker initial state
    EXPECT_EQ(postTradeMonitor->getCircuitBreakerState(), CircuitBreakerState::OFF);
}

TEST_F(PostTradeMonitorIntegrationTest, ProcessExecutionReport) {
    ASSERT_TRUE(initResult);
    
    // Create a valid execution report using the correct API
    ExecutionReport report;
    report.setExchangeId(ExchangeId::fromString("binance"));
    report.setSymbol(Symbol::fromString("BTC/USDT"));
    report.setOrderId("test_order_123");
    report.setQuantity(1.0);
    report.setFilledQuantity(1.0);
    report.setStatus(OrderState::FILLED);
    // Note: Don't call setTimestamp as it doesn't exist - timestamps are set internally
    
    // Process the execution report - should not throw
    EXPECT_NO_THROW(postTradeMonitor->processExecutionReport(report));
    
    // Verify system is still in normal mode
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::NORMAL);
}

TEST_F(PostTradeMonitorIntegrationTest, UpdateMarketData) {
    ASSERT_TRUE(initResult);
    
    // Create market data using direct field assignment (MarketData is a struct)
    MarketData marketData;
    marketData.exchangeId = ExchangeId::fromString("binance");
    marketData.tradingPair = Symbol::fromString("BTC/USDT");
    marketData.timestamp = std::chrono::system_clock::now();
    marketData.price = 50000.0;
    marketData.bid = 49900.0;
    marketData.ask = 50100.0;
    marketData.volume = 1000.0;
    
    // Update market data - should not throw
    EXPECT_NO_THROW(postTradeMonitor->updateMarketData(marketData));
    
    // Verify system state remains stable
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::NORMAL);
}

TEST_F(PostTradeMonitorIntegrationTest, UpdatePerformanceMetrics) {
    ASSERT_TRUE(initResult);
    
    // Create valid performance metrics
    PerformanceMetrics metrics;
    metrics.pnl = 1000.0;
    metrics.sharpeRatio = 2.0;
    metrics.maxDrawdown = 0.03; // 3%
    metrics.winRate = 0.65;
    metrics.profitFactor = 1.8;
    metrics.timestamp = std::chrono::system_clock::now();
    
    // Update performance metrics - should not throw
    EXPECT_NO_THROW(postTradeMonitor->updatePerformanceMetrics(metrics));
    
    // Verify system handles normal metrics well
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::NORMAL);
}

// =============================================================================
// CONFIGURATION TESTS 
// =============================================================================

TEST_F(PostTradeMonitorIntegrationTest, UpdateConfiguration) {
    ASSERT_TRUE(initResult);
    
    // Create new valid configuration
    RiskConfig newConfig = createValidRiskConfig();
    
    // Update configuration - should succeed
    bool updateResult = postTradeMonitor->updateConfiguration(newConfig);
    EXPECT_TRUE(updateResult) << "Configuration update should succeed with valid config";
}

TEST_F(PostTradeMonitorIntegrationTest, UpdateConfigurationWithInvalidData) {
    ASSERT_TRUE(initResult);
    
    // Create invalid configuration (negative drawdown thresholds)
    RiskConfig invalidConfig;
    DrawdownConfig drawdownConfig;
    
    // Invalid: negative thresholds
    drawdownConfig.warningThreshold = -0.05;  // Negative warning (invalid!)
    drawdownConfig.criticalThreshold = -0.10; // Negative critical (invalid!)
    drawdownConfig.lookbackPeriod = std::chrono::hours(-1); // Negative lookback (invalid!)
    
    invalidConfig.setDrawdownConfig(drawdownConfig);
    
    // Update should fail with invalid config
    bool updateResult = postTradeMonitor->updateConfiguration(invalidConfig);
    EXPECT_FALSE(updateResult) << "Configuration update should fail with invalid thresholds";
}

// =============================================================================
// RECONCILIATION TESTS
// =============================================================================

TEST_F(PostTradeMonitorIntegrationTest, ReconcilePositions) {
    ASSERT_TRUE(initResult);
    
    // Test general position reconciliation
    auto futureResult = postTradeMonitor->reconcilePositions();
    ASSERT_TRUE(futureResult.valid()) << "Reconciliation should return a valid future";
    
    // Wait for result (with timeout to avoid hanging)
    auto status = futureResult.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready) << "Reconciliation should complete within 5 seconds";
    
    auto result = futureResult.get();
    // Result should be valid (exact values depend on internal state)
    EXPECT_GE(result.mismatchedPositions, 0) << "Mismatched positions should be non-negative";
}

TEST_F(PostTradeMonitorIntegrationTest, ReconcileExchangePositions) {
    ASSERT_TRUE(initResult);
    
    // Test exchange-specific reconciliation
    ExchangeId binance = ExchangeId::fromString("binance");
    auto futureResult = postTradeMonitor->reconcilePositions(binance);
    ASSERT_TRUE(futureResult.valid()) << "Exchange reconciliation should return a valid future";
    
    // Wait for result
    auto status = futureResult.wait_for(std::chrono::seconds(5));
    ASSERT_EQ(status, std::future_status::ready) << "Exchange reconciliation should complete within 5 seconds";
    
    auto result = futureResult.get();
    EXPECT_GE(result.mismatchedPositions, 0) << "Exchange mismatched positions should be non-negative";
}

// =============================================================================
// SYSTEM MODE TESTS
// =============================================================================

TEST_F(PostTradeMonitorIntegrationTest, RiskModeTransitions) {
    ASSERT_TRUE(initResult);
    
    // Initial state should be NORMAL
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::NORMAL);
    
    // Test mode change to heightened risk
    postTradeMonitor->setMode(RiskManagerMode::HEIGHTENED_RISK);
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::HEIGHTENED_RISK);
    
    // Test mode change to emergency
    postTradeMonitor->setMode(RiskManagerMode::EMERGENCY);
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::EMERGENCY);
    
    // Test mode change to maintenance
    postTradeMonitor->setMode(RiskManagerMode::MAINTENANCE);
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::MAINTENANCE);
    
    // Reset to normal
    postTradeMonitor->setMode(RiskManagerMode::NORMAL);
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::NORMAL);
}

// =============================================================================
// PERFORMANCE AND STRESS TESTS
// =============================================================================

TEST_F(PostTradeMonitorIntegrationTest, HighFrequencyUpdates) {
    ASSERT_TRUE(initResult);
    
    // Test system stability under high frequency updates
    const int numUpdates = 100;
    
    for (int i = 0; i < numUpdates; ++i) {
        // Create market data update using direct field assignment
        MarketData marketData;
        marketData.exchangeId = ExchangeId::fromString("binance");
        marketData.tradingPair = Symbol::fromString("BTC/USDT");
        marketData.timestamp = std::chrono::system_clock::now();
        marketData.price = 50000.0 + i;
        marketData.bid = 50000.0 + i - 0.5;
        marketData.ask = 50000.0 + i + 0.5;
        marketData.volume = 1000.0;
        
        // Update should not throw even under high frequency
        EXPECT_NO_THROW(postTradeMonitor->updateMarketData(marketData));
    }
    
    // System should remain stable
    EXPECT_EQ(postTradeMonitor->getMode(), RiskManagerMode::NORMAL);
}

} // namespace test
} // namespace risk