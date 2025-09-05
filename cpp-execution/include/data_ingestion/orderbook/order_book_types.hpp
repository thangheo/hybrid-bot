#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <atomic>
#include <unordered_map>
#include "core/market_types.hpp"
#include "data_ingestion/orderbook/price_level.hpp"
namespace trading {
namespace data_ingestion {

// Forward declarations
class OrderBook;

using BidAskPair = std::pair<PriceLevel, PriceLevel>;
// Define OrderBookKey as a pair of exchange ID and symbol
struct OrderBookKey {
    ExchangeId exchangeId;
    core::Symbol symbol;
    
    OrderBookKey() = default;
    OrderBookKey(const ExchangeId& exchange, const core::Symbol& sym)
        : exchangeId(exchange), symbol(sym) {}
    
    bool operator==(const OrderBookKey& other) const {
        return exchangeId == other.exchangeId && symbol == other.symbol;
    }
    
    bool operator<(const OrderBookKey& other) const {
        if (exchangeId != other.exchangeId) {
            return exchangeId < other.exchangeId;
        }
        return symbol < other.symbol;
    }
    
    std::string toString() const {
        return exchangeId.toString() + ":" + symbol.toString();
    }
};
 
 // Define the hash function for OrderBookKey
// Fix for OrderBookKeyHash in order_book_types.hpp

// Replace the existing OrderBookKeyHash with this implementation
struct OrderBookKeyHash {
    std::size_t operator()(const OrderBookKey& key) const {
        // Hash the exchange ID directly as a string
        std::size_t h1 = std::hash<std::string>()(key.exchangeId.toString());
        // For Symbol, use the toString() method
        std::size_t h2 = std::hash<std::string>()(key.symbol.toString());
        
        return h1 ^ (h2 << 1);
    }
};

/**
 * @brief Structure representing a market data update
 */
 

// Normalized order book update
struct OrderBookUpdate {
    ExchangeId exchangeId;  // Identifier for the exchange (e.g., "binance")
    std::string tradingPair;
    core::Timestamp exchange_timestamp;
    core::Timestamp receivedTimestamp;
    // isSnapshot
    // int64_t processedTimestamp{0};  
    uint64_t sequenceNumber;
    bool isSnapshot;
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
    std::string checksum;  // For data integrity validation
    std::string anomaly_type;  // Type of detected anomaly
    std::string anomaly_details;  // Details of the detected anomaly
    bool has_anomaly{false};  // Flag to indicate if an anomaly was detected
    core::Symbol symbol;  // Symbol object for better type safety
    // additional_fields for future extensibility
    std::unordered_map<std::string, std::string> additional_fields;
    // Constructor
    OrderBookUpdate() = default;
    OrderBookUpdate(const ExchangeId& exchangeId, const std::string& tradingPair,
                    Timestamp exchange_timestamp, Timestamp receivedTimestamp,
                    uint64_t sequenceNumber, bool isSnapshot,
                    const std::vector<PriceLevel>& bids,
                    const std::vector<PriceLevel>& asks)
        : exchangeId(exchangeId), tradingPair(tradingPair),
          exchange_timestamp(exchange_timestamp), receivedTimestamp(receivedTimestamp),
          sequenceNumber(sequenceNumber), isSnapshot(isSnapshot),
          bids(bids), asks(asks) {}
    // Add other constructors or methods as needed
    // For example, a constructor that takes a MarketDataUpdate

};

// Callback types
// using OrderBookCallback = std::function<void(const OrderBookUpdate&)>;
using NormalizedOrderBookCallback = std::function<void(const OrderBookUpdate&)>;
 

// Compression options
enum class CompressionAlgorithm {
    NONE,
    SNAPPY,
    ZSTD,
    LZ4
};
 

} // namespace data_ingestion
} // namespace trading
