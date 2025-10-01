#include <gtest/gtest.h>
#include "risk/posttrade/post_trade_monitor.hpp"

using namespace trading::risk;

TEST(PostTradeMonitorTTL, DynamicTTLCalculation) {
    // Low volatility -> larger TTL
    double lowVol = 0.01; // 1%
    int64_t ttlLow = PostTradeMonitor::calculateDynamicTTL(lowVol);
    EXPECT_GT(ttlLow, 5000);

    // High volatility -> TTL closer to base
    double highVol = 2.0; // 200%
    int64_t ttlHigh = PostTradeMonitor::calculateDynamicTTL(highVol);
    EXPECT_LE(ttlHigh, ttlLow);

    // Ensure max cap
    double zeroVol = 0.0;
    int64_t ttlZero = PostTradeMonitor::calculateDynamicTTL(zeroVol);
    EXPECT_LE(ttlZero, 30000);
}
