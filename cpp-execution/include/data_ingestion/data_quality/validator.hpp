// include/data_ingestion/data_quality/validator.hpp
#pragma once

#include "data_ingestion/orderbook/order_book_types.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

namespace trading {
namespace data_ingestion {

// Validation rule function type
using ValidationRule = std::function<bool(const OrderBookUpdate&, std::string&)>;

// Validation context to store information about failures
struct ValidationContext {
    bool valid = true;
    std::vector<std::string> errors;
    
    void addError(const std::string& error) {
        valid = false;
        errors.push_back(error);
    }
    
    void reset() {
        valid = true;
        errors.clear();
    }
};

// Data validator class
class DataValidator {
public:
    DataValidator();
    
    // Add a validation rule
    void addRule(const std::string& name, ValidationRule rule);
    
    // Validate an order book update
    bool validate(const OrderBookUpdate& update, ValidationContext& context);
    
    // Static validation rules
    static bool validateBidAskOrder(const OrderBookUpdate& update, std::string& error);
    static bool validateNonNegativePrices(const OrderBookUpdate& update, std::string& error);
    static bool validateQuantities(const OrderBookUpdate& update, std::string& error);
    static bool validateBidAskSpread(const OrderBookUpdate& update, std::string& error);
    static bool validateRequiredFields(const OrderBookUpdate& update, std::string& error);
    
private:
    std::unordered_map<std::string, ValidationRule> rules_;
};

} // namespace data_ingestion
} // namespace trading