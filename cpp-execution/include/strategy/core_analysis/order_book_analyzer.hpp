/**
 * @file order_book_analyzer.hpp
 * @brief Order book analysis component for trading strategies
 * 
 * This component analyzes order book data to extract key metrics and
 * identify patterns that may indicate profitable trading opportunities,
 * particularly focused on order imbalances.
 * 
 * @author Trading System Team
 * @date May 6, 2025
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <deque>
#include <mutex>
#include <memory>
#include <functional>
#include <chrono>
#include "strategy/types.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "common/event_bus.hpp"
#include "common/trading_events.hpp"
#include "core/market_types.hpp"

namespace trading {
namespace strategy {

/**
 * Callback type for metric updates
 */
using MetricsCallback = std::function<void(const OrderBookMetrics&)>;

/**
 * Configuration for OrderBookAnalyzer
 */
// struct OrderBookAnalyzerConfig {
//     int depth = 10;
//     double priceWeightFactor = 1.0;
//     double volumeWeightFactor = 1.0;
//     double imbalanceThreshold = 0.1;
//     double liquidityPressureThreshold = 0.05;
//     double orderFlowThreshold = 0.2;
//     size_t historySize = 100;
//     double emaAlpha = 0.2;
//     double priceGroupingFactor = 0.0001;
//     int minSamplesForStability = 5;
// };
/**
 * OrderBookAnalyzer processes order book data to extract trading signals
 * based on order book imbalances and other market microstructure patterns.
 */
class OrderBookAnalyzer {
public:
    /**
     * Creates an order book analyzer for a specific exchange and trading pair
     */
    OrderBookAnalyzer(const core::ExchangeId& exchangeId, 
                     const std::string& tradingPair, 
                     const ParameterSet& params = ParameterSet{});

    /**
     * Creates an order book analyzer with explicit configuration
     */
    OrderBookAnalyzer(const core::ExchangeId& exchangeId, 
                     const std::string& tradingPair, 
                     int depthLevels, 
                     double priceGroupingFactor);

    /**
     * Creates an order book analyzer with configuration struct
     */
    explicit OrderBookAnalyzer(const OrderBookAnalyzerConfig& config);

    virtual ~OrderBookAnalyzer() = default;

    // Delete copy semantics to prevent accidental copying
    OrderBookAnalyzer(const OrderBookAnalyzer&) = delete;
    OrderBookAnalyzer& operator=(const OrderBookAnalyzer&) = delete;

    // Allow move semantics
    OrderBookAnalyzer(OrderBookAnalyzer&&) = default;
    OrderBookAnalyzer& operator=(OrderBookAnalyzer&&) = default;

    /**
     * Process an order book snapshot
     */
    virtual void processOrderBook(const data_ingestion::OrderBook& snapshot);

    /**
     * Get current order book metrics
     */
    virtual OrderBookMetrics getMetrics() const;

    /**
     * Get a specific metric value
     */
    virtual double getMetric(const std::string& metricName, int depth = 10) const;

    /**
     * Register a callback for metrics updates
     */
    virtual void registerMetricsCallback(MetricsCallback callback);

    /**
     * Check if enough data has been processed for reliable metrics
     */
    virtual bool hasStableMetrics() const;

    /**
     * Reset the analyzer state
     */
    virtual void reset();

    /**
     * Update configuration parameters
     */
    virtual void updateParameters(const ParameterSet& params);

    // Getters
    const core::ExchangeId& getExchangeId() const { return exchangeId_; }
    const std::string& getTradingPair() const { return tradingPair_; }
    const ParameterSet& getParameters() const { return params_; }
    core::Timestamp getLastUpdateTime() const { return lastUpdateTime_; }

    // Historical data accessors
    const std::vector<double>& getHistoricalMidPrices() const { return historicalMidPrices_; }
    const std::vector<double>& getHistoricalSpreads() const { return historicalSpreads_; }
    const std::vector<double>& getHistoricalVolumes() const { return historicalVolumes_; }
    const std::vector<double>& getHistoricalImbalances() const { return historicalImbalances_; }

protected:
    // Core calculation methods
    virtual void calculatePriceMetrics(const std::vector<data_ingestion::PriceLevel>& bids,
                                     const std::vector<data_ingestion::PriceLevel>& asks);
    
    virtual void calculateVolumeMetrics(const std::vector<data_ingestion::PriceLevel>& bids,
                                      const std::vector<data_ingestion::PriceLevel>& asks,
                                      int depth);
    
    virtual void calculateLevelMetrics(const std::vector<data_ingestion::PriceLevel>& bids,
                                     const std::vector<data_ingestion::PriceLevel>& asks);
    
    virtual void calculateImbalanceMetrics(const std::vector<data_ingestion::PriceLevel>& bids,
                                         const std::vector<data_ingestion::PriceLevel>& asks);
    
    virtual void calculateDerivedMetrics();
    virtual void updateTimeSeries();
    virtual void calculateTimeBasedMetrics();

    // Utility methods
    virtual std::vector<data_ingestion::PriceLevel> groupPriceLevels(
        const std::vector<data_ingestion::PriceLevel>& levels,
        bool isBids) const;
    
    virtual double calculateWeightedImbalance(
        const std::vector<data_ingestion::PriceLevel>& bids,
        const std::vector<data_ingestion::PriceLevel>& asks,
        const std::function<double(int)>& weightingFunction) const;
    
    virtual std::vector<int> detectLiquidityWalls(
        const std::vector<data_ingestion::PriceLevel>& levels,
        double threshold = 3.0) const;
    
    virtual double calculateDirectionalCoefficient() const;
    virtual double calculateVolatilityExpectation() const;
    virtual double calculateRealizedVolatility(double sample_interval_sec) const;

    virtual void notifyMetricsUpdated();

private:
    // Configuration
    core::ExchangeId exchangeId_;
    std::string tradingPair_;
    int depthLevels_;
    double priceGroupingFactor_;
    int minSamplesForStability_;
    double emaAlpha_;
    ParameterSet params_;
    // State
    OrderBookMetrics currentMetrics_;
    std::deque<OrderBookMetrics> metricsHistory_;
    
    // Historical data
    std::vector<double> historicalMidPrices_;
    std::vector<double> historicalSpreads_;
    std::vector<double> historicalVolumes_;
    std::vector<double> historicalImbalances_;
    std::vector<double> priceHistory_;
    std::vector<double> historicalTimestamps_;
    
    // Moving averages
    double emaMidPrice_ = 0.0;
    double emaSpread_ = 0.0;
    double emaVolumeImbalance_ = 0.0;
    
    // Callbacks and timing
    std::vector<MetricsCallback> metricsCallbacks_;
    core::Timestamp lastUpdateTime_;
    
    // Thread safety
    mutable std::mutex mutex_;

    // Helper methods
    void initializeHistoricalData(size_t capacity);
    void trimHistoricalData(size_t maxSize);
};

/**
 * Manager class for multiple OrderBookAnalyzers
 */
class OrderBookAnalyzerManager {
public:
    OrderBookAnalyzerManager() = default;
    explicit OrderBookAnalyzerManager(const ParameterSet& params);
    explicit OrderBookAnalyzerManager(const OrderBookAnalyzerConfig& config) ;

    ~OrderBookAnalyzerManager() = default;

    // Delete copy semantics
    OrderBookAnalyzerManager(const OrderBookAnalyzerManager&) = delete;
    OrderBookAnalyzerManager& operator=(const OrderBookAnalyzerManager&) = delete;

    // Allow move semantics
    OrderBookAnalyzerManager(OrderBookAnalyzerManager&&) = default;
    OrderBookAnalyzerManager& operator=(OrderBookAnalyzerManager&&) = default;

    /**
     * Get or create analyzer for a specific symbol and exchange
     */
    std::shared_ptr<OrderBookAnalyzer> getAnalyzerForSymbol(
        const std::string& symbol, 
        const core::ExchangeId& exchangeId,
        const ParameterSet& params = ParameterSet{});

    /**
     * Get existing analyzer (returns nullptr if not found)
     */
    std::shared_ptr<OrderBookAnalyzer> getAnalyzerForSymbol(
        const std::string& symbol, 
        const core::ExchangeId& exchangeId) const;

    /**
     * Remove analyzer for specific symbol and exchange
     */
    void removeAnalyzer(const std::string& symbol, const core::ExchangeId& exchangeId);

    /**
     * Get all active analyzers
     */
    std::vector<std::shared_ptr<OrderBookAnalyzer>> getAllAnalyzers() const;

    /**
     * Process order book data through appropriate analyzer
     */
    void processOrderBook(const data_ingestion::OrderBook& snapshot);

    /**
     * Get metrics for specific symbol and exchange
     */
    OrderBookMetrics getMetrics(const core::Symbol& symbol, 
                               const core::ExchangeId& exchange) const;

    /**
     * Cleanup analyzers that haven't been updated recently
     */
    void cleanupInactiveAnalyzers(std::chrono::seconds inactiveThreshold);

    /**
     * Update parameters for all analyzers
     */
    void updateParameters(const ParameterSet& params);

private:
    std::unordered_map<std::string, std::shared_ptr<OrderBookAnalyzer>> analyzers_;
    ParameterSet params_;
    mutable std::mutex mutex_;

    std::string createKey(const std::string& symbol, const core::ExchangeId& exchangeId) const;
};

/**
 * Factory function to create OrderBookAnalyzer instances
 */
std::shared_ptr<OrderBookAnalyzer> createOrderBookAnalyzer(
    const core::ExchangeId& exchangeId, 
    const std::string& tradingPair,
    const ParameterSet& params = ParameterSet{});

} // namespace strategy
} // namespace trading