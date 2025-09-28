#pragma once

#include "risk/interfaces/data_interface.hpp"
#include "market_data_cache.hpp"
#include "market_data_subscriber.hpp"
#include "data_ingestion/rest/exchanges/binance_client.hpp"
#include "risk/risk_config.hpp"
#include <memory>
#include <unordered_map>
#include <shared_mutex>

namespace trading {
namespace risk {

/**
 * Refactored MarketDataInterface - Thin coordinator layer
 * 
 * This class now focuses on:
 * - Implementing the DataInterface contract
 * - Coordinating between cache, subscriber, and REST clients
 * - Managing configuration and initialization
 * - Providing a clean API for the risk system
 * 
 * Heavy lifting is delegated to:
 * - MarketDataCache: Data storage, processing, volatility calculations
 * - MarketDataSubscriber: ZMQ communication, message queuing
 * - MarketDataProcessor: Message parsing and transformation
 */
class MarketDataInterface : public DataInterface {
public:
    /**
     * Constructors
     */
    MarketDataInterface();
    explicit MarketDataInterface(const RiskConfig& config);
    
    /**
     * Destructor
     */
    ~MarketDataInterface() override;
    
    // Disable copy/move
    MarketDataInterface(const MarketDataInterface&) = delete;
    MarketDataInterface& operator=(const MarketDataInterface&) = delete;
    MarketDataInterface(MarketDataInterface&&) = delete;
    MarketDataInterface& operator=(MarketDataInterface&&) = delete;
    
    // DataInterface implementation
    bool initialize() override;
    void shutdown() override;
    
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
    
    // Balance management
    void updateBalance(const core::ExchangeId& exchangeId, 
                      const std::string& asset, 
                      double balance);
    
    double getBalance(const core::ExchangeId& exchangeId, 
                     const std::string& asset) const;
    
    // Factory method
    static std::shared_ptr<DataInterface> createDataInterface(const RiskConfig& config);
    
    // For testing - allow injection of components
    void setZmqClient(std::unique_ptr<trading::communication::GenericZmqClient> zmqClient);
    
    // Health and statistics
    struct HealthStatus {
        bool cacheHealthy{false};
        bool subscriberHealthy{false};
        bool restClientHealthy{false};
        MarketDataCache::CacheStats cacheStats;
        SubscriberStats subscriberStats;
        ProcessorErrorStats processorStats;
    };
    HealthStatus getHealthStatus() const;

private:
    // Configuration
    RiskConfig config_;
    std::string defaultExchange_{"binance"};
    
    // Core components
    std::unique_ptr<MarketDataCache> cache_;
    std::unique_ptr<MarketDataSubscriber> subscriber_;
    
    // REST client for balance and order history
    data_ingestion::BinanceClient binanceClient_;
    
    // Balance cache
    mutable std::shared_mutex balanceMutex_;
    std::unordered_map<core::ExchangeId, std::map<std::string, double>> balanceCache_;
    
    // Order history cache (simplified)
    mutable std::shared_mutex orderHistoryMutex_;
    std::unordered_map<std::string, std::vector<OrderRequest>> orderHistoryCache_;
    
    // State
    std::atomic<bool> initialized_{false};
    
    // Internal methods
    void initializeComponents();
    void configureSubscriber();
    void configureCache();
    void configureBinanceClient();
    void onMarketDataReceived(const core::MarketData& data);
    std::string createCacheKey(const core::ExchangeId& exchangeId, 
                              const core::Symbol& tradingPair) const;
};

} // namespace risk
} // namespace trading
