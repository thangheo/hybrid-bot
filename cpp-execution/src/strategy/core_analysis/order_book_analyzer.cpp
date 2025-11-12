/**
 * @file order_book_analyzer.cpp
 * @brief Implementation of order book metrics calculation
 * 
 * @author Trading System Team
 * @date May 6, 2025
 */

#include "strategy/core_analysis/order_book_analyzer.hpp"
#include "strategy/core_analysis/strategy_metrics.hpp"
#include "strategy/core_analysis/orderbook_regime_detector.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <thread>
#include <common/logger.hpp>
#include <iostream>
#include <format>

namespace trading {
namespace strategy {

// OrderBookAnalyzer Implementation

OrderBookAnalyzer::OrderBookAnalyzer(const core::ExchangeId& exchangeId, 
                                   const core::Symbol& tradingPair,
                                   const ParameterSet& params)
    : exchangeId_(exchangeId)
    , tradingPair_(tradingPair)
    , depthLevels_(params.getInt("depthLevels", 10))
    , priceGroupingFactor_(params.getDouble("priceGroupingFactor", 0.0001))
    , minSamplesForStability_(params.getInt("minSamplesForStability", 5))
    , emaAlpha_(params.getDouble("emaAlpha", 0.2))
    , params_(params)
    , lastUpdateTime_(std::chrono::system_clock::now()) {
    
    initializeHistoricalData(100);
}

OrderBookAnalyzer::OrderBookAnalyzer(const core::ExchangeId& exchangeId, 
                                   const core::Symbol& tradingPair,
                                   int depthLevels, 
                                   double priceGroupingFactor)
    : exchangeId_(exchangeId)
    , tradingPair_(tradingPair)
    , depthLevels_(depthLevels)
    , priceGroupingFactor_(priceGroupingFactor)
    , minSamplesForStability_(5)
    , emaAlpha_(0.2)
    , lastUpdateTime_(std::chrono::system_clock::now()) {
    
    initializeHistoricalData(100);
}

OrderBookAnalyzer::OrderBookAnalyzer(const OrderBookAnalyzerConfig& config)
    : exchangeId_(core::ExchangeId::binance()) // Default exchange, should be configurable
    , tradingPair_(core::Symbol::fromString("BTC/USD")) // Should be part of config
    , depthLevels_(config.depth)
    , priceGroupingFactor_(config.priceGroupingFactor)
    , minSamplesForStability_(config.minSamplesForStability)
    , emaAlpha_(config.emaAlpha)
    , lastUpdateTime_(std::chrono::system_clock::now()) {
    
    initializeHistoricalData(config.historySize);
    
    // Store config values in parameters
    params_.setParameter("priceWeightFactor", config.priceWeightFactor);
    params_.setParameter("volumeWeightFactor", config.volumeWeightFactor);
    params_.setParameter("imbalanceThreshold", config.imbalanceThreshold);
    params_.setParameter("liquidityPressureThreshold", config.liquidityPressureThreshold);
    params_.setParameter("orderFlowThreshold", config.orderFlowThreshold);
    params_.setParameter("historySize", static_cast<double>(config.historySize));
    
    // Initialize current metrics
    currentMetrics_.exchangeId = exchangeId_;
    currentMetrics_.symbol = tradingPair_;
}

void OrderBookAnalyzer::processOrderBook(const data_ingestion::OrderBook& snapshot) {
    if (!snapshot.isValid()) {
        std::cout << std::format("DBG: OrderBookAnalyzer - received invalid snapshot for {} on {}\n",
                                 snapshot.getSymbol().toString(), snapshot.getExchangeId().toString());
        return;
    }
    
    OrderBookMetrics metricsToNotify;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Get price levels limited to our depth
        // auto bids = snapshot.getBids(depthLevels_);
        // auto asks = snapshot.getAsks(depthLevels_);
        cachedBids_.clear();
        cachedAsks_.clear();
        
        cachedBids_ = snapshot.getBids(depthLevels_);
        cachedAsks_ = snapshot.getAsks(depthLevels_);      

        // Diagnostic: if empty, print a message
        if (cachedBids_.empty() || cachedAsks_.empty()) {
            std::cout << std::format("DBG: OrderBookAnalyzer - empty bids={} asks={} for {} on {}\n",
                                     cachedBids_.size(), cachedAsks_.size(), snapshot.getSymbol().toString(), snapshot.getExchangeId().toString());
        }
        
        // Capture pre-grouping counts and volumes for warm-up decision. Grouping
        // can collapse many levels into fewer aggregated groups; use the raw
        // snapshot counts so we don't falsely consider the book incomplete.
        size_t preGroupBidCount = cachedBids_.size();
        size_t preGroupAskCount = cachedAsks_.size();
        double preGroupTotalBidVol = 0.0;
        for (const auto &lvl : cachedBids_) preGroupTotalBidVol += lvl.quantity;
        double preGroupTotalAskVol = 0.0;
        for (const auto &lvl : cachedAsks_) preGroupTotalAskVol += lvl.quantity;

        if (priceGroupingFactor_ > 0.0) {
            // Support an optional relative grouping factor: priceGroupingRelative is
            // a fraction of the mid price (e.g. 0.0001 = 0.01%). If provided and
            // mid price is known, compute an effective absolute grouping factor.
            double effectiveGroupingFactor = priceGroupingFactor_;
            double rel = params_.getDouble("priceGroupingRelative", -1.0);
            double midPricePreGroup = 0.0;
            if (preGroupBidCount > 0 && preGroupAskCount > 0) {
                midPricePreGroup = (cachedBids_[0].price + cachedAsks_[0].price) / 2.0;
            }

            if (rel > 0.0 && midPricePreGroup > 0.0) {
                effectiveGroupingFactor = midPricePreGroup * rel;
            }

            // Temporarily override the member grouping factor while we call the
            // in-place grouping so we don't need to change the group helper
            double savedGrouping = priceGroupingFactor_;
            priceGroupingFactor_ = effectiveGroupingFactor;
            groupPriceLevelsInPlace(cachedBids_, true);  // In-place modification
            groupPriceLevelsInPlace(cachedAsks_, false); // In-place modification
            priceGroupingFactor_ = savedGrouping;

        }

        // STARTUP WARM-UP: skip publishing metrics until we have a minimum
        // amount of depth on both sides and non-zero volume. Use pre-group
        // counts so grouping doesn't cause false negatives. Threshold is
        // configurable via params_.getInt("warmupMinLevels", 5).
        int warmupMinLevels = params_.getInt("warmupMinLevels", 5);

        // Require both sides to have at least warmupMinLevels AND non-zero volume
        if (!(preGroupBidCount >= static_cast<size_t>(warmupMinLevels)
              && preGroupAskCount >= static_cast<size_t>(warmupMinLevels)
              && preGroupTotalBidVol > 0.0
              && preGroupTotalAskVol > 0.0)) {
            std::cout << std::format("DBG: OrderBookAnalyzer - warmup skip bids={} asks={} bidVol={} askVol={} requiredLevels={} for {} on {}\n",
                                     preGroupBidCount, preGroupAskCount, preGroupTotalBidVol, preGroupTotalAskVol,
                                     warmupMinLevels, snapshot.getSymbol().toString(), snapshot.getExchangeId().toString());
            // Do not publish metrics yet; wait for fuller book
            return;
        }

        // Ensure we calculate basic price metrics (midPrice, spread) before volume metrics
        calculatePriceMetrics(cachedBids_, cachedAsks_);

        // Initialize metrics with snapshot metadata
        currentMetrics_.exchangeId = snapshot.getExchangeId();
        currentMetrics_.symbol = snapshot.getSymbol();
        currentMetrics_.timestamp = snapshot.getTimestamp();
        currentMetrics_.sequenceNumber = snapshot.getSequenceNumber();
        
        // Calculate all metrics
        calculateVolumeMetrics(cachedBids_, cachedAsks_, depthLevels_);
        // calculateVolumeMetrics(cachedBids_, cachedAsks_, depthLevels_); // Twice for robustness? really?
        calculateLevelMetrics(cachedBids_, cachedAsks_);
        calculateImbalanceMetrics(cachedBids_, cachedAsks_);
        
        // Add volatility snapshot
        if (!cachedBids_.empty() && !cachedAsks_.empty()) {
            OrderbookSnapshot volSnapshot(
                cachedBids_[0].price,    // best_bid
                cachedAsks_[0].price,    // best_ask
                cachedBids_[0].quantity, // bid_volume
                cachedAsks_[0].quantity, // ask_volume
                std::chrono::duration_cast<std::chrono::microseconds>(
                    snapshot.getTimestamp().time_since_epoch()).count()
            );
            addVolatilitySnapshot(volSnapshot);
        }
        
        // Calculate new volatility metrics
        if (volatilitySnapshots_.size() >= 2) {
            double rv_raw = calculateRV();
            currentMetrics_.realize_volatility_1m = rv_raw;
            currentMetrics_.rv_weighted = calculateWeightedRV();
            currentMetrics_.rv_adjusted = calculateAdjustedRV(rv_raw);
            currentMetrics_.rv_smoothed = getSmoothedRV(currentMetrics_.rv_adjusted);
            currentMetrics_.spread_volatility = calculateSpreadVolatility();
            currentMetrics_.trend_ratio = calculateTrendRatio();
        }
        
        // Update time series and derived metrics
        updateTimeSeries();
        calculateDerivedMetrics();
        calculateTimeBasedMetrics();
        // Calculate flow metrics
        calculateFlowMetrics(previousBids_, previousAsks_, cachedBids_, cachedAsks_,
                             std::chrono::duration<double>(snapshot.getTimestamp() - lastUpdateTime_).count());
        // Store current bids/asks as previous for next update
        previousBids_ = cachedBids_;
        previousAsks_ = cachedAsks_;

        // Update the metrics map for flexible access
        currentMetrics_.updateMetricsMap();
        // Store a copy of the current metrics in history but strip heavy
        // time-series payload to avoid keeping large duplicated vectors
        // inside the history which can cause memory growth over time.
        OrderBookMetrics historyEntry = currentMetrics_;
        historyEntry.timeSeriesMetrics.clear();
        metricsHistory_.push_back(std::move(historyEntry));
        
        // Properly prune metrics history to prevent unbounded growth
        while (metricsHistory_.size() > MAX_HISTORY_SIZE) {
            metricsHistory_.pop_front();
        }

        // Periodic cleanup and memory optimization
        static size_t cleanup_counter = 0;
        if (++cleanup_counter % 1000 == 0) {
            metricsHistory_.shrink_to_fit();
            // Also cleanup other historical vectors
            trimHistoricalData(MAX_HISTORY_SIZE);
        }
        //print some debug info
        static size_t debug_counter = 0;
        if (++debug_counter % 5000 == 0) {
            std::cout << "OrderBookAnalyzer Debug: Processed " << debug_counter << " snapshots for "
                      << tradingPair_.toString() << " on " << exchangeId_.toString() << std::endl;
        }
        //print some derrivative info
        // if (debug_counter % 1000 == 0) {
        //     std::cout << "Metrics for " << tradingPair_.toString() << " on " << exchangeId_.toString()
        //               << "  MidPrice: " << currentMetrics_.midPrice 
        //               << ", Spread: " << currentMetrics_.spread 
        //               << ", Volume Imbalance: " << currentMetrics_.volumeImbalance 
        //               << ", Liquidity Pressure: " << currentMetrics_.liquidityPressure
        //               << ", Volatility Expectation: " << currentMetrics_.volatilityExpectation
        //               << std::endl;
        // }
        lastUpdateTime_ = snapshot.getTimestamp();
        metricsToNotify = currentMetrics_;
        // --- Inline Orderbook-based regime detection ---
        try {
            // Prepare samples for detector (use recent history)
            std::deque<double> vol_samples;
            std::deque<double> spread_samples;
            std::deque<double> price_returns;

            // Use historical mid prices to compute returns and volatility-like samples
            const size_t max_samples = 60;
            size_t mid_count = historicalMidPrices_.size();
            if (mid_count >= 2) {
                size_t start = (mid_count > max_samples) ? mid_count - max_samples : 1;
                // price_returns: signed log-returns
                for (size_t i = (mid_count > max_samples ? mid_count - max_samples : 1); i < mid_count; ++i) {
                    double prev = historicalMidPrices_[i-1];
                    double cur = historicalMidPrices_[i];
                    if (prev > 0 && cur > 0) {
                        double lr = std::log(cur / prev);
                        price_returns.push_back(lr);
                        vol_samples.push_back(std::fabs(lr));
                    }
                }
            }

            // spread_samples: use historicalSpreads_ tail
            size_t spread_count = historicalSpreads_.size();
            size_t sstart = (spread_count > max_samples) ? spread_count - max_samples : 0;
            for (size_t i = sstart; i < spread_count; ++i) spread_samples.push_back(historicalSpreads_[i]);

            // Build microstructure view expected by detector
            MarketMicrostructure micro{};
            micro.volatility_1m = currentMetrics_.realize_volatility_1m;
            micro.spread_ema = currentMetrics_.emaSpread > 0.0 ? currentMetrics_.emaSpread : currentMetrics_.spread;
            micro.orderbook_imbalance = currentMetrics_.volumeImbalance;
            micro.trade_intensity = currentMetrics_.bidFlow + currentMetrics_.askFlow;
            micro.price_impact = 0.0;
            micro.tick_size = params_.getDouble("tickSize", 0.0);
            micro.volume_profile_skew = currentMetrics_.priceLevelImbalance;

            trading::strategy::core_analysis::OrderbookRegimeDetector detector;
            auto regime = detector.detect(micro, vol_samples, spread_samples, price_returns);

            // Expose numeric regime in metrics for downstream components
            currentMetrics_.metrics["marketRegime"] = static_cast<double>(static_cast<int>(regime));
            // Optionally also store a small diagnostic value
            currentMetrics_.metrics["regime_confidence_dummy"] = 0.0;
        } catch (const std::exception& e) {
            LOG_DEBUG("OrderBookAnalyzer: regime detection failed: {}", e.what());
        }
        // Avoid sending large time-series data in the notification copy. Callers
        // interested in time series should query the analyzer directly.
        metricsToNotify.timeSeriesMetrics.clear();
    }
    
    // Publish event outside of lock to prevent deadlocks
    common::EventBus::getInstance().publish(events::MetricsUpdatedEvent{metricsToNotify});
    
    notifyMetricsUpdated();
}

void OrderBookAnalyzer::calculateFlowMetrics(const std::vector<data_ingestion::PriceLevel>& previousBids,
                                     const std::vector<data_ingestion::PriceLevel>& previousAsks,
                                     const std::vector<data_ingestion::PriceLevel>& currentBids,
                                     const std::vector<data_ingestion::PriceLevel>& currentAsks,
                                     double timeDeltaSec) {
    if (previousBids.empty() || previousAsks.empty() || currentBids.empty() || currentAsks.empty()) {
        return;
    }

    // Calculate bid flow
    double bidFlow = 0.0;
    for (const auto& bid : currentBids) {
        bidFlow += bid.quantity;
    }
    currentMetrics_.bidFlow = bidFlow;

    // Calculate ask flow
    double askFlow = 0.0;
    for (const auto& ask : currentAsks) {
        askFlow += ask.quantity;
    }
    currentMetrics_.askFlow = askFlow;

    // Update the flow metrics
    //currentMetrics_.updateMetricsMap();
}


OrderBookMetrics OrderBookAnalyzer::getMetrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return currentMetrics_;
}

double OrderBookAnalyzer::getMetric(const std::string& metricName, int depth) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Handle depth-specific metrics first
    if (metricName == "bidVolume" && depth > 0 && 
        depth <= static_cast<int>(currentMetrics_.bidVolumesByLevel.size())) {
        return currentMetrics_.bidVolumesByLevel[depth - 1];
    }
    
    if (metricName == "askVolume" && depth > 0 && 
        depth <= static_cast<int>(currentMetrics_.askVolumesByLevel.size())) {
        return currentMetrics_.askVolumesByLevel[depth - 1];
    }
    
    if (metricName == "cumulativeBidVolume" && depth > 0 && 
        depth <= static_cast<int>(currentMetrics_.cumulativeBidVolumes.size())) {
        return currentMetrics_.cumulativeBidVolumes[depth - 1];
    }
    
    if (metricName == "cumulativeAskVolume" && depth > 0 && 
        depth <= static_cast<int>(currentMetrics_.cumulativeAskVolumes.size())) {
        return currentMetrics_.cumulativeAskVolumes[depth - 1];
    }
    
    // Handle common metrics directly from member variables
    if (metricName == "mid_price" || metricName == "midPrice" || metricName == "price") {
        return currentMetrics_.midPrice;
    }
    if (metricName == "spread") {
        return currentMetrics_.spread;
    }
    if (metricName == "spread_percent" || metricName == "spreadPercent") {
        return currentMetrics_.spreadPercent;
    }
    if (metricName == "volume_imbalance" || metricName == "volumeImbalance") {
        return currentMetrics_.volumeImbalance;
    }
    
    // Fall back to metrics map
    return currentMetrics_.getMetric(metricName, 0.0);
}
 

void OrderBookAnalyzer::registerMetricsCallback(MetricsCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    metricsCallbacks_.push_back(std::move(callback));
}

bool OrderBookAnalyzer::hasStableMetrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metricsHistory_.size() >= static_cast<size_t>(minSamplesForStability_);
}

void OrderBookAnalyzer::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Clear all historical data
    metricsHistory_.clear();
    historicalMidPrices_.clear();
    historicalSpreads_.clear();
    historicalVolumes_.clear();
    historicalImbalances_.clear();
    priceHistory_.clear();
    historicalTimestamps_.clear();
    
    // Reset EMAs
    emaMidPrice_ = 0.0;
    emaSpread_ = 0.0;
    emaVolumeImbalance_ = 0.0;
    
    // Reset volatility data
    volatilitySnapshots_.clear();
    rv_ema_ = 0.0;
    ema_initialized_ = false;
    
    // Reset current metrics
    currentMetrics_ = OrderBookMetrics{};
    currentMetrics_.exchangeId = exchangeId_;
    currentMetrics_.symbol = core::Symbol(tradingPair_);
    
    lastUpdateTime_ = std::chrono::system_clock::now();
}

void OrderBookAnalyzer::updateParameters(const ParameterSet& params) {
    OrderBookMetrics metricsCopy;
    std::vector<MetricsCallback> callbacks;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        params_ = params;
        
        // Update configuration parameters
        depthLevels_ = params_.getInt("depthLevels", depthLevels_);
        priceGroupingFactor_ = params_.getDouble("priceGroupingFactor", priceGroupingFactor_);
        minSamplesForStability_ = params_.getInt("minSamplesForStability", minSamplesForStability_);
        emaAlpha_ = params_.getDouble("emaAlpha", emaAlpha_);
        
        metricsCopy = currentMetrics_;
        callbacks = metricsCallbacks_;
    }
    
    // Notify callbacks outside of lock
    for (const auto& callback : callbacks) {
        try {
            callback(metricsCopy);
        } catch (const std::exception& e) {
            // Log exception but continue
        }
    }
}

void OrderBookAnalyzer::calculatePriceMetrics(
    const std::vector<data_ingestion::PriceLevel>& bids,
    const std::vector<data_ingestion::PriceLevel>& asks) {
    
    if (bids.empty() || asks.empty()) {
        return;
    }
    
    const double bestBid = bids[0].price;
    const double bestAsk = asks[0].price;
    
    // Calculate basic price metrics
    currentMetrics_.bidPrice = bestBid;
    currentMetrics_.askPrice = bestAsk;
    currentMetrics_.midPrice = (bestBid + bestAsk) / 2.0;
    currentMetrics_.spread = bestAsk - bestBid;
    
    if (currentMetrics_.midPrice > 0) {
        // currentMetrics_. = (currentMetrics_.spread / currentMetrics_.midPrice) * 100.0;
        currentMetrics_.spreadPercent = (currentMetrics_.spread / currentMetrics_.midPrice) * 100.0;
    }
    
    // Update EMAs
    if (emaMidPrice_ == 0.0) {
        emaMidPrice_ = currentMetrics_.midPrice;
    } else {
        emaMidPrice_ = StrategyMetrics::updateEMA(emaMidPrice_, currentMetrics_.midPrice, emaAlpha_);
    }
    
    if (emaSpread_ == 0.0) {
        emaSpread_ = currentMetrics_.spread;
    } else {
        emaSpread_ = StrategyMetrics::updateEMA(emaSpread_, currentMetrics_.spread, emaAlpha_);
    }
    
    // Store in metrics map for easy access
    currentMetrics_.metrics["midPrice"] = currentMetrics_.midPrice;
    currentMetrics_.metrics["bidPrice"] = bestBid;
    currentMetrics_.metrics["askPrice"] = bestAsk;    
    // bidVolume and askVolume are set in volume metrics
    currentMetrics_.metrics["bidVolume"] = currentMetrics_.totalBidVolume;
    currentMetrics_.metrics["askVolume"] = currentMetrics_.totalAskVolume;
    currentMetrics_.metrics["spread"] = currentMetrics_.spread;
    currentMetrics_.metrics["spreadPercent"] = currentMetrics_.spreadPercent;
    currentMetrics_.metrics["bestBid"] = bestBid;
    currentMetrics_.metrics["bestAsk"] = bestAsk;
    // Expose EMA members both via the metrics map and the typed members
    currentMetrics_.metrics["emaMidPrice"] = emaMidPrice_;
    currentMetrics_.metrics["emaSpread"] = emaSpread_;
    // Ensure the typed member is populated so external callers using
    // OrderBookMetrics::emaSpread (e.g. m.emaSpread) receive the value.
    currentMetrics_.emaSpread = emaSpread_;
    // Guard against division by zero when computing average spread percent
    if (currentMetrics_.midPrice > 0.0) {
        currentMetrics_.metrics["averageSpreadPercent"] = (emaSpread_ / currentMetrics_.midPrice) * 100.0;
    } else {
        currentMetrics_.metrics["averageSpreadPercent"] = 0.0;
    }

    // Update the flow metrics
    //currentMetrics_.updateMetricsMap();
}

void OrderBookAnalyzer::calculateVolumeMetrics(
    const std::vector<data_ingestion::PriceLevel>& bids,
    const std::vector<data_ingestion::PriceLevel>& asks,
    int depth) {
    
    // Resize level-specific arrays
    currentMetrics_.bidVolumesByLevel.resize(depth, 0.0);
    currentMetrics_.askVolumesByLevel.resize(depth, 0.0);
    currentMetrics_.cumulativeBidVolumes.resize(depth, 0.0);
    currentMetrics_.cumulativeAskVolumes.resize(depth, 0.0);
    
    // Calculate bid volumes
    currentMetrics_.totalBidVolume = 0.0;
    for (size_t i = 0; i < bids.size() && i < static_cast<size_t>(depth); ++i) {
        const double levelVolume = bids[i].quantity;
        currentMetrics_.bidVolumesByLevel[i] = levelVolume;
        currentMetrics_.totalBidVolume += levelVolume;
        
        currentMetrics_.cumulativeBidVolumes[i] = 
            (i == 0) ? levelVolume : currentMetrics_.cumulativeBidVolumes[i-1] + levelVolume;
    }
    
    // Calculate ask volumes
    currentMetrics_.totalAskVolume = 0.0;
    for (size_t i = 0; i < asks.size() && i < static_cast<size_t>(depth); ++i) {
        const double levelVolume = asks[i].quantity;
        currentMetrics_.askVolumesByLevel[i] = levelVolume;
        currentMetrics_.totalAskVolume += levelVolume;
        
        currentMetrics_.cumulativeAskVolumes[i] = 
            (i == 0) ? levelVolume : currentMetrics_.cumulativeAskVolumes[i-1] + levelVolume;
    }
    
    // Calculate bid/ask ratio
    if (currentMetrics_.totalAskVolume > 0) {
        currentMetrics_.bidAskVolumeRatio = currentMetrics_.totalBidVolume / currentMetrics_.totalAskVolume;
    } else {
        currentMetrics_.bidAskVolumeRatio = currentMetrics_.totalBidVolume > 0 ? 10.0 : 1.0;
    }
    
    // Store in metrics map
    currentMetrics_.metrics["bidVolume"] = currentMetrics_.totalBidVolume;
    currentMetrics_.metrics["askVolume"] = currentMetrics_.totalAskVolume;
    currentMetrics_.metrics["bidLevelCount"] = static_cast<double>(bids.size());
    currentMetrics_.metrics["askLevelCount"] = static_cast<double>(asks.size());
    currentMetrics_.metrics["bidAskVolumeRatio"] = currentMetrics_.bidAskVolumeRatio;
    // Store level-specific volumes
    for (size_t i = 0; i < currentMetrics_.bidVolumesByLevel.size(); ++i) {
        currentMetrics_.metrics[std::format("bidVolumeLevel_{}", i)] = currentMetrics_.bidVolumesByLevel[i];
    }
    for (size_t i = 0; i < currentMetrics_.askVolumesByLevel.size(); ++i) {
        currentMetrics_.metrics[std::format("askVolumeLevel_{}", i)] = currentMetrics_.askVolumesByLevel[i];
    }
    for (size_t i = 0; i < currentMetrics_.cumulativeBidVolumes.size(); ++i) {
        currentMetrics_.metrics[std::format("cumulativeBidVolumeLevel_{}", i)] = currentMetrics_.cumulativeBidVolumes[i];
    }
    for (size_t i = 0; i < currentMetrics_.cumulativeAskVolumes.size(); ++i) {
        currentMetrics_.metrics[std::format("cumulativeAskVolumeLevel_{}", i)] = currentMetrics_.cumulativeAskVolumes[i];
    }

    // Update the flow metrics
    //currentMetrics_.updateMetricsMap();


}

void OrderBookAnalyzer::calculateLevelMetrics(
    const std::vector<data_ingestion::PriceLevel>& bids,
    const std::vector<data_ingestion::PriceLevel>& asks) {
    
    // Calculate price level densities
    if (bids.size() > 1) {
        const double priceRange = bids.front().price - bids.back().price;
        if (priceRange > 0) {
            currentMetrics_.bidDensity = static_cast<double>(bids.size()) / priceRange;
        }
    }
    
    if (asks.size() > 1) {
        const double priceRange = asks.back().price - asks.front().price;
        if (priceRange > 0) {
            currentMetrics_.askDensity = static_cast<double>(asks.size()) / priceRange;
        }
    }
    
    // Calculate price level imbalance
    if (currentMetrics_.askDensity > 0) {
        currentMetrics_.priceLevelImbalance = 
            currentMetrics_.bidDensity / currentMetrics_.askDensity - 1.0;
    }
    
    currentMetrics_.metrics["bidDensity"] = currentMetrics_.bidDensity;
    currentMetrics_.metrics["askDensity"] = currentMetrics_.askDensity;
    currentMetrics_.metrics["priceLevelImbalance"] = currentMetrics_.priceLevelImbalance;

    // Update the flow metrics
    //currentMetrics_.updateMetricsMap();

}

void OrderBookAnalyzer::calculateImbalanceMetrics(
    const std::vector<data_ingestion::PriceLevel>& bids,
    const std::vector<data_ingestion::PriceLevel>& asks) {
    
    // Simple volume imbalance
    const double totalVolume = currentMetrics_.totalBidVolume + currentMetrics_.totalAskVolume;
    if (totalVolume > 0) {
        currentMetrics_.volumeImbalance = 
            (currentMetrics_.totalBidVolume - currentMetrics_.totalAskVolume) / totalVolume;
    }
    
    // Weighted volume imbalance
    auto linearWeight = [](int level) { return 1.0 / (level + 1); };
    currentMetrics_.weightedVolumeImbalance = calculateWeightedImbalance(bids, asks, linearWeight);
    
    // Depth imbalance calculation
    double bidDepthScore = 0.0;
    double askDepthScore = 0.0;
    
    if (!bids.empty() && !asks.empty()) {
        const double refPrice = (bids[0].price + asks[0].price) / 2.0;
        
        for (size_t i = 0; i < std::min(bids.size(), size_t(5)); ++i) {
            const double priceDistance = (refPrice - bids[i].price) / refPrice;
            bidDepthScore += bids[i].quantity / (1.0 + 10.0 * priceDistance);
        }
        
        for (size_t i = 0; i < std::min(asks.size(), size_t(5)); ++i) {
            const double priceDistance = (asks[i].price - refPrice) / refPrice;
            askDepthScore += asks[i].quantity / (1.0 + 10.0 * priceDistance);
        }
        
        const double totalDepth = bidDepthScore + askDepthScore;
        if (totalDepth > 0) {
            currentMetrics_.depthImbalance = (bidDepthScore - askDepthScore) / totalDepth;
        }
    }
    
    // Composite pressure imbalance
    currentMetrics_.pressureImbalance = 
        0.4 * currentMetrics_.volumeImbalance + 
        0.4 * currentMetrics_.weightedVolumeImbalance + 
        0.2 * currentMetrics_.depthImbalance;
    
    // Update EMA of volume imbalance
    if (emaVolumeImbalance_ == 0.0) {
        emaVolumeImbalance_ = currentMetrics_.volumeImbalance;
    } else {
        emaVolumeImbalance_ = StrategyMetrics::updateEMA(
            emaVolumeImbalance_, currentMetrics_.volumeImbalance, emaAlpha_);
    }
    
    currentMetrics_.metrics["volumeImbalance "] = currentMetrics_.volumeImbalance;
    currentMetrics_.metrics["volatility"] = currentMetrics_.realizedVolatility;
    currentMetrics_.metrics["liquidityPressure"] = currentMetrics_.liquidityPressure;
    currentMetrics_.metrics["emaVolumeImbalance"] = emaVolumeImbalance_;
    currentMetrics_.metrics["averageVolumeImbalance"] = emaVolumeImbalance_;
    currentMetrics_.metrics["pressureImbalance"] = currentMetrics_.pressureImbalance;
    currentMetrics_.metrics["volatilityExpectation"] = currentMetrics_.volatilityExpectation;
    currentMetrics_.metrics["realizedVolatility"] = currentMetrics_.realizedVolatility;
    currentMetrics_.metrics["liquidityProfileScore"] = currentMetrics_.liquidityProfileScore;
    
    // Update the flow metrics
    //currentMetrics_.updateMetricsMap();

}

void OrderBookAnalyzer::updateTimeSeries() {
    // Add new data points
    historicalMidPrices_.push_back(currentMetrics_.midPrice);
    historicalSpreads_.push_back(currentMetrics_.spread);
    historicalVolumes_.push_back(currentMetrics_.totalBidVolume + currentMetrics_.totalAskVolume);
    historicalImbalances_.push_back(currentMetrics_.volumeImbalance);
    
    // Trim to maintain size limit using the same constant as metricsHistory_
    trimHistoricalData(MAX_HISTORY_SIZE);
    
    // Update time series in metrics for external access
    currentMetrics_.timeSeriesMetrics["midPrice"] = historicalMidPrices_;
    currentMetrics_.timeSeriesMetrics["spread"] = historicalSpreads_;
    currentMetrics_.timeSeriesMetrics["volume"] = historicalVolumes_;
    currentMetrics_.timeSeriesMetrics["imbalance"] = historicalImbalances_;
    currentMetrics_.timeSeriesMetrics["timestamp"] = historicalTimestamps_;

    // Update the flow metrics
    //currentMetrics_.updateMetricsMap();
}

void OrderBookAnalyzer::calculateDerivedMetrics() {
    currentMetrics_.marketDirectionalCoefficient = calculateDirectionalCoefficient();
    currentMetrics_.volatilityExpectation = calculateVolatilityExpectation();
    currentMetrics_.realizedVolatility = calculateRealizedVolatility(60.0); // 1-minute interval
    currentMetrics_.volatilityPerUnitTime = 
        currentMetrics_.realizedVolatility / (1.0 + std::chrono::duration<double>(
            currentMetrics_.timestamp - lastUpdateTime_).count());
    currentMetrics_.liquidityPressure = std::abs(currentMetrics_.pressureImbalance);
    // Composite liquidity score
    const double bidLiquidity = currentMetrics_.totalBidVolume * (1.0 + currentMetrics_.bidDensity);
    const double askLiquidity = currentMetrics_.totalAskVolume * (1.0 + currentMetrics_.askDensity);
    currentMetrics_.liquidityProfileScore = (bidLiquidity + askLiquidity) / 2.0;
    // Update metrics map
    //currentMetrics_.updateMetricsMap();
}

void OrderBookAnalyzer::calculateTimeBasedMetrics() {
    if (metricsHistory_.size() < 2) {
        return;
    }
    
    const auto& prevMetrics = metricsHistory_.back();
    const double timeDiff = std::chrono::duration<double>(
        currentMetrics_.timestamp - prevMetrics.timestamp).count();
    
    if (timeDiff > 0) {
        // Calculate velocities
        currentMetrics_.priceVelocity = 
            (currentMetrics_.midPrice - prevMetrics.midPrice) / timeDiff;
        
        currentMetrics_.spreadVelocity = 
            (currentMetrics_.spread - prevMetrics.spread) / timeDiff;
        
        const double currentTotalVolume = currentMetrics_.totalBidVolume + currentMetrics_.totalAskVolume;
        const double prevTotalVolume = prevMetrics.totalBidVolume + prevMetrics.totalAskVolume;
        currentMetrics_.volumeVelocity = (currentTotalVolume - prevTotalVolume) / timeDiff;
    }
    //currentMetrics_.updateMetricsMap();

}
// groupPriceLevelsInPlace  
void OrderBookAnalyzer::groupPriceLevelsInPlace(
    std::vector<data_ingestion::PriceLevel>& levels,
    bool isBids) const {
    
    if (levels.empty() || priceGroupingFactor_ <= 0.0) {
        return;
    }
    
    std::map<double, double> priceGroups;
    
    for (const auto& level : levels) {
        double groupPrice;
        
        if (isBids) {
            groupPrice = std::floor(level.price / priceGroupingFactor_) * priceGroupingFactor_;
        } else {
            groupPrice = std::ceil(level.price / priceGroupingFactor_) * priceGroupingFactor_;
        }
        
        priceGroups[groupPrice] += level.quantity;
    }
    
    // Convert map back to vector
    levels.clear();
    levels.reserve(priceGroups.size());
    
    for (const auto& [price, quantity] : priceGroups) {
        levels.emplace_back(price, quantity);
    }
    
    // Sort appropriately
    if (isBids) {
        std::sort(levels.begin(), levels.end(), 
            [](const auto& a, const auto& b) { return a.price > b.price; });
    } else {
        std::sort(levels.begin(), levels.end(), 
            [](const auto& a, const auto& b) { return a.price < b.price; });
    }
}


std::vector<data_ingestion::PriceLevel> OrderBookAnalyzer::groupPriceLevels(
    const std::vector<data_ingestion::PriceLevel>& levels,
    bool isBids) const {
    
    if (levels.empty() || priceGroupingFactor_ <= 0.0) {
        return levels;
    }
    
    std::map<double, double> priceGroups;
    
    for (const auto& level : levels) {
        double groupPrice;
        
        if (isBids) {
            groupPrice = std::floor(level.price / priceGroupingFactor_) * priceGroupingFactor_;
        } else {
            groupPrice = std::ceil(level.price / priceGroupingFactor_) * priceGroupingFactor_;
        }
        
        priceGroups[groupPrice] += level.quantity;
    }
    
    // Convert map back to vector
    std::vector<data_ingestion::PriceLevel> groupedLevels;
    groupedLevels.reserve(priceGroups.size());
    
    for (const auto& [price, quantity] : priceGroups) {
        groupedLevels.emplace_back(price, quantity);
    }
    
    // Sort appropriately
    if (isBids) {
        std::sort(groupedLevels.begin(), groupedLevels.end(), 
            [](const auto& a, const auto& b) { return a.price > b.price; });
    } else {
        std::sort(groupedLevels.begin(), groupedLevels.end(), 
            [](const auto& a, const auto& b) { return a.price < b.price; });
    }
    
    return groupedLevels;
}

double OrderBookAnalyzer::calculateWeightedImbalance(
    const std::vector<data_ingestion::PriceLevel>& bids,
    const std::vector<data_ingestion::PriceLevel>& asks,
    const std::function<double(int)>& weightingFunction) const {
    
    double weightedBidVolume = 0.0;
    double weightedAskVolume = 0.0;
    
    // Process bid levels with weights
    for (size_t i = 0; i < bids.size(); ++i) {
        const double weight = weightingFunction(static_cast<int>(i));
        weightedBidVolume += bids[i].quantity * weight;
    }
    
    // Process ask levels with weights
    for (size_t i = 0; i < asks.size(); ++i) {
        const double weight = weightingFunction(static_cast<int>(i));
        weightedAskVolume += asks[i].quantity * weight;
    }
    
    // Calculate weighted imbalance
    const double totalWeightedVolume = weightedBidVolume + weightedAskVolume;
    if (totalWeightedVolume > 0) {
        return (weightedBidVolume - weightedAskVolume) / totalWeightedVolume;
    }
    
    return 0.0;
}

std::vector<int> OrderBookAnalyzer::detectLiquidityWalls(
    const std::vector<data_ingestion::PriceLevel>& levels,
    double threshold) const {
    
    if (levels.size() < 3) {
        return {};
    }
    
    // Calculate average volume
    const double avgVolume = std::accumulate(levels.begin(), levels.end(), 0.0,
        [](double sum, const auto& level) { return sum + level.quantity; }) / levels.size();
    
    std::vector<int> wallIndices;
    
    // Detect levels with volume above threshold * average
    for (size_t i = 0; i < levels.size(); ++i) {
        if (levels[i].quantity > threshold * avgVolume) {
            wallIndices.push_back(static_cast<int>(i));
        }
    }
    
    return wallIndices;
}

double OrderBookAnalyzer::calculateDirectionalCoefficient() const {
    constexpr size_t MIN_SAMPLES = 5;
    
    if (historicalMidPrices_.size() < MIN_SAMPLES || historicalImbalances_.size() < MIN_SAMPLES) {
        return 0.0;
    }
    
    // Calculate recent price trend
    const size_t sampleSize = std::min(historicalMidPrices_.size(), MIN_SAMPLES);
    const auto recentPrices = std::vector<double>(
        historicalMidPrices_.end() - sampleSize, historicalMidPrices_.end());
    
    double priceTrend = 0.0;
    for (size_t i = 1; i < recentPrices.size(); ++i) {
        priceTrend += recentPrices[i] - recentPrices[i-1];
    }
    priceTrend = (priceTrend > 0) ? 1.0 : (priceTrend < 0) ? -1.0 : 0.0;
    
    // Calculate recent imbalance average
    const auto recentImbalances = std::vector<double>(
        historicalImbalances_.end() - sampleSize, historicalImbalances_.end());
    
    const double avgImbalance = std::accumulate(
        recentImbalances.begin(), recentImbalances.end(), 0.0) / recentImbalances.size();
    
    // Combine trend and imbalance
    const double coefficient = 0.7 * avgImbalance + 0.3 * priceTrend;
    
    return std::clamp(coefficient, -1.0, 1.0);
}
// calculate realized volatility based on order book metrics
double OrderBookAnalyzer::calculateRealizedVolatility(double sample_interval_sec) const {
    const int min_periods = 5;   
    const int max_periods = 30;  
    
    auto it = currentMetrics_.timeSeriesMetrics.find("midPrice");
    if (it == currentMetrics_.timeSeriesMetrics.end()) {
        LOG_DEBUG("No midPrice history available, using default volatility");
        return 0.005; // Default 0.5%
    }
    
    const std::vector<double>& price_history = it->second;
    if (price_history.size() < min_periods) {
        LOG_DEBUG("Insufficient price history: {} points, need {}", 
                  price_history.size(), min_periods);
        return 0.005;
    }
    
    int periods_to_use = std::min(static_cast<int>(price_history.size()), max_periods);
    int start_idx = price_history.size() - periods_to_use;
    
    std::vector<double> returns;
    returns.reserve(periods_to_use - 1);
    
    for (int i = start_idx + 1; i < static_cast<int>(price_history.size()); ++i) {
        double prev_price = price_history[i-1];
        double curr_price = price_history[i];
        
        if (prev_price > 0 && curr_price > 0) {
            double log_ret = std::log(curr_price / prev_price);
            returns.push_back(log_ret);
        }
    }
    
    if (returns.size() < 2) {
        // LOG_WARN("Not enough valid returns for volatility");
        return 0.005;
    }
    
    double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    
    double variance = 0.0;
    for (double r : returns) {
        double diff = r - mean_return;
        variance += diff * diff;
    }
    variance /= (returns.size() - 1); // sample variance
    
    double sigma = std::sqrt(variance);
    
    // Annualization rule: volatility scales with sqrt(time)
    // Here: scale to per-second volatility
    double sigma_per_sec = sigma / std::sqrt(sample_interval_sec);
    
    return std::clamp(sigma_per_sec, 0.001, 0.05); // 0.1%–5%
}

// New volatility methods implementation

double OrderBookAnalyzer::calculateRV() const {
    if (volatilitySnapshots_.size() < 2) return 0.0;
    
    double sum_sq_returns = 0.0;
    
    for (size_t i = 1; i < volatilitySnapshots_.size(); ++i) {
        double mid_prev = (volatilitySnapshots_[i-1].best_bid + volatilitySnapshots_[i-1].best_ask) / 2.0;
        double mid_curr = (volatilitySnapshots_[i].best_bid + volatilitySnapshots_[i].best_ask) / 2.0;
        
        if (mid_prev > 0 && mid_curr > 0) {
            double log_return = std::log(mid_curr / mid_prev);
            sum_sq_returns += log_return * log_return;
        }
    }
    
    return std::sqrt(sum_sq_returns);
}

double OrderBookAnalyzer::calculateWeightedRV() const {
    if (volatilitySnapshots_.size() < 2) return 0.0;
    
    double total_volume = 0.0;
    for (const auto& snap : volatilitySnapshots_) {
        total_volume += snap.bid_volume + snap.ask_volume;
    }
    
    if (total_volume < 1e-8) return calculateRV();  // fallback
    
    double weighted_sum = 0.0;
    
    for (size_t i = 1; i < volatilitySnapshots_.size(); ++i) {
        double mid_prev = (volatilitySnapshots_[i-1].best_bid + volatilitySnapshots_[i-1].best_ask) / 2.0;
        double mid_curr = (volatilitySnapshots_[i].best_bid + volatilitySnapshots_[i].best_ask) / 2.0;
        
        if (mid_prev > 0 && mid_curr > 0) {
            double log_return = std::log(mid_curr / mid_prev);
            double volume = volatilitySnapshots_[i].bid_volume + volatilitySnapshots_[i].ask_volume;
            double weight = std::min(volume / total_volume, 0.1);  // clip at 0.1
            
            weighted_sum += weight * log_return * log_return;
        }
    }
    
    return std::sqrt(weighted_sum);
}

double OrderBookAnalyzer::calculateSpreadVolatility() const {
    if (volatilitySnapshots_.empty()) return 0.0;
    
    std::vector<double> spreads;
    spreads.reserve(volatilitySnapshots_.size());
    
    for (const auto& snap : volatilitySnapshots_) {
        double spread = snap.best_ask - snap.best_bid;
        spreads.push_back(spread);
    }
    
    double mean_spread = std::accumulate(spreads.begin(), spreads.end(), 0.0) / spreads.size();
    if (mean_spread < 1e-8) return 0.0;
    
    double variance = 0.0;
    for (double s : spreads) {
        double diff = s - mean_spread;
        variance += diff * diff;
    }
    variance /= spreads.size();
    
    return std::sqrt(variance) / mean_spread;  // normalized
}

double OrderBookAnalyzer::calculateAdjustedRV(double rv_raw) const {
    if (volatilitySnapshots_.empty()) return rv_raw;
    
    double sum_spread = 0.0;
    double sum_mid = 0.0;
    
    for (const auto& snap : volatilitySnapshots_) {
        double mid = (snap.best_bid + snap.best_ask) / 2.0;
        double spread = snap.best_ask - snap.best_bid;
        sum_spread += spread;
        sum_mid += mid;
    }
    
    double mean_spread = sum_spread / volatilitySnapshots_.size();
    double mean_mid = sum_mid / volatilitySnapshots_.size();
    
    if (mean_mid < 1e-8) return rv_raw;
    
    double spread_ratio = mean_spread / mean_mid;
    
    // Multiplicative correction
    double adjustment = 1.0 - (spread_ratio / 2.0);
    adjustment = std::max(0.5, std::min(1.0, adjustment));  // clip [0.5, 1.0]
    
    return rv_raw * adjustment;
}

double OrderBookAnalyzer::calculateImbalance() const {
    if (volatilitySnapshots_.empty()) return 0.0;
    
    double sum_bid_vol = 0.0;
    double sum_ask_vol = 0.0;
    
    for (const auto& snap : volatilitySnapshots_) {
        sum_bid_vol += snap.bid_volume;
        sum_ask_vol += snap.ask_volume;
    }
    
    double total_vol = sum_bid_vol + sum_ask_vol;
    if (total_vol < 1e-8) return 0.0;
    
    return (sum_bid_vol - sum_ask_vol) / total_vol;
}

double OrderBookAnalyzer::getSmoothedRV(double rv_current) const {
    if (!ema_initialized_) {
        rv_ema_ = rv_current;
        ema_initialized_ = true;
        return rv_ema_;
    }
    
    rv_ema_ = EWMA_ALPHA * rv_current + (1.0 - EWMA_ALPHA) * rv_ema_;
    return rv_ema_;
}

double OrderBookAnalyzer::calculateSpreadRatio() const {
    if (volatilitySnapshots_.empty()) return 0.0;
    
    double sum_spread_ratio = 0.0;
    
    for (const auto& snap : volatilitySnapshots_) {
        double mid = (snap.best_bid + snap.best_ask) / 2.0;
        if (mid > 1e-8) {
            double spread = snap.best_ask - snap.best_bid;
            sum_spread_ratio += spread / mid;
        }
    }
    
    return sum_spread_ratio / volatilitySnapshots_.size();
}

double OrderBookAnalyzer::calculateTrendRatio() const {
    if (volatilitySnapshots_.size() < 2) return 0.0;
    
    std::vector<double> returns;
    returns.reserve(volatilitySnapshots_.size() - 1);
    
    for (size_t i = 1; i < volatilitySnapshots_.size(); ++i) {
        double mid_prev = (volatilitySnapshots_[i-1].best_bid + volatilitySnapshots_[i-1].best_ask) / 2.0;
        double mid_curr = (volatilitySnapshots_[i].best_bid + volatilitySnapshots_[i].best_ask) / 2.0;
        
        if (mid_prev > 0 && mid_curr > 0) {
            returns.push_back(std::log(mid_curr / mid_prev));
        }
    }
    
    if (returns.empty()) return 0.0;
    
    double mean_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    double rv = calculateRV();
    
    if (rv < 1e-8) return 0.0;
    
    return std::abs(mean_return) / rv;
}

FilterResult OrderBookAnalyzer::evaluateVolatilityFilter(
    double rv_threshold,
    double spread_threshold,
    double min_volume_usd,
    double max_imbalance,
    double max_trend_ratio) const {
    
    FilterResult result{};
    
    if (volatilitySnapshots_.size() < MAX_VOLATILITY_SAMPLES) {
        result.should_trade = false;
        result.reason = "Insufficient samples";
        return result;
    }
    
    // Calculate metrics
    double rv_raw = calculateRV();
    result.rv = rv_raw;
    result.rv_adjusted = calculateAdjustedRV(rv_raw);
    
    result.rv_smoothed = getSmoothedRV(result.rv_adjusted);
    
    result.spread_ratio = calculateSpreadRatio();
    result.imbalance = calculateImbalance();
    result.trend_ratio = calculateTrendRatio();
    
    // Volume check
    if (!volatilitySnapshots_.empty()) {
        double mid = (volatilitySnapshots_.back().best_bid + volatilitySnapshots_.back().best_ask) / 2.0;
        double total_volume_usd = 0.0;
        for (const auto& snap : volatilitySnapshots_) {
            total_volume_usd += (snap.bid_volume + snap.ask_volume) * mid;
        }
        
        if (total_volume_usd < min_volume_usd) {
            result.should_trade = false;
            result.reason = "Insufficient volume";
            return result;
        }
    }
    
    // RV check
    if (result.rv_smoothed < rv_threshold) {
        result.should_trade = false;
        result.reason = "RV too low";
        return result;
    }
    
    // Spread check
    if (result.spread_ratio > spread_threshold) {
        result.should_trade = false;
        result.reason = "Spread too wide";
        return result;
    }
    
    // Imbalance check
    if (std::abs(result.imbalance) > max_imbalance) {
        result.should_trade = false;
        result.reason = "Imbalance too high";
        return result;
    }
    
    // Trend check
    if (result.trend_ratio > max_trend_ratio) {
        result.should_trade = false;
        result.reason = "Too trending";
        return result;
    }
    
    result.should_trade = true;
    result.reason = "All filters passed";
    return result;
}

void OrderBookAnalyzer::addVolatilitySnapshot(const OrderbookSnapshot& snapshot) {
    volatilitySnapshots_.push_back(snapshot);
    
    // Keep only last MAX_VOLATILITY_SAMPLES
    while (volatilitySnapshots_.size() > MAX_VOLATILITY_SAMPLES) {
        volatilitySnapshots_.pop_front();
    }
}


double OrderBookAnalyzer::calculateVolatilityExpectation() const {
    // Factors affecting expected volatility
    const double spreadFactor = currentMetrics_.spreadPercent / 0.1; // Normalize to typical spread
    const double imbalanceFactor = std::abs(currentMetrics_.volumeImbalance) * 2.0;
    
    // Calculate depth factor
    double depthFactor = 0.0;
    if (currentMetrics_.bidVolumesByLevel.size() >= 3 && 
        currentMetrics_.askVolumesByLevel.size() >= 3) {
        
        double topBidVolume = 0.0;
        double topAskVolume = 0.0;
        
        for (int i = 0; i < 3; ++i) {
            topBidVolume += currentMetrics_.bidVolumesByLevel[i];
            topAskVolume += currentMetrics_.askVolumesByLevel[i];
        }
        
        if (topBidVolume > 0 && topAskVolume > 0 && currentMetrics_.midPrice > 0) {
            const double avgTopVolume = (topBidVolume + topAskVolume) / 2.0;
            const double normalizedDepth = avgTopVolume / currentMetrics_.midPrice;
            depthFactor = 1.0 / (normalizedDepth + 0.1);
        }
    }
    
    const double velocityFactor = std::abs(currentMetrics_.priceVelocity) * 10.0;
    
    // Combine factors with weights
    const double volatilityExpectation = 
        0.3 * spreadFactor + 
        0.3 * imbalanceFactor + 
        0.2 * depthFactor + 
        0.2 * velocityFactor;
    
    return std::clamp(volatilityExpectation / 2.0, 0.0, 1.0);
}

void OrderBookAnalyzer::notifyMetricsUpdated() {
    std::vector<MetricsCallback> callbacks;
    OrderBookMetrics metricsCopy;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks = metricsCallbacks_;
        metricsCopy = currentMetrics_;
    }
    
    // Strip heavy time-series data before dispatching to external callbacks to
    // avoid accidental retention of large vectors by listeners.
    metricsCopy.timeSeriesMetrics.clear();

    // Execute callbacks asynchronously to prevent deadlocks
    for (const auto& callback : callbacks) {
        std::thread([callback, metricsCopy]() {
            try {
                callback(metricsCopy);
            } catch (const std::exception& e) {
                // Log error but continue
            }
        }).detach();
    }
}

void OrderBookAnalyzer::initializeHistoricalData(size_t capacity) {
    historicalMidPrices_.reserve(capacity);
    historicalSpreads_.reserve(capacity);
    historicalVolumes_.reserve(capacity);
    historicalImbalances_.reserve(capacity);
    priceHistory_.reserve(capacity);
    historicalTimestamps_.reserve(capacity);
}

void OrderBookAnalyzer::trimHistoricalData(size_t maxSize) {
    auto trimVector = [maxSize](std::vector<double>& vec) {
        if (vec.size() > maxSize) {
            // Remove excess elements from the front to maintain chronological order
            size_t excess = vec.size() - maxSize;
            vec.erase(vec.begin(), vec.begin() + excess);
        }
    };
    
    trimVector(historicalMidPrices_);
    trimVector(historicalSpreads_);
    trimVector(historicalVolumes_);
    trimVector(historicalImbalances_);
    trimVector(priceHistory_);
    trimVector(historicalTimestamps_);
    
}

// OrderBookAnalyzerManager Implementation

OrderBookAnalyzerManager::OrderBookAnalyzerManager(const ParameterSet& params) 
    : params_(params) {}
// In OrderBookAnalyzerManager class
OrderBookAnalyzerManager::OrderBookAnalyzerManager(const OrderBookAnalyzerConfig& config) {
    // Convert config to ParameterSet if needed

    //  int depth =20;                        // Default depth to analyze
    //  int historySize = 1000;                // Size of history to maintain
    //  double priceWeightFactor = 0.5;        // Weight factor for price-weighted metrics
    //  // Default price weight factor
    //     double volumeWeightFactor = 0.5;       // Weight factor for volume-weighted metrics
    //     double imbalanceThreshold = 0.6;       // Threshold for imbalance detection
    //     double liquidityPressureThreshold = 0.7; // Threshold for liquidity pressure detection
    //     double orderFlowThreshold = 0.5;       // Threshold for order flow detection
    // // ADD THESE MISSING ONES:
    //     double priceGroupingFactor = 0.0001;
    //     int minSamplesForStability = 5;
    //     double emaAlpha = 0.2;
    params_.setParameter("depth", config.depth); //we contruct orderbook with depth 
    params_.setParameter("priceWeightFactor", config.priceWeightFactor);
    params_.setParameter("minSamplesForStability", config.minSamplesForStability);
    params_.setParameter("priceGroupingFactor", config.priceGroupingFactor);
    params_.setParameter("emaAlpha", config.emaAlpha);
    params_.setParameter("volumeWeightFactor", config.volumeWeightFactor);
    params_.setParameter("imbalanceThreshold", config.imbalanceThreshold);
    params_.setParameter("liquidityPressureThreshold", config.liquidityPressureThreshold);
    params_.setParameter("orderFlowThreshold", config.orderFlowThreshold);
    params_.setParameter("historySize", config.historySize);
    // Add other parameters as needed
}

std::shared_ptr<OrderBookAnalyzer> OrderBookAnalyzerManager::getAnalyzerForSymbol(
    const core::Symbol& symbol, 
    const core::ExchangeId& exchangeId,
    const ParameterSet& params) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    const std::string key = createKey(symbol, exchangeId);
    auto it = analyzers_.find(key);
    
    if (it != analyzers_.end()) {
        return it->second;
    }
    
    // Create new analyzer
    auto analyzer = std::make_shared<OrderBookAnalyzer>(exchangeId, symbol, params);
    analyzers_[key] = analyzer;
    
    return analyzer;
}

std::shared_ptr<OrderBookAnalyzer> OrderBookAnalyzerManager::getAnalyzerForSymbol(
    const core::Symbol& symbol, 
    const core::ExchangeId& exchangeId) const {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    const std::string key = createKey(symbol, exchangeId);
    auto it = analyzers_.find(key);
    
    return (it != analyzers_.end()) ? it->second : nullptr;
}

void OrderBookAnalyzerManager::removeAnalyzer(const core::Symbol& symbol, 
                                             const core::ExchangeId& exchangeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = createKey(symbol, exchangeId);
    analyzers_.erase(key);
}

std::vector<std::shared_ptr<OrderBookAnalyzer>> OrderBookAnalyzerManager::getAllAnalyzers() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::shared_ptr<OrderBookAnalyzer>> result;
    result.reserve(analyzers_.size());
    
    for (const auto& [key, analyzer] : analyzers_) {
        result.push_back(analyzer);
    }
    
    return result;
}

void OrderBookAnalyzerManager::processOrderBook(const data_ingestion::OrderBook& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    const std::string key = createKey(snapshot.getSymbol().toString(), snapshot.getExchangeId());
    auto it = analyzers_.find(key);
    
    if (it != analyzers_.end()) {
        it->second->processOrderBook(snapshot);
    }
    else{
        // Optionally create a new analyzer if not found
        auto analyzer = std::make_shared<OrderBookAnalyzer>(
            snapshot.getExchangeId(), snapshot.getSymbol().toString(), params_);
        analyzers_[key] = analyzer;
        analyzer->processOrderBook(snapshot);
    }
}

OrderBookMetrics OrderBookAnalyzerManager::getMetrics(const core::Symbol& symbol, 
                                                     const core::ExchangeId& exchange) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    const std::string key = createKey(symbol.toString(), exchange);
    auto it = analyzers_.find(key);
    if (it == analyzers_.end()) return OrderBookMetrics{};

    // Return a lightweight copy: clear heavy time-series payloads to avoid
    // excessive allocations/copies in hot code paths. Callers that need full
    // time-series should query the analyzer directly.
    OrderBookMetrics metricsCopy = it->second->getMetrics();
    metricsCopy.timeSeriesMetrics.clear();
    return metricsCopy;
}

void OrderBookAnalyzerManager::cleanupInactiveAnalyzers(std::chrono::seconds inactiveThreshold) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    const auto now = std::chrono::steady_clock::now();
    auto it = analyzers_.begin();
    
    while (it != analyzers_.end()) {
        const auto& analyzer = it->second;
        const auto metrics = analyzer->getMetrics();
        
        // Convert timestamp for comparison
        const auto lastUpdate = std::chrono::time_point<std::chrono::steady_clock>(
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                metrics.timestamp.time_since_epoch()));
        
        if (now - lastUpdate > inactiveThreshold) {
            it = analyzers_.erase(it);
        } else {
            ++it;
        }
    }
}

void OrderBookAnalyzerManager::updateParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    params_ = params;
    
    // Update all existing analyzers
    for (const auto& [key, analyzer] : analyzers_) {
        analyzer->updateParameters(params);
    }
}

std::string OrderBookAnalyzerManager::createKey(const core::Symbol& symbol, 
                                               const core::ExchangeId& exchangeId) const {
    return exchangeId.toString() + ":" + symbol.toString();
}

// Factory function
std::shared_ptr<OrderBookAnalyzer> createOrderBookAnalyzer(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& tradingPair,
    const ParameterSet& params) {
    
    return std::make_shared<OrderBookAnalyzer>(exchangeId, tradingPair, params);
}

} // namespace strategy
} // namespace trading