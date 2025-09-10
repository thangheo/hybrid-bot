/**
 * @file volume_profile_analyzer.hpp
 * @brief Volume distribution analysis component
 * 
 * This component analyzes trading volume distribution across price levels
 * to identify significant accumulation patterns and anomalies.
 * 
 * @author Trading System Team
 * @date May 13, 2025
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
 namespace trading {
 namespace strategy {
 
 /**
  * Represents a price range for volume aggregation
  */
 struct PriceRange {
     double lower_bound;
     double upper_bound;
     
     PriceRange(double low, double high) : lower_bound(low), upper_bound(high) {}
     
     bool contains(double price) const {
         return price >= lower_bound && price < upper_bound;
     }
     
     double size() const {
         return upper_bound - lower_bound;
     }
 };
 
 /**
  * Represents volume data within a price range
  */
 struct VolumeData {
     double total_volume = 0.0;        // Total volume in this range
     double buy_volume = 0.0;          // Buy volume
     double sell_volume = 0.0;         // Sell volume
     int transaction_count = 0;        // Number of transactions
     double vwap = 0.0;                // Volume-weighted average price
     
     void addTrade(double price, double volume, bool is_buy) {
         total_volume += volume;
         if (is_buy) {
             buy_volume += volume;
         } else {
             sell_volume += volume;
         }
         transaction_count++;
         
         // Update VWAP
         vwap = ((vwap * (total_volume - volume)) + (price * volume)) / total_volume;
     }
     
     double getBuySellRatio() const {
         if (sell_volume == 0.0) {
             return buy_volume > 0.0 ? 10.0 : 1.0; // Avoid division by zero
         }
         return buy_volume / sell_volume;
     }
 };
 
 /**
  * Represents a complete volume profile for a trading pair
  */
 struct VolumeProfile {
     ExchangeId exchangeId;
     core::Symbol tradingPair;
     core::Timestamp start_time;
     core::Timestamp end_time;
     std::vector<PriceRange> price_ranges;
     std::vector<VolumeData> volume_data;
     double total_volume = 0.0;
     double highest_volume_price = 0.0;
     double vwap = 0.0;
     
     // Default constructor
     VolumeProfile()
         : exchangeId(), tradingPair(""),
           start_time(std::chrono::system_clock::now()),
           end_time(std::chrono::system_clock::now()) {}
     
     // Constructor with exchange and pair
     VolumeProfile(const core::ExchangeId& exchange, const core::Symbol& pair)
         : exchangeId(exchange), tradingPair(pair),
           start_time(std::chrono::system_clock::now()),
           end_time(std::chrono::system_clock::now()) {}
 };
 
 /**
  * Represents anomalies detected in volume profile
  */
 struct VolumeAnomalies {
     std::vector<double> high_volume_nodes;        // Price levels with abnormally high volume
     std::vector<double> rapid_accumulation_zones; // Zones with rapid volume accumulation
     std::vector<double> volume_imbalance_zones;   // Zones with buy/sell imbalance
     std::map<double, double> accumulation_rates;  // Rate of accumulation at each price level
     
     bool hasAnomalies() const {
         return !high_volume_nodes.empty() 
                || !rapid_accumulation_zones.empty() 
                || !volume_imbalance_zones.empty();
     }
 };
 
 /**
  * VolumeProfileAnalyzer tracks and analyzes volume distribution across price levels
  */
 class VolumeProfileAnalyzer {
 public:
     /**
      * Creates a volume profile analyzer for a specific exchange and trading pair
      */
     VolumeProfileAnalyzer(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair, 
                           const ParameterSet& params = ParameterSet());
    VolumeProfileAnalyzer(const VolumeProfileConfig& config);

     /**
      * Creates a volume profile analyzer with parameters explicitly specified
      */
     VolumeProfileAnalyzer(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair, 
                           int numPriceRanges, double rangeSizePercent);
     
     virtual ~VolumeProfileAnalyzer() = default;
     
     /**
      * Update the volume profile with a new order book
      * 
      * @param orderbook Current order book
      * @param trades Recent trades since last update
      */
     virtual void updateVolumeProfile(const data_ingestion::OrderBook& orderbook, 
                                     const std::vector<Trade>& trades);
     
     /**
      * Get the current volume profile
      * 
      * @return Current volume profile
      */
     virtual VolumeProfile getCurrentProfile() const;
     
     /**
      * Get profiles for multiple time frames
      * 
      * @return Map of time frame name to volume profile
      */
     virtual std::map<std::string, VolumeProfile> getMultiTimeframeProfiles() const;
     
     /**
      * Detect anomalies in the current volume profile
      * 
      * @return Detected volume anomalies
      */
     virtual VolumeAnomalies detectAnomalies() const;
     
     /**
      * Calculate value area high and low
      * 
      * @param value_area_percent Percentage of volume to include (default: 70%)
      * @return Pair of (high, low) prices for the value area
      */
     virtual std::pair<double, double> calculateValueArea(double value_area_percent = 0.7) const;
     
     /**
      * Calculate point of control (price with highest volume)
      * 
      * @return Point of control price
      */
     virtual double calculatePointOfControl() const;
     
     /**
      * Reset the volume profile
      */
     virtual void reset();
     
     /**
      * Set time frame for analysis
      * 
      * @param timeFrameMinutes Length of time frame in minutes
      */
     virtual void setTimeFrame(int timeFrameMinutes);
     
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
     //updateParameters(parameters)
        /**
        * Update parameters for the analyzer
        * 
        * @param params New parameters
        */
        virtual void updateParameters(const ParameterSet& params);
 protected:
     /**
      * Initialize price ranges based on current price and parameters
      * 
      * @param current_price Current market price
      */
     virtual void initializePriceRanges(double current_price);
     
     /**
      * Process a single trade
      * 
      * @param price Trade price
      * @param volume Trade volume
      * @param is_buy True if the trade was a buy (taker bought)
      * @param timestamp Trade timestamp
      */
     virtual void processTrade(double price, double volume, bool is_buy, core::Timestamp timestamp);
     
     /**
      * Update volume profile based on order book snapshot
      * 
      * @param bids Bid price levels
      * @param asks Ask price levels
      */
     virtual void updateFromOrderBook(const std::vector<core::PriceLevel>& bids, 
                                     const std::vector<core::PriceLevel>& asks);
     
     /**
      * Update profiles for multiple timeframes
      * 
      * @param trades Recent trades to process for timeframe profiles
      */
     virtual void updateMultiTimeframeProfiles(const std::vector<Trade>& trades);
     
     /**
      * Calculate accumulation rates
      * 
      * @return Map of price level to accumulation rate
      */
     virtual std::map<double, double> calculateAccumulationRates() const;
     
     /**
      * Detect high volume nodes
      * 
      * @param threshold Standard deviations above mean to consider high volume
      * @return Vector of prices with high volume
      */
     virtual std::vector<double> detectHighVolumeNodes(double threshold = 2.0) const;
     
     /**
      * Detect rapid accumulation zones
      * 
      * @param threshold Rate of change threshold
      * @return Vector of prices with rapid accumulation
      */
     virtual std::vector<double> detectRapidAccumulationZones(double threshold = 3.0) const;
     
     /**
      * Detect volume imbalance zones
      * 
      * @param threshold Buy/sell ratio threshold
      * @return Vector of prices with volume imbalance
      */
     virtual std::vector<double> detectVolumeImbalanceZones(double threshold = 3.0) const;
     
     // Exchange and trading pair identification
     core::ExchangeId exchangeId_;
     core::Symbol trading_pair_;
     
     // Configuration
     int num_price_ranges_;             // Number of price ranges to track
     double range_size_percent_;        // Size of each range as percentage of price
     int time_frame_minutes_;           // Time frame in minutes
     
     // Volume profiles for different time frames
     VolumeProfile current_profile_;
     std::map<std::string, VolumeProfile> timeframe_profiles_;
     
     // Historical trade data
     std::deque<std::tuple<double, double, bool, core::Timestamp>> recent_trades_;
     
     // Thread safety
     mutable std::mutex mutex_;
     
     // Last update time
     core::Timestamp last_update_time_;
 };
 
 // Factory function to create VolumeProfileAnalyzer instances
 std::shared_ptr<VolumeProfileAnalyzer> createVolumeProfileAnalyzer(
     const core::ExchangeId& exchangeId, 
     const core::Symbol& tradingPair,
     const ParameterSet& params = ParameterSet());
 
 } // namespace strategy
 } // namespace trading