#include "gtest/gtest.h"
#include "strategy/exit/adaptive_exit_strategy.hpp"

using namespace trading::strategy;

TEST(AdaptiveExitHistoryTest, InitializesMaxSamples) {
    AdaptiveExitStrategy s(60.0);
    // create a fake symbol and update market data - should initialize
    MarketMicrostructure micro;
    micro.tick_size = 0.01;
    s.updateMarketData("FAKE", 0.1, 0.2, 0.0, 1.0, 0.0);

    size_t samples = s.getHistoryMaxSamples("FAKE");
    EXPECT_EQ(samples, 60);
}

TEST(AdaptiveExitHistoryTest, UpdatePredictionWindowResizesHistory) {
    AdaptiveExitStrategy s(60.0);
    s.updateMarketData("FAKE", 0.1, 0.2, 0.0, 1.0, 0.0);
    // now increase window
    s.setPredictionWindow(30.0);
    size_t samples = s.getHistoryMaxSamples("FAKE");
    EXPECT_EQ(samples, 30);
}
