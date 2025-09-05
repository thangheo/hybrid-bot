// src/data_ingestion/order_book_normalizer.cpp
#include "data_ingestion/normalization/order_book_normalizer.hpp"
#include <iostream>
#include <sstream>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <map>
#include <memory>
#include <mutex>
#include "protocol/adapter_factory.hpp"

namespace trading {
namespace data_ingestion {

using namespace core;
// OrderBookNormalizer implementation

OrderBookNormalizer::OrderBookNormalizer(NormalizedOrderBookCallback callback)
    : callback_(std::move(callback)) {
    initAdapters();
}

OrderBookNormalizer::~OrderBookNormalizer() = default;

// Update your order_book_normalizer.cpp
void OrderBookNormalizer::initAdapters() {
    // Initialize adapters for each supported exchange
    
    // Binance adapter - use the new ExchangeAdapter implementation
    // exchange_adapters_["binance"] = std::static_pointer_cast<ExchangeAdapter>(std::make_shared<BinanceAdapter>());

    // Add more exchanges as needed...
    // exchange_adapters_["coinbase"] = std::static_pointer_cast<ExchangeAdapter>(std::make_shared<CoinbaseExchangeAdapter>());
    
    std::cout << "Initialized " << exchange_adapters_.size() 
              << " exchange adapters" << std::endl;
}


OrderBookUpdate OrderBookNormalizer::getOrderBook(
    const ExchangeId exchangeId,
    const std::string& tradingPair,
    int depth) const {
    
    std::string key = createOrderBookKey(exchangeId, tradingPair);
    
    // Thread-safe access to order books
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = order_books_.find(key);
    if (it != order_books_.end()) {
        return it->second->getSnapshot(depth);
    }
    
    // Return empty update if not found
    OrderBookUpdate empty_update;
    empty_update.exchangeId = exchangeId;
    empty_update.tradingPair = tradingPair;
    empty_update.isSnapshot = true;
    
    return empty_update;
}
void OrderBookNormalizer::processMarketData(const MarketDataUpdate& update) {
    // Find adapter for this exchange
    auto it = exchange_adapters_.find(update.exchangeId.toString());
    if (it == exchange_adapters_.end()) {
        std::cerr << "No adapter found for exchange: " << update.exchangeId.toString() << std::endl;
        return;
    }
    
    auto adapter = it->second;
    
    // Check if adapter can handle this update
    if (!adapter->canHandle(update)) {
        std::cerr << "Adapter cannot handle update type for exchange: " 
                  << update.exchangeId.toString() << std::endl;
        return;
    }
    
    try {
        // Normalize the update
        OrderBookUpdate normalized = adapter->normalize(update);
        
        // Standardize trading pair
        std::string standardized_pair = adapter->standardizeTradingPair(update.tradingPair);
        normalized.tradingPair = standardized_pair;
        
        // Get or create order book
        std::shared_ptr<OrderBook> order_book = getOrCreateOrderBook(update.exchangeId, standardized_pair);
        
        // Update the order book based on the normalized data
        if (normalized.isSnapshot) {
            order_book->applySnapshot(
                normalized.bids, 
                normalized.asks, 
                normalized.sequenceNumber, 
                normalized.exchange_timestamp
            );
        } else {
            order_book->applyUpdate(
                normalized.bids, 
                normalized.asks, 
                normalized.sequenceNumber, 
                normalized.exchange_timestamp
            );
        }
        
        // Forward the normalized update to subscribers
        callback_(normalized);
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing market data: " << e.what() << std::endl;
    }
}

std::shared_ptr<OrderBook> OrderBookNormalizer::getOrCreateOrderBook(
    const ExchangeId& exchangeId, 
    const std::string& tradingPair) {
    
    std::string key = createOrderBookKey(exchangeId, tradingPair);

    // Thread-safe access
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if the order book exists
    auto it = order_books_.find(key);
    if (it != order_books_.end()) {
        return it->second;
    }

    // Create a new order book
    std::cout << "Creating new order book for " << exchangeId.toString() 
              << ":" << tradingPair << std::endl;

    // Parse trading pair into base/quote (assuming format like "BTC/USDT")
    Symbol symbol;
    size_t slash_pos = tradingPair.find('/');
    if (slash_pos != std::string::npos) {
        symbol.base = tradingPair.substr(0, slash_pos);
        symbol.quote = tradingPair.substr(slash_pos + 1);
    } else {
        // Handle case where there's no slash
        symbol.base = tradingPair;
        symbol.quote = "";
    }

    // FIX: Use the correct constructor signature
    // Create OrderBook with proper callback that forwards updated order book data
    auto order_book = std::make_shared<OrderBook>(
        exchangeId,           // This should match the constructor parameter type
        tradingPair,          // Use string directly instead of Symbol
        [this](const OrderBook& book) {
            // Get snapshot from the updated order book and forward to callback
            OrderBookUpdate update = book.getSnapshot();
            callback_(update);
        }
    );

    order_books_[key] = order_book;
    return order_book;
}



std::string OrderBookNormalizer::createOrderBookKey(const ExchangeId& exchangeId, 
                                                   const std::string& tradingPair) const {
    // Create a unique key for the order book using exchange ID and trading pair
    return exchangeId.toString() + ":" + tradingPair;
}

void OrderBookNormalizer::processExchangeData(const MarketDataUpdate& update, 
                                                const ExchangeId exchangeId) {
        // Find the appropriate adapter for the exchange
        auto it = exchange_adapters_.find(exchangeId.toString());
        if (it != exchange_adapters_.end()) {
            auto adapter = it->second;
            
            // Normalize the data
            OrderBookUpdate normalized = adapter->normalize(update);
            
            // Forward to callback
            callback_(normalized);
        } else {
            std::cerr << "No adapter found for exchange: " << exchangeId.toString() << std::endl;
        }
    }

} // namespace data_ingestion
} // namespace trading
