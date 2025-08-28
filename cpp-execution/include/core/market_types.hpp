/**
 * @file core/market_types.hpp
 * @brief Core market-related types and utilities
 * 
 * This file defines common market structures used across all components
 * of the trading system.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include <string>
 #include <cmath>
 #include <vector>
 #include <algorithm>
 #include <chrono>
 #include <map>
 #include <optional>
 #include <cstdint>
 #include <memory>
 #include <functional>
 #include <unordered_map>
 #include <ostream>
 #include <common/exchange_types.hpp>
 #include "core/direction.hpp"
 namespace trading {
 namespace core {
 
 // Common timestamp definition
 using Timestamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;
 using ExchangeId = common::ExchangeId;

// Raw market data from an exchange
struct MarketDataUpdate {
    ExchangeId exchangeId;
    std::string tradingPair;
    std::string updateType;  // "orderbook", "trade", "ticker"
    int64_t exchange_timestamp;
    int64_t receivedTimestamp;
    std::vector<uint8_t> data;  // Raw data from exchange
    uint64_t sequenceNumber = 0;      // Optional sequence number
    std::chrono::system_clock::time_point timestamp;  // Update timestamp
    MarketDataUpdate() = default;

};

//  struct TradingSignal {
//      std::string signal_id;                        // Unique signal identifier
//      std::string tradingPair;                     // Trading pair (e.g., "BTC/USDT")
//      core::Direction direction;                    // Buy or sell direction
//      double strength;                              // Signal strength (0.0-1.0)
//      double target_size;                           // Suggested position size
//      std::chrono::milliseconds signal_ttl;         // Time-to-live for the signal
//      std::string strategy_id;                      // ID of the strategy that generated the signal
//      std::string strategy_version;                 // Version of the strategy
//      core::ExchangeId exchange;                      // Exchange to execute on
//      std::map<std::string, double> signal_metrics; // Additional metrics for this signal
//     core::Timestamp generation_time = core::now();  // Add this
//      /**
//       * Default constructor
//       */
//      TradingSignal()
//          : signal_id(""),
//            tradingPair(""),
//            direction(core::Direction::NEUTRAL),
//            strength(0.0),
//            target_size(0.0),
//            signal_ttl(std::chrono::milliseconds(0)),
//            strategy_id(""),
//            strategy_version(""),
//            exchange(core::ExchangeId()) {}
     
//      /**
//       * Constructor with required fields
//       */
//      TradingSignal(
//          const std::string& id,
//          const std::string& pair,
//          core::Direction dir,
//          double str
//      )
//          : signal_id(id),
//            tradingPair(pair),
//            direction(dir),
//            strength(str),
//            target_size(0.0),
//            signal_ttl(std::chrono::milliseconds(5000)), // Default 5 seconds
//            strategy_id(""),
//            strategy_version(""),
//            exchange(core::ExchangeId()) {}
     
//      /**
//       * Get the signal ID
//       */
//      const std::string& getSignalId() const { return signal_id; }
     
//      /**
//       * Get the trading pair
//       */
//      const std::string& getTradingPair() const { return tradingPair; }
     
//      /**
//       * Get the direction
//       */
//      core::Direction getDirection() const { return direction; }
     
//      /**
//       * Get the strength
//       */
//      double getStrength() const { return strength; }
     
//      /**
//       * Get the target size
//       */
//      double getTargetSize() const { return target_size; }
     
//      /**
//       * Get the signal TTL
//       */
//      const std::chrono::milliseconds& getSignalTtl() const { return signal_ttl; }
     
//      /**
//       * Get the strategy ID
//       */
//      const std::string& getStrategyId() const { return strategy_id; }
     
//      /**
//       * Get the strategy version
//       */
//      const std::string& getStrategyVersion() const { return strategy_version; }
     
//      /**
//       * Get the signal metrics
//       */
//      const std::map<std::string, double>& getSignalMetrics() const { return signal_metrics; }
     
//      /**
//       * Check if this is a buy signal
//       */
//      bool isBuySignal() const { return direction == core::Direction::BUY; }
     
//      /**
//       * Check if this is a sell signal
//       */
//      bool isSellSignal() const { return direction == core::Direction::SELL; }
     
//      /**
//       * Check if the signal is still valid based on its TTL and a reference time
//       */
//      bool isValid(const core::Timestamp& current_time) const {
//          // Implementation would need the signal's generation time, which
//          // is not currently included in this structure. Consider adding it.
//          return true; // Placeholder
//      }
//  };

/**
 * Structure to hold market data information
 */
struct MarketData {
    std::string tradingPair;              // Trading pair symbol (e.g., "BTC/USDT")
    ExchangeId exchangeId;               // Exchange identifier
    Timestamp timestamp;                  // Timestamp of the data
    double price;                         // Current market price
    double volume;                        // Trading volume
    double bid;                           // Best bid price
    double ask;                           // Best ask price
    double lastTradePrice;                // Last trade price
    double lastTradeQuantity;             // Last trade quantity
    double open24h;                       // Opening price 24 hours ago
    double high24h;                       // Highest price in last 24 hours
    double low24h;                        // Lowest price in last 24 hours
    double volumeQuote24h;                // 24-hour volume in quote currency
    double priceChange24h;
    double priceChangePercent24h;

    std::map<std::string, double> extra;  // Extra fields for extensibility
    
    MarketData() 
        : price(0.0), volume(0.0), bid(0.0), ask(0.0), 
          lastTradePrice(0.0), lastTradeQuantity(0.0),
          open24h(0.0), high24h(0.0), low24h(0.0), volumeQuote24h(0.0) {}
};
struct TradeData {
    std::string symbol;
    double price;
    double quantity;
    std::string side; // "buy" or "sell"
    uint64_t timestamp;
    ExchangeId exchangeId;
    std::string tradeId;
};

 /**
  * Market state information
  */
 struct MarketState {
    ExchangeId exchangeId;               // Exchange identifier
    std::string tradingPair;              // Trading pair
    core::Timestamp timestamp;                   // Timestamp of this state
    double trendStrength;                // Trend strength (0-1)
    // double volume_ratio;                 // Volume ratio compared to average
    double price;                          // Current price
    double volatility;                     // Current volatility
    double volume_24h;                     // 24-hour volume
    double price_change_24h;               // 24-hour price change percentage
    std::string market_regime;             // Market regime classification
    
    // Default constructor
    MarketState()
        : exchangeId(), tradingPair(""), timestamp(std::chrono::system_clock::now()),
          price(0.0), volatility(0.0), volume_24h(0.0), price_change_24h(0.0),
          market_regime("unknown") {}
    
    // Constructor with exchange and trading pair
    MarketState(const ExchangeId& exchange, const std::string& pair)
        : exchangeId(exchange), tradingPair(pair), timestamp(std::chrono::system_clock::now()),
          price(0.0), volatility(0.0), volume_24h(0.0), price_change_24h(0.0),
          market_regime("unknown") {}
};


 /**
  * Represents a cryptocurrency exchange
  */
//  enum class Exchange {
//      BINANCE,
//      COINBASE,
//      FTX,
//      KRAKEN,
//      HUOBI,
//      UNKNOWN
//  };
 
//  /**
//   * Converts Exchange enum to string
//   */
//  inline std::string exchangeToString(Exchange exchange) {
//      switch (exchange) {
//          case Exchange::BINANCE:  return "binance";
//          case Exchange::COINBASE: return "coinbase";
//          case Exchange::FTX:      return "ftx";
//          case Exchange::KRAKEN:   return "kraken";
//          case Exchange::HUOBI:    return "huobi";
//          case Exchange::UNKNOWN:  return "unknown";
//          default:                 return "unknown";
//      }
//  }
 
//  /**
//   * Converts string to Exchange enum
//   */
//  inline Exchange exchangeFromString(const std::string& str) {
//      auto lower = str;
//      std::transform(lower.begin(), lower.end(), lower.begin(), 
//          [](unsigned char c) { return std::tolower(c); });
     
//      if (lower == "binance")  return Exchange::BINANCE;
//      if (lower == "coinbase") return Exchange::COINBASE;
//      if (lower == "ftx")      return Exchange::FTX;
//      if (lower == "kraken")   return Exchange::KRAKEN;
//      if (lower == "huobi")    return Exchange::HUOBI;
//      return Exchange::UNKNOWN;
//  }
 
 // Using Exchange as the universal exchange identifier to replace data_ingestion::ExchangeId
//  using ExchangeId = Exchange;
 /**
  * @struct ExchangeData
  * @brief Represents raw data received from an exchange
  */
 
 /**
  * Represents a trading symbol (trading pair)
  */
 struct Symbol {
     std::string base;    // Base currency (e.g., BTC)
     std::string quote;   // Quote currency (e.g., USDT)
     
     Symbol() = default;
     
     Symbol(const std::string& b, const std::string& q) : base(b), quote(q) {}
     
     // Create from combined string (e.g., "BTC/USDT")
     static Symbol fromString(const std::string& symbol_str) {
         size_t delimiter_pos = symbol_str.find('/');
         if (delimiter_pos != std::string::npos) {
             return Symbol(
                 symbol_str.substr(0, delimiter_pos),
                 symbol_str.substr(delimiter_pos + 1)
             );
         }
         // Alternative delimiter
         delimiter_pos = symbol_str.find('-');
         if (delimiter_pos != std::string::npos) {
             return Symbol(
                 symbol_str.substr(0, delimiter_pos),
                 symbol_str.substr(delimiter_pos + 1)
             );
         }
         // For symbols without delimiter, try to split at a reasonable position
         if (symbol_str.length() >= 6) { // BTCUSDT
             // Heuristic: last 3-4 characters are usually the quote
             size_t split = symbol_str.length() - (symbol_str.length() >= 7 ? 4 : 3);
             return Symbol(
                 symbol_str.substr(0, split),
                 symbol_str.substr(split)
             );
         }
         return Symbol(symbol_str, "");
     }
     
     // Convert to string representation
     std::string toString() const {
         return base + "/" + quote;
     }
     
     // Equality operator
     bool operator==(const Symbol& other) const {
         return base == other.base && quote == other.quote;
     }
     
     // Inequality operator
     bool operator!=(const Symbol& other) const {
         return !(*this == other);
     }
     
     // Less than operator (for containers)
     bool operator<(const Symbol& other) const {
         if (base != other.base) return base < other.base;
         return quote < other.quote;
     }
 };
 
 // Stream operator for Symbol
 inline std::ostream& operator<<(std::ostream& os, const Symbol& symbol) {
     return os << symbol.toString();
 }

/**
  * VolatilityLevel enum for categorizing market volatility
  */
 enum class VolatilityLevel {
     VERY_LOW,
     LOW,
     NORMAL,
     HIGH,
     VERY_HIGH
 };
 struct ExchangeData {
     ExchangeId exchangeId;        // Identifier for the exchange (e.g., "binance")
     Symbol symbol;            // Trading pair symbol (e.g., "BTCUSDT")
     std::string data;              // Raw data received (e.g., JSON string)
     uint64_t receivedTime;         // Timestamp when data was received
     std::string dataType;          // Type of data (e.g., "orderbook", "trade")
     
     // Other potentially useful fields
     bool isSnapshot{false};        // Whether this is a full snapshot or an update
     std::string sourceType;        // "websocket" or "rest"
 };
 
 /**
  * Represents a price level in an order book
  */
 struct PriceLevel {
     double price;     // Price level
     double quantity;  // Quantity available at this price level
     
     PriceLevel() : price(0.0), quantity(0.0) {}
     
     PriceLevel(double p, double q) : price(p), quantity(q) {}
     
     // Equality operator
     bool operator==(const PriceLevel& other) const {
         // Use epsilon comparison for floating point values
         const double epsilon = 1e-9;
         return std::abs(price - other.price) < epsilon && 
                std::abs(quantity - other.quantity) < epsilon;
     }
     
     // Inequality operator
     bool operator!=(const PriceLevel& other) const {
         return !(*this == other);
     }
     
     // Less than operator (for containers)
     bool operator<(const PriceLevel& other) const {
         return price < other.price;
     }
 };
 
 // Update type for order book
 enum class UpdateType {
     SNAPSHOT,
     UPDATE,
     TRADE,
     TICKER
 };
 
 // Connection status
 enum class ConnectionStatus {
     CONNECTED,
     CONNECTING,
     DISCONNECTED,
     RECONNECTING,
     ERROR
 };
 
 /**
  * Common identifiers for strategy, signals, and other components
  */
 using StrategyId = std::string;
 using SignalId = std::string;
 using ExperimentId = std::string;
 using OrderId = std::string;
 

 } // namespace core
 
 // Add a namespace alias to maintain compatibility with existing code
 namespace data_ingestion {
     using Symbol = core::Symbol;
     using ExchangeId = core::ExchangeId;
     using PriceLevelBase = core::PriceLevel;
     using Timestamp = core::Timestamp;
     using UpdateType = core::UpdateType;
     using ConnectionStatus = core::ConnectionStatus;
     using ExchangeData = core::ExchangeData;
 } // namespace data_ingestion
 
 } // namespace trading