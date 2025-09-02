/**
 * @file order_book.hpp
 * @brief Definition of order book and related classes
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */
#pragma once

#include "core/market_types.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include <map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <deque>  // Added for delta history

namespace trading {
namespace data_ingestion {

/**
 * Represents an order book delta (change)
 */
struct OrderBookDelta {
    ExchangeId exchangeId;
    std::string tradingPair;
    core::Timestamp timestamp;
    uint64_t sequenceNumber;
    std::vector<PriceLevel> added_bids = {};
    std::vector<PriceLevel> removed_bids = {};
    std::vector<PriceLevel> added_asks = {};
    std::vector<PriceLevel> removed_asks= {};
    
    // Calculate total added and removed volume
    double getTotalAddedBidVolume() const;
    double getTotalRemovedBidVolume() const;
    double getTotalAddedAskVolume() const;
    double getTotalRemovedAskVolume() const;

    // Calculate net volume change
    double getNetBidVolumeChange() const {
        return getTotalAddedBidVolume() - getTotalRemovedBidVolume();
    }
    
    double getNetAskVolumeChange() const {
        return getTotalAddedAskVolume() - getTotalRemovedAskVolume();
    }
    
    // Is the delta empty?
    bool isEmpty() const {
        return added_bids.empty() && removed_bids.empty() && 
               added_asks.empty() && removed_asks.empty();
    }
};
// Forward declarations
class OrderBook;

// Callback for order book updates
using OrderBookCallback = std::function<void(const OrderBook&)>;
using OrderBookDeltaCallback = std::function<void(const OrderBookDelta&)>;


// Order book class
class OrderBook {
public:
    OrderBook(const ExchangeId& exchangeId, const std::string& tradingPair, OrderBookCallback callback = nullptr);
    OrderBook(const ExchangeId& exchangeId, const Symbol& symbol, OrderBookCallback callback = nullptr);
    ~OrderBook() = default;
    
    // Update methods
    bool applySnapshot(const std::vector<PriceLevel>& bids, 
                      const std::vector<PriceLevel>& asks,
                      uint64_t sequenceNumber,
                      Timestamp timestamp);
                      
    bool applyUpdate(const std::vector<PriceLevel>& bids,
                    const std::vector<PriceLevel>& asks,
                    uint64_t sequenceNumber,
                    Timestamp timestamp);
      

    // Snapshot and delta methods
    OrderBookUpdate getSnapshot(int depth = 0) const;
    // OrderBookDelta getOrderBookDelta(Timestamp previous_timestamp = 0) const;
    OrderBookDelta getOrderBookDelta(Timestamp previous_timestamp = Timestamp{}) const;
    // Set callbacks
    void setOrderBookCallback(OrderBookCallback callback) {
        callback_ = std::move(callback);
    }
    
    void setDeltaCallback(OrderBookDeltaCallback callback) {
        delta_callback_ = std::move(callback);
    }
    
    // Helper method for exchange ID conversion
    // std::string exchangeIdToString(ExchangeId id) const {
    //     return id;
    // }
    
    // Query methods
    std::vector<PriceLevel> getBids(int depth = 0) const;
    std::vector<PriceLevel> getAsks(int depth = 0) const;
    
    PriceLevel getBestBid() const;
    PriceLevel getBestAsk() const;
    
    double getMidPrice() const;
    double getSpread() const;
    double getSpreadPercent() const;
    
    // Order book info
    ExchangeId getExchangeId() const { return exchangeId_; }
    Symbol getSymbol() const { return symbol_; }
    uint64_t getSequenceNumber() const { return sequence_number_; }
    Timestamp getTimestamp() const { return timestamp_; }
    bool isValid() const { return is_valid_; }
    
    // Order book metrics
    double getBidVolume(int depth = 0) const;
    double getAskVolume(int depth = 0) const;
    double getVolumeImbalance(int depth = 0) const;
    
    // Calculate checksum for validation
    std::string calculateChecksum() const;
    
private:
    // Core order book data
    ExchangeId exchangeId_;
    Symbol symbol_;
    Timestamp last_update_time_;
    uint64_t sequence_number_ = 0;
    Timestamp timestamp_;
    std::atomic<bool> is_valid_{false};
    std::atomic<bool> is_snapshot_{false};  
    
    // Order book state
    // std::map<double, PriceLevel, std::greater<double>> bids_;  // Descending order for bids
    // std::map<double, PriceLevel, std::less<double>> asks_;     // Ascending order for asks
    std::map<double, PriceLevel, std::greater<double>> bids_;  // Descending
    std::map<double, PriceLevel> asks_;                       // Ascending (correct)
    // Previous state for delta tracking
    std::map<double, PriceLevel, std::greater<double>> previous_bids_;  
    std::map<double, PriceLevel> previous_asks_;

    // Delta history for more accurate delta retrieval
    static constexpr size_t MAX_DELTA_HISTORY = 100;
    std::deque<OrderBookDelta> recent_deltas_;
    
    // Callbacks
    OrderBookCallback callback_;
    OrderBookDeltaCallback delta_callback_;
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Helper methods
    void updateBidLevel(const PriceLevel& level);
    void updateAskLevel(const PriceLevel& level);
    void notifyCallback();
    void notifyDeltaCallback(const OrderBookDelta& delta);
    
    // Delta tracking
    void trackBidDelta(const PriceLevel& level, OrderBookDelta& delta);
    void trackAskDelta(const PriceLevel& level, OrderBookDelta& delta);
    void storeDelta(const OrderBookDelta& delta);
    void captureOrderBookState();
};
 
} // namespace data_ingestion
} // namespace trading