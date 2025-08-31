/**
 * @file binance_adapter.cpp
 * @brief Updated Binance adapter with interface implementation - Fixed version
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#include "protocol/binance_adapter.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <iostream>
#include "common/exchange_types.hpp"
using json = nlohmann::json;

namespace trading {
namespace data_ingestion {

bool BinanceAdapter::canHandle(const core::ExchangeData& data) {
    // Check if this is Binance data
    if (data.exchangeId != common::ExchangeId::binance()) {
        return false;
    }
    
    try {
        // Convert vector<uint8_t> to string - handle properly if data is bytes
        std::string jsonStr;
        if (!data.data.empty()) {
            jsonStr = std::string(data.data.begin(), data.data.end());
        } else {
            // Fallback to string data field if available
            jsonStr = data.data;
        }
        
        const auto& jsonData = json::parse(jsonStr);
        
        return jsonData.contains("e") &&
               (jsonData["e"] == "depthUpdate" || jsonData["e"] == "depth");
    } catch (...) {
        return false;
    }
}

OrderBookUpdate BinanceAdapter::convertToStandardFormat(const core::ExchangeData& data) {
    try {
        // Convert bytes to string properly
        std::string jsonStr;
        if (!data.data.empty()) {
            jsonStr = std::string(data.data.begin(), data.data.end());
        } else {
            throw std::runtime_error("Empty data in ExchangeData");
        }
        
        return convertDepthUpdate(jsonStr);
    } catch (const std::exception& e) {
        throw std::runtime_error("Binance adapter conversion failed: " + std::string(e.what()));
    }
}

common::ExchangeId BinanceAdapter::getSupportedExchange() const {
    return common::ExchangeId::binance();
}

std::string BinanceAdapter::getAdapterName() const {
    return "BinanceAdapter";
}

bool BinanceAdapter::isConfigured() const {
    return true; // Simple implementation is always configured
}

OrderBookUpdate BinanceAdapter::convertDepthUpdate(const std::string& jsonData) {
    if (!validateBinanceMessage(jsonData)) {
        throw std::runtime_error("Invalid Binance message format");
    }
    
    const auto& jsonObj = json::parse(jsonData);
    
    // Extract basic information
    std::string binanceSymbol = jsonObj["s"].get<std::string>();
    std::string tradingPair = extractTradingPair(binanceSymbol);
    uint64_t sequenceNumber = jsonObj["u"].get<uint64_t>();
    int64_t exchangeTimestamp = jsonObj["E"].get<int64_t>();
    
    // Create OrderBookUpdate using the constructor
    OrderBookUpdate update(
        common::ExchangeId::binance(),
        tradingPair,
        exchangeTimestamp,
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count(),
        sequenceNumber,
        false,
        std::vector<PriceLevel>(),
        std::vector<PriceLevel>()
    );
    // Set snapshot flag
    update.is_snapshot = jsonObj.contains("lastUpdateId");
    
    // Parse bids
    if (jsonObj.contains("b")) {
        for (const auto& bid : jsonObj["b"]) {
            try {
                update.bids.push_back(parsePriceLevel(bid));
            } catch (const std::exception& e) {
                update.has_anomaly = true;
                update.anomaly_type = "parsing_error";
                update.anomaly_details = std::string("Error parsing bid: ") + e.what();
            }
        }
    }
    
    // Parse asks
    if (jsonObj.contains("a")) {
        for (const auto& ask : jsonObj["a"]) {
            try {
                update.asks.push_back(parsePriceLevel(ask));
            } catch (const std::exception& e) {
                update.has_anomaly = true;
                update.anomaly_type = "parsing_error";
                update.anomaly_details = std::string("Error parsing ask: ") + e.what();
            }
        }
    }
    
    // Handle checksum if available
    if (jsonObj.contains("checksum")) {
        update.checksum = jsonObj["checksum"].get<std::string>();
    }
    
    // Validate sequence number
    if (!validateSequenceNumber(update.sequence_number, tradingPair)) {
        update.has_anomaly = true;
        update.anomaly_type = "sequence_gap";
        update.anomaly_details = "Sequence number gap detected";
    }
    
    return update;
}

OrderBookUpdate BinanceAdapter::convertDepthSnapshot(const std::string& jsonData) {
    // Very similar to convertDepthUpdate but handles snapshot format
    OrderBookUpdate update = convertDepthUpdate(jsonData);
    update.is_snapshot = true;
    return update;
}

bool BinanceAdapter::validateBinanceMessage(const std::string& jsonData) const {
    try {
        const auto& jsonObj = json::parse(jsonData);
        
        // Check required fields
        if (!jsonObj.contains("e") || !jsonObj.contains("s") || !jsonObj.contains("E")) {
            return false;
        }
        
        // Check event type
        std::string eventType = jsonObj["e"];
        if (eventType != "depthUpdate" && eventType != "depth") {
            return false;
        }
        
        return true;
    } catch (...) {
        return false;
    }
}

PriceLevel BinanceAdapter::parsePriceLevel(const nlohmann::json& levelArray) {
    if (!levelArray.is_array() || levelArray.size() < 2) {
        throw std::runtime_error("Invalid price level format");
    }
    
    std::string priceStr = levelArray[0].get<std::string>();
    std::string qtyStr = levelArray[1].get<std::string>();
    
    double price = std::stod(priceStr);
    double quantity = std::stod(qtyStr);
    
    return PriceLevel(price, quantity);
}

std::string BinanceAdapter::extractTradingPair(const std::string& binanceSymbol) {
    // Convert BTCUSDT -> BTC/USDT
    // This is a simplified heuristic - in production you'd want a symbol mapping table
    
    // Common quote currencies in Binance
    std::vector<std::string> quoteCurrencies = {"USDT", "BUSD", "BTC", "ETH", "BNB", "USDC"};
    
    for (const auto& quote : quoteCurrencies) {
        if (binanceSymbol.length() > quote.length() && 
            binanceSymbol.substr(binanceSymbol.length() - quote.length()) == quote) {
            std::string base = binanceSymbol.substr(0, binanceSymbol.length() - quote.length());
            return base + "/" + quote;
        }
    }
    
    // Fallback: assume last 4 characters are quote currency
    if (binanceSymbol.length() > 4) {
        std::string base = binanceSymbol.substr(0, binanceSymbol.length() - 4);
        std::string quote = binanceSymbol.substr(binanceSymbol.length() - 4);
        return base + "/" + quote;
    }
    
    return binanceSymbol; // Return as-is if can't parse
}

bool BinanceAdapter::validateSequenceNumber(uint64_t sequence_number, const std::string& symbol) {
    std::lock_guard<std::mutex> lock(sequenceTrackingMutex_);
    
    auto it = lastSequenceNumbers_.find(symbol);
    if (it == lastSequenceNumbers_.end()) {
        // First message for this symbol
        lastSequenceNumbers_[symbol] = sequence_number;
        return true;
    }
    
    uint64_t lastSequence = it->second;
    if (sequence_number <= lastSequence) {
        // Out of order or duplicate message
        std::cerr << "Warning: Out-of-order sequence for " << symbol 
                  << ". Expected > " << lastSequence 
                  << ", got " << sequence_number << std::endl;
        return false;
    }
    
    if (sequence_number > lastSequence + 1) {
        // Gap detected
        std::cerr << "Warning: Sequence gap for " << symbol 
                  << ". Expected " << (lastSequence + 1) 
                  << ", got " << sequence_number << std::endl;
        // Update sequence number but mark as anomaly
        lastSequenceNumbers_[symbol] = sequence_number;
        return false;
    }
    
    // Sequential update
    lastSequenceNumbers_[symbol] = sequence_number;
    return true;
}

} // namespace data_ingestion
} // namespace trading