/**
 * @file binance_adapter.cpp
 * @brief Fixed Binance adapter implementation with proper error handling
 */

#include "protocol/binance_adapter.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cctype>

using json = nlohmann::json;

namespace trading {
namespace data_ingestion {

// IProtocolAdapter interface implementations
bool BinanceAdapter::canHandle(const core::ExchangeData& data) {
    if (data.exchangeId != common::ExchangeId::binance()) {
        return false;
    }
    
    try {
        json jsonData = json::parse(data.data);
        
        if (!jsonData.is_object() || !jsonData.contains("e") || !jsonData["e"].is_string()) {
            return false;
        }
        
        std::string eventType = jsonData["e"].get<std::string>();
        return (eventType == "depthUpdate" || eventType == "depth");
        
    } catch (...) {
        return false;
    }
}

OrderBookUpdate BinanceAdapter::convertToStandardFormat(const core::ExchangeData& data) {
    try {
        return convertDepthUpdate(data.data);
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
    return true;
}

// ExchangeAdapter interface implementations
bool BinanceAdapter::canHandle(const MarketDataUpdate& update) {
    return update.exchangeId == common::ExchangeId::binance();
}

OrderBookUpdate BinanceAdapter::normalize(const MarketDataUpdate& update) {
    OrderBookUpdate normalized;
    
    try {
        // Set basic fields
        normalized.exchangeId = update.exchangeId;
        normalized.tradingPair = standardizeTradingPair(update.tradingPair);
        normalized.exchange_timestamp = update.exchange_timestamp;
        normalized.receivedTimestamp = update.receivedTimestamp;
        normalized.sequenceNumber = update.sequenceNumber;
        normalized.symbol = core::Symbol::fromString(normalized.tradingPair);
        
        // Parse the raw data based on update type
        if (update.updateType == "orderbook") {
            std::string jsonData(update.data.begin(), update.data.end());
            json jsonObj = json::parse(jsonData);
            
            // Determine if this is a snapshot or update
            normalized.isSnapshot = (jsonObj.contains("bids") && jsonObj.contains("asks"));
            
            // Parse bids safely
            if (jsonObj.contains("bids") && jsonObj["bids"].is_array()) {
                for (const auto& bidArray : jsonObj["bids"]) {
                    if (bidArray.is_array() && bidArray.size() >= 2) {
                        try {
                            PriceLevel level = parsePriceLevel(bidArray);
                            if (level.getQuantity() > 0.0) { // Only add non-zero quantities
                                normalized.bids.push_back(level);
                            }
                        } catch (...) {
                            // Skip invalid price levels
                            continue;
                        }
                    }
                }
            }
            
            // Parse asks safely
            if (jsonObj.contains("asks") && jsonObj["asks"].is_array()) {
                for (const auto& askArray : jsonObj["asks"]) {
                    if (askArray.is_array() && askArray.size() >= 2) {
                        try {
                            PriceLevel level = parsePriceLevel(askArray);
                            if (level.getQuantity() > 0.0) { // Only add non-zero quantities
                                normalized.asks.push_back(level);
                            }
                        } catch (...) {
                            // Skip invalid price levels
                            continue;
                        }
                    }
                }
            }
            
            // Handle sequence numbers safely
            if (jsonObj.contains("lastUpdateId") && jsonObj["lastUpdateId"].is_number()) {
                normalized.sequenceNumber = jsonObj["lastUpdateId"].get<uint64_t>();
            } else if (jsonObj.contains("U") && jsonObj["U"].is_number()) {
                normalized.sequenceNumber = jsonObj["U"].get<uint64_t>();
            }
            
            // Validate sequence number
            bool validSequence = validateSequenceNumber(normalized.sequenceNumber, normalized.tradingPair);
            if (!validSequence) {
                normalized.has_anomaly = true;
                normalized.anomaly_type = "sequence_gap";
                normalized.anomaly_details = "Sequence number gap detected";
            }
            
            // Generate checksum for data integrity
            normalized.checksum = generateChecksum(normalized.bids, normalized.asks);
        }
        
    } catch (const std::exception& e) {
        normalized.has_anomaly = true;
        normalized.anomaly_type = "parsing_error";
        normalized.anomaly_details = e.what();
    }
    
    return normalized;
}

std::string BinanceAdapter::standardizeTradingPair(const std::string& tradingPair) {
    return extractTradingPair(tradingPair);
}

ExchangeId BinanceAdapter::getExchangeId() const {
    return common::ExchangeId::binance();
}

// Binance-specific methods
OrderBookUpdate BinanceAdapter::convertDepthUpdate(const std::string& jsonData) {
    if (!validateBinanceMessage(jsonData)) {
        throw std::runtime_error("Invalid Binance message format");
    }
    
    json jsonObj = json::parse(jsonData);
    OrderBookUpdate update;
    
    // Extract basic information safely
    if (jsonObj.contains("s") && jsonObj["s"].is_string()) {
        std::string binanceSymbol = jsonObj["s"].get<std::string>();
        update.tradingPair = extractTradingPair(binanceSymbol);
        update.symbol = core::Symbol::fromString(update.tradingPair);
    }
    
    update.exchangeId = common::ExchangeId::binance();
    update.receivedTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Extract timestamps safely
    if (jsonObj.contains("E") && jsonObj["E"].is_number()) {
        update.exchange_timestamp = jsonObj["E"].get<int64_t>();
    }
    
    // Extract sequence numbers safely
    if (jsonObj.contains("u") && jsonObj["u"].is_number()) {
        update.sequenceNumber = jsonObj["u"].get<uint64_t>();
    } else if (jsonObj.contains("lastUpdateId") && jsonObj["lastUpdateId"].is_number()) {
        update.sequenceNumber = jsonObj["lastUpdateId"].get<uint64_t>();
    }
    
    // Determine if this is a snapshot
    update.isSnapshot = jsonObj.contains("bids") && jsonObj.contains("asks");
    
    // Parse bids safely
    if (jsonObj.contains("b") && jsonObj["b"].is_array()) {
        for (const auto& bidArray : jsonObj["b"]) {
            try {
                PriceLevel level = parsePriceLevel(bidArray);
                if (level.getQuantity() > 0.0) {
                    update.bids.push_back(level);
                }
            } catch (...) {
                continue; // Skip invalid levels
            }
        }
    }
    
    // Parse asks safely
    if (jsonObj.contains("a") && jsonObj["a"].is_array()) {
        for (const auto& askArray : jsonObj["a"]) {
            try {
                PriceLevel level = parsePriceLevel(askArray);
                if (level.getQuantity() > 0.0) {
                    update.asks.push_back(level);
                }
            } catch (...) {
                continue; // Skip invalid levels
            }
        }
    }
    
    // Generate checksum
    update.checksum = generateChecksum(update.bids, update.asks);
    
    return update;
}

OrderBookUpdate BinanceAdapter::convertDepthSnapshot(const std::string& jsonData) {
    json jsonObj = json::parse(jsonData);
    OrderBookUpdate update;
    
    update.exchangeId = common::ExchangeId::binance();
    update.isSnapshot = true;
    update.receivedTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Extract symbol if available
    if (jsonObj.contains("symbol") && jsonObj["symbol"].is_string()) {
        std::string binanceSymbol = jsonObj["symbol"].get<std::string>();
        update.tradingPair = extractTradingPair(binanceSymbol);
        update.symbol = core::Symbol::fromString(update.tradingPair);
    }
    
    // Extract sequence number
    if (jsonObj.contains("lastUpdateId") && jsonObj["lastUpdateId"].is_number()) {
        update.sequenceNumber = jsonObj["lastUpdateId"].get<uint64_t>();
    }
    
    // Parse bids
    if (jsonObj.contains("bids") && jsonObj["bids"].is_array()) {
        for (const auto& bidArray : jsonObj["bids"]) {
            try {
                PriceLevel level = parsePriceLevel(bidArray);
                if (level.getQuantity() > 0.0) {
                    update.bids.push_back(level);
                }
            } catch (...) {
                continue;
            }
        }
    }
    
    // Parse asks
    if (jsonObj.contains("asks") && jsonObj["asks"].is_array()) {
        for (const auto& askArray : jsonObj["asks"]) {
            try {
                PriceLevel level = parsePriceLevel(askArray);
                if (level.getQuantity() > 0.0) {
                    update.asks.push_back(level);
                }
            } catch (...) {
                continue;
            }
        }
    }
    
    update.checksum = generateChecksum(update.bids, update.asks);
    return update;
}

PriceLevel BinanceAdapter::parsePriceLevel(const nlohmann::json& levelArray) {
    if (!levelArray.is_array() || levelArray.size() < 2) {
        throw std::runtime_error("Invalid price level format");
    }
    
    if (!levelArray[0].is_string() || !levelArray[1].is_string()) {
        throw std::runtime_error("Price level elements must be strings");
    }
    
    try {
        double price = std::stod(levelArray[0].get<std::string>());
        double quantity = std::stod(levelArray[1].get<std::string>());
        
        if (price <= 0.0) {
            throw std::runtime_error("Invalid price: must be positive");
        }
        
        // Allow zero quantity for deletions
        if (quantity < 0.0) {
            throw std::runtime_error("Invalid quantity: cannot be negative");
        }
        
        return PriceLevel(price, quantity);
        
    } catch (const std::invalid_argument& e) {
        throw std::runtime_error("Failed to parse price/quantity: " + std::string(e.what()));
    }
}

std::string BinanceAdapter::extractTradingPair(const std::string& binanceSymbol) {
    // Common quote currencies in Binance, ordered by length (longest first)
    static const std::vector<std::string> quoteCurrencies = {
        "USDT", "BUSD", "USDC", "TUSD", "FDUSD",  // Stablecoins
        "BTC", "ETH", "BNB",                       // Major cryptos
        "EUR", "GBP", "AUD", "TRY",               // Fiat currencies
        "DOT", "ADA", "XRP", "SOL"                // Other major alts
    };
    
    // Try to match against known quote currencies
    for (const auto& quote : quoteCurrencies) {
        if (binanceSymbol.length() > quote.length()) {
            std::string suffix = binanceSymbol.substr(binanceSymbol.length() - quote.length());
            if (suffix == quote) {
                std::string base = binanceSymbol.substr(0, binanceSymbol.length() - quote.length());
                return base + "/" + quote;
            }
        }
    }
    
    // Fallback: assume last 4 characters are quote currency for longer symbols
    if (binanceSymbol.length() >= 6) {
        std::string base = binanceSymbol.substr(0, binanceSymbol.length() - 4);
        std::string quote = binanceSymbol.substr(binanceSymbol.length() - 4);
        return base + "/" + quote;
    }
    
    // Last resort: return as-is
    return binanceSymbol;
}

bool BinanceAdapter::validateSequenceNumber(uint64_t sequenceNumber, const std::string& symbol) {
    std::lock_guard<std::mutex> lock(sequenceTrackingMutex_);
    
    auto it = lastSequenceNumbers_.find(symbol);
    if (it == lastSequenceNumbers_.end()) {
        // First sequence number for this symbol
        lastSequenceNumbers_[symbol] = sequenceNumber;
        return true;
    }
    
    uint64_t lastSequence = it->second;
    
    // Check for out-of-order or duplicate messages
    if (sequenceNumber <= lastSequence) {
        // This is either a duplicate or out-of-order message
        return false;
    }
    
    // Check for sequence gaps
    if (sequenceNumber > lastSequence + 1) {
        // There's a gap in sequence numbers - this might indicate missed messages
        // Update the stored sequence number but return false to indicate the gap
        lastSequenceNumbers_[symbol] = sequenceNumber;
        return false;
    }
    
    // Everything looks good
    lastSequenceNumbers_[symbol] = sequenceNumber;
    return true;
}

std::string BinanceAdapter::generateChecksum(const std::vector<PriceLevel>& bids,
                                           const std::vector<PriceLevel>& asks) const {
    std::string combined;
    combined.reserve(1024); // Pre-allocate for performance
    
    // Combine all bids (sorted by price descending for bids)
    for (const auto& bid : bids) {
        combined += std::to_string(bid.getPrice()) + ":" + std::to_string(bid.getQuantity()) + "|";
    }
    
    combined += "||"; // Separator between bids and asks
    
    // Combine all asks (sorted by price ascending for asks)
    for (const auto& ask : asks) {
        combined += std::to_string(ask.getPrice()) + ":" + std::to_string(ask.getQuantity()) + "|";
    }
    
    // Generate hash
    return std::to_string(std::hash<std::string>{}(combined));
}

// OrderBookUpdate BinanceAdapter::convertDepthSnapshot(const std::string& jsonData) {
//     json jsonObj = json::parse(jsonData);
//     OrderBookUpdate update;
    
//     update.exchangeId = common::ExchangeId::binance();
//     update.isSnapshot = true;
//     update.receivedTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
//         std::chrono::system_clock::now().time_since_epoch()).count();
    
//     // Extract symbol if available
//     if (jsonObj.contains("symbol") && jsonObj["symbol"].is_string()) {
//         std::string binanceSymbol = jsonObj["symbol"].get<std::string>();
//         update.tradingPair = extractTradingPair(binanceSymbol);
//         update.symbol = core::Symbol::fromString(update.tradingPair);
//     }
    
//     // Extract sequence number
//     if (jsonObj.contains("lastUpdateId") && jsonObj["lastUpdateId"].is_number()) {
//         update.sequenceNumber = jsonObj["lastUpdateId"].get<uint64_t>();
//     }
    
//     // Parse bids safely
//     if (jsonObj.contains("bids") && jsonObj["bids"].is_array()) {
//         for (const auto& bidArray : jsonObj["bids"]) {
//             try {
//                 PriceLevel level = parsePriceLevel(bidArray);
//                 if (level.getQuantity() > 0.0) {
//                     update.bids.push_back(level);
//                 }
//             } catch (...) {
//                 continue; // Skip invalid levels
//             }
//         }
//     }
    
//     // Parse asks safely
//     if (jsonObj.contains("asks") && jsonObj["asks"].is_array()) {
//         for (const auto& askArray : jsonObj["asks"]) {
//             try {
//                 PriceLevel level = parsePriceLevel(askArray);
//                 if (level.getQuantity() > 0.0) {
//                     update.asks.push_back(level);
//                 }
//             } catch (...) {
//                 continue; // Skip invalid levels
//             }
//         }
//     }
    
//     update.checksum = generateChecksum(update.bids, update.asks);
//     return update;
// }

// OrderBookUpdate BinanceAdapter::convertDepthUpdate(const std::string& jsonData) {
//     if (!validateBinanceMessage(jsonData)) {
//         throw std::runtime_error("Invalid Binance message format");
//     }
    
//     json jsonObj = json::parse(jsonData);
//     OrderBookUpdate update;
    
//     // Extract basic information safely
//     if (jsonObj.contains("s") && jsonObj["s"].is_string()) {
//         std::string binanceSymbol = jsonObj["s"].get<std::string>();
//         update.tradingPair = extractTradingPair(binanceSymbol);
//         update.symbol = core::Symbol::fromString(update.tradingPair);
//     }
    
//     update.exchangeId = common::ExchangeId::binance();
//     update.isSnapshot = false; // Depth updates are incremental
//     update.receivedTimestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
//         std::chrono::system_clock::now().time_since_epoch()).count();
    
//     // Extract timestamps safely
//     if (jsonObj.contains("E") && jsonObj["E"].is_number()) {
//         update.exchange_timestamp = jsonObj["E"].get<int64_t>();
//     }
    
//     // Extract sequence numbers safely
//     if (jsonObj.contains("u") && jsonObj["u"].is_number()) {
//         update.sequenceNumber = jsonObj["u"].get<uint64_t>();
//     }
    
//     // Parse bid updates
//     if (jsonObj.contains("b") && jsonObj["b"].is_array()) {
//         for (const auto& bidArray : jsonObj["b"]) {
//             try {
//                 PriceLevel level = parsePriceLevel(bidArray);
//                 update.bids.push_back(level); // Include zero quantities for deletions
//             } catch (...) {
//                 continue; // Skip invalid levels
//             }
//         }
//     }
    
//     // Parse ask updates
//     if (jsonObj.contains("a") && jsonObj["a"].is_array()) {
//         for (const auto& askArray : jsonObj["a"]) {
//             try {
//                 PriceLevel level = parsePriceLevel(askArray);
//                 update.asks.push_back(level); // Include zero quantities for deletions
//             } catch (...) {
//                 continue; // Skip invalid levels
//             }
//         }
//     }
    
//     // Validate sequence number
//     bool validSequence = validateSequenceNumber(update.sequenceNumber, update.tradingPair);
//     if (!validSequence) {
//         update.has_anomaly = true;
//         update.anomaly_type = "sequence_gap";
//         update.anomaly_details = "Sequence number gap or duplicate detected";
//     }
    
//     // Generate checksum
//     update.checksum = generateChecksum(update.bids, update.asks);
    
//     return update;
// }

bool BinanceAdapter::validateBinanceMessage(const std::string& jsonData) const {
    try {
        json jsonObj = json::parse(jsonData);
        
        if (!jsonObj.is_object()) return false;
        if (!jsonObj.contains("e") || !jsonObj["e"].is_string()) return false;
        if (!jsonObj.contains("s") || !jsonObj["s"].is_string()) return false;
        if (!jsonObj.contains("E") || !jsonObj["E"].is_number()) return false;
        
        std::string eventType = jsonObj["e"].get<std::string>();
        return (eventType == "depthUpdate" || eventType == "depth");
        
    } catch (...) {
        return false;
    }
}

} // namespace data_ingestion
} // namespace trading