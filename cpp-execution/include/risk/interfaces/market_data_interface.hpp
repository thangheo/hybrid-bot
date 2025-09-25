#pragma once

#include "risk/interfaces/data_interface.hpp"
#include "communication/zmq_client.hpp"
// later we should include abstract class for rest client interface
//now use binance client directly
#include "data_ingestion/rest/exchanges/binance_client.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "data_ingestion/normalization/order_book_normalizer.hpp"
#include "risk/risk_config.hpp"
#include <core/market_types.hpp>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <deque>
#include <condition_variable>
#include <queue>
#include <mutex>

using namespace trading::communication;
using namespace trading::core;
using namespace trading::data_ingestion;
namespace trading {
namespace risk {

// Add to market_data_interface.hpp
struct VolatilityCalculator {
    // EWMA volatility state
    struct EWMAState {
        double volatility = 0.0;
        double lambda = 0.94;  // Decay factor (RiskMetrics standard)
        bool initialized = false;
    };
    
    std::unordered_map<std::string, EWMAState> ewmaStates;
    // Protect access to ewmaStates since volatility calculations may be
    // called from multiple threads (processing thread + external callers).
    mutable std::mutex ewmaMutex_; 
    
    double calculateEWMAVolatility(const std::deque<std::pair<Timestamp, double>>& prices, 
                                   const std::string& key);
    double calculateSimpleVolatility(const std::deque<std::pair<Timestamp, double>>& prices,
                                    const std::string& timeframe);
};


/**
 * Market data cache entry
 */
struct MarketDataCacheEntry {
    core::MarketData data;
    std::chrono::steady_clock::time_point lastUpdate;
    bool isValid = false;
};

/**
 * Price history for volatility calculation
 */
struct PriceHistory {
    std::deque<std::pair<Timestamp, double>> prices;
    size_t maxSize = 1000;  // Keep last 1000 price points
    
    enum class VolatilityMethod {
        SIMPLE,
        EWMA,
        GARCH  // Skip GARCH - too complex for limited benefit
    };

    void addPrice(Timestamp timestamp, double price);
    std::vector<std::pair<Timestamp, double>> getRecentPrices(int count) const;
    void removeOlderThan(const Timestamp& cutoff) {
    while (!prices.empty() && prices.front().first < cutoff) {
        prices.pop_front();
    }
}
 

    
    // Single calculateVolatility method with default parameter
    // double calculateVolatility(const std::string& timeframe, 
    //                           VolatilityMethod method = VolatilityMethod::SIMPLE) const;
};
/**
 * Concrete implementation of DataInterface
 */

class MarketDataInterface : public DataInterface {
public:
    /**
     * Constructor
     */
    MarketDataInterface();
    // with config
    explicit MarketDataInterface(const RiskConfig& config) ;
    /**
     * Destructor
     */
    ~MarketDataInterface() override;
    
    // DataInterface implementation
    bool initialize() override;
    
    std::optional<MarketData> getMarketData(
        const core::Symbol& tradingPair,
        const std::optional<std::string>& exchangeId = std::nullopt) override;
    
    double getCurrentPrice(
        const core::Symbol& tradingPair, 
        const std::optional<std::string>& exchangeId = std::nullopt) override;
    
    std::vector<std::pair<Timestamp, double>> getHistoricalPrices(
        const core::Symbol& tradingPair,
        const std::string& interval,
        int count,
        const std::optional<std::string>& exchangeId = std::nullopt) override;
    
    double getVolatility(
        const core::Symbol& tradingPair, 
        const std::string& timeframe, 
        const std::optional<std::string>& exchangeId = std::nullopt) override;
    
    std::vector<OrderRequest> getOrderHistory(
        const core::ExchangeId& exchangeId, 
        const core::Symbol& tradingPair, 
        int limit) override;
    
    std::map<std::string, double> getExchangeBalances(
        const core::ExchangeId& exchangeId) override;
    
    void registerMarketDataCallback(
        const std::function<void(const MarketData&)>& callback) override;
    
    bool subscribeMarketData(
        const core::Symbol& tradingPair, 
        const std::optional<std::string>& exchangeId = std::nullopt) override;
    
    bool unsubscribeMarketData(
        const core::Symbol& tradingPair, 
        const std::optional<std::string>& exchangeId = std::nullopt) override;
    bool isConnected() const override;
    std::map<std::string, double> getTradingFees(
        const core::ExchangeId& exchangeId, 
        const std::optional<std::string>& tradingPair = std::nullopt) override;
    void updateBalance(const core::ExchangeId& exchangeId, 
                      const std::string& asset, 
                      double balance);
    
    double getBalance(const core::ExchangeId& exchangeId, 
                     const std::string& asset) const;
    // createDataInterface(m_config) header
    static std::shared_ptr<DataInterface> createDataInterface(const RiskConfig& config);

    // function to set zmqClient_
    void setZmqClient(std::unique_ptr<trading::communication::GenericZmqClient> zmqClient){
        zmqClient_ = std::move(zmqClient);
    };

private:
    void pruneMarketDataCache();
    void performPeriodicCleanup();
    // Binance REST client for balance and order history
    data_ingestion::BinanceClient m_binanceClient;

    void processSyntheticOrderBook(const std::string& topic, const std::string& message);
    VolatilityCalculator volatilityCalculator_;
    // Helper methods
    std::string createCacheKey(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair) const;
    void processOrderBookUpdate(const std::string& topic, const std::string& message);
    void processTradeUpdate(const std::string& topic, const std::string& message);
    void updateMarketDataCache(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair, 
                              const data_ingestion::OrderBookUpdate& update);
    core::MarketData convertOrderBookToMarketData(const data_ingestion::OrderBookUpdate& orderBook) const;
    
    // ZeroMQ client for subscribing to market data
    std::unique_ptr<trading::communication::GenericZmqClient> zmqClient_;
    
    // Order book normalizer for processing raw data
    // std::shared_ptr<data_ingestion::OrderBookNormalizer> normalizer_;
    // we should use shared_ptr for normalizer_ to avoid ownership issues in callbacks
    // Cache for market data
    mutable std::shared_mutex cacheMutex_;
    std::unordered_map<std::string, MarketDataCacheEntry> marketDataCache_;
    
    // Price history for volatility calculations
    mutable std::shared_mutex historyMutex_;
    std::unordered_map<std::string, PriceHistory> priceHistories_;
    
    // Order history cache (simplified for now)
    mutable std::shared_mutex orderHistoryMutex_;
    std::unordered_map<std::string, std::vector<OrderRequest>> orderHistoryCache_;
    
    // Callbacks
    std::vector<std::function<void(const MarketData&)>> marketDataCallbacks_;
    std::mutex callbackMutex_;
    // Track subscriptions so we can reapply them after reconnect
    mutable std::mutex subscriptionMutex_;
    std::unordered_set<std::string> subscribedTopics_;
    
    // Configuration
    std::string zmqSubscriberEndpoint_ = SERVER_INTERNAL_ENDPOINT;
    std::string zmqPublisherEndpoint_ = SERVER_REQUEST_ENDPOINT;
    // Default cache validity for market data. Config overrides this in constructor.
    // Made more reasonable default to avoid immediate expiration on each read.
    std::chrono::milliseconds cacheValidityDuration_{2000};  // 2000ms (2s) cache validity
    
    // State
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    
    // Default exchange if not specified
    static constexpr const char* DEFAULT_EXCHANGE = "binance";
    mutable std::shared_mutex balanceMutex_;
    std::unordered_map<core::ExchangeId, std::map<std::string, double>> exchangeBalances_;
        std::thread reconnectionThread_;
    std::atomic<bool> needsReconnection_{false};
        std::mutex reconnectionMutex_;
        std::condition_variable reconnectionCv_;
    std::chrono::steady_clock::time_point lastConnectionCheck_;
    
    void connectionMonitorLoop();
    bool checkConnection();
    void handleDisconnection();
    
    // Inbound message queue + processing thread to avoid doing heavy work inside ZMQ recv thread
    struct RawMessage {
        std::string topic;
        std::string message;
        std::chrono::steady_clock::time_point recv_time;
    };

    // Queue used by ZMQ callback (producer) and processing thread (consumer)
    mutable std::mutex inboundQueueMutex_;
    std::condition_variable inboundQueueCv_;
    std::deque<RawMessage> inboundQueue_;
    size_t inboundQueueMaxSize_ = 10000; // bounded queue size to avoid OOM
    // Maximum allowed incoming message size to avoid parsing extremely large
    // messages that could exhaust memory or lead to pathological behavior.
    // Set to 256KB by default; can be tuned if needed.
    size_t maxMessageSize_ = 256 * 1024;
    std::thread processingThread_;
    std::atomic<bool> processingRunning_{false};

    // Counters for instrumentation
    std::atomic<uint64_t> totalReceived_{0};
    std::atomic<uint64_t> totalEnqueued_{0};
    std::atomic<uint64_t> totalProcessed_{0};
    std::atomic<uint64_t> totalDropped_{0};

    // Processing loop
    void processQueueLoop();
    
    // Error tracking
    struct ErrorStats {
        std::atomic<uint64_t> connectionErrors{0};
        std::atomic<uint64_t> dataErrors{0};
        std::atomic<uint64_t> subscriptionErrors{0};
        std::chrono::steady_clock::time_point lastError;
    };
    ErrorStats errorStats_;
};

} // namespace risk
} // namespace trading