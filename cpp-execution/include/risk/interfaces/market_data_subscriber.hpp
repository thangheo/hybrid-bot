#pragma once

#include "market_data_processor.hpp"
#include "communication/zmq_server_client.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <deque>
#include <condition_variable>
#include <unordered_map>
#include <unordered_set>
#include <functional>

using namespace trading::communication;

namespace trading {
namespace risk {

/**
 * Raw message structure for queuing
 */
struct RawMessage {
    std::string topic;
    std::string message;
    std::chrono::steady_clock::time_point recv_time;
};

/**
 * Configuration for MarketDataSubscriber
 */
struct SubscriberConfig {
    std::string subscriberEndpoint = trading::communication::SERVER_REQUEST_ENDPOINT;
    std::string publisherEndpoint = trading::communication::SERVER_PUBLISER_ENDPOINT;
    std::string defaultExchange = "binance";
    // Queue settings
    size_t inboundQueueMaxSize = 10000;
    bool enableConflation = true;
    
    // Processing settings
    size_t processingThreadCount = 8;
    size_t processingBatchSize = 64;
    size_t maxProcessingBatchSize = 1024;
    size_t minProcessingBatchSize = 1;
    
    // Connection settings
    std::chrono::milliseconds reconnectInterval{5000};
    std::chrono::milliseconds requestTimeout{10000};
    bool autoReconnect = true;
    int maxReconnectAttempts = 10;
};

/**
 * Callback type for processed market data
 */
using MarketDataCallback = std::function<void(const core::MarketData&)>;

/**
 * Statistics for the subscriber
 */
struct SubscriberStats {
    uint64_t totalReceived{0};
    uint64_t totalEnqueued{0};
    uint64_t totalProcessed{0};
    uint64_t totalDropped{0};
    uint64_t connectionErrors{0};
    std::chrono::steady_clock::time_point lastError;
    
    // Constructor from atomic values
    SubscriberStats() = default;
    
    SubscriberStats(uint64_t received, uint64_t enqueued, uint64_t processed, 
                   uint64_t dropped, uint64_t connErrors, 
                   std::chrono::steady_clock::time_point lastErr)
        : totalReceived(received), totalEnqueued(enqueued), totalProcessed(processed)
        , totalDropped(dropped), connectionErrors(connErrors), lastError(lastErr) {}
};

/**
 * MarketDataSubscriber - Handles ZMQ communication and message queuing
 * 
 * Responsibilities:
 * - Manage ZMQ client connection and subscriptions
 * - Queue incoming messages with optional conflation
 * - Process messages using MarketDataProcessor
 * - Handle connection monitoring and automatic reconnection
 * - Provide processing statistics and health monitoring
 */
class MarketDataSubscriber {
public:
    explicit MarketDataSubscriber(const SubscriberConfig& config = SubscriberConfig{});
    ~MarketDataSubscriber();
    
    // Disable copy/move
    MarketDataSubscriber(const MarketDataSubscriber&) = delete;
    MarketDataSubscriber& operator=(const MarketDataSubscriber&) = delete;
    MarketDataSubscriber(MarketDataSubscriber&&) = delete;
    MarketDataSubscriber& operator=(MarketDataSubscriber&&) = delete;
    
    /**
     * Initialize and start the subscriber
     */
    bool initialize();
    
    /**
     * Shutdown the subscriber
     */
    void shutdown();
    
    /**
     * Subscribe to market data for a trading pair
     */
    bool subscribe(const core::Symbol& tradingPair, 
                  const std::optional<std::string>& exchangeId = std::nullopt);
    
    /**
     * Unsubscribe from market data
     */
    bool unsubscribe(const core::Symbol& tradingPair, 
                    const std::optional<std::string>& exchangeId = std::nullopt);
    
    /**
     * Register callback for processed market data
     */
    void registerCallback(const MarketDataCallback& callback);
    
    /**
     * Check connection status
     */
    bool isConnected() const;
    
    /**
     * Get processing statistics (snapshot of current values)
     */
    SubscriberStats getStats() const;
    
    /**
     * Get processor statistics
     */
    ProcessorErrorStats getProcessorStats() const;
    
    /**
     * Set custom ZMQ client (for testing)
     */
    void setZmqClient(std::unique_ptr<trading::communication::GenericZmqClient> client);
    void stopProcessingThreads();
    void startProcessingThreads();
    void stopConnectionMonitor();
    void startConnectionMonitor();
private:
    SubscriberConfig config_;
    
    // Internal atomic statistics
    std::atomic<uint64_t> totalReceived_{0};
    std::atomic<uint64_t> totalEnqueued_{0};
    std::atomic<uint64_t> totalProcessed_{0};
    std::atomic<uint64_t> totalDropped_{0};
    std::atomic<uint64_t> connectionErrors_{0};
    std::chrono::steady_clock::time_point lastError_;
    
    // Helper for recording errors
    void recordError() {
        connectionErrors_.fetch_add(1, std::memory_order_relaxed);
        lastError_ = std::chrono::steady_clock::now();
    }
    
    // ZMQ client
    std::unique_ptr<trading::communication::GenericZmqClient> zmqClient_;
    
    // Message processor
    std::unique_ptr<MarketDataProcessor> processor_;
    
    // Callbacks
    std::vector<MarketDataCallback> callbacks_;
    mutable std::mutex callbackMutex_;
    
    // Subscription tracking
    mutable std::mutex subscriptionMutex_;
    std::unordered_set<std::string> subscribedTopics_;
    
    // Message queue and processing
    mutable std::mutex queueMutex_;
    std::condition_variable queueCv_;
    std::deque<RawMessage> inboundQueue_;
    std::unordered_map<std::string, RawMessage> conflationMap_;
    std::deque<std::string> conflationKeyQueue_;
    
    // Processing threads
    std::vector<std::thread> processingThreads_;
    std::atomic<bool> processingRunning_{false};
    
    // Connection monitoring
    std::thread connectionThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> needsReconnection_{false};
    std::mutex connectionMutex_;
    std::condition_variable connectionCv_;
    std::chrono::steady_clock::time_point lastConnectionCheck_;
    
    // Rate measurement for adaptive batching
    std::atomic<uint64_t> lastReceivedSnapshot_{0};
    std::chrono::steady_clock::time_point lastRateCheck_ = std::chrono::steady_clock::now();
    double batchSizeSmoothingAlpha_ = 0.75;
    size_t currentBatchSize_;
    
    // Internal methods
    void processQueueLoop();
    void connectionMonitorLoop();
    bool checkConnection();
    void handleDisconnection();
    bool reconnect();
    void onMessageReceived(const std::string& topic, const std::string& message,
                          const std::unordered_map<std::string, std::string>& metadata);
    void invokeCallbacks(const core::MarketData& data);
    
    // Adaptive batch sizing
    void measureAndAdjustBatchSize();
    size_t calculateOptimalBatchSize(size_t messageRate) const;
    
    // Topic creation helpers
    std::string createOrderbookTopic(const core::Symbol& tradingPair, 
                                   const std::string& exchange) const;
};

} // namespace risk
} // namespace trading