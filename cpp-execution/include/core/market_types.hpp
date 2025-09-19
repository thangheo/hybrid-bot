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
#include "core/time.hpp"   

namespace trading {
 namespace core {

// using RawMarketData = std::vector<uint8_t>;

 // Connection status
 enum class ConnectionStatus {
     CONNECTED,
     CONNECTING,
     DISCONNECTED,
     DISCONNECTING,
     RECONNECTING,
     ERROR
 };
 /**
  * Represents a trading symbol (trading pair)
  */
 
 struct Symbol {
     std::string base;    // Base currency (e.g., BTC)
     std::string quote;   // Quote currency (e.g., USDT)
     
     Symbol() = default;
     
     Symbol(const std::string& b, const std::string& q) : base(b), quote(q) {}
     Symbol(const Symbol&) = default;
     Symbol(const std::string& symbol_str) {
         *this = Symbol::fromString(symbol_str);
     }
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
         
         // For symbols without delimiter, use smart parsing with known quote currencies
         static const std::vector<std::string> quoteCurrencies = {
             "USDT", "BUSD", "USDC", "TUSD", "FDUSD",  // Stablecoins
             "BTC", "ETH", "BNB",                       // Major cryptos
             "EUR", "GBP", "AUD", "TRY",               // Fiat currencies
             "DOT", "ADA", "XRP", "SOL"                // Other major alts
         };
         
         // Try to match against known quote currencies
         for (const auto& quote : quoteCurrencies) {
             if (symbol_str.length() > quote.length()) {
                 std::string suffix = symbol_str.substr(symbol_str.length() - quote.length());
                 if (suffix == quote) {
                     std::string base = symbol_str.substr(0, symbol_str.length() - quote.length());
                     return Symbol(base, quote);
                 }
             }
         }
         
         // Fallback: assume last 4 characters are quote currency for longer symbols
         if (symbol_str.length() >= 6) {
             std::string base = symbol_str.substr(0, symbol_str.length() - 4);
             std::string quote = symbol_str.substr(symbol_str.length() - 4);
             return Symbol(base, quote);
         }
         // If string is too short, treat the whole string as base currency
         return Symbol(symbol_str, "USDT");  // Default quote currency
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
     //empty
    bool isEmpty() const { return base.empty() || quote.empty(); }
    bool empty() const { return base.empty() || quote.empty(); }
 };
 

// Add proper namespace aliases where needed
// Ensure all files use the same ExchangeId definition
using ExchangeId = common::ExchangeId;
// using StrategyId = core::StrategyId;

// Raw market data from an exchange
struct MarketDataUpdate {
    ExchangeId exchangeId;
    Symbol tradingPair;
    std::string updateType;  // "orderbook", "trade", "ticker"
    core::Timestamp exchangeTimestamp;
    core::Timestamp receivedTimestamp;
    std::vector<uint8_t> data;  // Raw data from exchange
    uint64_t sequenceNumber = 0;      // Optional sequence number
    core::Timestamp timestamp;  // Update timestamp
    std::string messageType;
    MarketDataUpdate() = default;

};

struct RawMarketData {
    ExchangeId exchangeId;
    core::Symbol symbol;
    std::string streamType;  // "depth", "bookTicker", "trade"
    core::Timestamp timestamp;
    // std::vector<uint8_t> rawData;     // Raw JSON bytes
    std::string rawData;     // Raw JSON bytes
};
/**
 * Structure to hold market data information
 */
struct MarketData {
    core::Symbol tradingPair;              // Trading pair symbol (e.g., "BTC/USDT")
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
    core::Symbol tradingPair;              // Trading pair
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
        : exchangeId(), tradingPair(), timestamp(std::chrono::system_clock::now()),
          price(0.0), volatility(0.0), volume_24h(0.0), price_change_24h(0.0),
          market_regime("unknown") {}
    
    // Constructor with exchange and trading pair
    MarketState(const ExchangeId& exchange, const core::Symbol& pair)
        : exchangeId(exchange), tradingPair(pair), timestamp(std::chrono::system_clock::now()),
          price(0.0), volatility(0.0), volume_24h(0.0), price_change_24h(0.0),
          market_regime("unknown") {}
};


 /**
  * @struct ExchangeData
  * @brief Represents raw data received from an exchange
  */
 
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
    
    // Constructors
    PriceLevel() : price(0.0), quantity(0.0) {}
    PriceLevel(double p, double q) : price(p), quantity(q) {}
    
    // String constructor for data ingestion compatibility
    PriceLevel(const std::string& priceStr, const std::string& quantityStr) {
        try {
            price = std::stod(priceStr);
            quantity = std::stod(quantityStr);
        } catch (const std::exception& e) {
            throw std::invalid_argument("Invalid price or quantity format");
        }
    }
    
    // Equality operator with epsilon comparison
    bool operator==(const PriceLevel& other) const {
        static constexpr double epsilon = 1e-9;
        return std::abs(price - other.price) < epsilon && 
               std::abs(quantity - other.quantity) < epsilon;
    }
    
    bool operator!=(const PriceLevel& other) const {
        return !(*this == other);
    }
    
    bool operator<(const PriceLevel& other) const {
        return price < other.price;
    }
    
    // Utility methods
    bool isEmpty() const { return quantity <= 0.0; }
    bool isValid() const { return price > 0.0 && quantity >= 0.0; }
};
 

 // Update type for order book
 enum class UpdateType {
     SNAPSHOT,
     UPDATE,
     TRADE,
     TICKER
 };
 
 
 /**
  * Common identifiers for strategy, signals, and other components
  */
 using SignalId = std::string;
 using ExperimentId = std::string;
 using OrderId = std::string;
 
 }
 
 // Add a namespace alias to maintain compatibility with existing code
 namespace data_ingestion {
     using Symbol = core::Symbol;
     using ExchangeId = core::ExchangeId;
     using PriceLevelBase = core::PriceLevel;
     using Timestamp = core::Timestamp;
     using UpdateType = core::UpdateType;
     using ConnectionStatus = core::ConnectionStatus;
     using ExchangeData = core::ExchangeData;
     using RawMarketData = core::RawMarketData;
    } // namespace data_ingestion
 
} // namespace trading