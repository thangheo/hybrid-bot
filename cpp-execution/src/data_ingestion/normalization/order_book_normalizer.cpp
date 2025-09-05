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
#include <algorithm>
#include <string>
#include <cctype>

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
    exchange_adapters_["BINANCE"] = std::static_pointer_cast<ExchangeAdapter>(std::make_shared<BinanceAdapter>());

    // Add more exchanges as needed...
    // exchange_adapters_["coinbase"] = std::static_pointer_cast<ExchangeAdapter>(std::make_shared<CoinbaseExchangeAdapter>());
    
    std::cout << "Initialized " << exchange_adapters_.size() 
              << " exchange adapters" << std::endl;
}

OrderBookUpdate OrderBookNormalizer::getOrderBook(
    const ExchangeId exchangeId,
    const std::string& tradingPair,
    int depth) const {
    
    // This method should probably be removed or delegated to OrderBookManager
    // Return empty update for now
    OrderBookUpdate empty_update;
    empty_update.exchangeId = exchangeId;
    empty_update.tradingPair = tradingPair;
    empty_update.isSnapshot = true;
    
    return empty_update;
}
inline std::string toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

void OrderBookNormalizer::processMarketData(const MarketDataUpdate& update) {
    // Find adapter and normalize (keep existing code)
    auto it = exchange_adapters_.find(update.exchangeId.toString());
    if (it == exchange_adapters_.end()) {
        std::cerr << "No adapter found for exchange: " << update.exchangeId.toString() << std::endl;
        return;
    }
    
    auto adapter = it->second;
    if (!adapter->canHandle(update)) {
        std::cerr << "Adapter cannot handle update" << std::endl;
        return;
    }
    
    try {
        // Normalize the update
        OrderBookUpdate normalized = adapter->normalize(update);
        
        // Standardize trading pair
        std::string standardized_pair = adapter->standardizeTradingPair(update.tradingPair);
        normalized.tradingPair = standardized_pair;
        
        // DON'T create order books here - just forward the normalized data
        // Let the OrderBookManager handle order book creation and updates
        
        if (callback_) {
            callback_(normalized);  // Send normalized data to DataIngestionService
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing market data: " << e.what() << std::endl;
    }
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
