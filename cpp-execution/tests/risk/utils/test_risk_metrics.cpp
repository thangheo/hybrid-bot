#include <gtest/gtest.h>
#include "risk/utils/risk_metrics.hpp"
#include <vector>
#include <map>

class RiskMetricsTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Sample positions and prices
        positions = {{"BTC", 1.5}, {"ETH", 10.0}, {"SOL", 50.0}};
        prices = {{"BTC", 50000.0}, {"ETH", 3000.0}, {"SOL", 100.0}};
        volatilities = {{"BTC", 0.05}, {"ETH", 0.07}, {"SOL", 0.09}};
    }

    std::map<std::string, double> positions;
    std::map<std::string, double> prices;
    std::map<std::string, double> volatilities;
    RiskMetrics metrics;
};

TEST_F(RiskMetricsTest, CalculateExposure) {
    double exposure = metrics.calculateExposure(positions, prices);
    
    // Expected exposure: (1.5 * 50000) + (10 * 3000) + (50 * 100) = 105000
    EXPECT_NEAR(exposure, 105000.0, 0.001);
}

TEST_F(RiskMetricsTest, CalculateRiskAdjustedExposure) {
    double riskAdjustedExposure = metrics.calculateRiskAdjustedExposure(
        positions, prices, volatilities);
    
    // Higher than regular exposure due to volatility adjustment
    EXPECT_GT(riskAdjustedExposure, 105000.0);
}

TEST_F(RiskMetricsTest, CalculateVaR) {
    double var = metrics.calculateVaR(positions, prices, volatilities, 0.95);
    
    // VaR should be positive
    EXPECT_GT(var, 0.0);
    
    // Higher confidence should increase VaR
    double higherVar = metrics.calculateVaR(positions, prices, volatilities, 0.99);
    EXPECT_GT(higherVar, var);
}

TEST_F(RiskMetricsTest, EmptyPositions) {
    std::map<std::string, double> emptyPositions;
    double exposure = metrics.calculateExposure(emptyPositions, prices);
    
    // Exposure should be zero with no positions
    EXPECT_DOUBLE_EQ(exposure, 0.0);
}