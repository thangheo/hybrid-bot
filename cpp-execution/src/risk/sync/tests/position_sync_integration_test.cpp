#include <gtest/gtest.h>
#include "risk/sync/position_synchronizer.hpp"
#include "risk/portfolio/position_tracker.hpp"

using namespace trading::risk;
using namespace trading::risk::sync;

TEST(PositionSyncIntegration, PublisherToSubscriberInprocCaching) {
    // Setup ZMQ context for inproc transport
    auto zmqCtx = std::make_shared<zmq::context_t>(1);

    PositionSyncConfig cfg;
    cfg.publishEndpoint = "inproc://test_pos_sync";
    cfg.subscribeEndpoints = {"inproc://test_pos_sync"};
    cfg.batchSize = 4;
    cfg.batchTimeoutMs = 10;

    // Create PositionTracker (shared)
    RiskConfig rcfg;
    auto tracker = std::make_shared<PositionTracker>(rcfg, nullptr);
    ASSERT_TRUE(tracker != nullptr);

    // Create PositionSynchronizer via factory
    auto sync = createPositionSynchronizer(cfg, zmqCtx);
    ASSERT_TRUE(sync != nullptr);

    ASSERT_TRUE(sync->initialize());
    ASSERT_TRUE(sync->start(tracker));

    auto publisher = sync->getPublisher();
    auto subscriber = sync->getSubscriber();
    ASSERT_TRUE(publisher);
    ASSERT_TRUE(subscriber);

    // Build a PositionEvent to simulate a position opened
    PositionEvent ev;
    ev.eventType = PositionEventType::POSITION_OPENED;
    ev.priority = PositionEventPriority::HIGH;
    ev.symbol = "BTCUSDT";
    ev.exchangeId = "binance";
    ev.strategyId = "test";
    ev.quantity = 1.2345;
    ev.avgPrice = 45000.0;
    ev.lastPrice = 45000.0;
    ev.timestamp = std::chrono::system_clock::now();

    // publish event via publisher (this should deliver to local subscriber and update cache)
    ASSERT_TRUE(publisher->publishEvent(ev));

    // give some time for worker to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Now ensure subscriber cache has the position
    auto optPos = subscriber->getPosition("BTCUSDT", "binance");
    ASSERT_TRUE(optPos.has_value());
    EXPECT_NEAR(optPos->getQuantity(), 1.2345, 1e-9);

    // cleanup
    sync->stop();
    sync->shutdown();
}
