/**
 * @file order_book_fixed.cpp
 * @brief Memory-optimized implementation of order book
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * Added memory management features to prevent memory leaks.
 * 
 * Created on: April 30, 2025
 * Updated: September 21, 2025 - Added memory management
 */
#include "data_ingestion/orderbook/order_book.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <boost/crc.hpp>
#include <boost/functional/hash.hpp>
#include "common/logger.hpp"
#include <fmt/format.h>

namespace trading {
namespace data_ingestion {

// OrderBookDelta methods (unchanged)
double OrderBookDelta::getTotalAddedBidVolume() const {
    double total = 0.0;
    for (const auto& level : added_bids) {
        total += level.quantity;
    }
    return total;
}

double OrderBookDelta::getTotalRemovedBidVolume() const {
    double total = 0.0;
    for (const auto& level : removed_bids) {
        total += level.quantity;
    }
    return total;
}

double OrderBookDelta::getTotalAddedAskVolume() const {
    double total = 0.0;
    for (const auto& level : added_asks) {
        total += level.quantity;
    }
    return total;
}

double OrderBookDelta::getTotalRemovedAskVolume() const {
    double total = 0.0;
    for (const auto& level : removed_asks) {
        total += level.quantity;
    }
    return total;
}

// OrderBook constructor with config
OrderBook::OrderBook(const ExchangeId& exchangeId, const Symbol& symbol, 
                     OrderBookCallback callback, const OrderBookConfig& config)
    : config_(config),
      exchangeId_(exchangeId),
      symbol_(symbol),
      callback_(std::move(callback)),
      sequence_number_(0),
      is_valid_(false),
      last_cleanup_time_(std::chrono::system_clock::now()) {
    
}

bool OrderBook::applySnapshot(const std::vector<PriceLevel>& bids, 
                            const std::vector<PriceLevel>& asks,
                            uint64_t sequenceNumber,
                            Timestamp timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
 
    // Skip older snapshots
    if (sequenceNumber <= sequence_number_) {
        return false;
    }
    
    // Store the current state for delta calculation
    captureOrderBookState();
    
    // Initialize a delta for this snapshot
    OrderBookDelta delta{
        .exchangeId = exchangeId_,
        .tradingPair = symbol_,
        .timestamp = timestamp,
        .sequenceNumber = sequenceNumber
    };
    
    // Clear existing book and track removed levels
    for (const auto& [price, level] : bids_) {
        delta.removed_bids.push_back(level);
    }
    
    for (const auto& [price, level] : asks_) {
        delta.removed_asks.push_back(level);
    }
    
    bids_.clear();
    asks_.clear();
    
    // Add bid levels with depth limit
    size_t bid_count = 0;
    for (const auto& bid : bids) {
        if (bid_count >= config_.max_depth) break;
        updateBidLevel(bid);
        delta.added_bids.push_back(bid);
        bid_count++;
    }
    
    // Add ask levels with depth limit
    size_t ask_count = 0;
    for (const auto& ask : asks) {
        if (ask_count >= config_.max_depth) break;
        updateAskLevel(ask);
        delta.added_asks.push_back(ask);
        ask_count++;
    }
    
    // Update metadata
    sequence_number_ = sequenceNumber;
    timestamp_ = timestamp;
    is_valid_ = true;
    
    // Store this delta
    storeDelta(delta);
    
    // Perform cleanup if needed
    autoCleanupIfNeeded();
    
    // Notify subscribers
    notifyCallback();
    notifyDeltaCallback(delta);
    
    return true;
}

bool OrderBook::applyUpdate(const std::vector<PriceLevel>& bids,
                          const std::vector<PriceLevel>& asks,
                          uint64_t sequenceNumber,
                          Timestamp timestamp) {
    // Skip out-of-sequence updates
    if (sequenceNumber <= sequence_number_) {
        return false;
    }
    
    // Detect sequence gaps
    if (is_valid_ && sequenceNumber > sequence_number_ + 1) {
        is_valid_ = false;
        std::cerr << "Sequence gap detected for "
                  << exchangeId_.toString() << " "
                  << symbol_.base << "/" << symbol_.quote
                  << " (" << sequence_number_
                  << " -> " << sequenceNumber << ")" << std::endl;
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Store the current state for delta calculation
    captureOrderBookState();
    
    // Initialize a delta for this update
    OrderBookDelta delta{
        .exchangeId = exchangeId_,
        .tradingPair = symbol_,
        .timestamp = timestamp,
        .sequenceNumber = sequenceNumber
    };
    
    // Apply bid updates and track deltas
    for (const auto& bid : bids) {
        trackBidDelta(bid, delta);
        updateBidLevel(bid);
    }
    
    // Apply ask updates and track deltas
    for (const auto& ask : asks) {
        trackAskDelta(ask, delta);
        updateAskLevel(ask);
    }
    
    // Enforce depth limits after updates
    enforceBidDepthLimit();
    enforceAskDepthLimit();
    
    // Update metadata
    sequence_number_ = sequenceNumber;
    timestamp_ = timestamp;
    
    // Store this delta if it's not empty
    if (!delta.isEmpty()) {
        storeDelta(delta);
        notifyDeltaCallback(delta);
    }
    
    // Perform cleanup if needed
    autoCleanupIfNeeded();
    
    // Notify subscribers
    notifyCallback();
    
    return true;
}

void OrderBook::enforceBidDepthLimit() {
    if (bids_.size() > config_.cleanup_threshold) {
        trimBidsToDepth(config_.max_depth);
    }
}

void OrderBook::enforceAskDepthLimit() {
    if (asks_.size() > config_.cleanup_threshold) {
        trimAsksToDepth(config_.max_depth);
    }
}

void OrderBook::trimBidsToDepth(size_t max_depth) {
    if (bids_.size() <= max_depth) return;
    
    auto it = bids_.begin();
    std::advance(it, max_depth);
    bids_.erase(it, bids_.end());
}

void OrderBook::trimAsksToDepth(size_t max_depth) {
    if (asks_.size() <= max_depth) return;
    
    auto it = asks_.begin();
    std::advance(it, max_depth);
    asks_.erase(it, asks_.end());
}

void OrderBook::performCleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Trim order book depth
    trimBidsToDepth(config_.max_depth);
    trimAsksToDepth(config_.max_depth);
    
    // Clear old deltas
    clearOldDeltas();
    
    // Update cleanup timestamp
    last_cleanup_time_ = std::chrono::system_clock::now();
}

void OrderBook::clearOldDeltas() {
    while (recent_deltas_.size() > config_.max_delta_history) {
        recent_deltas_.pop_front();
    }
}

void OrderBook::autoCleanupIfNeeded() {
    if (!config_.enable_auto_cleanup) return;
    
    auto now = std::chrono::system_clock::now();
    auto time_since_cleanup = std::chrono::duration_cast<std::chrono::minutes>(
        now - last_cleanup_time_);
    
    if (time_since_cleanup >= config_.cleanup_interval || shouldPerformCleanup()) {
        performCleanup();
    }
}

bool OrderBook::shouldPerformCleanup() const {
    return (bids_.size() > config_.cleanup_threshold) ||
           (asks_.size() > config_.cleanup_threshold) ||
           (recent_deltas_.size() > config_.max_delta_history);
}

void OrderBook::setMaxDepth(size_t max_depth) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_.max_depth = max_depth;
    
    // Immediately enforce new limits
    trimBidsToDepth(max_depth);
    trimAsksToDepth(max_depth);
}

size_t OrderBook::getMemoryUsage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    size_t total = sizeof(*this);
    
    // Order book levels
    total += bids_.size() * (sizeof(double) + sizeof(PriceLevel));
    total += asks_.size() * (sizeof(double) + sizeof(PriceLevel));
    
    // Previous state
    total += previous_bids_.size() * (sizeof(double) + sizeof(PriceLevel));
    total += previous_asks_.size() * (sizeof(double) + sizeof(PriceLevel));
    
    // Delta history
    for (const auto& delta : recent_deltas_) {
        total += delta.getMemoryUsage();
    }
    
    return total;
}

void OrderBook::updateConfig(const OrderBookConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    config_ = config;
    
    // Apply new limits immediately
    if (bids_.size() > config_.max_depth) {
        trimBidsToDepth(config_.max_depth);
    }
    if (asks_.size() > config_.max_depth) {
        trimAsksToDepth(config_.max_depth);
    }
    if (recent_deltas_.size() > config_.max_delta_history) {
        clearOldDeltas();
    }
}

// Debug and monitoring methods
size_t OrderBook::getBidLevelCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return bids_.size();
}

size_t OrderBook::getAskLevelCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return asks_.size();
}

size_t OrderBook::getDeltaHistorySize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return recent_deltas_.size();
}

// Rest of the methods remain the same as original implementation
void OrderBook::trackBidDelta(const PriceLevel& level, OrderBookDelta& delta) {
    double price = level.price;
    double quantity = level.quantity;
    
    auto it = bids_.find(price);
    
    if (quantity <= 0.0) {
        if (it != bids_.end()) {
            delta.removed_bids.push_back(it->second);
        }
    } else {
        if (it == bids_.end()) {
            delta.added_bids.push_back(level);
        } else {
            delta.removed_bids.push_back(it->second);
            delta.added_bids.push_back(level);
        }
    }
}

void OrderBook::trackAskDelta(const PriceLevel& level, OrderBookDelta& delta) {
    double price = level.price;
    double quantity = level.quantity;
    
    auto it = asks_.find(price);
    
    if (quantity <= 0.0) {
        if (it != asks_.end()) {
            delta.removed_asks.push_back(it->second);
        }
    } else {
        if (it == asks_.end()) {
            delta.added_asks.push_back(level);
        } else {
            delta.removed_asks.push_back(it->second);
            delta.added_asks.push_back(level);
        }
    }
}

void OrderBook::updateBidLevel(const PriceLevel& level) {
    double price = level.price;
    
    if (level.quantity <= 0.0) {
        bids_.erase(price);
    } else {
        bids_[price] = level;
    }
    
    last_update_time_ = std::chrono::system_clock::now();
}

void OrderBook::updateAskLevel(const PriceLevel& level) {
    double price = level.price;
    
    if (level.quantity <= 0.0) {
        asks_.erase(price);
    } else {
        asks_[price] = level;
    }
    
    last_update_time_ = std::chrono::system_clock::now();
}

void OrderBook::captureOrderBookState() {
    previous_bids_ = bids_;
    previous_asks_ = asks_;
}

void OrderBook::storeDelta(const OrderBookDelta& delta) {
    recent_deltas_.push_back(delta);
    
    // Maintain maximum history size
    if (recent_deltas_.size() > config_.max_delta_history) {
        recent_deltas_.pop_front();
    }
}

// All other methods remain the same as in original implementation...
// (getMidPrice, getSpread, getBids, getAsks, etc.)

OrderBookDelta OrderBook::getOrderBookDelta(Timestamp previous_timestamp) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    OrderBookDelta result{
        .exchangeId = exchangeId_,
        .tradingPair = symbol_,
        .timestamp = timestamp_,
        .sequenceNumber = sequence_number_
    };
    
    if (previous_timestamp == Timestamp{}) {
        for (const auto& [price, level] : bids_) {
            result.added_bids.push_back(level);
        }
        
        for (const auto& [price, level] : asks_) {
            result.added_asks.push_back(level);
        }
        
        return result;
    }
    
    bool found_relevant_deltas = false;
    
    for (auto it = recent_deltas_.rbegin(); it != recent_deltas_.rend(); ++it) {
        const auto& delta = *it;
        
        if (delta.timestamp > previous_timestamp) {
            found_relevant_deltas = true;
            
            result.added_bids.insert(result.added_bids.end(), 
                                    delta.added_bids.begin(), 
                                    delta.added_bids.end());
            
            result.removed_bids.insert(result.removed_bids.end(), 
                                      delta.removed_bids.begin(), 
                                      delta.removed_bids.end());
            
            result.added_asks.insert(result.added_asks.end(), 
                                    delta.added_asks.begin(), 
                                    delta.added_asks.end());
            
            result.removed_asks.insert(result.removed_asks.end(), 
                                      delta.removed_asks.begin(), 
                                      delta.removed_asks.end());
        } else {
            break;
        }
    }
    
    if (!found_relevant_deltas) {
        for (const auto& [price, level] : bids_) {
            result.added_bids.push_back(level);
        }
        
        for (const auto& [price, level] : asks_) {
            result.added_asks.push_back(level);
        }
    }
    
    return result;
}

OrderBookUpdate OrderBook::getSnapshot(int depth) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    OrderBookUpdate update;
    update.exchangeId = exchangeId_;
    update.tradingPair = symbol_;
    update.exchangeTimestamp = timestamp_;
    update.receivedTimestamp = last_update_time_;
    update.sequenceNumber = sequence_number_;
    update.isSnapshot = true;
    
    int bid_count = 0;
    for (const auto& [price, level] : bids_) {
        update.bids.push_back(level);
        bid_count++;
        if (depth > 0 && bid_count >= depth) break;
    }
    
    int ask_count = 0;
    for (const auto& [price, level] : asks_) {
        update.asks.push_back(level);
        ask_count++;
        if (depth > 0 && ask_count >= depth) break;
    }
    
    update.checksum = calculateChecksum();
    
    return update;
}

std::vector<PriceLevel> OrderBook::getBids(int depth) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PriceLevel> result;
    
    int count = 0;
    for (const auto& [price, level] : bids_) {
        result.push_back(level);
        count++;
        if (depth > 0 && count >= depth) break;
    }
    
    return result;
}

std::vector<PriceLevel> OrderBook::getAsks(int depth) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<PriceLevel> result;
    
    int count = 0;
    for (const auto& [price, level] : asks_) {
        result.push_back(level);
        count++;
        if (depth > 0 && count >= depth) break;
    }
    
    return result;
}

PriceLevel OrderBook::getBestBid() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (bids_.empty()) {
        return PriceLevel();
    }
    
    return bids_.begin()->second;
}

PriceLevel OrderBook::getBestAsk() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (asks_.empty()) {
        return PriceLevel();
    }
    
    return asks_.begin()->second;
}

double OrderBook::getMidPrice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (bids_.empty() || asks_.empty()) {
        return 0.0;
    }
    
    double bestBid = bids_.begin()->first;
    double bestAsk = asks_.begin()->first;
    
    return (bestBid + bestAsk) / 2.0;
}

double OrderBook::getSpread() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (bids_.empty() || asks_.empty()) {
        return -1.0;
    }
    
    double bestBid = bids_.begin()->first;
    double bestAsk = asks_.begin()->first;
    
    return bestAsk - bestBid;
}

double OrderBook::getSpreadPercent() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (bids_.empty() || asks_.empty()) {
        return -1.0;
    }
    
    double bestBid = bids_.begin()->first;
    double bestAsk = asks_.begin()->first;
    
    double midPrice = (bestBid + bestAsk) / 2.0;
    if (midPrice <= 0.0) {
        return -1.0;
    }
    
    return (bestAsk - bestBid) / midPrice * 100.0;
}

double OrderBook::getBidVolume(int depth) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    double volume = 0.0;
    int count = 0;
    
    for (auto it = bids_.begin(); it != bids_.end() && (depth == 0 || count < depth); ++it, ++count) {
        const auto& level = it->second;
        volume += level.quantity;
    }
    
    return volume;
}

double OrderBook::getAskVolume(int depth) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    double volume = 0.0;
    int count = 0;
    
    for (auto it = asks_.begin(); it != asks_.end() && (depth == 0 || count < depth); ++it, ++count) {
        const auto& level = it->second;
        volume += level.quantity;
    }
    
    return volume;
}

double OrderBook::getVolumeImbalance(int depth) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    double bidVolume = getBidVolume(depth);
    double askVolume = getAskVolume(depth);
    
    double total_volume = bidVolume + askVolume;
    if (total_volume <= 0.0) {
        return 0.0;
    }
    
    return (bidVolume - askVolume) / total_volume;
}

std::string OrderBook::calculateChecksum() const {
    std::stringstream ss;
    
    const int levels_to_include = 10;
    int bid_count = 0;
    int ask_count = 0;
    
    for (auto it = bids_.begin(); it != bids_.end() && bid_count < levels_to_include; ++it, ++bid_count) {
        const auto& level = it->second;
        
        uint64_t price_int = static_cast<uint64_t>(level.price * 100000000);
        uint64_t qty_int = static_cast<uint64_t>(level.quantity * 100000000);
        
        ss << "B:" << price_int << ":" << qty_int << "|";
    }
    
    for (auto it = asks_.begin(); it != asks_.end() && ask_count < levels_to_include; ++it, ++ask_count) {
        const auto& level = it->second;
        
        uint64_t price_int = static_cast<uint64_t>(level.price * 100000000);
        uint64_t qty_int = static_cast<uint64_t>(level.quantity * 100000000);
        
        ss << "A:" << price_int << ":" << qty_int << "|";
    }
    
    boost::crc_32_type crc;
    std::string data = ss.str();
    crc.process_bytes(data.data(), data.size());
    
    std::stringstream checksum;
    checksum << std::hex << std::setw(8) << std::setfill('0') << crc.checksum();
    
    return checksum.str();
}

void OrderBook::notifyCallback() {
    if (!callback_) {
        return;
    }
    
    callback_(*this);
}

void OrderBook::notifyDeltaCallback(const OrderBookDelta& delta) {
    if (!delta_callback_) {
        return;
    }
    
    delta_callback_(delta);
}

} // namespace data_ingestion
} // namespace trading