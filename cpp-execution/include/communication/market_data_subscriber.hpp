/**
 * @file market_data_subscriber.hpp
 * @brief ZeroMQ subscriber for market data and intelligence
 */

#pragma once

#include "core/types.hpp"
#include "../../generated/cpp/proto/trading_messages.pb.h"
#include <nlohmann/json.hpp>
#include <zmq.hpp>
#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <memory>

using json = nlohmann::json;

namespace trading {
namespace communication {

struct OrderBookData {
    std::string exchange;
    std::string symbol;
    uint64_t sequence;
    uint64_t timestamp;
    std::vector<std::pair<double, double>> bids;  // price, quantity
    std::vector<std::pair<double, double>> asks;  // price, quantity
};

struct MarketIntelligenceData {
    std::string symbol;
    double bidAskSpread;
    double liquidityRatio;
    double orderBookImbalance;
    double priceMomentum;
    double volatilityEstimate;
    int currentRegime;
    double regimeConfidence;
    uint64_t timestamp;
};

class MarketDataSubscriber {
public:
    using OrderBookCallback = std::function<void(const OrderBookData&)>;
    using IntelligenceCallback = std::function<void(const MarketIntelligenceData&)>;
    using StatusCallback = std::function<void(const std::string&)>;

    explicit MarketDataSubscriber(const std::string& publisherEndpoint = "tcp://localhost:5555");
    // Accept shared context from ZmqManager or tests
    MarketDataSubscriber(const std::string& publisherEndpoint, std::shared_ptr<zmq::context_t> sharedContext);
    ~MarketDataSubscriber();

    // Lifecycle
    bool start();
    void stop();
    bool isRunning() const { return running_.load(); }

    // Subscriptions
    void subscribeToOrderBooks(const std::vector<std::string>& symbols = {});
    void subscribeToIntelligence(const std::vector<std::string>& symbols = {});
    void subscribeToAll(); // Subscribe to all topics

    // Callbacks
    void setOrderBookCallback(OrderBookCallback callback) { orderBookCallback_ = std::move(callback); }
    void setIntelligenceCallback(IntelligenceCallback callback) { intelligenceCallback_ = std::move(callback); }
    void setStatusCallback(StatusCallback callback) { statusCallback_ = std::move(callback); }

    // Statistics
    uint64_t getOrderBooksReceived() const { return orderBooksReceived_.load(); }
    uint64_t getIntelligenceReceived() const { return intelligenceReceived_.load(); }

private:
    void subscriptionLoop();
    void handleOrderBookMessage(const std::string& topic, const std::string& data);
    void handleIntelligenceMessage(const std::string& topic, const std::string& data);

    std::string publisherEndpoint_;
    std::shared_ptr<zmq::context_t> context_;
    std::unique_ptr<zmq::socket_t> subscriber_;
    
    std::thread subscriptionThread_;
    std::atomic<bool> running_{false};
    
    OrderBookCallback orderBookCallback_;
    IntelligenceCallback intelligenceCallback_;
    StatusCallback statusCallback_;
    
    std::atomic<uint64_t> orderBooksReceived_{0};
    std::atomic<uint64_t> intelligenceReceived_{0};
};

} // namespace communication
} // namespace trading
