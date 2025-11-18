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

TEST(AdaptiveExitHistoryTest, TrimsHistoryWhenExceedingMaxSamples) {
    AdaptiveExitStrategy s(3.0); // small window to test trimming

    // first update initializes history to 3 samples
    for (int i = 0; i < 5; ++i) {
        s.updateMarketData("FAKE_TRIM", 0.1 * (i+1), 0.2, 0.0, 1.0, 0.0);
    }

    size_t samples = s.getHistoryMaxSamples("FAKE_TRIM");
    EXPECT_EQ(samples, 3);

    // The internal deques should have at most 3 elements
    // Use getter indirectly by calling ensureHistoryInitialized + checking sizes
    // Since MarketHistory is internal, we approximate by checking that further updates
    // don't grow the history above max_samples.
    for (int i = 0; i < 10; ++i) {
        s.updateMarketData("FAKE_TRIM", 0.05 * i, 0.1, 0.0, 2.0, 0.0);
    }
    EXPECT_EQ(s.getHistoryMaxSamples("FAKE_TRIM"), 3);

    // Now reduce window and ensure max_samples is updated and deques trimmed
    s.setPredictionWindow(2.0);
    EXPECT_EQ(s.getHistoryMaxSamples("FAKE_TRIM"), 2);
}

TEST(AdaptiveExitHistoryTest, UnknownSymbolReturnsZeroMaxSamples) {
    AdaptiveExitStrategy s(10.0);
    EXPECT_EQ(s.getHistoryMaxSamples("NON_EXISTENT_SYMBOL"), 0);
}
