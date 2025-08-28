// include/data_ingestion/normalizer.hpp
#pragma once

#include "data_ingestion/orderbook/order_book_types.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "core/market_types.hpp"
#include <memory>
#include <map>
#include <string>

namespace trading {
namespace data_ingestion {

class Normalizer {
public:
    Normalizer(OrderBookCallback callback);
    
    // Process order book updates and normalize them
    void processOrderBookUpdate(const OrderBookUpdate& update);
    
    // Convert exchange-specific price/quantity formats to standard format
    static std::string normalizePrice(const core::ExchangeId& exchangeId, 
                                     const std::string& tradingPair,
                                     const std::string& price);
    
    static std::string normalizeQuantity(const core::ExchangeId& exchangeId, 
                                        const std::string& tradingPair,
                                        const std::string& quantity);
    
private:
    OrderBookCallback callback_;
    std::map<std::string, std::map<std::string, int>> price_precisions_;
    std::map<std::string, std::map<std::string, int>> quantity_precisions_;
    
    // Load exchange-specific formatting rules
    void loadFormatConfig();
};

} // namespace data_ingestion
} // namespace trading
