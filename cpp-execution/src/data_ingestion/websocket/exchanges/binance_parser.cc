// src/data_ingestion/binance_parser.cpp
#include "data_ingestion/websocket/exchanges/binance_parser.hpp"
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace trading {
namespace data_ingestion {

BinanceParser::BinanceParser() {
    loadPrecisionConfig();
}

void BinanceParser::loadPrecisionConfig() {
    try {
        // In a real implementation, this would load from a config file
        // For now, we'll hardcode some common trading pairs
        price_precision_["BTCUSDT"] = 2;
        price_precision_["ETHUSDT"] = 2;
        price_precision_["BNBUSDT"] = 2;
        price_precision_["ADAUSDT"] = 6;
        price_precision_["XRPUSDT"] = 5;
        
        quantity_precision_["BTCUSDT"] = 6;
        quantity_precision_["ETHUSDT"] = 5;
        quantity_precision_["BNBUSDT"] = 4;
        quantity_precision_["ADAUSDT"] = 1;
        quantity_precision_["XRPUSDT"] = 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading precision config: " << e.what() << std::endl;
    }
}

bool BinanceParser::parseOrderBookSnapshot(const std::vector<uint8_t>& data,
                                          int64_t& last_update_id,
                                          std::vector<PriceLevel>& bids,
                                          std::vector<PriceLevel>& asks) {
    try {
        // Parse the JSON data
        std::string json_str(data.begin(), data.end());
        json data_json = json::parse(json_str);
        
        // Extract the lastUpdateId
        if (data_json.contains("lastUpdateId")) {
            last_update_id = data_json["lastUpdateId"];
        }
        
        // Process bids
        if (data_json.contains("bids") && data_json["bids"].is_array()) {
            for (const auto& bid : data_json["bids"]) {
                if (bid.is_array() && bid.size() >= 2) {
                    PriceLevel level;
                    level.price = bid[0];
                    level.quantity = bid[1];
                    bids.push_back(level);
                }
            }
        }
        
        // Process asks
        if (data_json.contains("asks") && data_json["asks"].is_array()) {
            for (const auto& ask : data_json["asks"]) {
                if (ask.is_array() && ask.size() >= 2) {
                    PriceLevel level;
                    level.price = ask[0];
                    level.quantity = ask[1];
                    asks.push_back(level);
                }
            }
        }
        
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error parsing order book snapshot: " << e.what() << std::endl;
        return false;
    }
}

bool BinanceParser::parseOrderBookUpdate(const std::vector<uint8_t>& data,
                                        int64_t& first_update_id,
                                        int64_t& last_update_id,
                                        std::vector<PriceLevel>& bids_updates,
                                        std::vector<PriceLevel>& asks_updates) {
    try {
        // Parse the JSON data
        std::string json_str(data.begin(), data.end());
        json data_json = json::parse(json_str);
        
        // Extract the update IDs
        if (data_json.contains("U")) {
            first_update_id = data_json["U"];
        }
        
        if (data_json.contains("u")) {
            last_update_id = data_json["u"];
        }
        
        // Process bid updates
        if (data_json.contains("b") && data_json["b"].is_array()) {
            for (const auto& bid : data_json["b"]) {
                if (bid.is_array() && bid.size() >= 2) {
                    PriceLevel level;
                    level.price = bid[0];
                    level.quantity = bid[1];
                    bids_updates.push_back(level);
                }
            }
        }
        
        // Process ask updates
        if (data_json.contains("a") && data_json["a"].is_array()) {
            for (const auto& ask : data_json["a"]) {
                if (ask.is_array() && ask.size() >= 2) {
                    PriceLevel level;
                    level.price = ask[0];
                    level.quantity = ask[1];
                    asks_updates.push_back(level);
                }
            }
        }
        
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error parsing order book update: " << e.what() << std::endl;
        return false;
    }
}

bool BinanceParser::parseTrade(const std::vector<uint8_t>& data,
                              std::string& symbol,
                              std::string& price,
                              std::string& quantity,
                              bool& is_buyer_maker,
                              int64_t& trade_time) {
    try {
        // Parse the JSON data
        std::string json_str(data.begin(), data.end());
        json data_json = json::parse(json_str);
        
        // Extract the trade data
        if (data_json.contains("s")) {
            symbol = data_json["s"];
        }
        
        if (data_json.contains("p")) {
            price = data_json["p"];
        }
        
        if (data_json.contains("q")) {
            quantity = data_json["q"];
        }
        
        if (data_json.contains("m")) {
            is_buyer_maker = data_json["m"];
        }
        
        if (data_json.contains("T")) {
            trade_time = data_json["T"];
        }
        
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error parsing trade: " << e.what() << std::endl;
        return false;
    }
}

bool BinanceParser::parseTicker(const std::vector<uint8_t>& data,
                               std::string& symbol,
                               std::string& last_price,
                               std::string& bid_price,
                               std::string& askPrice,
                               std::string& volume,
                               int64_t& ticker_time) {
    try {
        // Parse the JSON data
        std::string json_str(data.begin(), data.end());
        json data_json = json::parse(json_str);
        
        // Extract the ticker data
        if (data_json.contains("s")) {
            symbol = data_json["s"];
        }
        
        if (data_json.contains("c")) {
            last_price = data_json["c"];
        }
        
        if (data_json.contains("b")) {
            bid_price = data_json["b"];
        }
        
        if (data_json.contains("a")) {
            askPrice = data_json["a"];
        }
        
        if (data_json.contains("v")) {
            volume = data_json["v"];
        }
        
        if (data_json.contains("E")) {
            ticker_time = data_json["E"];
        }
        
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Error parsing ticker: " << e.what() << std::endl;
        return false;
    }
}

std::string BinanceParser::normalizeTradingPair(const std::string& binance_symbol) {
    // Binance uses symbols like "BTCUSDT" without a separator
    // We'll normalize to a standard format like "BTC/USDT"
    
    // Common quote currencies to look for
    static const std::vector<std::string> quote_currencies = {
        "USDT", "BUSD", "USDC", "BTC", "ETH", "BNB", "USD", "EUR", "GBP", "AUD"
    };
    
    for (const auto& quote : quote_currencies) {
        if (binance_symbol.length() > quote.length()) {
            size_t pos = binance_symbol.length() - quote.length();
            
            if (binance_symbol.substr(pos) == quote) {
                std::string base = binance_symbol.substr(0, pos);
                return base + "/" + quote;
            }
        }
    }
    
    // If no quote currency is identified, return the original
    return binance_symbol;
}

std::string BinanceParser::standardizeTradingPair(const std::string& binance_symbol) {
    // Converts Binance's format to our internal standard format
    // This could be a different format than the normalized version
    
    // In this example, we'll use the same format as normalizeTradingPair
    return normalizeTradingPair(binance_symbol);
}

} // namespace data_ingestion
} // namespace trading 