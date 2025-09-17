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

void OrderBookNormalizer::processOrderBookUpdate(const OrderBookUpdate& update) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Forward the update to the callback
    if (callback_) {
        callback_(update);
    }
}

OrderBookUpdate OrderBookNormalizer::getOrderBook(
    const ExchangeId exchangeId,
    const core::Symbol& tradingPair,
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
        if (normalized.tradingPair.toString().empty() || normalized.tradingPair.toString() == "UNKNOWN") {
            std::cerr << "Warning: Normalized trading pair is empty or UNKNOWN for exchange: "
                      << update.exchangeId.toString() << std::endl;
        }
       

        // Standardize trading pair
        core::Symbol standardized_pair;
        // standardized_pair.fromString(adapter->standardizeTradingPair(update.tradingPair));
        normalized.tradingPair = update.tradingPair;
        normalized.tradingPair = update.tradingPair;
        // normalized.tradingPair = standardized_pair;
        // normalized.symbol = standardized_pair;
        // DON'T create order books here - just forward the normalized data
        
        if (callback_) {
            callback_(normalized);  // Send normalized data to DataIngestionService
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing market data: " << e.what() << std::endl;
    }
}

void OrderBookNormalizer::processMarketDataJson(const nlohmann::json& jsonMsg) {
    try {
        MarketDataUpdate update;

        // exchangeId
        if (jsonMsg.contains("exchangeId") && jsonMsg["exchangeId"].is_string()) {
            update.exchangeId = ExchangeId::fromString(jsonMsg["exchangeId"].get<std::string>());
        }

        // tradingPair
        if (jsonMsg.contains("tradingPair") && jsonMsg["tradingPair"].is_string()) {
            update.tradingPair = core::Symbol::fromString(jsonMsg["tradingPair"].get<std::string>());
        }

        // updateType
        if (jsonMsg.contains("updateType") && jsonMsg["updateType"].is_string()) {
            update.updateType = jsonMsg["updateType"].get<std::string>();
        }

        // sequenceNumber
        if (jsonMsg.contains("sequenceNumber") && jsonMsg["sequenceNumber"].is_number_unsigned()) {
            update.sequenceNumber = jsonMsg["sequenceNumber"].get<uint64_t>();
        }

        // timestamps (optional)
        if (jsonMsg.contains("exchangeTimestamp") && jsonMsg["exchangeTimestamp"].is_number_unsigned()) {
            update.exchangeTimestamp = core::timestampFromMillis(jsonMsg["exchangeTimestamp"].get<uint64_t>());
        }
        update.receivedTimestamp = core::now();

        // raw data: keep original JSON string in data
        try {
            std::string s = jsonMsg.dump();
            update.data.assign(s.begin(), s.end());
        } catch (...) {
            // ignore
        }

        // Forward to existing processing path
        processMarketData(update);
    } catch (const std::exception& e) {
        std::cerr << "OrderBookNormalizer::processMarketDataJson error: " << e.what() << std::endl;
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
