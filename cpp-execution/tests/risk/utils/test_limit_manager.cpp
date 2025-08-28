#include <gtest/gtest.h>
#include "risk/utils/limit_manager.hpp"

class LimitManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up some test limits
        limits.setAssetLimit("BTC", 2.0);
        limits.setAssetLimit("ETH", 50.0);
        limits.setStrategyLimit("strategy1", 100000.0);
        limits.setExchangeLimit("binance", 200000.0);
        limits.setTotalExposureLimit(500000.0);
    }

    LimitManager limits;
};

TEST_F(LimitManagerTest, AssetLimits) {
    EXPECT_DOUBLE_EQ(limits.getAssetLimit("BTC"), 2.0);
    EXPECT_DOUBLE_EQ(limits.getAssetLimit("ETH"), 50.0);
    
    // Test non-existent asset
    EXPECT_DOUBLE_EQ(limits.getAssetLimit("XRP"), 0.0);
    
    // Update existing limit
    limits.setAssetLimit("BTC", 3.0);
    EXPECT_DOUBLE_EQ(limits.getAssetLimit("BTC"), 3.0);
}

TEST_F(LimitManagerTest, StrategyLimits) {
    EXPECT_DOUBLE_EQ(limits.getStrategyLimit("strategy1"), 100000.0);
    EXPECT_DOUBLE_EQ(limits.getStrategyLimit("unknown_strategy"), 0.0);
}

TEST_F(LimitManagerTest, ExchangeLimits) {
    EXPECT_DOUBLE_EQ(limits.getExchangeLimit("binance"), 200000.0);
}

TEST_F(LimitManagerTest, TotalExposure) {
    EXPECT_DOUBLE_EQ(limits.getTotalExposureLimit(), 500000.0);
    
    // Update total limit
    limits.setTotalExposureLimit(600000.0);
    EXPECT_DOUBLE_EQ(limits.getTotalExposureLimit(), 600000.0);
}

TEST_F(LimitManagerTest, IsExceedingAssetLimit) {
    EXPECT_FALSE(limits.isExceedingAssetLimit("BTC", 1.5));
    EXPECT_TRUE(limits.isExceedingAssetLimit("BTC", 2.5));
}

TEST_F(LimitManagerTest, ConfigLoad) {
    // Test loading from config file
    bool loaded = limits.loadFromConfig("../../config/risk/position_limits.json");
    EXPECT_TRUE(loaded);
    
    // Verify loaded values
    // (This would depend on your config file content)
}