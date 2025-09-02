/**
 * @file liquidity_flow_monitor.cpp
 * @brief Implementation of order flow tracking
 * 
 * @author Trading System Team
 * @date May 6, 2025
 */

 #include "strategy/core_analysis/liquidity_flow_monitor.hpp"
 #include <algorithm>
 #include <numeric>
 #include <cmath>
 #include <set>
 
 namespace trading {
 namespace strategy {
 
 LiquidityFlowMonitor::LiquidityFlowMonitor(
     const core::ExchangeId& exchangeId, 
     const std::string& tradingPair, 
     const ParameterSet& params)
     : exchangeId_(exchangeId),
       trading_pair_(tradingPair),
       large_order_threshold_(params.getDouble("large_order_threshold", 5.0)),
       history_window_seconds_(params.getInt("history_window_seconds", 60)),
       typical_order_size_(1.0),
       last_update_time_(std::chrono::system_clock::now()),
       last_sequence_number_(0) {
     
     // Initialize metrics
     current_metrics_.exchangeId = exchangeId;
     current_metrics_.tradingPair = tradingPair;
     current_metrics_.timestamp = std::chrono::system_clock::now();
 }
 LiquidityFlowMonitor::LiquidityFlowMonitor(const LiquidityFlowConfig& config)
    : LiquidityFlowMonitor(config.exchangeId, config.tradingPair, config.params)
{
    // Initialize with config
    large_order_threshold_ = config.largeOrderThreshold;
    history_window_seconds_ = config.historyWindowSeconds;
    
    // Initialize metrics
    current_metrics_.exchangeId = config.exchangeId;
    current_metrics_.tradingPair = config.tradingPair;
    current_metrics_.timestamp = std::chrono::system_clock::now();
}
 LiquidityFlowMonitor::LiquidityFlowMonitor(
     const core::ExchangeId& exchangeId, 
     const std::string& tradingPair, 
     double large_order_threshold, 
     int history_window_seconds)
     : exchangeId_(exchangeId),
       trading_pair_(tradingPair),
       large_order_threshold_(large_order_threshold),
       history_window_seconds_(history_window_seconds),
       typical_order_size_(1.0),
       last_update_time_(std::chrono::system_clock::now()),
       last_sequence_number_(0) {
     
     // Initialize metrics
     current_metrics_.exchangeId = exchangeId;
     current_metrics_.tradingPair = tradingPair;
     current_metrics_.timestamp = std::chrono::system_clock::now();
 }
 
void LiquidityFlowMonitor::processOrderBookDelta(const OrderBookDelta& delta) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Store delta in history
    delta_history_.push_back(delta);
    
    // Remove old deltas based on window size
    auto cutoff_time = std::chrono::system_clock::now() - 
                      std::chrono::seconds(history_window_seconds_);
    
    while (!delta_history_.empty() && delta_history_.front().timestamp < cutoff_time) {
        delta_history_.pop_front();
    }
    
    // Update metrics based on this delta
    updateLiquidityMetrics();
    
    // Update timestamps and sequence number
    last_update_time_ = delta.timestamp;
    last_sequence_number_ = delta.sequenceNumber;
}
//  void LiquidityFlowMonitor::processOrderBookSnapshots(
//      const data_ingestion::OrderBook& prev_book,
//      const data_ingestion::OrderBook& curr_book) {
     
//      std::lock_guard<std::mutex> lock(mutex_);
     
//      // Store books in history
//      order_book_history_.push_back(curr_book);
     
//      // Remove old books based on window size
//      auto cutoff_time = std::chrono::system_clock::now() - 
//                        std::chrono::seconds(history_window_seconds_);
     
//      while (!order_book_history_.empty() && 
//             order_book_history_.front().getTimestamp() < cutoff_time) {
//          order_book_history_.pop_front();
//      }
     
//      // Compute delta between the two snapshots
//      auto delta = computeDelta(
//          prev_book.getBids(),
//          prev_book.getAsks(),
//          curr_book.getBids(),
//          curr_book.getAsks(),
//          curr_book.getTimestamp(),
//          curr_book.getSequenceNumber());
     
//      // Process the computed delta
//      processOrderBookDelta(delta);
//  }
void LiquidityFlowMonitor::processOrderBookSnapshots(
    const data_ingestion::OrderBook& prev_book,
    const data_ingestion::OrderBook& curr_book) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create a copy of the current order book using a shared pointer
    // to avoid issues with non-copyable mutex and atomic members
    auto curr_book_ptr = std::make_shared<data_ingestion::OrderBook>(
        curr_book.getExchangeId(), curr_book.getSymbol());
    
    // Apply the current snapshot to our copy
    auto snapshot = curr_book.getSnapshot();
    curr_book_ptr->applySnapshot(
        curr_book.getBids(),
        curr_book.getAsks(),
        curr_book.getSequenceNumber(),
        curr_book.getTimestamp());
    
    // Store the copied book in history
    order_book_history_.push_back(curr_book_ptr);
    
    // Remove old books based on window size
    auto cutoff_time = std::chrono::system_clock::now() - 
                      std::chrono::seconds(history_window_seconds_);
    
    while (!order_book_history_.empty() && 
           (*order_book_history_.front()).getTimestamp() < cutoff_time) {
        order_book_history_.pop_front();
    }
    
    // Compute delta between the two snapshots
    auto delta = computeDelta(
        prev_book.getBids(),
        prev_book.getAsks(),
        curr_book.getBids(),
        curr_book.getAsks(),
        curr_book.getTimestamp(),
        curr_book.getSequenceNumber());
    
    // Process the computed delta
    processOrderBookDelta(delta);
}
 LiquidityMetrics LiquidityFlowMonitor::getLiquidityMetrics() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return current_metrics_;
 }
 
 LiquidityEvents LiquidityFlowMonitor::detectLiquidityEvents() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     LiquidityEvents events;
     events.start_time = std::chrono::system_clock::now() - std::chrono::seconds(history_window_seconds_);
     events.end_time = std::chrono::system_clock::now();
     
     // Detect various types of events
     auto rapid_changes = detectRapidLiquidityChanges();
     auto spread_changes = detectSpreadChanges();
     auto imbalances = detectLiquidityImbalances();
     
     // Combine all events
     events.events.insert(events.events.end(), rapid_changes.begin(), rapid_changes.end());
     events.events.insert(events.events.end(), spread_changes.begin(), spread_changes.end());
     events.events.insert(events.events.end(), imbalances.begin(), imbalances.end());
     
     // Sort events by timestamp (newest first)
     std::sort(events.events.begin(), events.events.end(), 
         [](const LiquidityEvent& a, const LiquidityEvent& b) {
             return a.timestamp > b.timestamp;
         });
     
     return events;
 }
 
 bool LiquidityFlowMonitor::hasSignificantEvents(double min_significance) const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Check rapid liquidity changes
     auto rapid_changes = detectRapidLiquidityChanges();
     for (const auto& event : rapid_changes) {
         if (event.significance >= min_significance) {
             return true;
         }
     }
     
     // Check spread changes
     auto spread_changes = detectSpreadChanges();
     for (const auto& event : spread_changes) {
         if (event.significance >= min_significance) {
             return true;
         }
     }
     
     // Check liquidity imbalances
     auto imbalances = detectLiquidityImbalances();
     for (const auto& event : imbalances) {
         if (event.significance >= min_significance) {
             return true;
         }
     }
     
     return false;
 }
 
 void LiquidityFlowMonitor::setLargeOrderThreshold(double threshold) {
     std::lock_guard<std::mutex> lock(mutex_);
     large_order_threshold_ = threshold;
 }
 void LiquidityFlowMonitor::setHistoryWindowSize(int seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    history_window_seconds_ = seconds;
    
    // Remove old data that falls outside the new window
    auto cutoff_time = std::chrono::system_clock::now() - 
                      std::chrono::seconds(history_window_seconds_);
    
    while (!delta_history_.empty() && delta_history_.front().timestamp < cutoff_time) {
        delta_history_.pop_front();
    }
    
    // Fix: Dereference the shared pointer before accessing getTimestamp()
    while (!order_book_history_.empty() && 
           (*order_book_history_.front()).getTimestamp() < cutoff_time) {
        order_book_history_.pop_front();
    }
}

 
// Reset should also handle our shared pointers
void LiquidityFlowMonitor::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Clear all history
    delta_history_.clear();
    order_book_history_.clear();
    
    // Reset metrics
    current_metrics_ = LiquidityMetrics();
    current_metrics_.exchangeId = exchangeId_;
    current_metrics_.tradingPair = trading_pair_;
    current_metrics_.timestamp = std::chrono::system_clock::now();
    
    // Reset tracked values
    typical_order_size_ = 1.0;
    last_update_time_ = std::chrono::system_clock::now();
    last_sequence_number_ = 0;
}
 
 OrderBookDelta LiquidityFlowMonitor::computeDelta(
     const std::vector<PriceLevel>& prev_bids,
     const std::vector<PriceLevel>& prev_asks,
     const std::vector<PriceLevel>& curr_bids,
     const std::vector<PriceLevel>& curr_asks,
     core::Timestamp timestamp,
     uint64_t sequenceNumber) {
     
     OrderBookDelta delta;
     delta.exchangeId = exchangeId_;
     delta.tradingPair = trading_pair_;
     delta.timestamp = timestamp;
     delta.sequenceNumber = sequenceNumber;
     
     // Identify added and removed bids
     std::set<double> prev_bid_prices;
     std::set<double> curr_bid_prices;
     std::map<double, double> prev_bid_quantities;
     std::map<double, double> curr_bid_quantities;
     
     for (const auto& level : prev_bids) {
         prev_bid_prices.insert(level.getPrice());
         prev_bid_quantities[level.getPrice()] = level.getQuantity();
     }
     
     for (const auto& level : curr_bids) {
        curr_bid_prices.insert(level.getPrice());
        curr_bid_quantities[level.getPrice()] = level.getQuantity();
    }
    
    // Find added bids (in current but not in previous)
    for (const auto& level : curr_bids) {
        double price = level.getPrice();
        if (prev_bid_prices.find(price) == prev_bid_prices.end()) {
            // This is a new price level
            delta.added_bids.push_back(level);
        } else if (std::abs(curr_bid_quantities[price] - prev_bid_quantities[price]) > 1e-6) {
            // Quantity changed
            if (curr_bid_quantities[price] > prev_bid_quantities[price]) {
                // Quantity increased, add the difference as an addition
                PriceLevel added_level(price, curr_bid_quantities[price] - prev_bid_quantities[price]);
                delta.added_bids.push_back(added_level);
            } else {
                // Quantity decreased, add the difference as a removal
                PriceLevel removed_level(price, prev_bid_quantities[price] - curr_bid_quantities[price]);
                delta.removed_bids.push_back(removed_level);
            }
        }
    }
    
    // Find removed bids (in previous but not in current)
    for (const auto& level : prev_bids) {
        double price = level.getPrice();
        if (curr_bid_prices.find(price) == curr_bid_prices.end()) {
            // This price level was removed
            delta.removed_bids.push_back(level);
        }
    }
    
    // Identify added and removed asks
    std::set<double> prev_ask_prices;
    std::set<double> curr_ask_prices;
    std::map<double, double> prev_ask_quantities;
    std::map<double, double> curr_ask_quantities;
    
    for (const auto& level : prev_asks) {
        prev_ask_prices.insert(level.getPrice());
        prev_ask_quantities[level.getPrice()] = level.getQuantity();
    }
    
    for (const auto& level : curr_asks) {
        curr_ask_prices.insert(level.getPrice());
        curr_ask_quantities[level.getPrice()] = level.getQuantity();
    }
    
    // Find added asks (in current but not in previous)
    for (const auto& level : curr_asks) {
        double price = level.getPrice();
        if (prev_ask_prices.find(price) == prev_ask_prices.end()) {
            // This is a new price level
            delta.added_asks.push_back(level);
        } else if (std::abs(curr_ask_quantities[price] - prev_ask_quantities[price]) > 1e-6) {
            // Quantity changed
            if (curr_ask_quantities[price] > prev_ask_quantities[price]) {
                // Quantity increased, add the difference as an addition
                PriceLevel added_level(price, curr_ask_quantities[price] - prev_ask_quantities[price]);
                delta.added_asks.push_back(added_level);
            } else {
                // Quantity decreased, add the difference as a removal
                PriceLevel removed_level(price, prev_ask_quantities[price] - curr_ask_quantities[price]);
                delta.removed_asks.push_back(removed_level);
            }
        }
    }
    
    // Find removed asks (in previous but not in current)
    for (const auto& level : prev_asks) {
        double price = level.getPrice();
        if (curr_ask_prices.find(price) == curr_ask_prices.end()) {
            // This price level was removed
            delta.removed_asks.push_back(level);
        }
    }
    
    return delta;
}
// //  updateParameters
void LiquidityFlowMonitor::updateParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Update parameters
    large_order_threshold_ = params.getDouble("large_order_threshold", large_order_threshold_);
    history_window_seconds_ = params.getInt("history_window_seconds", history_window_seconds_);
    
    // Reset the monitor state if necessary
    reset();
}

std::vector<PriceLevel> LiquidityFlowMonitor::identifyLargeOrders(
    const std::vector<PriceLevel>& levels) const {
    
    std::vector<PriceLevel> large_orders;
    
    // If we don't have a typical order size yet, return empty
    if (typical_order_size_ <= 0) {
        return large_orders;
    }
    
    // Identify orders significantly larger than typical
    for (const auto& level : levels) {
        if (level.getQuantity() > typical_order_size_ * large_order_threshold_) {
            large_orders.push_back(level);
        }
    }
    
    return large_orders;
}

std::vector<LiquidityEvent> LiquidityFlowMonitor::detectRapidLiquidityChanges(
    int window_size, double threshold) const {
    
    std::vector<LiquidityEvent> events;
    
    // Need enough history for detection
    if (delta_history_.size() < static_cast<size_t>(window_size)) {
        return events;
    }
    
    // Get recent deltas
    std::vector<OrderBookDelta> recent_deltas(
        delta_history_.end() - std::min(delta_history_.size(), static_cast<size_t>(window_size)),
        delta_history_.end());
    
    // Calculate baseline rates of liquidity addition/removal
    double avg_bid_add_rate = 0.0;
    double avg_ask_add_rate = 0.0;
    double avg_bid_remove_rate = 0.0;
    double avg_ask_remove_rate = 0.0;
    
    for (const auto& delta : recent_deltas) {
        avg_bid_add_rate += delta.getTotalAddedBidVolume();
        avg_ask_add_rate += delta.getTotalAddedAskVolume();
        avg_bid_remove_rate += delta.getTotalRemovedBidVolume();
        avg_ask_remove_rate += delta.getTotalRemovedAskVolume();
    }
    
    avg_bid_add_rate /= recent_deltas.size();
    avg_ask_add_rate /= recent_deltas.size();
    avg_bid_remove_rate /= recent_deltas.size();
    avg_ask_remove_rate /= recent_deltas.size();
    
    // Check the most recent delta for significant changes
    const auto& latest_delta = recent_deltas.back();
    
    // Check for rapid bid additions
    if (latest_delta.getTotalAddedBidVolume() > avg_bid_add_rate * threshold) {
        // Find the price level with the largest addition
        double max_add_volume = 0.0;
        double max_add_price = 0.0;
        
        for (const auto& level : latest_delta.added_bids) {
            if (level.getQuantity() > max_add_volume) {
                max_add_volume = level.getQuantity();
                max_add_price = level.getPrice();
            }
        }
        
        double significance = latest_delta.getTotalAddedBidVolume() / (avg_bid_add_rate * threshold);
        significance = std::min(1.0, significance - 0.5); // Normalize to [0, 1]
        
        events.emplace_back(
            LiquidityEvent::Type::RAPID_LIQUIDITY_ADDITION,
            exchangeId_,
            trading_pair_,
            latest_delta.timestamp,
            core::Direction::BUY,
            max_add_price,
            latest_delta.getTotalAddedBidVolume(),
            significance);
    }
    
    // Check for rapid ask additions
    if (latest_delta.getTotalAddedAskVolume() > avg_ask_add_rate * threshold) {
        // Find the price level with the largest addition
        double max_add_volume = 0.0;
        double max_add_price = 0.0;
        
        for (const auto& level : latest_delta.added_asks) {
            if (level.getQuantity() > max_add_volume) {
                max_add_volume = level.getQuantity();
                max_add_price = level.getPrice();
            }
        }
        
        double significance = latest_delta.getTotalAddedAskVolume() / (avg_ask_add_rate * threshold);
        significance = std::min(1.0, significance - 0.5); // Normalize to [0, 1]
        
        events.emplace_back(
            LiquidityEvent::Type::RAPID_LIQUIDITY_ADDITION,
            exchangeId_,
            trading_pair_,
            latest_delta.timestamp,
            core::Direction::SELL,
            max_add_price,
            latest_delta.getTotalAddedAskVolume(),
            significance);
    }
    
    // Check for rapid bid removals
    if (latest_delta.getTotalRemovedBidVolume() > avg_bid_remove_rate * threshold) {
        // Find the price level with the largest removal
        double max_remove_volume = 0.0;
        double max_remove_price = 0.0;
        
        for (const auto& level : latest_delta.removed_bids) {
            if (level.getQuantity() > max_remove_volume) {
                max_remove_volume = level.getQuantity();
                max_remove_price = level.getPrice();
            }
        }
        
        double significance = latest_delta.getTotalRemovedBidVolume() / (avg_bid_remove_rate * threshold);
        significance = std::min(1.0, significance - 0.5); // Normalize to [0, 1]
        
        events.emplace_back(
            LiquidityEvent::Type::RAPID_LIQUIDITY_REMOVAL,
            exchangeId_,
            trading_pair_,
            latest_delta.timestamp,
            core::Direction::SELL, // Removing bids often signals selling pressure
            max_remove_price,
            latest_delta.getTotalRemovedBidVolume(),
            significance);
    }
    
    // Check for rapid ask removals
    if (latest_delta.getTotalRemovedAskVolume() > avg_ask_remove_rate * threshold) {
        // Find the price level with the largest removal
        double max_remove_volume = 0.0;
        double max_remove_price = 0.0;
        
        for (const auto& level : latest_delta.removed_asks) {
            if (level.getQuantity() > max_remove_volume) {
                max_remove_volume = level.getQuantity();
                max_remove_price = level.getPrice();
            }
        }
        
        double significance = latest_delta.getTotalRemovedAskVolume() / (avg_ask_remove_rate * threshold);
        significance = std::min(1.0, significance - 0.5); // Normalize to [0, 1]
        
        events.emplace_back(
            LiquidityEvent::Type::RAPID_LIQUIDITY_REMOVAL,
            exchangeId_,
            trading_pair_,
            latest_delta.timestamp,
            core::Direction::BUY, // Removing asks often signals buying pressure
            max_remove_price,
            latest_delta.getTotalRemovedAskVolume(),
            significance);
    }
    
    return events;
}

 

// 2. Fix the detectSpreadChanges method
std::vector<LiquidityEvent> LiquidityFlowMonitor::detectSpreadChanges(
    int window_size, double threshold) const {
    
    std::vector<LiquidityEvent> events;
    
    // Need enough history for detection
    if (order_book_history_.size() < static_cast<size_t>(window_size + 1)) {
        return events;
    }
    
    // Get recent order books (as references to the actual OrderBook objects through dereferencing)
    std::vector<std::reference_wrapper<const data_ingestion::OrderBook>> recent_books;
    
    // Take the window_size + 1 most recent books
    for (auto it = order_book_history_.end() - std::min(order_book_history_.size(), 
                                                        static_cast<size_t>(window_size + 1));
         it != order_book_history_.end(); ++it) {
        // Dereference the shared pointer to get a reference to the OrderBook
        recent_books.push_back(std::cref(**it));
    }
    
    // Calculate baseline spread
    std::vector<double> spreads;
    for (size_t i = 0; i < recent_books.size() - 1; ++i) {
        spreads.push_back(recent_books[i].get().getSpread());
    }
    
    // Calculate average and standard deviation of spread
    double avg_spread = std::accumulate(spreads.begin(), spreads.end(), 0.0) / spreads.size();
    
    double variance = 0.0;
    for (double spread : spreads) {
        double diff = spread - avg_spread;
        variance += diff * diff;
    }
    variance /= spreads.size();
    double std_dev = std::sqrt(variance);
    
    // Check the most recent spread change
    double current_spread = recent_books.back().get().getSpread();
    double previous_spread = recent_books[recent_books.size() - 2].get().getSpread();
    double spread_change = current_spread - previous_spread;
    
    // Detect significant spread changes
    if (std_dev > 0 && std::abs(spread_change) > threshold * std_dev) {
        LiquidityEvent::Type event_type;
        core::Direction direction;
        
        if (spread_change > 0) {
            event_type = LiquidityEvent::Type::SPREAD_EXPANSION;
            direction = core::Direction::NEUTRAL; // Spread expansion often indicates uncertainty
        } else {
            event_type = LiquidityEvent::Type::SPREAD_COLLAPSE;
            // Spread collapse with higher volume might indicate directional movement
            direction = current_metrics_.flow_imbalance > 0 ? core::Direction::BUY : core::Direction::SELL;
        }
        
        double significance = std::abs(spread_change) / (threshold * std_dev);
        significance = std::min(1.0, significance - 0.5); // Normalize to [0, 1]
        
        events.emplace_back(
            event_type,
            exchangeId_,
            trading_pair_,
            recent_books.back().get().getTimestamp(),
            direction,
            recent_books.back().get().getMidPrice(),
            std::abs(spread_change),
            significance);
    }
    
    return events;
}

std::vector<LiquidityEvent> LiquidityFlowMonitor::detectLiquidityImbalances(
    double threshold) const {
    
    std::vector<LiquidityEvent> events;
    
    // Not enough data for detection
    if (delta_history_.empty() || order_book_history_.empty()) {
        return events;
    }
    
    // Check for significant bid/ask imbalance
    if (std::abs(current_metrics_.flow_imbalance) > threshold) {
        core::Direction direction = current_metrics_.flow_imbalance > 0 ? core::Direction::BUY : core::Direction::SELL;
        double significance = std::abs(current_metrics_.flow_imbalance) / threshold;
        significance = std::min(1.0, significance - 0.5); // Normalize to [0, 1]
        
        events.emplace_back(
            LiquidityEvent::Type::LIQUIDITY_IMBALANCE,
            exchangeId_,
            trading_pair_,
            std::chrono::system_clock::now(),
            direction,
            (*order_book_history_.back()).getMidPrice(),  // Dereferenced
            std::abs(current_metrics_.flow_imbalance),
            significance);
    }
    
    return events;
}

void LiquidityFlowMonitor::updateLiquidityMetrics() {
    // Not enough data for calculations
    if (delta_history_.empty()) {
        return;
    }
    
    // Update timestamp
    current_metrics_.timestamp = std::chrono::system_clock::now();
    
    // Get recent deltas (up to last 10 seconds)
    auto cutoff_time = std::chrono::system_clock::now() - std::chrono::seconds(10);
    std::vector<OrderBookDelta> recent_deltas;
    
    for (auto it = delta_history_.rbegin(); it != delta_history_.rend(); ++it) {
        if (it->timestamp >= cutoff_time) {
            recent_deltas.push_back(*it);
        } else {
            break;
        }
    }
    
    if (recent_deltas.empty()) {
        return;
    }
    
    // Calculate time window in seconds
    double time_window = std::chrono::duration<double>(
        recent_deltas.front().timestamp - recent_deltas.back().timestamp).count();
    
    if (time_window <= 0) {
        time_window = 1.0; // Avoid division by zero
    }
    
    // Calculate bid side metrics
    double total_bid_adds = 0.0;
    double total_bid_removes = 0.0;
    
    for (const auto& delta : recent_deltas) {
        total_bid_adds += delta.getTotalAddedBidVolume();
        total_bid_removes += delta.getTotalRemovedBidVolume();
    }
    
    current_metrics_.bid_add_rate = total_bid_adds / time_window;
    current_metrics_.bid_cancel_rate = total_bid_removes / time_window;
    
    // Estimate execution rate as a portion of removals
    current_metrics_.bid_execution_rate = current_metrics_.bid_cancel_rate * 0.2; // Heuristic
    
    // Try to estimate modification rate (imperfect)
    current_metrics_.bid_modification_rate = 
        std::min(current_metrics_.bid_add_rate, current_metrics_.bid_cancel_rate) * 0.5; // Heuristic
    
    // Calculate bid toxicity (high cancel rate relative to additions)
    if (current_metrics_.bid_add_rate > 0) {
        current_metrics_.bid_toxicity = 
            current_metrics_.bid_cancel_rate / current_metrics_.bid_add_rate;
    }
    
    // Calculate ask side metrics
    double total_ask_adds = 0.0;
    double total_ask_removes = 0.0;
    
    for (const auto& delta : recent_deltas) {
        total_ask_adds += delta.getTotalAddedAskVolume();
        total_ask_removes += delta.getTotalRemovedAskVolume();
    }
    
    current_metrics_.ask_add_rate = total_ask_adds / time_window;
    current_metrics_.ask_cancel_rate = total_ask_removes / time_window;
    
    // Estimate execution rate as a portion of removals
    current_metrics_.ask_execution_rate = current_metrics_.ask_cancel_rate * 0.2; // Heuristic
    
    // Try to estimate modification rate (imperfect)
    current_metrics_.ask_modification_rate = 
        std::min(current_metrics_.ask_add_rate, current_metrics_.ask_cancel_rate) * 0.5; // Heuristic
    
    // Calculate ask toxicity (high cancel rate relative to additions)
    if (current_metrics_.ask_add_rate > 0) {
        current_metrics_.ask_toxicity = 
            current_metrics_.ask_cancel_rate / current_metrics_.ask_add_rate;
    }
    
    // Calculate combined metrics
    double total_adds = current_metrics_.bid_add_rate + current_metrics_.ask_add_rate;
    double total_cancels = current_metrics_.bid_cancel_rate + current_metrics_.ask_cancel_rate;
    
    // Flow imbalance (negative = more sell flow, positive = more buy flow)
    if (total_adds > 0) {
        current_metrics_.flow_imbalance = 
            (current_metrics_.bid_add_rate - current_metrics_.ask_add_rate) / total_adds;
    }
    
    // Cancellation ratio
    if (total_adds > 0) {
        current_metrics_.cancellation_ratio = total_cancels / total_adds;
    }
    
    // Calculate large order ratio
    double large_order_volume = 0.0;
    double total_volume = 0.0;
    
    for (const auto& delta : recent_deltas) {
        // Check for large orders in added bids
        for (const auto& level : delta.added_bids) {
            total_volume += level.getQuantity();
            if (level.getQuantity() > typical_order_size_ * large_order_threshold_) {
                large_order_volume += level.getQuantity();
            }
        }
        
        // Check for large orders in added asks
        for (const auto& level : delta.added_asks) {
            total_volume += level.getQuantity();
            if (level.getQuantity() > typical_order_size_ * large_order_threshold_) {
                large_order_volume += level.getQuantity();
            }
        }
    }
    
    if (total_volume > 0) {
        current_metrics_.large_order_ratio = large_order_volume / total_volume;
    }
    
    // Calculate pressure index (composite metric)
    current_metrics_.pressure_index = 
        0.4 * std::abs(current_metrics_.flow_imbalance) + 
        0.3 * current_metrics_.large_order_ratio + 
        0.3 * (current_metrics_.cancellation_ratio - 1.0);
    
    // Update typical order size
    typical_order_size_ = calculateTypicalOrderSize();
}

double LiquidityFlowMonitor::calculateTypicalOrderSize() const {
    std::vector<double> order_sizes;
    
    // Collect order sizes from recent deltas
    for (const auto& delta : delta_history_) {
        for (const auto& level : delta.added_bids) {
            order_sizes.push_back(level.getQuantity());
        }
        
        for (const auto& level : delta.added_asks) {
            order_sizes.push_back(level.getQuantity());
        }
    }
    
    // If we don't have enough samples, return current value
    if (order_sizes.size() < 10) {
        return typical_order_size_;
    }
    
    // Sort sizes to find median
    std::sort(order_sizes.begin(), order_sizes.end());
    
    // Return median
    if (order_sizes.size() % 2 == 0) {
        return (order_sizes[order_sizes.size() / 2 - 1] + order_sizes[order_sizes.size() / 2]) / 2.0;
    } else {
        return order_sizes[order_sizes.size() / 2];
    }
}

std::shared_ptr<LiquidityFlowMonitor> createLiquidityFlowMonitor(
    const core::ExchangeId& exchangeId, 
    const std::string& tradingPair,
    const ParameterSet& params) {
    
    return std::make_shared<LiquidityFlowMonitor>(exchangeId, tradingPair, params);
}

} // namespace strategy
} // namespace trading