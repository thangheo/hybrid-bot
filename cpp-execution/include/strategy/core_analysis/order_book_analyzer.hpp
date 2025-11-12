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
// Regime detector (per-analyzer instance)
#include "strategy/core_analysis/orderbook_regime_detector.hpp"

namespace trading {
namespace strategy {

using trading::data_ingestion::PriceLevel;

/**
 * Snapshot structure for volatility calculations
 */
struct OrderbookSnapshot {
    double best_bid = 0.0;
    double best_ask = 0.0;
    double bid_volume = 0.0;
    double ask_volume = 0.0;
    uint64_t timestamp_us = 0;
    
    OrderbookSnapshot() = default;
    OrderbookSnapshot(double bid, double ask, double bid_vol, double ask_vol, uint64_t ts)
        : best_bid(bid), best_ask(ask), bid_volume(bid_vol), ask_volume(ask_vol), timestamp_us(ts) {}
};

/**
 * Result structure for volatility filtering
 */
struct FilterResult {
    bool should_trade = false;
    double rv = 0.0;
    double rv_adjusted = 0.0;
    double rv_smoothed = 0.0;
    double spread_ratio = 0.0;
    double imbalance = 0.0;
    double trend_ratio = 0.0;
    std::string reason = "Not evaluated";
};

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
                     const core::Symbol& tradingPair, 
                     const ParameterSet& params = ParameterSet{});

    /**
     * Creates an order book analyzer with explicit configuration
     */
    OrderBookAnalyzer(const core::ExchangeId& exchangeId, 
                     const core::Symbol& tradingPair, 
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
    const core::Symbol& getTradingPair() const { return tradingPair_; }
    const ParameterSet& getParameters() const { return params_; }
    core::Timestamp getLastUpdateTime() const { return lastUpdateTime_; }

    // Historical data accessors
    const std::vector<double>& getHistoricalMidPrices() const { return historicalMidPrices_; }
    const std::vector<double>& getHistoricalSpreads() const { return historicalSpreads_; }
    const std::vector<double>& getHistoricalVolumes() const { return historicalVolumes_; }
    const std::vector<double>& getHistoricalImbalances() const { return historicalImbalances_; }

protected:
        // calculateFlowMetrics
    virtual void calculateFlowMetrics(const std::vector<data_ingestion::PriceLevel>& previousBids,
                                     const std::vector<data_ingestion::PriceLevel>& previousAsks,
                                     const std::vector<data_ingestion::PriceLevel>& currentBids,
                                     const std::vector<data_ingestion::PriceLevel>& currentAsks,
                                     double timeDeltaSec);
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
    // groupPriceLevelsInPlace
    virtual void groupPriceLevelsInPlace(
        std::vector<data_ingestion::PriceLevel>& levels,
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

    // New volatility methods
    virtual double calculateRV() const;
    virtual double calculateWeightedRV() const;
    virtual double calculateSpreadVolatility() const;
    virtual double calculateAdjustedRV(double rv_raw) const;
    virtual double calculateImbalance() const;
    virtual double getSmoothedRV(double rv_current) const;
    virtual double calculateSpreadRatio() const;
    virtual double calculateTrendRatio() const;
    virtual FilterResult evaluateVolatilityFilter(
        double rv_threshold = 0.02,
        double spread_threshold = 0.002,
        double min_volume_usd = 1000.0,
        double max_imbalance = 0.5,
        double max_trend_ratio = 0.3) const;

    virtual void notifyMetricsUpdated();

private:
    mutable std::vector<PriceLevel> cachedBids_;
    mutable std::vector<PriceLevel> cachedAsks_;
    mutable std::vector<PriceLevel> previousBids_;
    mutable std::vector<PriceLevel> previousAsks_;
    // Configuration
    core::ExchangeId exchangeId_;
    core::Symbol tradingPair_;
    int depthLevels_;
    double priceGroupingFactor_;
    int minSamplesForStability_;
    double emaAlpha_;
    ParameterSet params_;
    // State
    OrderBookMetrics currentMetrics_;
    std::deque<OrderBookMetrics> metricsHistory_;
    static constexpr size_t MAX_HISTORY_SIZE = 50; // Giảm từ 100
    
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
    
    // Volatility filter variables
    static constexpr double WINDOW_SIZE_SEC = 2.0;
    static constexpr size_t MAX_VOLATILITY_SAMPLES = 30;  // 60s / 2s
    static constexpr double EWMA_ALPHA = 0.2;
    
    mutable std::deque<OrderbookSnapshot> volatilitySnapshots_;
    mutable double rv_ema_ = 0.0;
    mutable bool ema_initialized_ = false;
    
    // Thread safety
    mutable std::mutex mutex_;

    // Helper methods
    void initializeHistoricalData(size_t capacity);
    void trimHistoricalData(size_t maxSize);
    void addVolatilitySnapshot(const OrderbookSnapshot& snapshot);

    // Per-analyzer regime detector to preserve smoothing state per symbol
    trading::strategy::core_analysis::OrderbookRegimeDetector regimeDetector_;

    // Diagnostics for detecting stale/unchanged snapshots
    size_t lastContentHash_ = 0;
    int repeatSnapshotCount_ = 0;
    int staleSnapshotThreshold_ = 50; // configurable via params (warmup/diagnostics)
    uint64_t lastSnapshotSequenceNumber_ = 0;
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
        const core::Symbol& symbol, 
        const core::ExchangeId& exchangeId,
        const ParameterSet& params = ParameterSet{});

    /**
     * Get existing analyzer (returns nullptr if not found)
     */
    std::shared_ptr<OrderBookAnalyzer> getAnalyzerForSymbol(
        const core::Symbol& symbol, 
        const core::ExchangeId& exchangeId) const;

    /**
     * Remove analyzer for specific symbol and exchange
     */
    void removeAnalyzer(const core::Symbol& symbol, const core::ExchangeId& exchangeId);

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

    std::string createKey(const core::Symbol& symbol, const core::ExchangeId& exchangeId) const;
};

/**
 * Factory function to create OrderBookAnalyzer instances
 */
std::shared_ptr<OrderBookAnalyzer> createOrderBookAnalyzer(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair,
    const ParameterSet& params = ParameterSet{});

} // namespace strategy
} // namespace trading