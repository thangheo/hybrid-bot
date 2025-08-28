// include/data_ingestion/binance_parser.hpp
#pragma once

#include "data_ingestion/orderbook/order_book.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace trading {
namespace data_ingestion {

using json = nlohmann::json;

class BinanceParser {
public:
    BinanceParser();
    
    // Parse a Binance order book snapshot
    bool parseOrderBookSnapshot(const std::vector<uint8_t>& data,
                               int64_t& last_update_id,
                               std::vector<PriceLevel>& bids,
                               std::vector<PriceLevel>& asks);
    
    // Parse a Binance order book update
    bool parseOrderBookUpdate(const std::vector<uint8_t>& data,
                             int64_t& first_update_id,
                             int64_t& last_update_id,
                             std::vector<PriceLevel>& bids_updates,
                             std::vector<PriceLevel>& asks_updates);
    
    // Parse a Binance trade
    bool parseTrade(const std::vector<uint8_t>& data,
                   std::string& symbol,
                   std::string& price,
                   std::string& quantity,
                   bool& is_buyer_maker,
                   int64_t& trade_time);
    
    // Parse a Binance ticker
    bool parseTicker(const std::vector<uint8_t>& data,
                    std::string& symbol,
                    std::string& last_price,
                    std::string& bid_price,
                    std::string& ask_price,
                    std::string& volume,
                    int64_t& ticker_time);
    
    // Normalize Binance trading pair to standard format
    static std::string normalizeTradingPair(const std::string& binance_symbol);
    
    // Convert Binance trading pair to standard format
    static std::string standardizeTradingPair(const std::string& binance_symbol);
    
private:
    // Trading pair precision information
    std::unordered_map<std::string, int> price_precision_;
    std::unordered_map<std::string, int> quantity_precision_;
    
    // Load precision information from configuration
    void loadPrecisionConfig();
};

} // namespace data_ingestion
} // namespace trading