/**
 * @file order_book_analyzer.cpp
 * @brief Implementation of order book metrics calculation
 * 
 * @author Trading System Team
 * @date May 6, 2025
 */

 #include "strategy/core_analysis/order_book_analyzer.hpp"
 #include "strategy/core_analysis/strategy_metrics.hpp" // Include for StrategyMetrics::updateEMA function
 #include <algorithm>
 #include <numeric>
 #include <cmath>
 
 namespace trading {
 namespace strategy {
 
 OrderBookAnalyzer::OrderBookAnalyzer(const core::ExchangeId& exchangeId, const std::string& tradingPair,
                                    const ParameterSet& params)
     : exchangeId_(exchangeId),
       trading_pair_(tradingPair),
       depth_levels_(params.getInt("depth_levels", 10)),
       price_grouping_factor_(params.getDouble("price_grouping_factor", 0.0001)),
       min_samples_for_stability_(params.getInt("min_samples_for_stability", 5)),
       last_update_time_(std::chrono::system_clock::now()) {
     
     // Initialize historical data containers with zeros
     historical_mid_prices_.reserve(100);
     historical_spreads_.reserve(100);
     historical_volumes_.reserve(100);
     historical_imbalances_.reserve(100);
 }
 
 OrderBookAnalyzer::OrderBookAnalyzer(const core::ExchangeId& exchangeId, const std::string& tradingPair,
                                    int depth_levels, double price_grouping_factor)
     : exchangeId_(exchangeId),
       trading_pair_(tradingPair),
       depth_levels_(depth_levels),
       price_grouping_factor_(price_grouping_factor),
       min_samples_for_stability_(5),
       last_update_time_(std::chrono::system_clock::now()) {
     
     // Initialize historical data containers with zeros
     historical_mid_prices_.reserve(100);
     historical_spreads_.reserve(100);
     historical_volumes_.reserve(100);
     historical_imbalances_.reserve(100);
 }
 
 void OrderBookAnalyzer::processOrderBook(const data_ingestion::OrderBook& snapshot) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Get bids and asks limited to our depth level
     auto bids = snapshot.getBids(depth_levels_);
     auto asks = snapshot.getAsks(depth_levels_);
     
     // Group price levels if grouping factor is non-zero
     if (price_grouping_factor_ > 0.0) {
         bids = groupPriceLevels(bids, true);
         asks = groupPriceLevels(asks, false);
     }
     
     // Initialize metrics with snapshot metadata
     current_metrics_.exchangeId = exchangeId_;
     // Using the appropriate method to create a Symbol from string
     current_metrics_.symbol = core::Symbol::fromString(trading_pair_); // Using factory method instead of constructor
     current_metrics_.timestamp = snapshot.getTimestamp();
     current_metrics_.sequence_number = snapshot.getSequenceNumber();
     
     // Calculate various metrics
     calculatePriceMetrics(bids, asks);
     calculateVolumeMetrics(bids, asks, depth_levels_);
     calculateLevelMetrics(bids, asks);
     calculateImbalanceMetrics(bids, asks);
     
     // Update time series data
     updateTimeSeries();
     
     // Calculate derived metrics based on time series
     calculateDerivedMetrics();
     
     // Calculate time-based metrics
     calculateTimeBasedMetrics();
     
     // Store snapshot in history (limit to 100 items)
     metrics_history_.push_back(current_metrics_);
     if (metrics_history_.size() > 100) {
         metrics_history_.pop_front();
     }
     
     // Update timestamps
     last_update_time_ = snapshot.getTimestamp();
     
     // Notify callbacks
     notifyMetricsUpdated();
 }
 
 OrderBookMetrics OrderBookAnalyzer::getMetrics() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return current_metrics_;
 }
 
 double OrderBookAnalyzer::getMetric(const std::string& metric_name, int depth) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Handle depth-specific metrics
     if (metric_name == "bid_volume" && depth > 0 && 
         depth <= static_cast<int>(current_metrics_.bid_volumes_by_level.size())) {
         return current_metrics_.bid_volumes_by_level[depth - 1];
     }
     
     if (metric_name == "ask_volume" && depth > 0 && 
         depth <= static_cast<int>(current_metrics_.ask_volumes_by_level.size())) {
         return current_metrics_.ask_volumes_by_level[depth - 1];
     }
     
     if (metric_name == "cumulative_bid_volume" && depth > 0 && 
         depth <= static_cast<int>(current_metrics_.cumulative_bid_volumes.size())) {
         return current_metrics_.cumulative_bid_volumes[depth - 1];
     }
     
     if (metric_name == "cumulative_ask_volume" && depth > 0 && 
         depth <= static_cast<int>(current_metrics_.cumulative_ask_volumes.size())) {
         return current_metrics_.cumulative_ask_volumes[depth - 1];
     }
     
     // For non-depth metrics, use the getMetric method from OrderBookMetrics
     // Fix: Resolve ambiguous function call by specifying default value
     return current_metrics_.getMetric(metric_name, 0.0);
 }
 
 void OrderBookAnalyzer::registerMetricsCallback(MetricsCallback callback) {
     std::lock_guard<std::mutex> lock(mutex_);
     metrics_callbacks_.push_back(callback);
 }
 
 bool OrderBookAnalyzer::hasStableMetrics() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return metrics_history_.size() >= static_cast<size_t>(min_samples_for_stability_);
 }
 
 void OrderBookAnalyzer::reset() {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Clear historical data
     metrics_history_.clear();
     historical_mid_prices_.clear();
     historical_spreads_.clear();
     historical_volumes_.clear();
     historical_imbalances_.clear();
     
     // Reset EMAs
     ema_mid_price_ = 0.0;
     ema_spread_ = 0.0;
     ema_volume_imbalance_ = 0.0;
     
     // Reset current metrics to defaults
     current_metrics_ = OrderBookMetrics();
     current_metrics_.exchangeId = exchangeId_;
     // Using factory method to create Symbol
     current_metrics_.symbol = core::Symbol::fromString(trading_pair_);
     
     // Reset timestamps
     last_update_time_ = std::chrono::system_clock::now();
 }
 
 void OrderBookAnalyzer::setDepthLevels(int depth_levels) {
     std::lock_guard<std::mutex> lock(mutex_);
     depth_levels_ = depth_levels;
 }
 
 void OrderBookAnalyzer::setPriceGroupingFactor(double factor) {
     std::lock_guard<std::mutex> lock(mutex_);
     price_grouping_factor_ = factor;
 }
 
 void OrderBookAnalyzer::calculatePriceMetrics(
     const std::vector<data_ingestion::PriceLevel>& bids,
     const std::vector<data_ingestion::PriceLevel>& asks) {
     
     // Need at least one bid and one ask
     if (bids.empty() || asks.empty()) {
         return;
     }
     
     // Best bid and ask are the first elements (assuming sorted)
     double best_bid = bids[0].getPrice();
     double best_ask = asks[0].getPrice();
     
     // Calculate mid price
     current_metrics_.mid_price = (best_bid + best_ask) / 2.0;
     
     // Calculate spread
     current_metrics_.spread = best_ask - best_bid;
     
     // Calculate spread percent
     if (current_metrics_.mid_price > 0) {
         current_metrics_.spread_percent = (current_metrics_.spread / current_metrics_.mid_price) * 100.0;
     }
     
     // Update EMA of mid price if we have a previous value
     if (ema_mid_price_ == 0.0) {
         ema_mid_price_ = current_metrics_.mid_price;
     } else {
         // Using the StrategyMetrics::updateEMA function from strategy namespace
         ema_mid_price_ = StrategyMetrics::updateEMA(
             ema_mid_price_, current_metrics_.mid_price, ema_alpha_);
     }
     
     // Update EMA of spread if we have a previous value
     if (ema_spread_ == 0.0) {
         ema_spread_ = current_metrics_.spread;
     } else {
         // Using the StrategyMetrics::updateEMA function from strategy namespace
         ema_spread_ = StrategyMetrics::updateEMA(
             ema_spread_, current_metrics_.spread, ema_alpha_);
     }
 }
 
 void OrderBookAnalyzer::calculateVolumeMetrics(
     const std::vector<data_ingestion::PriceLevel>& bids,
     const std::vector<data_ingestion::PriceLevel>& asks,
     int depth) {
     
     // Resize level-specific arrays
     current_metrics_.bid_volumes_by_level.resize(depth, 0.0);
     current_metrics_.ask_volumes_by_level.resize(depth, 0.0);
     current_metrics_.cumulative_bid_volumes.resize(depth, 0.0);
     current_metrics_.cumulative_ask_volumes.resize(depth, 0.0);
     
     // Calculate total volumes and level-specific volumes
     current_metrics_.total_bid_volume = 0.0;
     current_metrics_.total_ask_volume = 0.0;
     
     // Process bid levels
     for (size_t i = 0; i < bids.size() && i < static_cast<size_t>(depth); ++i) {
         double level_volume = bids[i].getQuantity();
         current_metrics_.bid_volumes_by_level[i] = level_volume;
         current_metrics_.total_bid_volume += level_volume;
         
         // Calculate cumulative volume
         if (i == 0) {
             current_metrics_.cumulative_bid_volumes[i] = level_volume;
         } else {
             current_metrics_.cumulative_bid_volumes[i] = 
                 current_metrics_.cumulative_bid_volumes[i-1] + level_volume;
         }
     }
     
     // Process ask levels
     for (size_t i = 0; i < asks.size() && i < static_cast<size_t>(depth); ++i) {
         double level_volume = asks[i].getQuantity();
         current_metrics_.ask_volumes_by_level[i] = level_volume;
         current_metrics_.total_ask_volume += level_volume;
         
         // Calculate cumulative volume
         if (i == 0) {
             current_metrics_.cumulative_ask_volumes[i] = level_volume;
         } else {
             current_metrics_.cumulative_ask_volumes[i] = 
                 current_metrics_.cumulative_ask_volumes[i-1] + level_volume;
         }
     }
     
     // Calculate bid/ask volume ratio (avoid division by zero)
     if (current_metrics_.total_ask_volume > 0) {
         current_metrics_.bid_ask_volume_ratio = 
             current_metrics_.total_bid_volume / current_metrics_.total_ask_volume;
     } else {
         current_metrics_.bid_ask_volume_ratio = 
             current_metrics_.total_bid_volume > 0 ? 10.0 : 1.0;
     }
 }
 
 void OrderBookAnalyzer::calculateLevelMetrics(
     const std::vector<data_ingestion::PriceLevel>& bids,
     const std::vector<data_ingestion::PriceLevel>& asks) {
     
     // Calculate price level densities
     if (!bids.empty() && bids.size() > 1) {
         double price_range = bids.front().getPrice() - bids.back().getPrice();
         if (price_range > 0) {
             current_metrics_.bid_density = static_cast<double>(bids.size()) / price_range;
         }
     }
     
     if (!asks.empty() && asks.size() > 1) {
         double price_range = asks.back().getPrice() - asks.front().getPrice();
         if (price_range > 0) {
             current_metrics_.ask_density = static_cast<double>(asks.size()) / price_range;
         }
     }
     
     // Calculate price level imbalance
     if (current_metrics_.ask_density > 0) {
         current_metrics_.price_level_imbalance = 
             current_metrics_.bid_density / current_metrics_.ask_density - 1.0;
     }
 }
 
 void OrderBookAnalyzer::calculateImbalanceMetrics(
     const std::vector<data_ingestion::PriceLevel>& bids,
     const std::vector<data_ingestion::PriceLevel>& asks) {
     
     // Calculate simple volume imbalance
     if (current_metrics_.total_bid_volume + current_metrics_.total_ask_volume > 0) {
         current_metrics_.volume_imbalance = 
             (current_metrics_.total_bid_volume - current_metrics_.total_ask_volume) / 
             (current_metrics_.total_bid_volume + current_metrics_.total_ask_volume);
     }
     
     // Calculate weighted volume imbalance
     auto linear_weight = [](int level) { return 1.0 / (level + 1); };
     current_metrics_.weighted_volume_imbalance = 
         calculateWeightedImbalance(bids, asks, linear_weight);
     
     // Calculate depth imbalance
     double bid_depth_score = 0.0;
     double ask_depth_score = 0.0;
     
     if (!bids.empty() && !asks.empty()) {
         // Use price of the best bid/ask as reference
         double ref_price = (bids[0].getPrice() + asks[0].getPrice()) / 2.0;
         
         for (size_t i = 0; i < bids.size() && i < 5; ++i) {
             double price_distance = (ref_price - bids[i].getPrice()) / ref_price;
             bid_depth_score += bids[i].getQuantity() / (1.0 + 10.0 * price_distance);
         }
         
         for (size_t i = 0; i < asks.size() && i < 5; ++i) {
             double price_distance = (asks[i].getPrice() - ref_price) / ref_price;
             ask_depth_score += asks[i].getQuantity() / (1.0 + 10.0 * price_distance);
         }
         
         if (bid_depth_score + ask_depth_score > 0) {
             current_metrics_.depth_imbalance = 
                 (bid_depth_score - ask_depth_score) / (bid_depth_score + ask_depth_score);
         }
     }
     
     // Calculate pressure imbalance
     current_metrics_.pressure_imbalance = 
         0.4 * current_metrics_.volume_imbalance + 
         0.4 * current_metrics_.weighted_volume_imbalance + 
         0.2 * current_metrics_.depth_imbalance;
     
     // Update EMA of volume imbalance
     if (ema_volume_imbalance_ == 0.0) {
         ema_volume_imbalance_ = current_metrics_.volume_imbalance;
     } else {
         // Using the StrategyMetrics::updateEMA function from strategy namespace
         ema_volume_imbalance_ = StrategyMetrics::updateEMA(
             ema_volume_imbalance_, current_metrics_.volume_imbalance, ema_alpha_);
     }
 }
 
 void OrderBookAnalyzer::updateTimeSeries() {
     // Keep time series limited to a reasonable size
     const size_t MAX_HISTORY = 100;
     
     historical_mid_prices_.push_back(current_metrics_.mid_price);
     historical_spreads_.push_back(current_metrics_.spread);
     historical_volumes_.push_back(current_metrics_.total_bid_volume + current_metrics_.total_ask_volume);
     historical_imbalances_.push_back(current_metrics_.volume_imbalance);
     
     // Trim if necessary
     if (historical_mid_prices_.size() > MAX_HISTORY) {
         historical_mid_prices_.erase(historical_mid_prices_.begin());
     }
     
     if (historical_spreads_.size() > MAX_HISTORY) {
         historical_spreads_.erase(historical_spreads_.begin());
     }
     
     if (historical_volumes_.size() > MAX_HISTORY) {
         historical_volumes_.erase(historical_volumes_.begin());
     }
     
     if (historical_imbalances_.size() > MAX_HISTORY) {
         historical_imbalances_.erase(historical_imbalances_.begin());
     }
     
     // Update time series in the metrics struct for external use
     current_metrics_.time_series_metrics["mid_price"] = historical_mid_prices_;
     current_metrics_.time_series_metrics["spread"] = historical_spreads_;
     current_metrics_.time_series_metrics["volume"] = historical_volumes_;
     current_metrics_.time_series_metrics["imbalance"] = historical_imbalances_;
 }
 
 void OrderBookAnalyzer::calculateDerivedMetrics() {
     current_metrics_.market_directional_coefficient = calculateDirectionalCoefficient();
     current_metrics_.volatility_expectation = calculateVolatilityExpectation();
     
     // Liquidity profile score (a composite measure of overall liquidity)
     double bid_liquidity = current_metrics_.total_bid_volume * (1.0 + current_metrics_.bid_density);
     double ask_liquidity = current_metrics_.total_ask_volume * (1.0 + current_metrics_.ask_density);
     current_metrics_.liquidity_profile_score = (bid_liquidity + ask_liquidity) / 2.0;
 }
 
 void OrderBookAnalyzer::calculateTimeBasedMetrics() {
     // Need at least 2 data points to calculate velocities
     if (metrics_history_.size() < 2) {
         return;
     }
     
     const auto& prev_metrics = metrics_history_.back();
     
     // Calculate time difference in seconds
     double time_diff = std::chrono::duration<double>(
         current_metrics_.timestamp - prev_metrics.timestamp).count();
     
     if (time_diff > 0) {
         // Price velocity (rate of change of mid price)
         current_metrics_.price_velocity = 
             (current_metrics_.mid_price - prev_metrics.mid_price) / time_diff;
         
         // Spread velocity (rate of change of spread)
         current_metrics_.spread_velocity = 
             (current_metrics_.spread - prev_metrics.spread) / time_diff;
         
         // Volume velocity (rate of change of total volume)
         double current_total_volume = current_metrics_.total_bid_volume + current_metrics_.total_ask_volume;
         double prev_total_volume = prev_metrics.total_bid_volume + prev_metrics.total_ask_volume;
         current_metrics_.volume_velocity = 
             (current_total_volume - prev_total_volume) / time_diff;
     }
 }
 
 std::vector<data_ingestion::PriceLevel> OrderBookAnalyzer::groupPriceLevels(
     const std::vector<data_ingestion::PriceLevel>& levels,
     bool is_bids) const {
     
     if (levels.empty() || price_grouping_factor_ <= 0.0) {
         return levels;
     }
     
     std::vector<data_ingestion::PriceLevel> grouped_levels;
     std::map<double, double> price_groups;
     
     for (const auto& level : levels) {
         // Round price to the nearest group
         double price = level.getPrice();
         double group_price;
         
         if (is_bids) {
             // For bids, round down to ensure we don't overestimate
             group_price = std::floor(price / price_grouping_factor_) * price_grouping_factor_;
         } else {
             // For asks, round up to ensure we don't underestimate
             group_price = std::ceil(price / price_grouping_factor_) * price_grouping_factor_;
         }
         
         // Accumulate quantities at the same price level
         price_groups[group_price] += level.getQuantity();
     }
     
     // Convert the map back to a vector
     for (const auto& group : price_groups) {
         grouped_levels.emplace_back(group.first, group.second);
     }
     
     // Sort the levels appropriately
     if (is_bids) {
         // Bids in descending order by price
         std::sort(grouped_levels.begin(), grouped_levels.end(), 
             [](const data_ingestion::PriceLevel& a, const data_ingestion::PriceLevel& b) {
                 return a.getPrice() > b.getPrice();
             });
     } else {
         // Asks in ascending order by price
         std::sort(grouped_levels.begin(), grouped_levels.end(), 
             [](const data_ingestion::PriceLevel& a, const data_ingestion::PriceLevel& b) {
                 return a.getPrice() < b.getPrice();
             });
     }
     
     return grouped_levels;
 }
 
 double OrderBookAnalyzer::calculateWeightedImbalance(
     const std::vector<data_ingestion::PriceLevel>& bids,
     const std::vector<data_ingestion::PriceLevel>& asks,
     const std::function<double(int)>& weighting_function) const {
     
     double weighted_bid_volume = 0.0;
     double weighted_ask_volume = 0.0;
     
     // Process bid levels with weights
     for (size_t i = 0; i < bids.size(); ++i) {
         double weight = weighting_function(static_cast<int>(i));
         weighted_bid_volume += bids[i].getQuantity() * weight;
     }
     
     // Process ask levels with weights
     for (size_t i = 0; i < asks.size(); ++i) {
         double weight = weighting_function(static_cast<int>(i));
         weighted_ask_volume += asks[i].getQuantity() * weight;
     }
     
     // Calculate weighted imbalance
     if (weighted_bid_volume + weighted_ask_volume > 0) {
         return (weighted_bid_volume - weighted_ask_volume) / 
                (weighted_bid_volume + weighted_ask_volume);
     }
     
     return 0.0;
 }
 
 std::vector<int> OrderBookAnalyzer::detectLiquidityWalls(
     const std::vector<data_ingestion::PriceLevel>& levels,
     double threshold) const {
     
     if (levels.size() < 3) {
         return {};
     }
     
     std::vector<int> wall_indices;
     
     // Calculate average volume
     double avg_volume = 0.0;
     for (const auto& level : levels) {
         avg_volume += level.getQuantity();
     }
     avg_volume /= levels.size();
     
     // Detect levels with volume above threshold * average
     for (size_t i = 0; i < levels.size(); ++i) {
         if (levels[i].getQuantity() > threshold * avg_volume) {
             wall_indices.push_back(static_cast<int>(i));
         }
     }
     
     return wall_indices;
 }
 
 double OrderBookAnalyzer::calculateDirectionalCoefficient() const {
     // Need at least a few data points for meaningful analysis
     if (historical_mid_prices_.size() < 5 || historical_imbalances_.size() < 5) {
         return 0.0;
     }
     
     // Calculate recent price trend
     auto recent_prices = std::vector<double>(
         historical_mid_prices_.end() - std::min(historical_mid_prices_.size(), size_t(5)),
         historical_mid_prices_.end());
     
     double price_trend = 0.0;
     for (size_t i = 1; i < recent_prices.size(); ++i) {
         price_trend += recent_prices[i] - recent_prices[i-1];
     }
     price_trend = price_trend > 0 ? 1.0 : (price_trend < 0 ? -1.0 : 0.0);
     
     // Calculate recent imbalance average
     auto recent_imbalances = std::vector<double>(
         historical_imbalances_.end() - std::min(historical_imbalances_.size(), size_t(5)),
         historical_imbalances_.end());
     
     double avg_imbalance = std::accumulate(
         recent_imbalances.begin(), recent_imbalances.end(), 0.0) / recent_imbalances.size();
     
     // Combine price trend and imbalance
     double coefficient = 0.7 * avg_imbalance + 0.3 * price_trend;
     
     // Ensure result is in [-1, 1] range
     return std::max(-1.0, std::min(1.0, coefficient));
 }
 
 double OrderBookAnalyzer::calculateVolatilityExpectation() const {
     // Factors affecting expected volatility:
     // 1. Spread (wider spread = higher volatility)
     // 2. Volume imbalance (higher imbalance = higher volatility)
     // 3. Depth profile (shallow depth = higher volatility)
     // 4. Recent price movement (higher price velocity = higher volatility)
     
     double spread_factor = current_metrics_.spread_percent / 0.1; // Normalize to typical spread
     double imbalance_factor = std::abs(current_metrics_.volume_imbalance) * 2.0;
     double depth_factor = 0.0;
     
     // Calculate depth factor based on volume in first 3 levels
     double top_bid_volume = 0.0;
     double top_ask_volume = 0.0;
     
     if (current_metrics_.bid_volumes_by_level.size() >= 3) {
         for (int i = 0; i < 3; ++i) {
             top_bid_volume += current_metrics_.bid_volumes_by_level[i];
         }
     }
     
     if (current_metrics_.ask_volumes_by_level.size() >= 3) {
         for (int i = 0; i < 3; ++i) {
             top_ask_volume += current_metrics_.ask_volumes_by_level[i];
         }
     }
     
     // If we have depth data, calculate depth factor
     if (top_bid_volume > 0 && top_ask_volume > 0) {
         double avg_top_volume = (top_bid_volume + top_ask_volume) / 2.0;
         double mid_price = current_metrics_.mid_price;
         
         if (mid_price > 0 && avg_top_volume > 0) {
             // Normalize by price to make it comparable across assets
             double normalized_depth = avg_top_volume / mid_price;
             
             // Invert so that lower depth = higher volatility factor
             depth_factor = 1.0 / (normalized_depth + 0.1);
         }
     }
     
     // Velocity factor based on recent price velocity
     double velocity_factor = std::abs(current_metrics_.price_velocity) * 10.0;
     
     // Combine factors with weights
     double volatility_expectation = 
         0.3 * spread_factor + 
         0.3 * imbalance_factor + 
         0.2 * depth_factor + 
         0.2 * velocity_factor;
     
     // Normalize to a reasonable range (0-1, where 0.5 is average expected volatility)
     return std::max(0.0, std::min(1.0, volatility_expectation / 2.0));
 }
 
 void OrderBookAnalyzer::notifyMetricsUpdated() {
     // Copy callbacks to avoid calling them while holding the lock
     std::vector<MetricsCallback> callbacks;
     {
         std::lock_guard<std::mutex> lock(mutex_);
         callbacks = metrics_callbacks_;
     }
     
     // Call all registered callbacks
     for (const auto& callback : callbacks) {
         callback(current_metrics_);
     }
 }
 
 std::shared_ptr<OrderBookAnalyzer> createOrderBookAnalyzer(
     const core::ExchangeId& exchangeId, 
     const std::string& tradingPair,
     const ParameterSet& params) {
     
     return std::make_shared<OrderBookAnalyzer>(exchangeId, tradingPair, params);
 }
 //updateParameters function
void OrderBookAnalyzer::updateParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    params_ = params;
    
    // Update parameters in the analyzer
    depth_levels_ = params_.getInt("depth_levels", depth_levels_);
    price_grouping_factor_ = params_.getDouble("price_grouping_factor", price_grouping_factor_);
    min_samples_for_stability_ = params_.getInt("min_samples_for_stability", min_samples_for_stability_);
    ema_alpha_ = params_.getDouble("ema_alpha", 0.1); // Default EMA alpha
    // Update other parameters as needed
    // Notify callbacks about parameter update
    notifyMetricsUpdated();
}

 } // namespace strategy
 } // namespace trading