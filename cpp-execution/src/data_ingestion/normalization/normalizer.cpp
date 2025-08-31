/**
 * @file normalizer.cpp
 * @brief Implementation of the Normalizer class
 * 
 * @author Trading System Team
 * @date August 31, 2025
 */

#include "data_ingestion/normalization/normalizer.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace trading {
namespace data_ingestion {

Normalizer::Normalizer(OrderBookCallback callback)
    : callback_(callback) {
    loadFormatConfig();
}

void Normalizer::processOrderBookUpdate(const OrderBookUpdate& update) {
    // For now, create a simple OrderBook from the update
    // In a real implementation, this would be more sophisticated
    OrderBook orderBook(
        update.exchangeId, 
        update.tradingPair, 
        nullptr  // No callback for this temporary order book
    );
    
    // Call the callback with the normalized order book
    if (callback_) {
        callback_(orderBook);
    }
}

std::string Normalizer::normalizePrice(const core::ExchangeId& exchangeId, 
                                      const std::string& tradingPair,
                                      const std::string& price) {
    // Simple implementation - in practice this would use exchange-specific rules
    try {
        double priceValue = std::stod(price);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(8) << priceValue;
        return oss.str();
    } catch (const std::exception& e) {
        std::cerr << "Error normalizing price " << price << ": " << e.what() << std::endl;
        return price; // Return original if normalization fails
    }
}

std::string Normalizer::normalizeQuantity(const core::ExchangeId& exchangeId, 
                                         const std::string& tradingPair,
                                         const std::string& quantity) {
    // Simple implementation - in practice this would use exchange-specific rules
    try {
        double quantityValue = std::stod(quantity);
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(8) << quantityValue;
        return oss.str();
    } catch (const std::exception& e) {
        std::cerr << "Error normalizing quantity " << quantity << ": " << e.what() << std::endl;
        return quantity; // Return original if normalization fails
    }
}

void Normalizer::loadFormatConfig() {
    // Load exchange-specific formatting rules
    // For now, use default precision values
    
    // Example: Binance typically uses 8 decimal places for prices
    price_precisions_["binance"]["BTCUSDT"] = 2;
    price_precisions_["binance"]["ETHUSDT"] = 2;
    quantity_precisions_["binance"]["BTCUSDT"] = 6;
    quantity_precisions_["binance"]["ETHUSDT"] = 6;
    
    // Add more exchanges and trading pairs as needed
    price_precisions_["coinbase"]["BTC-USD"] = 2;
    price_precisions_["coinbase"]["ETH-USD"] = 2;
    quantity_precisions_["coinbase"]["BTC-USD"] = 8;
    quantity_precisions_["coinbase"]["ETH-USD"] = 8;
}

} // namespace data_ingestion
} // namespace trading
