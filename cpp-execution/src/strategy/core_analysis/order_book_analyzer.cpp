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
 #include <thread>
 #include "logger.hpp"
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
 // Add this to order_book_analyzer.cpp

// Add this constructor implementation to order_book_analyzer.cpp
OrderBookAnalyzer::OrderBookAnalyzer(const OrderBookAnalyzerConfig& config)
    : exchangeId_(core::ExchangeId::fromString("BINANCE")), // TODO: Add exchangeId to config
      trading_pair_(""), // TODO: Add tradingPair to config  
      depth_levels_(config.depth),
      price_grouping_factor_(0.0001), // Default value, or add to config
      min_samples_for_stability_(5), // Default value, or add to config
      ema_alpha_(0.2), // Default value, or add to config
      last_update_time_(std::chrono::system_clock::now()) {
    
    // Initialize historical data containers with configured capacity
    historical_mid_prices_.reserve(config.historySize);
    historical_spreads_.reserve(config.historySize);
    historical_volumes_.reserve(config.historySize);
    historical_imbalances_.reserve(config.historySize);
    
    // Store config values that might be used in calculations
    // You can add these as member variables if needed
    // For now, they could be stored in params_
    params_.setParameter("priceWeightFactor", config.priceWeightFactor);
    params_.setParameter("volumeWeightFactor", config.volumeWeightFactor);
    params_.setParameter("imbalanceThreshold", config.imbalanceThreshold);
    params_.setParameter("liquidityPressureThreshold", config.liquidityPressureThreshold);
    params_.setParameter("orderFlowThreshold", config.orderFlowThreshold);
    params_.setParameter("historySize", config.historySize);
    
    // Initialize current metrics with proper values
    current_metrics_.exchangeId = exchangeId_;
    current_metrics_.symbol = core::Symbol::fromString(trading_pair_);
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
    OrderBookMetrics metrics_to_notify;  // Declare outside the lock
    //check if the snapshot is valid
    if (!snapshot.isValid()) {
        return;
    }
    {  // Scope the lock
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Get bids and asks limited to our depth level
        auto bids = snapshot.getBids(depth_levels_);
        auto asks = snapshot.getAsks(depth_levels_);
        
        // Group price levels if grouping factor is non-zero
        if (price_grouping_factor_ > 0.0) {
            bids = groupPriceLevels(bids, true);
            asks = groupPriceLevels(asks, false);
        }
        
        // Initialize metrics with snapshot metadata - USE ACTUAL DATA FROM ORDERBOOK
        current_metrics_.exchangeId = snapshot.getExchangeId();
        current_metrics_.symbol = snapshot.getSymbol();
        current_metrics_.timestamp = snapshot.getTimestamp();
        current_metrics_.sequenceNumber = snapshot.getSequenceNumber();
        
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
        
        // Copy metrics for notification
        metrics_to_notify = current_metrics_;
        
    }  // Lock is released here
    
    // Notify callbacks AFTER releasing the lock
    common::EventBus::getInstance().publish(events::MetricsUpdatedEvent{current_metrics_});
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
     double best_bid = bids[0].price;
     double best_ask = asks[0].price;
     
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
    current_metrics_.metrics["mid_price"] = current_metrics_.mid_price;
    current_metrics_.metrics["spread"] = current_metrics_.spread;
    current_metrics_.metrics["spread_percent"] = current_metrics_.spread_percent;
    current_metrics_.metrics["best_bid"] = best_bid;
    current_metrics_.metrics["best_ask"] = best_ask;
    
    // Store EMA values for historical analysis
    current_metrics_.metrics["ema_mid_price"] = ema_mid_price_;
    current_metrics_.metrics["ema_spread"] = ema_spread_;
    
    // For abnormal spread detection
    current_metrics_.metrics["average_spread_percent"] = ema_spread_ / current_metrics_.mid_price * 100.0;
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
         double level_volume = bids[i].quantity;
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
         double level_volume = asks[i].quantity;
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
         // Store in the metrics map for imbalance detection
    current_metrics_.metrics["bid_volume"] = current_metrics_.total_bid_volume;
    current_metrics_.metrics["ask_volume"] = current_metrics_.total_ask_volume;
    current_metrics_.metrics["bid_level_count"] = static_cast<double>(bids.size());
    current_metrics_.metrics["ask_level_count"] = static_cast<double>(asks.size());
    current_metrics_.metrics["mid_price"] = current_metrics_.mid_price;
    current_metrics_.metrics["spread_percent"] = current_metrics_.spread_percent;
 }
 
 void OrderBookAnalyzer::calculateLevelMetrics(
     const std::vector<data_ingestion::PriceLevel>& bids,
     const std::vector<data_ingestion::PriceLevel>& asks) {
     
     // Calculate price level densities
     if (!bids.empty() && bids.size() > 1) {
         double price_range = bids.front().price - bids.back().price;
         if (price_range > 0) {
             current_metrics_.bid_density = static_cast<double>(bids.size()) / price_range;
         }
     }
     
     if (!asks.empty() && asks.size() > 1) {
         double price_range = asks.back().price - asks.front().price;
         if (price_range > 0) {
             current_metrics_.ask_density = static_cast<double>(asks.size()) / price_range;
         }
     }
     
     // Calculate price level imbalance
     if (current_metrics_.ask_density > 0) {
         current_metrics_.price_level_imbalance = 
             current_metrics_.bid_density / current_metrics_.ask_density - 1.0;
     }
    current_metrics_.metrics["bid_density"] = current_metrics_.bid_density;
    current_metrics_.metrics["ask_density"] = current_metrics_.ask_density;
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
         double ref_price = (bids[0].price + asks[0].price) / 2.0;
         
         for (size_t i = 0; i < bids.size() && i < 5; ++i) {
             double price_distance = (ref_price - bids[i].price) / ref_price;
             bid_depth_score += bids[i].quantity / (1.0 + 10.0 * price_distance);
         }
         
         for (size_t i = 0; i < asks.size() && i < 5; ++i) {
             double price_distance = (asks[i].price - ref_price) / ref_price;
             ask_depth_score += asks[i].quantity / (1.0 + 10.0 * price_distance);
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
    current_metrics_.metrics["volume_imbalance"] = current_metrics_.volume_imbalance;
    // current_metrics_.metrics["volatility"] = current_metrics_.volatility_expectation;
    current_metrics_.metrics["volatility"] = calculateRealizedVolatility(); // or your fixed expectation
    current_metrics_.metrics["volatility_expectation"] = calculateVolatilityExpectation();
    current_metrics_.metrics["weighted_volume_imbalance"] = current_metrics_.weighted_volume_imbalance;
    current_metrics_.metrics["depth_imbalance"] = current_metrics_.depth_imbalance;
    current_metrics_.metrics["pressure_imbalance"] = current_metrics_.pressure_imbalance;
    current_metrics_.metrics["ema_volume_imbalance"] = ema_volume_imbalance_;
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
    //  current_metrics_.volatility_expectation = calculateVolatilityExpectation();//why duplicate call?
        //we already call it in calculateImbalanceMetrics
        current_metrics_.realized_volatility = current_metrics_.metrics["volatility"];
        current_metrics_.realized_volatility = calculateRealizedVolatility();
        current_metrics_.liquidity_pressure = current_metrics_.pressure_imbalance * 
                                            (current_metrics_.total_bid_volume + current_metrics_.total_ask_volume);
        current_metrics_.volatility_expectation = current_metrics_.metrics["volatility_expectation"];                                    
     
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
         double price = level.price;
         double group_price;
         
         if (is_bids) {
             // For bids, round down to ensure we don't overestimate
             group_price = std::floor(price / price_grouping_factor_) * price_grouping_factor_;
         } else {
             // For asks, round up to ensure we don't underestimate
             group_price = std::ceil(price / price_grouping_factor_) * price_grouping_factor_;
         }
         
         // Accumulate quantities at the same price level
         price_groups[group_price] += level.quantity;
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
                 return a.price > b.price;
             });
     } else {
         // Asks in ascending order by price
         std::sort(grouped_levels.begin(), grouped_levels.end(), 
             [](const data_ingestion::PriceLevel& a, const data_ingestion::PriceLevel& b) {
                 return a.price < b.price;
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
         weighted_bid_volume += bids[i].quantity * weight;
     }
     
     // Process ask levels with weights
     for (size_t i = 0; i < asks.size(); ++i) {
         double weight = weighting_function(static_cast<int>(i));
         weighted_ask_volume += asks[i].quantity * weight;
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
         avg_volume += level.quantity;
     }
     avg_volume /= levels.size();
     
     // Detect levels with volume above threshold * average
     for (size_t i = 0; i < levels.size(); ++i) {
         if (levels[i].quantity > threshold * avg_volume) {
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
 
double OrderBookAnalyzer::calculateRealizedVolatility() const {
    const int min_periods = 5;   
    const int max_periods = 30;  
    
    // Extract mid_price history from time series metrics
    auto it = current_metrics_.time_series_metrics.find("mid_price");
    if (it == current_metrics_.time_series_metrics.end()) {
        LOG_DEBUG("No mid_price history available, using default volatility");
        return 0.005; // Default 0.5%
    }
    
    // ✅ FIX: Use vector instead of deque
    const std::vector<double>& price_history = it->second;
    
    if (price_history.size() < min_periods) {
        LOG_DEBUG("Insufficient price history: {} points, need {}", 
                  price_history.size(), min_periods);
        return 0.005;
    }
    
    // Use recent history (but not more than max_periods)
    int periods_to_use = std::min(static_cast<int>(price_history.size()), max_periods);
    int start_idx = price_history.size() - periods_to_use;
    
    // Calculate price returns
    std::vector<double> returns;
    returns.reserve(periods_to_use - 1);
    
    for (int i = start_idx + 1; i < static_cast<int>(price_history.size()); ++i) {
        double prev_price = price_history[i-1];
        double curr_price = price_history[i];
        
        if (prev_price > 0 && curr_price > 0) {
            double return_pct = (curr_price - prev_price) / prev_price;
            returns.push_back(return_pct);
        }
    }
    
    if (returns.empty()) {
        LOG_WARN("No valid returns calculated from price history");
        return 0.005;
    }
    
    // Calculate standard deviation of returns
    double mean_return = 0.0;
    for (double ret : returns) {
        mean_return += ret;
    }
    mean_return /= returns.size();
    
    double variance = 0.0;
    for (double ret : returns) {
        double diff = ret - mean_return;
        variance += diff * diff;
    }
    
    // Use sample variance (n-1) for small samples
    if (returns.size() > 1) {
        variance /= (returns.size() - 1);
    }
    
    double volatility = std::sqrt(variance);
    
    // Clamp to reasonable short-term volatility range
    double clamped_vol = std::clamp(volatility, 0.001, 0.05); // 0.1% to 5%
        
    return clamped_vol;
}

double OrderBookAnalyzer::calculateVolatilityExpectation() const {
    // Calculate individual factors with proper bounds
    
    // 1. Spread factor (0.0 to 1.0)
    double spread_factor = 0.0;
    if (current_metrics_.spread_percent > 0) {
        // Normalize spread: 0.1% = low volatility, 1.0% = high volatility
        spread_factor = std::clamp(current_metrics_.spread_percent / 0.01, 0.0, 1.0);
    }
    
    // 2. Volume imbalance factor (0.0 to 1.0)
    double imbalance_factor = std::clamp(std::abs(current_metrics_.volume_imbalance), 0.0, 1.0);
    
    // 3. Depth factor (0.0 to 1.0)
    double depth_factor = 0.0;
    double top_bid_volume = 0.0;
    double top_ask_volume = 0.0;
    
    // Sum volumes from top 3 levels
    int levels_to_check = std::min(3, static_cast<int>(current_metrics_.bid_volumes_by_level.size()));
    for (int i = 0; i < levels_to_check; ++i) {
        top_bid_volume += current_metrics_.bid_volumes_by_level[i];
    }
    
    levels_to_check = std::min(3, static_cast<int>(current_metrics_.ask_volumes_by_level.size()));
    for (int i = 0; i < levels_to_check; ++i) {
        top_ask_volume += current_metrics_.ask_volumes_by_level[i];
    }
    
    if (top_bid_volume > 0 && top_ask_volume > 0 && current_metrics_.mid_price > 0) {
        double avg_top_volume = (top_bid_volume + top_ask_volume) / 2.0;
        double normalized_depth = avg_top_volume / current_metrics_.mid_price;
        
        // Convert to volatility factor: thin books = high volatility
        // Use exponential decay to keep values bounded
        depth_factor = std::clamp(1.0 - std::exp(-1.0 / (normalized_depth + 0.001)), 0.0, 1.0);
    }
    
    // 4. Price velocity factor (0.0 to 1.0)
    double velocity_factor = 0.0;
    if (current_metrics_.price_velocity != 0.0) {
        // Normalize velocity: 1% per second = high volatility
        velocity_factor = std::clamp(std::abs(current_metrics_.price_velocity) * 100.0, 0.0, 1.0);
    }
    
    // Combine factors with weights (all factors now 0.0 to 1.0)
    double combined_factor = 
        0.3 * spread_factor +
        0.3 * imbalance_factor + 
        0.2 * depth_factor +
        0.2 * velocity_factor;
    
    // Convert to realistic volatility percentage
    // 0.0 factor = 0.1% volatility (very low)
    // 1.0 factor = 3.0% volatility (very high for short-term)
    double volatility_percentage = 0.001 + (combined_factor * 0.029); // 0.1% to 3.0%
    
    // Debug logging
    // LOG_DEBUG("Volatility calculation: spread={:.3f}, imbalance={:.3f}, depth={:.3f}, velocity={:.3f} -> {:.4f} ({:.2f}%)",
    //           spread_factor, imbalance_factor, depth_factor, velocity_factor, 
    //           volatility_percentage, volatility_percentage * 100);
    
    return volatility_percentage; // Returns 0.001 to 0.03 (0.1% to 3.0%)
}
void OrderBookAnalyzer::notifyMetricsUpdated() {
    // Copy callbacks and metrics
    std::vector<MetricsCallback> callbacks;
    OrderBookMetrics metrics_copy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks = metrics_callbacks_;
        metrics_copy = current_metrics_;
    }
    
    // Execute callbacks asynchronously to break any deadlock cycle
    for (const auto& callback : callbacks) {
        std::thread([callback, metrics_copy]() {
            try {
                callback(metrics_copy);
            } catch (const std::exception& e) {
                // Log error
            }
        }).detach();
    }
}
 
 std::shared_ptr<OrderBookAnalyzer> createOrderBookAnalyzer(
     const core::ExchangeId& exchangeId, 
     const std::string& tradingPair,
     const ParameterSet& params) {
     
     return std::make_shared<OrderBookAnalyzer>(exchangeId, tradingPair, params);
 }
 void OrderBookAnalyzer::updateParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    params_ = params;
    
    // Update parameters
    depth_levels_ = params_.getInt("depth_levels", depth_levels_);
    price_grouping_factor_ = params_.getDouble("price_grouping_factor", price_grouping_factor_);
    min_samples_for_stability_ = params_.getInt("min_samples_for_stability", min_samples_for_stability_);
    ema_alpha_ = params_.getDouble("ema_alpha", 0.1);
    
    // COPY current_metrics_ while we have the lock
    OrderBookMetrics metrics_copy = current_metrics_;
    
    // Release lock, then notify with the copy
    lock.~lock_guard();  // Explicit unlock
    
    // Now call callbacks with the copied metrics (no mutex needed)
    std::vector<MetricsCallback> callbacks;
    {
        std::lock_guard<std::mutex> callback_lock(mutex_);
        callbacks = metrics_callbacks_;
    }
    
    for (const auto& callback : callbacks) {
        try {
            callback(metrics_copy);  // Use copy instead of current_metrics_
        } catch (const std::exception& e) {
            // Log exception
        }
    }
}
 } // namespace strategy
 } // namespace trading