/**
 * @file binance_adapter.cpp
 * @brief Implementation of Binance-specific message format conversion
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */

// TODO: Implement Implementation of Binance-specific message format conversion
// binance_adapter.cpp
#include "data_ingestion/protocol/binance_adapter.hpp"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

bool BinanceAdapter::canHandle(const ExchangeData& data) {
    // Check if this is Binance data
    try {
        const auto& jsonData = json::parse(data.data);
        return jsonData.contains("e") && 
               (jsonData["e"] == "depthUpdate" || jsonData["e"] == "depth");
    } catch (...) {
        return false;
    }
}

// OrderBookUpdate BinanceAdapter::convertToStandardFormat(const ExchangeData& data) {
//     OrderBookUpdate update;
//     const auto& jsonData = json::parse(data.data);
    
//     update.exchangeId = "binance";
//     update.symbol = jsonData["s"];
//     update.exchangeTime = jsonData["E"];
//     update.receivedTime = data.receivedTime;
//     update.sequenceNumber = jsonData["u"];
//     update.isSnapshot = jsonData.contains("lastUpdateId");
    
//     // Parse bids and asks
//     for (const auto& bid : jsonData["b"]) {
//         PriceLevel level;
//         level.price = std::stod(bid[0].get<std::string>());
//         level.quantity = std::stod(bid[1].get<std::string>());
//         update.bids.push_back(level);
//     }
    
//     for (const auto& ask : jsonData["a"]) {
//         PriceLevel level;
//         level.price = std::stod(ask[0].get<std::string>());
//         level.quantity = std::stod(ask[1].get<std::string>());
//         update.asks.push_back(level);
//     }
    
//     // Some exchanges provide checksums
//     if (jsonData.contains("checksum")) {
//         update.checksum = jsonData["checksum"];
//     }
    
//     return update;
// }
OrderBookUpdate BinanceAdapter::convertToStandardFormat(const ExchangeData& data) {
    OrderBookUpdate update;
    const auto& jsonData = json::parse(data.data);
    
    // Set basic fields
    update.exchangeId.fromString("binance");
    update.tradingPair = jsonData["s"];
    update.exchange_timestamp = jsonData["E"];
    update.receivedTimestamp = data.receivedTime;
    update.sequence_number = jsonData["u"];
    update.is_snapshot = jsonData.contains("lastUpdateId");
    
    // Parse bids
    if (jsonData.contains("b")) {
        for (const auto& bid : jsonData["b"]) {
            try {
                std::string price_str = bid[0].get<std::string>();
                std::string qty_str = bid[1].get<std::string>();
                
                // Create PriceLevel directly with the constructor
                PriceLevel level(std::stod(price_str), std::stod(qty_str));
                
                // No need for order tracking functionality since we're using the simplified class
                // Just add the level to the update
                update.bids.push_back(level);
            } catch (const std::exception& e) {
                // Store anomaly information
                update.has_anomaly = true;
                update.anomaly_type = "parsing_error";
                update.anomaly_details = std::string("Error parsing bid: ") + e.what();
            }
        }
    }
    
    // Parse asks
    if (jsonData.contains("a")) {
        for (const auto& ask : jsonData["a"]) {
            try {
                std::string price_str = ask[0].get<std::string>();
                std::string qty_str = ask[1].get<std::string>();
                
                // Create PriceLevel directly with the constructor
                PriceLevel level(std::stod(price_str), std::stod(qty_str));
                
                // No need for order tracking functionality since we're using the simplified class
                // Just add the level to the update
                update.asks.push_back(level);
            } catch (const std::exception& e) {
                update.has_anomaly = true;
                update.anomaly_type = "parsing_error";
                update.anomaly_details = std::string("Error parsing ask: ") + e.what();
            }
        }
    }
    
    // Handle checksum if available
    if (jsonData.contains("checksum")) {
        update.checksum = jsonData["checksum"].get<std::string>();
    }
    
    // Store any additional fields for future extensibility
    for (auto it = jsonData.begin(); it != jsonData.end(); ++it) {
        const std::string& key = it.key();
        // Skip fields we've already processed
        if (key != "s" && key != "E" && key != "u" && key != "b" &&
            key != "a" && key != "lastUpdateId" && key != "checksum") {
            // Store as string value
            update.additional_fields[key] = it.value().dump();
        }
    }
    
    return update;
}
// OrderBookUpdate BinanceAdapter::convertToStandardFormat(const ExchangeData& data) {
//     OrderBookUpdate update;
//     const auto& jsonData = json::parse(data.data);
    
//     // Set basic fields
//     update.exchangeId = "binance";
//     update.tradingPair = jsonData["s"];
//     update.exchange_timestamp = jsonData["E"];
//     update.receivedTimestamp = data.receivedTime;
//     update.sequence_number = jsonData["u"];
//     update.is_snapshot = jsonData.contains("lastUpdateId");
    
//     // Parse bids
//     if (jsonData.contains("b")) {
//         for (const auto& bid : jsonData["b"]) {
//             try {
//                 std::string price_str = bid[0].get<std::string>();
//                 std::string qty_str = bid[1].get<std::string>();
                
//                 PriceLevel level(std::stod(price_str), std::stod(qty_str));
                
//                 // If individual order info is available (not typical for Binance)
//                 if (bid.size() > 2 && bid[2].is_string()) {
//                     std::string order_id = bid[2].get<std::string>();
//                     level.addOrder(order_id, level.getQuantity());
//                 }
                
//                 update.bids.push_back(level);
//             } catch (const std::exception& e) {
//                 // Store anomaly information
//                 update.has_anomaly = true;
//                 update.anomaly_type = "parsing_error";
//                 update.anomaly_details = std::string("Error parsing bid: ") + e.what();
//             }
//         }
//     }
    
//     // Parse asks
//     if (jsonData.contains("a")) {
//         for (const auto& ask : jsonData["a"]) {
//             try {
//                 std::string price_str = ask[0].get<std::string>();
//                 std::string qty_str = ask[1].get<std::string>();
                
//                 PriceLevel level(std::stod(price_str), std::stod(qty_str));
                
//                 // If individual order info is available (not typical for Binance)
//                 if (ask.size() > 2 && ask[2].is_string()) {
//                     std::string order_id = ask[2].get<std::string>();
//                     level.addOrder(order_id, level.getQuantity());
//                 }
                
//                 update.asks.push_back(level);
//             } catch (const std::exception& e) {
//                 update.has_anomaly = true;
//                 update.anomaly_type = "parsing_error";
//                 update.anomaly_details = std::string("Error parsing ask: ") + e.what();
//             }
//         }
//     }
    
//     // Handle checksum if available
//     if (jsonData.contains("checksum")) {
//         update.checksum = jsonData["checksum"].get<std::string>();
//     }
    
//     // Store any additional fields for future extensibility
//     for (auto it = jsonData.begin(); it != jsonData.end(); ++it) {
//         const std::string& key = it.key();
//         // Skip fields we've already processed
//         if (key != "s" && key != "E" && key != "u" && key != "b" && 
//             key != "a" && key != "lastUpdateId" && key != "checksum") {
            
//             // Store as string value
//             update.additional_fields[key] = it.value().dump();
//         }
//     }
    
//     return update;
// }