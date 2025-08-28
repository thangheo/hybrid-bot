// include/data_ingestion/order_book_types.hpp
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
    ExchangeId exchange;
    Symbol symbol;
    
    bool operator==(const OrderBookKey& other) const {
        return exchange == other.exchange && symbol == other.symbol;
    }
};
 
 // Define the hash function for OrderBookKey
// Fix for OrderBookKeyHash in order_book_types.hpp

// Replace the existing OrderBookKeyHash with this implementation
struct OrderBookKeyHash {
    std::size_t operator()(const OrderBookKey& key) const {
        // Hash the exchange ID directly as a string
        std::size_t h1 = std::hash<std::string>()(key.exchange.toString());
        // For Symbol, use the toString() method
        std::size_t h2 = std::hash<std::string>()(key.symbol.toString());
        
        return h1 ^ (h2 << 1);
    }
};

/**
 * @brief Structure representing a market data update
 */

// struct MarketDataUpdate {
//     ExchangeId exchangeId;           // Exchange identifier
//     std::string tradingPair;          // Trading pair (e.g., "BTCUSDT")
//     std::string updateType;           // Type of update ("orderbook", "trade", "ticker")
//     std::chrono::system_clock::time_point timestamp;  // Update timestamp
//     std::vector<uint8_t> data;        // Raw data bytes
//     uint64_t sequenceNumber = 0;      // Optional sequence number
    
//     MarketDataUpdate() = default;
// };


// Normalized order book update
struct OrderBookUpdate {
    ExchangeId exchangeId;  // Identifier for the exchange (e.g., "binance")
    std::string tradingPair;
    int64_t exchange_timestamp;
    int64_t receivedTimestamp;
    int64_t sequence_number;
    bool is_snapshot;
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
                    int64_t exchange_timestamp, int64_t receivedTimestamp,
                    int64_t sequence_number, bool is_snapshot,
                    const std::vector<PriceLevel>& bids,
                    const std::vector<PriceLevel>& asks)
        : exchangeId(exchangeId), tradingPair(tradingPair),
          exchange_timestamp(exchange_timestamp), receivedTimestamp(receivedTimestamp),
          sequence_number(sequence_number), is_snapshot(is_snapshot),
          bids(bids), asks(asks) {}
    // Add other constructors or methods as needed
    // For example, a constructor that takes a MarketDataUpdate

};

// Callback types
// using OrderBookCallback = std::function<void(const OrderBookUpdate&)>;
using NormalizedOrderBookCallback = std::function<void(const OrderBookUpdate&)>;

// Statistics for publishers
// struct PublisherStats {
//     std::atomic<uint64_t> messages_published{0};
//     std::atomic<uint64_t> messages_dropped{0};
//     std::atomic<uint64_t> current_queue_size{0};
//     std::atomic<uint64_t> max_queue_size_reached{0};
//     std::atomic<uint64_t> bytes_published{0};
//     std::atomic<uint64_t> bytes_before_compression{0};
//     std::atomic<uint64_t> bytes_after_compression{0};
//     std::chrono::system_clock::time_point start_time;
//     std::chrono::system_clock::time_point last_publish_time;
// };

// Compression options
enum class CompressionAlgorithm {
    NONE,
    SNAPPY,
    ZSTD,
    LZ4
};

// Publisher configuration
// struct PublisherConfig {
//     std::string zmq_endpoint = "tcp://*:5555";
//     int zmq_hwm = 1000;
//     int zmq_linger_ms = 1000;
//     size_t max_queue_size = 10000;
//     int max_publish_rate = 0;  // 0 means no limit
//     CompressionAlgorithm compression = CompressionAlgorithm::NONE;
//     int compression_level = 3;
//     int heartbeat_interval_ms = 5000;
// };

} // namespace data_ingestion
} // namespace trading
