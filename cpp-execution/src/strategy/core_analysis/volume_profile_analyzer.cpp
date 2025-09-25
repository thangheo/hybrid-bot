/**
 * @file volume_profile_analyzer.cpp
 * @brief Implementation of volume distribution analysis
 * 
 * @author Trading System Team
 * @date May 13, 2025
 */

 #include "strategy/core_analysis/volume_profile_analyzer.hpp"    
 #include "strategy/core_analysis/strategy_metrics.hpp"
 #include "strategy/utils/type_adapters.hpp"
 #include <algorithm>
 #include <numeric>
 #include <cmath>
 #include <logger.hpp>
 #include <iostream>
 namespace trading {
 namespace strategy {
 
 VolumeProfileAnalyzer::VolumeProfileAnalyzer(
     const core::ExchangeId& exchangeId, 
     const core::Symbol& tradingPair, 
     const ParameterSet& params)
     : exchangeId_(exchangeId),
       trading_pair_(tradingPair),
       num_price_ranges_(params.getInt("numPriceRanges", 20)),
       range_size_percent_(params.getDouble("rangeSizePercent", 0.001)),
       time_frame_minutes_(params.getInt("timeFrameMinutes", 60)),
       current_profile_(exchangeId, tradingPair),
       last_update_time_(std::chrono::system_clock::now()) {
     
     // Initialize multi-timeframe profiles
     timeframe_profiles_["1m"] = VolumeProfile(exchangeId, tradingPair);
     timeframe_profiles_["5m"] = VolumeProfile(exchangeId, tradingPair);
     timeframe_profiles_["15m"] = VolumeProfile(exchangeId, tradingPair);
     timeframe_profiles_["1h"] = VolumeProfile(exchangeId, tradingPair);
     timeframe_profiles_["4h"] = VolumeProfile(exchangeId, tradingPair);
     timeframe_profiles_["1d"] = VolumeProfile(exchangeId, tradingPair);
 }

 VolumeProfileAnalyzer::VolumeProfileAnalyzer(const VolumeProfileConfig& config)
    : VolumeProfileAnalyzer(config.exchangeId, config.tradingPair, config.params) // or use other fields as needed
{
    // Initialize with config
    num_price_ranges_ = config.numPriceRanges;
    range_size_percent_ = config.rangeSizePercent;
    time_frame_minutes_ = config.timeFrameMinutes;
    
    // Initialize multi-timeframe profiles
    timeframe_profiles_["1m"] = VolumeProfile(config.exchangeId, config.tradingPair);
    timeframe_profiles_["5m"] = VolumeProfile(config.exchangeId, config.tradingPair);
    timeframe_profiles_["15m"] = VolumeProfile(config.exchangeId, config.tradingPair);
    timeframe_profiles_["1h"] = VolumeProfile(config.exchangeId, config.tradingPair);
    timeframe_profiles_["4h"] = VolumeProfile(config.exchangeId, config.tradingPair);
    timeframe_profiles_["1d"] = VolumeProfile(config.exchangeId, config.tradingPair);
}
 VolumeProfileAnalyzer::VolumeProfileAnalyzer(
     const core::ExchangeId& exchangeId, 
     const core::Symbol& tradingPair, 
     int numPriceRanges, 
     double rangeSizePercent)
     : exchangeId_(exchangeId),
       trading_pair_(tradingPair),
       num_price_ranges_(numPriceRanges),
       range_size_percent_(rangeSizePercent),
       time_frame_minutes_(60),
       current_profile_(exchangeId, tradingPair),
       last_update_time_(std::chrono::system_clock::now()) {
     
     // Initialize multi-timeframe profiles
     timeframe_profiles_["1m"] = VolumeProfile(exchangeId, tradingPair);
     timeframe_profiles_["5m"] = VolumeProfile(exchangeId, tradingPair);
     timeframe_profiles_["15m"] = VolumeProfile(exchangeId, tradingPair);
     timeframe_profiles_["1h"] = VolumeProfile(exchangeId, tradingPair);
     timeframe_profiles_["4h"] = VolumeProfile(exchangeId, tradingPair);
     timeframe_profiles_["1d"] = VolumeProfile(exchangeId, tradingPair);
 }
 
 void VolumeProfileAnalyzer::updateVolumeProfile(
     const data_ingestion::OrderBook& orderbook, 
     const std::vector<Trade>& trades) {
     
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Initialize price ranges if this is the first update
     if (current_profile_.price_ranges.empty()) {
         initializePriceRanges(orderbook.getMidPrice());
     }
     
     // Process new trades
     for (const auto& trade : trades) {
        // Process trade
        processTrade(trade.price, trade.quantity, trade.is_buy, trade.timestamp);        
        // Store trade
        recent_trades_.emplace_back(
            trade.price, trade.quantity, trade.is_buy, trade.timestamp);
    }
    while (recent_trades_.size() > MAX_RECENT_TRADES) {
        recent_trades_.pop_front();
    }
    updateFromOrderBook(
        utils::convertPriceLevels(orderbook.getBids()),
        utils::convertPriceLevels(orderbook.getAsks())
    );
     
     // Update timestamp for profile
     current_profile_.end_time = std::chrono::system_clock::now();
          
     // Remove old trades based on timeframe
     auto cutoff_time = std::chrono::system_clock::now() - 
                       std::chrono::minutes(time_frame_minutes_);
     
     while (!recent_trades_.empty() && 
            std::get<3>(recent_trades_.front()) < cutoff_time) {
         recent_trades_.pop_front();
     }
     
     // Update multi-timeframe profiles
     updateMultiTimeframeProfiles(trades);
     
     // Update last update time
     last_update_time_ = orderbook.getTimestamp();
   
 }
 
 void VolumeProfileAnalyzer::updateMultiTimeframeProfiles(
     const std::vector<Trade>& trades) {
     
     // Update each timeframe profile
     for (auto& [timeframe, profile] : timeframe_profiles_) {
         int minutes = 0;
         
         // Determine minutes for this timeframe
         if (timeframe == "1m") minutes = 1;
         else if (timeframe == "5m") minutes = 5;
         else if (timeframe == "15m") minutes = 15;
         else if (timeframe == "1h") minutes = 60;
         else if (timeframe == "4h") minutes = 240;
         else if (timeframe == "1d") minutes = 1440;
         
         // Filter trades that belong to this timeframe
         auto cutoff_time = std::chrono::system_clock::now() - 
                           std::chrono::minutes(minutes);
         
         // Initialize price ranges if empty
         if (profile.price_ranges.empty() && !current_profile_.price_ranges.empty()) {
             profile.price_ranges = current_profile_.price_ranges;
             profile.volume_data.resize(profile.price_ranges.size());
         }
         
         // Process trades for this timeframe
         for (const auto& trade : trades) {
             if (trade.timestamp >= cutoff_time) {
                 // Find the relevant price range
                 for (size_t i = 0; i < profile.price_ranges.size(); ++i) {
                     if (profile.price_ranges[i].contains(trade.price)) {
                         profile.volume_data[i].addTrade(
                             trade.price, trade.quantity, trade.is_buy);
                         profile.total_volume += trade.quantity;
                         break;
                     }
                 }
             }
         }
         
         // Update VWAP for the profile
         double volume_price_sum = 0.0;
         for (size_t i = 0; i < profile.price_ranges.size(); ++i) {
             double midPrice = (profile.price_ranges[i].lower_bound + 
                                profile.price_ranges[i].upper_bound) / 2.0;
             volume_price_sum += midPrice * profile.volume_data[i].total_volume;
         }
         
         if (profile.total_volume > 0) {
             profile.vwap = volume_price_sum / profile.total_volume;
         }
         
         // Find highest volume price
         double max_volume = 0.0;
         for (size_t i = 0; i < profile.price_ranges.size(); ++i) {
             if (profile.volume_data[i].total_volume > max_volume) {
                 max_volume = profile.volume_data[i].total_volume;
                 profile.highest_volume_price = 
                     (profile.price_ranges[i].lower_bound + 
                      profile.price_ranges[i].upper_bound) / 2.0;
             }
         }
         
         // Update end time
         profile.end_time = std::chrono::system_clock::now();
     }
 }
 
 VolumeProfile VolumeProfileAnalyzer::getCurrentProfile() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return current_profile_;
 }
 
 std::map<std::string, VolumeProfile> VolumeProfileAnalyzer::getMultiTimeframeProfiles() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return timeframe_profiles_;
 }
 
 VolumeAnomalies VolumeProfileAnalyzer::detectAnomalies() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     VolumeAnomalies anomalies;
     
     // Detect high volume nodes
     anomalies.high_volume_nodes = detectHighVolumeNodes();
     
     // Detect rapid accumulation zones
     anomalies.rapid_accumulation_zones = detectRapidAccumulationZones();
     
     // Detect volume imbalance zones
     anomalies.volume_imbalance_zones = detectVolumeImbalanceZones();
     
     // Calculate accumulation rates
     anomalies.accumulation_rates = calculateAccumulationRates();
     
     return anomalies;
 }
 
 std::pair<double, double> VolumeProfileAnalyzer::calculateValueArea(
     double value_area_percent) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (current_profile_.price_ranges.empty() || 
         current_profile_.volume_data.empty() || 
         current_profile_.total_volume <= 0) {
         return {0.0, 0.0};
     }
     
     // Calculate volume target
     double target_volume = current_profile_.total_volume * value_area_percent;
     
     // Find highest volume price level (Point of Control)
     size_t poc_index = 0;
     double max_volume = 0.0;
     
     for (size_t i = 0; i < current_profile_.volume_data.size(); ++i) {
         if (current_profile_.volume_data[i].total_volume > max_volume) {
             max_volume = current_profile_.volume_data[i].total_volume;
             poc_index = i;
         }
     }
     
     // Start from POC and expand the range
     double accumulated_volume = current_profile_.volume_data[poc_index].total_volume;
     size_t upper_index = poc_index;
     size_t lower_index = poc_index;
     
     while (accumulated_volume < target_volume && 
           (upper_index < current_profile_.volume_data.size() - 1 || 
            lower_index > 0)) {
         
         // Determine which direction to expand (go toward higher volume)
         double upper_volume = (upper_index < current_profile_.volume_data.size() - 1) ? 
             current_profile_.volume_data[upper_index + 1].total_volume : 0.0;
         
         double lower_volume = (lower_index > 0) ? 
             current_profile_.volume_data[lower_index - 1].total_volume : 0.0;
         
         if (upper_volume >= lower_volume && upper_index < current_profile_.volume_data.size() - 1) {
             upper_index++;
             accumulated_volume += upper_volume;
         } else if (lower_index > 0) {
             lower_index--;
             accumulated_volume += lower_volume;
         } else {
             break; // Can't expand anymore
         }
     }
     
     // Get value area bounds
     double value_area_high = current_profile_.price_ranges[upper_index].upper_bound;
     double value_area_low = current_profile_.price_ranges[lower_index].lower_bound;
     
     return {value_area_high, value_area_low};
 }
 
 double VolumeProfileAnalyzer::calculatePointOfControl() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (current_profile_.price_ranges.empty() || 
         current_profile_.volume_data.empty()) {
         return 0.0;
     }
     
     // Find highest volume price level
     size_t poc_index = 0;
     double max_volume = 0.0;
     
     for (size_t i = 0; i < current_profile_.volume_data.size(); ++i) {
         if (current_profile_.volume_data[i].total_volume > max_volume) {
             max_volume = current_profile_.volume_data[i].total_volume;
             poc_index = i;
         }
     }
     
     // Return the mid-point of the range with highest volume
     return (current_profile_.price_ranges[poc_index].lower_bound + 
             current_profile_.price_ranges[poc_index].upper_bound) / 2.0;
 }
 
 void VolumeProfileAnalyzer::reset() {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Reset main profile
     current_profile_ = VolumeProfile(exchangeId_, trading_pair_);
     
     // Reset timeframe profiles
     for (auto& [timeframe, profile] : timeframe_profiles_) {
         profile = VolumeProfile(exchangeId_, trading_pair_);
     }
     
     // Clear historical data
     recent_trades_.clear();
     
     // Reset last update time
     last_update_time_ = std::chrono::system_clock::now();
 }
 
 void VolumeProfileAnalyzer::setTimeFrame(int timeFrameMinutes) {
     std::lock_guard<std::mutex> lock(mutex_);
     time_frame_minutes_ = timeFrameMinutes;
 }
 
 void VolumeProfileAnalyzer::initializePriceRanges(double current_price) {
     // Clear existing ranges
     current_profile_.price_ranges.clear();
     current_profile_.volume_data.clear();
     
     // Determine price range
     double range_size = current_price * range_size_percent_;
     double min_price = current_price * (1.0 - (num_price_ranges_ / 2.0) * range_size_percent_);
     
     // Create price ranges
     for (int i = 0; i < num_price_ranges_; ++i) {
         double lower_bound = min_price + i * range_size;
         double upper_bound = lower_bound + range_size;
         
         current_profile_.price_ranges.emplace_back(lower_bound, upper_bound);
     }
     
     // Initialize volume data for each range
     current_profile_.volume_data.resize(current_profile_.price_ranges.size());
     
     // Reset profile statistics
     current_profile_.total_volume = 0.0;
     current_profile_.highest_volume_price = current_price;
     current_profile_.vwap = current_price;
     current_profile_.start_time = std::chrono::system_clock::now();
     current_profile_.end_time = std::chrono::system_clock::now();
 }
  //updateParameters
    void VolumeProfileAnalyzer::updateParameters(const ParameterSet& params) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Update parameters
        num_price_ranges_ = params.getInt("numPriceRanges", num_price_ranges_);
        range_size_percent_ = params.getDouble("rangeSizePercent", range_size_percent_);
        time_frame_minutes_ = params.getInt("timeFrameMinutes", time_frame_minutes_);
        
        // Reinitialize price ranges
        if (!current_profile_.price_ranges.empty()) {
            initializePriceRanges(current_profile_.price_ranges[0].lower_bound);
        }
    }
 void VolumeProfileAnalyzer::processTrade(
     double price, double volume, bool is_buy, core::Timestamp timestamp) {
     
     // Find the relevant price range
     for (size_t i = 0; i < current_profile_.price_ranges.size(); ++i) {
         if (current_profile_.price_ranges[i].contains(price)) {
             current_profile_.volume_data[i].addTrade(price, volume, is_buy);
             current_profile_.total_volume += volume;
             
             // Update VWAP
             double volume_price_sum = 0.0;
             for (size_t j = 0; j < current_profile_.price_ranges.size(); ++j) {
                 double midPrice = (current_profile_.price_ranges[j].lower_bound + 
                                    current_profile_.price_ranges[j].upper_bound) / 2.0;
                 volume_price_sum += midPrice * current_profile_.volume_data[j].total_volume;
             }
             
             if (current_profile_.total_volume > 0) {
                 current_profile_.vwap = volume_price_sum / current_profile_.total_volume;
             }
             
             // Update highest volume price
             if (current_profile_.volume_data[i].total_volume > 
                 current_profile_.volume_data[0].total_volume) {
                 current_profile_.highest_volume_price = 
                     (current_profile_.price_ranges[i].lower_bound + 
                      current_profile_.price_ranges[i].upper_bound) / 2.0;
             }
             
             break;
         }
     }
 }
 
 void VolumeProfileAnalyzer::updateFromOrderBook(
     const std::vector<core::PriceLevel>& bids, 
     const std::vector<core::PriceLevel>& asks) {
     
     // Use order book to refine volume profile
     // This is a heuristic since we don't know how much of the order book
     // will actually be executed
     
     double bid_influence = 0.05; // 5% influence from order book
     double ask_influence = 0.05; // 5% influence from order book
     
     // Process bids
     for (const auto& bid : bids) {
         double price = bid.price;
         double quantity = bid.quantity * bid_influence;
         
         // Find the relevant price range
         for (size_t i = 0; i < current_profile_.price_ranges.size(); ++i) {
             if (current_profile_.price_ranges[i].contains(price)) {
                 // Treat order book bids as potential buys
                 current_profile_.volume_data[i].addTrade(price, quantity, true);
                 current_profile_.total_volume += quantity;
                 break;
             }
         }
     }
     
     // Process asks
     for (const auto& ask : asks) {
         double price = ask.price;
         double quantity = ask.quantity * ask_influence;
         
         // Find the relevant price range
         for (size_t i = 0; i < current_profile_.price_ranges.size(); ++i) {
             if (current_profile_.price_ranges[i].contains(price)) {
                 // Treat order book asks as potential sells
                 current_profile_.volume_data[i].addTrade(price, quantity, false);
                 current_profile_.total_volume += quantity;
                 break;
             }
         }
     }
     
     // Update VWAP and highest volume price
     double volume_price_sum = 0.0;
     double max_volume = 0.0;
     size_t max_volume_index = 0;
     
     for (size_t i = 0; i < current_profile_.price_ranges.size(); ++i) {
         double midPrice = (current_profile_.price_ranges[i].lower_bound + 
                            current_profile_.price_ranges[i].upper_bound) / 2.0;
         volume_price_sum += midPrice * current_profile_.volume_data[i].total_volume;
         
         if (current_profile_.volume_data[i].total_volume > max_volume) {
             max_volume = current_profile_.volume_data[i].total_volume;
             max_volume_index = i;
         }
     }
     
     if (current_profile_.total_volume > 0) {
         current_profile_.vwap = volume_price_sum / current_profile_.total_volume;
     }
     
     current_profile_.highest_volume_price = 
         (current_profile_.price_ranges[max_volume_index].lower_bound + 
          current_profile_.price_ranges[max_volume_index].upper_bound) / 2.0;
 }
 
 std::map<double, double> VolumeProfileAnalyzer::calculateAccumulationRates() const {
     std::map<double, double> rates;
     
     if (recent_trades_.empty() || 
         recent_trades_.size() < 10) { // Need enough trades for meaningful analysis
         return rates;
     }
     
     // Group trades by price level (using the same ranges as the profile)
     std::map<size_t, std::vector<std::tuple<double, double, bool, core::Timestamp>>> trades_by_level;
     
     for (const auto& trade : recent_trades_) {
         double price = std::get<0>(trade);
         
         // Find the relevant price range
         for (size_t i = 0; i < current_profile_.price_ranges.size(); ++i) {
             if (current_profile_.price_ranges[i].contains(price)) {
                 trades_by_level[i].push_back(trade);
                 break;
             }
         }
     }
     
     // Calculate accumulation rate for each level
     for (const auto& [level_idx, level_trades] : trades_by_level) {
         if (level_trades.size() < 2) {
             continue; // Need at least 2 trades for rate calculation
         }
         
         // Sort trades by timestamp
         std::vector<std::tuple<double, double, bool, core::Timestamp>> sorted_trades = level_trades;
         std::sort(sorted_trades.begin(), sorted_trades.end(), 
             [](const auto& a, const auto& b) {
                 return std::get<3>(a) < std::get<3>(b);
             });
         
         // Calculate volume over time
         double total_volume = 0.0;
         for (const auto& trade : sorted_trades) {
             total_volume += std::get<1>(trade);
         }
         
         // Calculate time span in seconds
         double time_span = std::chrono::duration<double>(
             std::get<3>(sorted_trades.back()) - std::get<3>(sorted_trades.front())).count();
         
         if (time_span > 0) {
             // Rate is volume per second
             double rate = total_volume / time_span;
             
             // Use midpoint of price range as key
             double midPrice = (current_profile_.price_ranges[level_idx].lower_bound + 
                                current_profile_.price_ranges[level_idx].upper_bound) / 2.0;
             
             rates[midPrice] = rate;
         }
     }
     
     return rates;
 }
 
 std::vector<double> VolumeProfileAnalyzer::detectHighVolumeNodes(double threshold) const {
     std::vector<double> nodes;
     
     if (current_profile_.price_ranges.empty() || 
         current_profile_.volume_data.empty()) {
         return nodes;
     }
     
     // Calculate average volume
     double avg_volume = current_profile_.total_volume / current_profile_.volume_data.size();
     
     // Calculate standard deviation
     double variance = 0.0;
     for (const auto& volume_data : current_profile_.volume_data) {
         double diff = volume_data.total_volume - avg_volume;
         variance += diff * diff;
     }
     variance /= current_profile_.volume_data.size();
     double std_dev = std::sqrt(variance);
     
     // Detect high volume nodes
     for (size_t i = 0; i < current_profile_.volume_data.size(); ++i) {
         if (current_profile_.volume_data[i].total_volume > avg_volume + threshold * std_dev) {
             double midPrice = (current_profile_.price_ranges[i].lower_bound + 
                                current_profile_.price_ranges[i].upper_bound) / 2.0;
             nodes.push_back(midPrice);
         }
     }
     
     return nodes;
 }
 
 std::vector<double> VolumeProfileAnalyzer::detectRapidAccumulationZones(double threshold) const {
     std::vector<double> zones;
     
     // Get accumulation rates
     auto rates = calculateAccumulationRates();
     
     if (rates.empty()) {
         return zones;
     }
     
     // Calculate average rate
     double avg_rate = 0.0;
     for (const auto& [price, rate] : rates) {
         avg_rate += rate;
     }
     avg_rate /= rates.size();
     
     // Detect zones with rapid accumulation
     for (const auto& [price, rate] : rates) {
         if (rate > threshold * avg_rate) {
             zones.push_back(price);
         }
     }
     
     return zones;
 }
 
 std::vector<double> VolumeProfileAnalyzer::detectVolumeImbalanceZones(double threshold) const {
     std::vector<double> zones;
     
     if (current_profile_.price_ranges.empty() || 
         current_profile_.volume_data.empty()) {
         return zones;
     }
     
     // Detect zones with significant buy/sell imbalance
     for (size_t i = 0; i < current_profile_.volume_data.size(); ++i) {
         double ratio = current_profile_.volume_data[i].getBuySellRatio();
         
         // Check for significant imbalance (either buy or sell heavy)
         if (ratio > threshold || ratio < 1.0 / threshold) {
             double midPrice = (current_profile_.price_ranges[i].lower_bound + 
                                current_profile_.price_ranges[i].upper_bound) / 2.0;
             zones.push_back(midPrice);
         }
     }
     
     return zones;
 }
 
 std::shared_ptr<VolumeProfileAnalyzer> createVolumeProfileAnalyzer(
     const core::ExchangeId& exchangeId, 
     const core::Symbol& tradingPair,
     const ParameterSet& params) {
     
     return std::make_shared<VolumeProfileAnalyzer>(exchangeId, tradingPair, params);
 }
 
 } // namespace strategy
 } // namespace trading