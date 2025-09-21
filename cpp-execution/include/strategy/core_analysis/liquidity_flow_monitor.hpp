/**
 * @file liquidity_flow_monitor.hpp
 * @brief Order flow and liquidity tracking component
 * 
 * This component tracks changes in order book liquidity, including order
 * additions, cancellations, and modifications, to detect significant
 * liquidity shifts that may precede price movements.
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
 #include "strategy/types.hpp"
 #include "data_ingestion/orderbook/order_book.hpp"
 #include "core/market_types.hpp"
//  #include "data_ingestion/orderbook/price_level.hpp"
   

 namespace trading {
 namespace strategy {
 using trading::data_ingestion::PriceLevel;
 using trading::data_ingestion::OrderBookDelta;
 using ExchangeId = core::ExchangeId;
 /**
  * Metrics related to liquidity flow
  */
 struct LiquidityMetrics {
     ExchangeId exchangeId;
     core::Symbol tradingPair;
     core::Timestamp timestamp;
     
     // Bid-side metrics
     double bid_add_rate = 0.0;             // Rate of order additions on bid side
     double bid_cancel_rate = 0.0;          // Rate of order cancellations on bid side
     double bid_execution_rate = 0.0;       // Rate of order executions on bid side
     double bid_modification_rate = 0.0;    // Rate of order modifications on bid side
     double bid_toxicity = 0.0;             // Measure of bid side toxicity
     
     // Ask-side metrics
     double ask_add_rate = 0.0;             // Rate of order additions on ask side
     double ask_cancel_rate = 0.0;          // Rate of order cancellations on ask side
     double ask_execution_rate = 0.0;       // Rate of order executions on ask side
     double ask_modification_rate = 0.0;    // Rate of order modifications on ask side
     double ask_toxicity = 0.0;             // Measure of ask side toxicity
     
     // Combined metrics
     double flow_imbalance = 0.0;           // Imbalance in bid/ask order flow
     double cancellation_ratio = 0.0;       // Ratio of cancellations to additions
     double large_order_ratio = 0.0;        // Ratio of large orders to total
     double pressure_index = 0.0;           // Composite pressure index
 };
 
 /**
  * Represents events detected in liquidity flow
  */
 struct LiquidityEvent {
     enum class Type {
         LARGE_ORDER_ADDED,
         LARGE_ORDER_REMOVED,
         RAPID_LIQUIDITY_ADDITION,
         RAPID_LIQUIDITY_REMOVAL,
         SPREAD_COLLAPSE,
         SPREAD_EXPANSION,
         LIQUIDITY_IMBALANCE
     };
     
     Type type;
     ExchangeId exchangeId;
     core::Symbol tradingPair;
     core::Timestamp timestamp;
     core::Direction direction;          // core::Direction implied by this event
     double price_level;           // Price level where event occurred
     double quantity;              // Quantity involved
     double significance;          // Significance score (0.0-1.0)
     std::map<std::string, double> metrics;
     
     LiquidityEvent(Type t, const core::ExchangeId& exchange, const core::Symbol& pair,
                   core::Timestamp ts, core::Direction dir, double price, double qty, double sig)
         : type(t), exchangeId(exchange), tradingPair(pair),
           timestamp(ts), direction(dir), price_level(price),
           quantity(qty), significance(sig) {}
 };
 
 /**
  * Aggregated collection of liquidity events
  */
 struct LiquidityEvents {
     std::vector<LiquidityEvent> events;
     core::Timestamp start_time;
     core::Timestamp end_time;
     
     bool hasSignificantEvents(double threshold = 0.7) const {
         for (const auto& event : events) {
             if (event.significance >= threshold) {
                 return true;
             }
         }
         return false;
     }
     
     std::vector<LiquidityEvent> getSignificantEvents(double threshold = 0.7) const {
         std::vector<LiquidityEvent> result;
         for (const auto& event : events) {
             if (event.significance >= threshold) {
                 result.push_back(event);
             }
         }
         return result;
     }
 };
 
/**
 * Lightweight OrderBook snapshot for history storage
 * Stores only essential data instead of full OrderBook objects
 */
struct OrderBookSnapshot {
    core::Timestamp timestamp;
    uint64_t sequence_number;
    ExchangeId exchange_id;
    Symbol symbol;
    
    // Store only limited depth (top N levels)
    std::vector<PriceLevel> top_bids;   // Only top 10-20 levels
    std::vector<PriceLevel> top_asks;   // Only top 10-20 levels
    
    // Metrics for analysis
    double mid_price;
    double spread;
    double bid_volume;
    double ask_volume;
    // capacity()
    // size_t capacity() const {
    //     return sizeof(*this) +
    //            (top_bids.capacity() * sizeof(PriceLevel)) +
    //            (top_asks.capacity() * sizeof(PriceLevel));
    // }

    // Memory footprint is much smaller than full OrderBook
    static constexpr size_t MAX_LEVELS_TO_STORE = 20;
    
    // Factory method to create from OrderBook
    static OrderBookSnapshot fromOrderBook(const data_ingestion::OrderBook& ob) {
        OrderBookSnapshot snapshot;
        snapshot.timestamp = ob.getTimestamp();
        snapshot.sequence_number = ob.getSequenceNumber();
        snapshot.exchange_id = ob.getExchangeId();
        snapshot.symbol = ob.getSymbol();
        
        // Store only top levels to save memory
        auto bids = ob.getBids(MAX_LEVELS_TO_STORE);
        auto asks = ob.getAsks(MAX_LEVELS_TO_STORE);
        
        snapshot.top_bids = std::move(bids);
        snapshot.top_asks = std::move(asks);
        
        // Pre-calculate metrics
        snapshot.mid_price = ob.getMidPrice();
        snapshot.spread = ob.getSpread();
        snapshot.bid_volume = ob.getBidVolume(MAX_LEVELS_TO_STORE);
        snapshot.ask_volume = ob.getAskVolume(MAX_LEVELS_TO_STORE);
        
        return snapshot;
    }
    
    size_t getMemoryUsage() const {
        return sizeof(*this) + 
               (top_bids.capacity() * sizeof(PriceLevel)) +
               (top_asks.capacity() * sizeof(PriceLevel));
    }
};


 /**
  * LiquidityFlowMonitor tracks changes in order book liquidity to detect
  * significant shifts that may precede price movements
  */
 class LiquidityFlowMonitor {
 public:
     /**
      * Creates a liquidity flow monitor for a specific exchange and trading pair
      */
     LiquidityFlowMonitor(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair, 
                          const ParameterSet& params = ParameterSet());
     LiquidityFlowMonitor(const LiquidityFlowConfig& config);

     /**
      * Creates a liquidity flow monitor with parameters explicitly specified
      */
     LiquidityFlowMonitor(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair, 
                          double large_order_threshold, int history_window_seconds);
     
     virtual ~LiquidityFlowMonitor() = default;
     
     /**
      * Process an order book delta (change)
      * 
      * @param delta The order book changes
      */
     virtual void processOrderBookDelta(const OrderBookDelta& delta);
     
     /**
      * Process two consecutive order book snapshots to derive a delta
      * 
      * @param prev_book Previous order book
      * @param curr_book Current order book
      */
     virtual void processOrderBookSnapshots(const data_ingestion::OrderBook& prev_book,
                                           const data_ingestion::OrderBook& curr_book);
     
     /**
      * Get current liquidity metrics
      * 
      * @return Liquidity metrics
      */
     virtual LiquidityMetrics getLiquidityMetrics() const;
     
     /**
      * Detect significant liquidity events
      * 
      * @return Collection of detected liquidity events
      */
     virtual LiquidityEvents detectLiquidityEvents() const;
     
     /**
      * Check if there are any significant liquidity events
      * 
      * @param min_significance Minimum significance threshold
      * @return True if significant events exist, false otherwise
      */
     virtual bool hasSignificantEvents(double min_significance = 0.7) const;
     
     /**
      * Get the exchange ID
      * @return Exchange ID
      */
     const core::ExchangeId& getExchangeId() const { return exchangeId_; }
     
     /**
      * Get the trading pair
      * @return Trading pair
      */
     const core::Symbol& getTradingPair() const { return trading_pair_; }
     
     /**
      * Set the large order threshold
      * 
      * @param threshold Threshold as percentage of typical order size
      */
     virtual void setLargeOrderThreshold(double threshold);
     
     /**
      * Set the history window size
      * 
      * @param seconds Window size in seconds
      */
     virtual void setHistoryWindowSize(int seconds);
     
     /**
      * Reset the monitor state
      */
     virtual void resetInternal() ;

     virtual void reset();
     //updateParameters(parameters)
     /**
      * Update parameters for the monitor
      * 
      * @param params New parameters
      */
     virtual void updateParameters(const ParameterSet& params);
     // Fixed version - no more memory leaks!
    // void processOrderBookSnapshots(
    //     const data_ingestion::OrderBook& prev_book,
    //     const data_ingestion::OrderBook& curr_book);
    
    // Memory management methods
    void performCleanup();
    size_t getMemoryUsage() const;
    size_t getHistorySize() const;
    void clearHistory();
      // Configuration
    void updateConfig(const LiquidityFlowConfig& config);
    const LiquidityFlowConfig& getConfig() const { return config_; }
    
    // Analysis methods (using lightweight snapshots)
    double calculateFlowImbalance(std::chrono::seconds lookback_period) const;
    std::vector<double> getPriceHistory(std::chrono::seconds lookback_period) const;
    double getAverageSpread(std::chrono::seconds lookback_period) const;

private:
    LiquidityFlowConfig config_;
    
    // Use lightweight snapshots instead of full OrderBooks
    std::deque<OrderBookSnapshot> snapshot_history_;
    
    // Memory management
    std::chrono::system_clock::time_point last_cleanup_time_;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Helper methods
    void enforceHistoryLimits();
    void autoCleanupIfNeeded();
    bool shouldPerformCleanup() const;
    
    // Analysis helpers
    data_ingestion::OrderBookDelta computeDelta(
        const std::vector<PriceLevel>& prev_bids,
        const std::vector<PriceLevel>& prev_asks,
        const std::vector<PriceLevel>& curr_bids,
        const std::vector<PriceLevel>& curr_asks,
        core::Timestamp timestamp,
        uint64_t sequence_number);
    
    // void processOrderBookDelta(const data_ingestion::OrderBookDelta& delta);

     /**
      * Compute order book delta between two snapshots
      * 
      * @param prev_bids Previous bids
      * @param prev_asks Previous asks
      * @param curr_bids Current bids
      * @param curr_asks Current asks
      * @param timestamp core::Timestamp for the delta
      * @param sequenceNumber Sequence number for the delta
      * @return Computed order book delta
      */
    //  virtual OrderBookDelta computeDelta(
    //      const std::vector<PriceLevel>& prev_bids,
    //      const std::vector<PriceLevel>& prev_asks,
    //      const std::vector<PriceLevel>& curr_bids,
    //      const std::vector<PriceLevel>& curr_asks,
    //      core::Timestamp timestamp,
        //  uint64_t sequenceNumber);
     
 protected:

     /**
      * Identify large orders in a set of price levels
      * 
      * @param levels Price levels to check
      * @return Vector of large order price levels
      */
     virtual std::vector<PriceLevel> identifyLargeOrders(const std::vector<PriceLevel>& levels) const;
     
     /**
      * Detect rapid liquidity changes
      * 
      * @param window_size Number of deltas to analyze
      * @param threshold Change threshold
      * @return Vector of detected events
      */
     virtual std::vector<LiquidityEvent> detectRapidLiquidityChanges(
         int window_size = 5, double threshold = 2.0) const;
     
     /**
      * Detect spread changes
      * 
      * @param window_size Number of order books to analyze
      * @param threshold Change threshold
      * @return Vector of detected events
      */
     virtual std::vector<LiquidityEvent> detectSpreadChanges(
         int window_size = 3, double threshold = 2.0) const;
     
     /**
      * Detect liquidity imbalances
      * 
      * @param threshold Imbalance threshold
      * @return Vector of detected events
      */
     virtual std::vector<LiquidityEvent> detectLiquidityImbalances(double threshold = 3.0) const;
     
     /**
      * Update liquidity metrics
      */
     virtual void updateLiquidityMetrics();
     
     /**
      * Calculate typical order size
      * 
      * @return Median order size
      */
     virtual double calculateTypicalOrderSize() const;
     
     // Exchange and trading pair identification
     core::ExchangeId exchangeId_;
     core::Symbol trading_pair_;
     
     // Configuration
     double large_order_threshold_;      // Multiple of typical size to consider "large"
     int history_window_seconds_;        // Window size for historical analysis
     
     // State
    //  mutable std::mutex mutex_;
     std::deque<OrderBookDelta> delta_history_;
    //  std::deque<data_ingestion::OrderBook> order_book_history_;
    std::deque<std::shared_ptr<data_ingestion::OrderBook>> order_book_history_;

     LiquidityMetrics current_metrics_;
     double typical_order_size_;
     
     // Last processed data
     core::Timestamp last_update_time_;
     uint64_t last_sequence_number_;
 };
 
 // Factory function to create LiquidityFlowMonitor instances
 std::shared_ptr<LiquidityFlowMonitor> createLiquidityFlowMonitor(
     const core::ExchangeId& exchangeId, 
     const core::Symbol& tradingPair,
     const ParameterSet& params = ParameterSet());
 
 
    
 } // namespace strategy
 } // namespace trading