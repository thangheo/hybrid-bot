/**
 * @file order_book.cpp
 * @brief Implementation of order book and related classes
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */
#include "data_ingestion/orderbook/order_book.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <boost/crc.hpp> // for crc_32_type
#include <boost/functional/hash.hpp>

namespace trading {
namespace data_ingestion {

// OrderBookDelta methods
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

OrderBook::OrderBook(const ExchangeId& exchangeId, const Symbol& symbol, OrderBookCallback callback)
    : exchangeId_(exchangeId),  // Also fix the member initialization
      symbol_(symbol),
      callback_(std::move(callback)),
      sequence_number_(0),
      is_valid_(false) {
}
OrderBook::OrderBook(const ExchangeId& exchangeId, const std::string& tradingPair, OrderBookCallback callback)
    : exchangeId_(exchangeId),
      callback_(std::move(callback)),
      sequence_number_(0),
      is_valid_(false) {
    
    // Parse trading pair
    size_t separator = tradingPair.find('/');
    if (separator != std::string::npos) {
        symbol_.base = tradingPair.substr(0, separator);
        symbol_.quote = tradingPair.substr(separator + 1);
    } else {
        symbol_.base = tradingPair;
        symbol_.quote = "";
    }
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
        .tradingPair = symbol_.toString(),
        .timestamp = timestamp,
        .sequenceNumber = sequenceNumber
    };
    
    // Clear existing book
    for (const auto& [price, level] : bids_) {
        delta.removed_bids.push_back(level);
    }
    
    for (const auto& [price, level] : asks_) {
        delta.removed_asks.push_back(level);
    }
    
    bids_.clear();
    asks_.clear();
    
    // Add all bid levels
    for (const auto& bid : bids) {
        updateBidLevel(bid);
        delta.added_bids.push_back(bid);
    }
    
    // Add all ask levels
    for (const auto& ask : asks) {
        updateAskLevel(ask);
        delta.added_asks.push_back(ask);
    }
    
    // Update metadata
    sequence_number_ = sequenceNumber;
    timestamp_ = timestamp;
    is_valid_ = true;
    
    // Store this delta
    storeDelta(delta);
    
    // Notify subscribers
    notifyCallback();
    notifyDeltaCallback(delta);
    
    return true;
}
// bool OrderBook::removeBid(const PriceLevel& level){
//     std::lock_guard<std::mutex> lock(mutex_);
//     auto it = bids_.find(level.price);
//     if (it != bids_.end()) {
//         bids_.erase(it);
//         return true;
//     }
//     return false;
// }
 
// bool OrderBook::updateBid(const PriceLevel& level) {
//     if (level.quantity <= 0.0) {
//         bids_.erase(level.price);
//     } else {
//         bids_[level.price] = level;  // Creates if not exists
//     }
//     return true;
// }
// bool OrderBook::removeAsk(const PriceLevel& level) {
//     std::lock_guard<std::mutex> lock(mutex_);
//     auto it = asks_.find(level.price);
//     if (it != asks_.end()) {
//         asks_.erase(it);
//         return true;
//     }
//     return false;
// }

// // Update an ask level
// bool OrderBook::updateAsk(const PriceLevel& level) {
//     std::lock_guard<std::mutex> lock(mutex_);
//     auto it = asks_.find(level.price);
//     if (it != asks_.end()) {
//         it->second = level;
//         return true;
//     }
//     return false;
// }

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
        // We missed some updates, invalidate the book
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
        .tradingPair = symbol_.toString(),
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
    
    // Update metadata
    sequence_number_ = sequenceNumber;
    timestamp_ = timestamp;
    
    // Store this delta if it's not empty
    if (!delta.isEmpty()) {
        storeDelta(delta);
        notifyDeltaCallback(delta);
    }
    
    // Notify subscribers
    notifyCallback();
    
    return true;
}

void OrderBook::trackBidDelta(const PriceLevel& level, OrderBookDelta& delta) {
    double price = level.price;
    double quantity = level.quantity;
    
    auto it = bids_.find(price);
    
    if (quantity <= 0.0) {
        // Level is being removed
        if (it != bids_.end()) {
            delta.removed_bids.push_back(it->second);
        }
    } else {
        // Level is being added or modified
        if (it == bids_.end()) {
            // New level
            delta.added_bids.push_back(level);
        } else {
            // Modified level - remove old and add new
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
        // Level is being removed
        if (it != asks_.end()) {
            delta.removed_asks.push_back(it->second);
        }
    } else {
        // Level is being added or modified
        if (it == asks_.end()) {
            // New level
            delta.added_asks.push_back(level);
        } else {
            // Modified level - remove old and add new
            delta.removed_asks.push_back(it->second);
            delta.added_asks.push_back(level);
        }
    }
}

void OrderBook::updateBidLevel(const PriceLevel& level) {
    double price = level.price;
    
    if (level.quantity <= 0.0) {
        // Remove the level if quantity is zero or negative
        bids_.erase(price);
    } else {
        // Update or add the level
        bids_[price] = level;
    }
    
    // Update timestamp
    last_update_time_ = std::chrono::system_clock::now();
}

void OrderBook::updateAskLevel(const PriceLevel& level) {
    double price = level.price;
    
    if (level.quantity <= 0.0) {
        // Remove the level if quantity is zero or negative
        asks_.erase(price);
    } else {
        // Update or add the level
        asks_[price] = level;
    }
    
    // Update timestamp
    last_update_time_ = std::chrono::system_clock::now();
}

void OrderBook::captureOrderBookState() {
    // Copy current state for delta calculations
    previous_bids_ = bids_;
    previous_asks_ = asks_;
}

void OrderBook::storeDelta(const OrderBookDelta& delta) {
    // Store delta in history
    recent_deltas_.push_back(delta);
    
    // Maintain maximum history size
    if (recent_deltas_.size() > MAX_DELTA_HISTORY) {
        recent_deltas_.pop_front();
    }
}

OrderBookDelta OrderBook::getOrderBookDelta(Timestamp previous_timestamp) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Initialize the delta result
    OrderBookDelta result{
        .exchangeId = exchangeId_,
        .tradingPair = symbol_.toString(),
        .timestamp = timestamp_,
        .sequenceNumber = sequence_number_
    };
    
    // If timestamp is 0 or very old, return the entire current book as added
    if (previous_timestamp == Timestamp{}) {
        // Add all current bids
        for (const auto& [price, level] : bids_) {
            result.added_bids.push_back(level);
        }
        
        // Add all current asks
        for (const auto& [price, level] : asks_) {
            result.added_asks.push_back(level);
        }
        
        return result;
    }
    
    // Try to find deltas in our history that occurred after the requested timestamp
    bool found_relevant_deltas = false;
    
    // Iterate through recent deltas from newest to oldest
    for (auto it = recent_deltas_.rbegin(); it != recent_deltas_.rend(); ++it) {
        const auto& delta = *it;
        
        // If this delta is newer than the requested timestamp, include its changes
        if (delta.timestamp > previous_timestamp) {
            found_relevant_deltas = true;
            
            // Add the changes to our result
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
            // We've reached deltas older than the requested timestamp
            break;
        }
    }
    
    // If we couldn't find any relevant deltas, return the current state
    if (!found_relevant_deltas) {
        // Add all current bids
        for (const auto& [price, level] : bids_) {
            result.added_bids.push_back(level);
        }
        
        // Add all current asks
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
    update.tradingPair = symbol_.toString();
    update.exchangeTimestamp = timestamp_;
    update.receivedTimestamp = last_update_time_;
    update.sequenceNumber = sequence_number_;
    update.isSnapshot = true;
    
    // Add bid levels
    int bid_count = 0;
    for (const auto& [price, level] : bids_) {
        update.bids.push_back(level);
        bid_count++;
        if (depth > 0 && bid_count >= depth) break;
    }
    
    // Add ask levels
    int ask_count = 0;
    for (const auto& [price, level] : asks_) {
        update.asks.push_back(level);
        ask_count++;
        if (depth > 0 && ask_count >= depth) break;
    }
    
    // Calculate checksum for validation
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

// double OrderBook::getMidPrice() const {
//     std::lock_guard<std::mutex> lock(mutex_);
    
//     if (bids_.empty() || asks_.empty()) {
//         return 0.0;
//     }
    
//     double bestBid = bids_.begin()->first;
//     double bestAsk = asks_.begin()->first;
    
//     return (bestBid + bestAsk) / 2.0;
// }
double OrderBook::getMidPrice() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // DEBUG: Print order book state
    
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
    
    // Add top N levels of bids and asks to the checksum
    const int levels_to_include = 10;
    int bid_count = 0;
    int ask_count = 0;
    
    // Include bids
    for (auto it = bids_.begin(); it != bids_.end() && bid_count < levels_to_include; ++it, ++bid_count) {
        const auto& level = it->second;
        
        // Convert price to 8 decimal places and remove decimal point
        uint64_t price_int = static_cast<uint64_t>(level.price * 100000000);
        
        // Convert quantity to 8 decimal places and remove decimal point
        uint64_t qty_int = static_cast<uint64_t>(level.quantity * 100000000);
        
        // Add to checksum
        ss << "B:" << price_int << ":" << qty_int << "|";
    }
    
    // Include asks
    for (auto it = asks_.begin(); it != asks_.end() && ask_count < levels_to_include; ++it, ++ask_count) {
        const auto& level = it->second;
        
        // Convert price to 8 decimal places and remove decimal point
        uint64_t price_int = static_cast<uint64_t>(level.price * 100000000);
        
        // Convert quantity to 8 decimal places and remove decimal point
        uint64_t qty_int = static_cast<uint64_t>(level.quantity * 100000000);
        
        // Add to checksum
        ss << "A:" << price_int << ":" << qty_int << "|";
    }
    
    // Calculate CRC32 of the string
    boost::crc_32_type crc;
    std::string data = ss.str();
    crc.process_bytes(data.data(), data.size());
    
    // Format as hexadecimal
    std::stringstream checksum;
    checksum << std::hex << std::setw(8) << std::setfill('0') << crc.checksum();
    
    return checksum.str();
}

void OrderBook::notifyCallback() {
    if (!callback_) {
        return;
    }
    
    // Invoke callback with reference to this order book
    callback_(*this);
}

void OrderBook::notifyDeltaCallback(const OrderBookDelta& delta) {
    if (!delta_callback_) {
        return;
    }
    
    // Invoke delta callback with this delta
    delta_callback_(delta);
}

} // namespace data_ingestion
} // namespace trading