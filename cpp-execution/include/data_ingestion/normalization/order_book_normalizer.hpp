#pragma once
#include "data_ingestion/protocol/adapter_factory.hpp"
#include "data_ingestion/protocol/binance_adapter.hpp"

#include "data_ingestion/orderbook/order_book_types.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "data_ingestion/protocol/exchange_adapter.hpp"
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
    // OrderBookNormalizer(NormalizedOrderBookCallback callback,std::vector<ExchangeId>& exchangId);
    
    ~OrderBookNormalizer();
    
    // Process market data from any exchange
    void processMarketData(const MarketDataUpdate& update);
    // Convenience: accept raw JSON (as received from subscriber) and convert to MarketDataUpdate
    void processMarketDataJson(const nlohmann::json& jsonMsg);
    
    // Get current state of an order book
    OrderBookUpdate getOrderBook(const ExchangeId exchangeId,
                               const core::Symbol& tradingPair,
                               int depth = 10) const;
    // processOrderBookUpdate
    void processOrderBookUpdate(const OrderBookUpdate& update);
private:
    NormalizedOrderBookCallback callback_;
    // Mutex for thread safety
    mutable std::mutex mutex_;
    
    // Exchange-specific adapters
    std::unordered_map<std::string, std::shared_ptr<ExchangeAdapter>> exchange_adapters_;
    
    // Initialize exchange adapters
    void initAdapters();
                             
    
    // Process exchange-specific data
    void processExchangeData(const MarketDataUpdate& update, 
                           const ExchangeId exchangeId);
};


} // namespace data_ingestion
} // namespace trading
