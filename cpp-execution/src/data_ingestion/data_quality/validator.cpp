// src/data_ingestion/data_quality/validator.cpp
#include "data_ingestion/data_quality/validator.hpp"
#include "data_ingestion/orderbook/price_level.hpp"
#include <algorithm>
#include <sstream>

namespace trading {
namespace data_ingestion {

DataValidator::DataValidator() {
    // Add default validation rules
    addRule("bid_ask_order", validateBidAskOrder);
    addRule("non_negative_prices", validateNonNegativePrices);
    addRule("quantities", validateQuantities);
    addRule("bid_ask_spread", validateBidAskSpread);
    addRule("required_fields", validateRequiredFields);
}

void DataValidator::addRule(const std::string& name, ValidationRule rule) {
    rules_[name] = rule;
}

bool DataValidator::validate(const OrderBookUpdate& update, ValidationContext& context) {
    context.reset();
    
    // Apply all validation rules
    for (const auto& rule_pair : rules_) {
        std::string error;
        if (!rule_pair.second(update, error)) {
            context.addError(rule_pair.first + ": " + error);
        }
    }
    
    return context.valid;
}

bool DataValidator::validateBidAskOrder(const OrderBookUpdate& update, std::string& error) {
    // Ensure bids are sorted in descending order
    for (size_t i = 1; i < update.bids.size(); ++i) {
        double prev_price = update.bids[i-1].getPrice();
        double curr_price = update.bids[i].getPrice();
        
        if (curr_price > prev_price) {
            error = "Bids are not properly sorted (descending order)";
            return false;
        }
    }
    
    // Ensure asks are sorted in ascending order
    for (size_t i = 1; i < update.asks.size(); ++i) {
        double prev_price = update.asks[i-1].getPrice();
        double curr_price = update.asks[i].getPrice();
        
        if (curr_price < prev_price) {
            error = "Asks are not properly sorted (ascending order)";
            return false;
        }
    }
    
    return true;
}

bool DataValidator::validateNonNegativePrices(const OrderBookUpdate& update, std::string& error) {
    // Check bid prices
    for (const auto& level : update.bids) {
        double price = level.getPrice();
        if (price <= 0.0) {
            error = "Found non-positive bid price: " + std::to_string(price);
            return false;
        }
    }
    
    // Check ask prices
    for (const auto& level : update.asks) {
        double price = level.getPrice();
        if (price <= 0.0) {
            error = "Found non-positive ask price: " + std::to_string(price);
            return false;
        }
    }
    
    return true;
}

bool DataValidator::validateQuantities(const OrderBookUpdate& update, std::string& error) {
    // Check bid quantities
    for (const auto& level : update.bids) {
        double quantity = level.getQuantity();
        if (quantity <= 0.0) {
            error = "Found non-positive bid quantity: " + std::to_string(quantity);
            return false;
        }
    }
    
    // Check ask quantities
    for (const auto& level : update.asks) {
        double quantity = level.getQuantity();
        if (quantity <= 0.0) {
            error = "Found non-positive ask quantity: " + std::to_string(quantity);
            return false;
        }
    }
    
    return true;
}

bool DataValidator::validateBidAskSpread(const OrderBookUpdate& update, std::string& error) {
    if (update.bids.empty() || update.asks.empty()) {
        // Skip validation for empty order books
        return true;
    }
    
    double best_bid = update.bids[0].getPrice();
    double best_ask = update.asks[0].getPrice();
    
    if (best_bid >= best_ask) {
        error = "Invalid spread: best bid (" + std::to_string(best_bid) + 
                ") >= best ask (" + std::to_string(best_ask) + ")";
        return false;
    }
    
    return true;
}

bool DataValidator::validateRequiredFields(const OrderBookUpdate& update, std::string& error) {
    std::stringstream ss;
    bool valid = true;
    
    if (update.exchangeId.toString().empty()) {
        ss << "Missing exchangeId; ";
        valid = false;
    }
    
    if (update.tradingPair.empty()) {
        ss << "Missing tradingPair; ";
        valid = false;
    }
    
    if (update.exchange_timestamp == 0) {
        ss << "Missing exchange_timestamp; ";
        valid = false;
    }
    
    if (update.receivedTimestamp == 0) {
        ss << "Missing receivedTimestamp; ";
        valid = false;
    }
    
    if (update.sequenceNumber == 0 && !update.isSnapshot) {
        ss << "Missing sequenceNumber for incremental update; ";
        valid = false;
    }
    
    if (!valid) {
        error = ss.str();
    }
    
    return valid;
}

} // namespace data_ingestion
} // namespace trading