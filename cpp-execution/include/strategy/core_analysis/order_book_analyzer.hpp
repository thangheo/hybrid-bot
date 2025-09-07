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
 #include <deque>
 #include <mutex>
 #include <memory>
 #include <functional>
 #include "strategy/types.hpp"
 #include "data_ingestion/orderbook/order_book.hpp"
 #include "common/event_bus.hpp"
 #include "common/trading_events.hpp"
//  #include "core/market_types.hpp"
//  #include "core/types.hpp"

 // Use the appropriate timestamp type
//  using Timestamp = trading::core::Timestamp;

 namespace trading {
 namespace strategy {
 
 /**
  * Callback type for metric updates
  */
 using MetricsCallback = std::function<void(const OrderBookMetrics&)>;
 
 /**
  * OrderBookAnalyzer processes order book data to extract trading signals
  * based on order book imbalances and other market microstructure patterns.
  */
 class OrderBookAnalyzer {
 public:
     /**
      * Creates an order book analyzer for a specific exchange and trading pair
      */
     OrderBookAnalyzer(const core::ExchangeId& exchangeId, const std::string& tradingPair, 
                      const ParameterSet& params = ParameterSet());
     
     /**
      * Creates an order book analyzer with parameters explicitly specified
      */
     OrderBookAnalyzer(const core::ExchangeId& exchangeId, const std::string& tradingPair, 
                      int depth_levels, double price_grouping_factor);
    OrderBookAnalyzer(const OrderBookAnalyzerConfig& config);



     virtual ~OrderBookAnalyzer() = default;
     
     /**
      * Process an order book snapshot
      * 
      * @param snapshot Order book snapshot to analyze
      */
     virtual void processOrderBook(const data_ingestion::OrderBook& snapshot);
     
     /**
      * Get current order book metrics
      * 
      * @return Order book metrics
      */
     virtual OrderBookMetrics getMetrics() const;
     
     /**
      * Get a specific metric value
      * 
      * @param type Metric type
      * @param depth Depth level for level-specific metrics
      * @return Metric value
      */
     virtual double getMetric(const std::string& metric_name, int depth = 10) const;
     
     /**
      * Register a callback for metrics updates
      * 
      * @param callback Function to call when metrics are updated
      */
     virtual void registerMetricsCallback(MetricsCallback callback);
     
     /**
      * Check if enough data has been processed for reliable metrics
      * 
      * @return True if metrics are stable, false otherwise
      */
     virtual bool hasStableMetrics() const;
     
     /**
      * Reset the analyzer state
      */
     virtual void reset();
     
     /**
      * Set the depth of analysis
      * 
      * @param depth_levels Number of price levels to analyze
      */
     virtual void setDepthLevels(int depth_levels);
     
     /**
      * Set price grouping factor
      * 
      * @param factor Price grouping factor
      */
     virtual void setPriceGroupingFactor(double factor);
     
     /**
      * Get the exchange ID
      * @return Exchange ID
      */
     const core::ExchangeId& getExchangeId() const { return exchangeId_; }
     
     /**
      * Get the trading pair
      * @return Trading pair
      */
     const std::string& getTradingPair() const { return trading_pair_; }
    //  updateParameters function
     virtual void updateParameters(const ParameterSet& params);
     
     /**
      * Get the current parameters
      * @return Current parameters
      */
     const ParameterSet& getParameters() const { return params_; }
     
     /**
      * Get the last update time
      * @return Last update time
      */
     core::Timestamp getLastUpdateTime() const { return last_update_time_; }
     
     /**
      * Get the historical mid prices
      * @return Historical mid prices
      */
     const std::vector<double>& getHistoricalMidPrices() const { return historical_mid_prices_; }
     
     /**
      * Get the historical spreads
      * @return Historical spreads
      */
     const std::vector<double>& getHistoricalSpreads() const { return historical_spreads_; }
     
     /**
      * Get the historical volumes
      * @return Historical volumes
      */
     const std::vector<double>& getHistoricalVolumes() const { return historical_volumes_; }
     
     /**
      * Get the historical imbalances
      * @return Historical imbalances
      */
     const std::vector<double>& getHistoricalImbalances() const { return historical_imbalances_; }

 protected:
     /**
      * Calculate basic price metrics
      * 
      * @param bids Bid price levels
      * @param asks Ask price levels
      */
     virtual void calculatePriceMetrics(
         const std::vector<data_ingestion::PriceLevel>& bids,
         const std::vector<data_ingestion::PriceLevel>& asks);
     
     /**
      * Calculate volume-based metrics
      * 
      * @param bids Bid price levels
      * @param asks Ask price levels
      * @param depth Depth level to consider
      */
     virtual void calculateVolumeMetrics(
         const std::vector<data_ingestion::PriceLevel>& bids,
         const std::vector<data_ingestion::PriceLevel>& asks,
         int depth);
     
     /**
      * Calculate metrics for each price level
      * 
      * @param bids Bid price levels
      * @param asks Ask price levels
      */
     virtual void calculateLevelMetrics(
         const std::vector<data_ingestion::PriceLevel>& bids,
         const std::vector<data_ingestion::PriceLevel>& asks);
     
     /**
      * Calculate imbalance metrics
      * 
      * @param bids Bid price levels
      * @param asks Ask price levels
      */
     virtual void calculateImbalanceMetrics(
         const std::vector<data_ingestion::PriceLevel>& bids,
         const std::vector<data_ingestion::PriceLevel>& asks);
     
     /**
      * Calculate derived metrics based on time series data
      */
     virtual void calculateDerivedMetrics();
     
     /**
      * Update the time series data
      */
     virtual void updateTimeSeries();
     
     /**
      * Calculate time-based metrics (velocities, accelerations)
      */
     virtual void calculateTimeBasedMetrics();
     
     /**
      * Process level data with price grouping
      * 
      * @param levels Original price levels
      * @param is_bids Whether these are bid levels
      * @return Grouped price levels
      */
     virtual std::vector<data_ingestion::PriceLevel> groupPriceLevels(
         const std::vector<data_ingestion::PriceLevel>& levels,
         bool is_bids) const;
     
     /**
      * Calculate weighted imbalance metric
      * 
      * @param bids Bid price levels
      * @param asks Ask price levels
      * @param weighting_function Function to weight each level
      * @return Weighted imbalance
      */
     virtual double calculateWeightedImbalance(
         const std::vector<data_ingestion::PriceLevel>& bids,
         const std::vector<data_ingestion::PriceLevel>& asks,
         const std::function<double(int)>& weighting_function) const;
     
     /**
      * Detect liquidity walls (large orders at specific levels)
      * 
      * @param levels Price levels to analyze
      * @param threshold Volume threshold factor
      * @return Vector of indices with liquidity walls
      */
     virtual std::vector<int> detectLiquidityWalls(
         const std::vector<data_ingestion::PriceLevel>& levels,
         double threshold = 3.0) const;
     
     /**
      * Calculate market directional coefficient
      * 
      * @return Directional coefficient (-1 to 1)
      */
     virtual double calculateDirectionalCoefficient() const;
     
     /**
      * Calculate volatility expectation based on order book structure
      * 
      * @return Volatility expectation
      */
     virtual double calculateVolatilityExpectation() const;
     double calculateRealizedVolatility() const;
    double calculateVolatilityPerUnitTime() const ;

     /**
      * Notify callbacks of metrics update
      */
     virtual void notifyMetricsUpdated();

     // Exchange and trading pair identification
     core::ExchangeId exchangeId_;
     std::string trading_pair_;
     
     // Configuration
     int depth_levels_;                    // Number of price levels to analyze
     double price_grouping_factor_;        // Factor for grouping close prices
     
     // State
     OrderBookMetrics current_metrics_;
     std::deque<OrderBookMetrics> metrics_history_;
    //  price_history_
    std::vector<double> price_history_; // If you need to store it as member
     int min_samples_for_stability_;        // Minimum samples for stable metrics
     
     // Callbacks
     std::vector<MetricsCallback> metrics_callbacks_;
     
     // Thread safety
     mutable std::mutex mutex_;
     
     // Historical data
     std::vector<double> historical_mid_prices_;
     std::vector<double> historical_spreads_;
     std::vector<double> historical_volumes_;
     std::vector<double> historical_imbalances_;
     
     // Timestamps for rate calculations
     core::Timestamp last_update_time_;
    std::vector<double> historical_timestamps_; // Add this line
     // Moving averages for smoothing
     double ema_mid_price_ = 0.0;
     double ema_spread_ = 0.0;
     double ema_volume_imbalance_ = 0.0;
     double ema_alpha_ = 0.2;              // Smoothing factor for EMAs

        // Parameters
        ParameterSet params_;
 };
 
 // Factory function to create OrderBookAnalyzer instances
 std::shared_ptr<OrderBookAnalyzer> createOrderBookAnalyzer(
     const core::ExchangeId& exchangeId, 
     const std::string& tradingPair,
     const ParameterSet& params = ParameterSet());
 
 } // namespace strategy
 } // namespace trading