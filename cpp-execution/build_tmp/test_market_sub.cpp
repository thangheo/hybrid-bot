#include <iostream>
#include <thread>
#include <chrono>
#include "communication/zmq_server_client.hpp"
#include "risk/interfaces/market_data_subscriber.hpp"

using namespace trading::communication;
using namespace trading::risk;

int main() {
    ZmqServerConfig serverCfg;
    serverCfg.pub_endpoint = "tcp://127.0.0.1:55777";
    serverCfg.rep_endpoint = "tcp://127.0.0.1:55778";
    serverCfg.sndhwm = 1000;
    serverCfg.rcvhwm = 1000;
    serverCfg.io_threads = 1;

    GenericZmqServer server(serverCfg);
    if (!server.start()) {
        std::cerr << "Failed to start server: " << server.getLastError() << std::endl;
        return 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    SubscriberConfig subCfg;
    subCfg.publisherEndpoint = serverCfg.pub_endpoint;
    subCfg.subscriberEndpoint = serverCfg.rep_endpoint;
    subCfg.processingThreadCount = 1;
    subCfg.processingBatchSize = 4;
    subCfg.enableConflation = false;

    MarketDataSubscriber subscriber(subCfg);
    if (!subscriber.initialize()) {
        std::cerr << "Failed to init subscriber" << std::endl;
        return 1;
    }
    if (!subscriber.subscribePattern("orderbook.*")) {
        std::cerr << "Failed to subscribe pattern" << std::endl;
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::string topic = "orderbook.TEST.BTC/USDT";
    std::string data = "hello-proto";
    bool sent = server.publishSync(topic, data);
    std::cout << "sent=" << sent << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    SubscriberStats stats = subscriber.getStats();
    std::cout << "received=" << stats.totalReceived << std::endl;

    subscriber.shutdown();
    server.stop();
    return 0;
}
