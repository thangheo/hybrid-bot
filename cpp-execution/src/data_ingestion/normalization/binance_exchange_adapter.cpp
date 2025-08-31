#include "data_ingestion/normalization/binance_exchange_adapter.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
namespace trading {
namespace data_ingestion {

bool BinanceExchangeAdapter::canHandle(const MarketDataUpdate& update) {
    // Check if this is from Binance
    if (update.exchangeId.toString() != "binance") {
        return false;
    }
    
    // Check if it's an order book related update
    return (update.messageType == "depth" || 
            update.messageType == "depthUpdate" ||
            update.messageType == "book");
}

OrderBookUpdate BinanceExchangeAdapter::normalize(const MarketDataUpdate& update) {
    try {
        // Parse JSON data
        nlohmann::json data = nlohmann::json::parse(
            std::string(update.data.begin(), update.data.end()));
        
        // Check if this is a depth update or snapshot
        if (data.contains("e") && data.value("e", "") == "depthUpdate") {
            return parseDepthUpdate(data, update);
        } else if (data.contains("bids") && data.contains("asks")) {
            return parseDepthSnapshot(data, update);
        }
        
        throw std::runtime_error("Unknown Binance message format");
        
    } catch (const std::exception& e) {
        std::cerr << "Error normalizing Binance data: " << e.what() << std::endl;
        throw;
    }
}

OrderBookUpdate BinanceExchangeAdapter::parseDepthUpdate(
    const nlohmann::json& data, const MarketDataUpdate& update) {
    
    OrderBookUpdate result;
    result.exchangeId = update.exchangeId;
    result.tradingPair = standardizeTradingPair(update.tradingPair);
    result.is_snapshot = false;
    
    // Extract timestamp
    if (data.contains("E")) {
        result.exchange_timestamp = data.value("E", 0LL);
    } else {
        result.exchange_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
    
    result.receivedTimestamp = update.receivedTimestamp;
    
    // Extract sequence number
    if (data.contains("u")) {
        result.sequence_number = data.value("u", 0LL);
    }
    
    // Parse bids and asks
    if (data.contains("b")) {
        result.bids = parsePriceLevels(data.value("b", nlohmann::json::array()));
    }
    if (data.contains("a")) {
        result.asks = parsePriceLevels(data.value("a", nlohmann::json::array()));
    }
    
    return result;
}

OrderBookUpdate BinanceExchangeAdapter::parseDepthSnapshot(
    const nlohmann::json& data, const MarketDataUpdate& update) {
    
    OrderBookUpdate result;
    result.exchangeId = update.exchangeId;
    result.tradingPair = standardizeTradingPair(update.tradingPair);
    result.is_snapshot = true;
    
    // Extract timestamp
    if (data.contains("T")) {
        result.exchange_timestamp = data.value("T", 0LL);
    } else {
        result.exchange_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    result.receivedTimestamp = update.receivedTimestamp;

    // Extract sequence number
    if (data.contains("lastUpdateId")) {
        result.sequence_number = data.value("lastUpdateId", 0LL);
    }
    
    // Parse bids and asks
    if (data.contains("bids")) {
        result.bids = parsePriceLevels(data.value("bids", nlohmann::json::array()));
    }
    if (data.contains("asks")) {
        result.asks = parsePriceLevels(data.value("asks", nlohmann::json::array()));
    }
    
    return result;
}

std::vector<PriceLevel> BinanceExchangeAdapter::parsePriceLevels(const nlohmann::json& levels) {
    std::vector<PriceLevel> result;
    
    for (const auto& level : levels) {
        if (level.is_array() && level.size() >= 2) {
            PriceLevel priceLevel;
            priceLevel.setPrice(std::stod(level[0].get<std::string>()));
            priceLevel.setQuantity(std::stod(level[1].get<std::string>()));
            priceLevel.setTimestamp(std::chrono::system_clock::now());

            result.push_back(priceLevel);
        }
    }
    
    return result;
}

std::string BinanceExchangeAdapter::standardizeTradingPair(const std::string& tradingPair) {
    // Binance uses format like "BTCUSDT", convert to "BTC/USDT"
    // This is a simplified conversion - in production you'd want a proper mapping
    
    // Common patterns for major pairs
    if (tradingPair.length() >= 4 && tradingPair.substr(tradingPair.length() - 4) == "USDT") {
        std::string base = tradingPair.substr(0, tradingPair.length() - 4);
        return base + "/USDT";
    } else if (tradingPair.length() >= 3 && tradingPair.substr(tradingPair.length() - 3) == "BTC") {
        std::string base = tradingPair.substr(0, tradingPair.length() - 3);
        return base + "/BTC";
    } else if (tradingPair.length() >= 3 && tradingPair.substr(tradingPair.length() - 3) == "ETH") {
        std::string base = tradingPair.substr(0, tradingPair.length() - 3);
        return base + "/ETH";
    }
    
    // Fallback: return as-is
    return tradingPair;
}

ExchangeId BinanceExchangeAdapter::getExchangeId() const {
    return ExchangeId::binance();
}

} // namespace data_ingestion
} // namespace trading