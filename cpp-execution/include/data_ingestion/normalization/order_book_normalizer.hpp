#pragma once
#include "data_ingestion/protocol/adapter_factory.hpp"
#include "data_ingestion/normalization/binance_exchange_adapter.hpp"

#include "data_ingestion/orderbook/order_book_types.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "data_ingestion/protocol/binance_adapter.hpp"
#include "core/market_types.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace trading {
namespace data_ingestion {

using namespace core;
// Forward declarations
// class ExchangeAdapter;

// Normalizes order book data from various exchanges
class OrderBookNormalizer {
public:
    OrderBookNormalizer(NormalizedOrderBookCallback callback);
    ~OrderBookNormalizer();
    
    // Process market data from any exchange
    void processMarketData(const MarketDataUpdate& update);
    
    // Get current state of an order book
    OrderBookUpdate getOrderBook(const ExchangeId exchangeId,
                               const std::string& tradingPair,
                               int32_t depth = 10) const;
    
private:
    NormalizedOrderBookCallback callback_;
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Exchange-specific adapters
    std::unordered_map<std::string, std::shared_ptr<ExchangeAdapter>> exchange_adapters_;
    
    // Order books
    std::unordered_map<std::string, std::shared_ptr<OrderBook>> order_books_;
    
    // Initialize exchange adapters
    void initAdapters();
    
    // Get or create an order book
    std::shared_ptr<OrderBook> getOrCreateOrderBook(const ExchangeId& exchangeId, 
                                                 const std::string& tradingPair);
                                                
    
    // Create a key for the order book map
    std::string createOrderBookKey(const ExchangeId& exchangeId,
                                 const std::string& tradingPair) const;
    // Process exchange-specific data
    void processExchangeData(const MarketDataUpdate& update, 
                           const ExchangeId exchangeId);
};


// Concrete adapter for Binance
// class BinanceAdapter : public ExchangeAdapter {
// public:
//     bool canHandle(const MarketDataUpdate& update) const override;
//     OrderBookUpdate normalize(const MarketDataUpdate& update) const override;
//     std::string standardizeTradingPair(const std::string& pair) const override;
// };

// Factory for creating adapters
// class AdapterFactory {
// public:
//     static std::shared_ptr<ExchangeAdapter> createAdapter(const ExchangeId& exchangeId);
// };

} // namespace data_ingestion
} // namespace trading
