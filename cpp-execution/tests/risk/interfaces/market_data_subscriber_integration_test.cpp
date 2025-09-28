#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "communication/zmq_server_client.hpp"
#include "risk/interfaces/market_data_subscriber.hpp"

using namespace trading;
using namespace trading::risk;
using namespace trading::communication;

// Integration test: start a real ZMQ server and a MarketDataSubscriber client,
// publish a small message and assert the subscriber receives it.

TEST(MarketDataSubscriberIntegration, ReceivesPublishedMessages) {
    // Server endpoints (use high-numbered ports to avoid collisions)
    ZmqServerConfig serverCfg;
    serverCfg.pub_endpoint = "tcp://127.0.0.1:55777";
    serverCfg.rep_endpoint = "tcp://127.0.0.1:55778";
    serverCfg.sndhwm = 1000;
    serverCfg.rcvhwm = 1000;
    serverCfg.io_threads = 1;

    GenericZmqServer server(serverCfg);
    ASSERT_TRUE(server.start());

    // Allow server to bind
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SubscriberConfig subCfg;
    subCfg.publisherEndpoint = serverCfg.pub_endpoint; // SUB connects to this
    subCfg.subscriberEndpoint = serverCfg.rep_endpoint; // REQ/REP endpoint for requests
    subCfg.processingThreadCount = 1;
    subCfg.processingBatchSize = 4;
    subCfg.enableConflation = false;

    MarketDataSubscriber subscriber(subCfg);
    // Initialize subscriber (this will create client and connect)
    ASSERT_TRUE(subscriber.initialize());

    // Subscribe to wildcard orderbook topic
    ASSERT_TRUE(subscriber.subscribePattern("orderbook.*"));

    // Wait briefly for connections to establish
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Publish a test message
    std::string topic = "orderbook.TEST.BTC/USDT";
    std::string data = "test-payload";
    bool sent = server.publishSync(topic, data, MessagePriority::NORMAL);
    ASSERT_TRUE(sent);

    // Give some time for the message to traverse
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // Check stats
    SubscriberStats stats = subscriber.getStats();
    EXPECT_GT(stats.totalReceived, 0U);

    // Cleanup
    subscriber.shutdown();
    server.stop();
}
