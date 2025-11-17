#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <chrono>

#include "strategy/pattern/imbalance_detection_engine.hpp"

using namespace trading::strategy;

TEST(ImbalanceDetectionEngineTest, SafeTeardownWithPendingTraining) {
    auto eng = std::make_shared<ImbalanceDetectionEngine>();
    eng->setTrainingEnabled(true);

    OrderBookMetrics m;
    m.bidVolumesByLevel = {1.0, 1.0, 1.0};
    m.askVolumesByLevel = {1.0, 1.0, 1.0};
    m.midPrice = 100.0;
    m.exchangeId = trading::core::ExchangeId::binance();
    m.symbol = trading::core::Symbol("BTC","USDT");

    // Enqueue some training tasks that submit to the training thread pool and may reference
    // the engine via weak_ptr captures. If weak_ptr protection is broken, destroying the
    // engine here may cause a crash.
    for (int i = 0; i < 20; ++i) {
        eng->analyzeMetrics(m);
    }

    // Allow some tasks start being enqueued
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Reset the engine (destroy the object) while tasks are still possibly pending
    eng.reset();

    // Allow time for worker threads to run (should not crash)
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    SUCCEED();
}
