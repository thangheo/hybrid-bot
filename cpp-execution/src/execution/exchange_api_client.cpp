// exchange_api_client.cpp
#include "exchange_api_client.hpp"
#include "models/order.hpp"   
#include <iostream>
#include <iomanip>   // For std::setprecision
#include <sstream>
#include <chrono>
#include <thread>
#include <algorithm>  // For std::transform
#include <cctype>

namespace trading {
namespace execution {

// This is just the implementation of the abstract base class.
// Since ExchangeApiClient is an interface class with pure virtual functions,
// we don't actually implement the methods here.
//
// However, we can provide some helper functions that might be useful for derived classes.
// These are implemented in this file but are not part of the public interface.

namespace util {

// Convert string to uppercase
std::string toUpper(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    return result;
}

// Convert string to lowercase
std::string toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

// Normalize trading symbol to a form acceptable by most exchange APIs.
// Removes or replaces illegal characters and uppercases letters. Allowed
// characters (per exchange regex) are A-Z, 0-9, '-', '_' and '.'.
std::string normalizeSymbolForExchange(const std::string& sym) {
    std::string out;
    out.reserve(sym.size());

    for (unsigned char c : sym) {
        // Skip common separators that are illegal for some exchanges
        if (c == '/' || c == ':' || c == ' ') {
            continue;
        }

        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            out.push_back(static_cast<char>(std::toupper(c)));
        }
        // Any other character is ignored
    }

    return out;
}

// Generate a nonce (timestamp in milliseconds)
int64_t generateNonce() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Format order side as a string (different exchanges use different formats)
std::string formatOrderSide(models::OrderSide side, const std::string& format) {
    if (format == "UPPER") {
        return side == models::OrderSide::BUY ? "BUY" : "SELL";
    } else if (format == "LOWER") {
        return side == models::OrderSide::BUY ? "buy" : "sell";
    } else if (format == "NUMERIC") {
        return side == models::OrderSide::BUY ? "1" : "2";
    }
    
    // Default format
    return side == models::OrderSide::BUY ? "buy" : "sell";
}
/**
 * Formats an order type into a string representation based on specified format.
 * 
 * @param type The order type to format
 * @param format The desired format ("UPPER", "LOWER", or any other for default)
 * @return String representation of the order type
 */
std::string formatOrderType(models::OrderType type, const std::string& format) {
    if (format == "UPPER") {
        switch (type) {
            case models::OrderType::MARKET:             return "MARKET";
            case models::OrderType::LIMIT:              return "LIMIT";
            case models::OrderType::STOP:               return "STOP";
            case models::OrderType::STOP_LIMIT:         return "STOP_LIMIT";
            case models::OrderType::ICEBERG:            return "ICEBERG";
            case models::OrderType::TRAILING_STOP:      return "TRAILING_STOP";
            case models::OrderType::FILL_OR_KILL:       return "FILL_OR_KILL";
            case models::OrderType::IMMEDIATE_OR_CANCEL: return "IMMEDIATE_OR_CANCEL";
            case models::OrderType::POST_ONLY:          return "POST_ONLY";
            default:                            return "UNKNOWN";
        }
    } else if (format == "LOWER") {
        switch (type) {
            case models::OrderType::MARKET:             return "market";
            case models::OrderType::LIMIT:              return "limit";
            case models::OrderType::STOP:               return "stop";
            case models::OrderType::STOP_LIMIT:         return "stop_limit";
            case models::OrderType::ICEBERG:            return "iceberg";
            case models::OrderType::TRAILING_STOP:      return "trailing_stop";
            case models::OrderType::FILL_OR_KILL:       return "fill_or_kill";
            case models::OrderType::IMMEDIATE_OR_CANCEL: return "ioc";
            case models::OrderType::POST_ONLY:          return "post_only";
            default:                            return "unknown";
        }
    }
    
    // Default format
    switch (type) {
        case models::OrderType::MARKET:             return "market";
        case models::OrderType::LIMIT:              return "limit";
        case models::OrderType::STOP:               return "stop";
        case models::OrderType::STOP_LIMIT:         return "stop_limit";
        case models::OrderType::ICEBERG:            return "iceberg";
        case models::OrderType::TRAILING_STOP:      return "trailing_stop";
        case models::OrderType::FILL_OR_KILL:       return "fok";
        case models::OrderType::IMMEDIATE_OR_CANCEL: return "ioc";
        case models::OrderType::POST_ONLY:          return "post_only";
        default:                            return "unknown";
    }
}

// Format timeInForce as a string (different exchanges use different formats)
std::string formatTimeInForce(models::TimeInForce tif, const std::string& format) {
    if (format == "UPPER") {
        switch (tif) {
            case models::TimeInForce::GTC: return "GTC";
            case models::TimeInForce::IOC: return "IOC";
            case models::TimeInForce::FOK: return "FOK";
            case models::TimeInForce::GTD: return "GTD";
            default: return "UNKNOWN";
        }
    } else if (format == "LOWER") {
        switch (tif) {
            case models::TimeInForce::GTC: return "gtc";
            case models::TimeInForce::IOC: return "ioc";
            case models::TimeInForce::FOK: return "fok";
            case models::TimeInForce::GTD: return "gtd";
            default: return "unknown";
        }
    }
    
    // Default format
    switch (tif) {
        case models::TimeInForce::GTC: return "gtc";
        case models::TimeInForce::IOC: return "ioc";
        case models::TimeInForce::FOK: return "fok";
        case models::TimeInForce::GTD: return "gtd";
        default: return "unknown";
    }
}

// Create common parameters map for order placement
std::map<std::string, std::string> createOrderParams(const models::Order& order) {
    std::map<std::string, std::string> params;
    
    // Common parameters
    params["symbol"] = normalizeSymbolForExchange(order.getTradingPair().toString());
    params["side"] = formatOrderSide(order.getSide(), "LOWER");
    params["type"] = formatOrderType(order.getType(), "LOWER");
    
    // Quantity
    std::ostringstream qtyStr;
    qtyStr << std::fixed << std::setprecision(8) << order.getQuantity();
    params["quantity"] = qtyStr.str();
    
    // Price (for limit orders)
    if (order.getType() != models::OrderType::MARKET) {
        std::ostringstream priceStr;
        priceStr << std::fixed << std::setprecision(8) << order.getPrice();
        params["price"] = priceStr.str();
    }
    
    // Stop price (for stop orders)
    auto stopPrice = order.getStopPrice();
    if (stopPrice.has_value()) {
        std::ostringstream stopPriceStr;
        stopPriceStr << std::fixed << std::setprecision(8) << stopPrice.value();
        params["stopPrice"] = stopPriceStr.str();
    }
    
    // Time in force
    params["timeInForce"] = formatTimeInForce(order.getTimeInForce(), "UPPER");
    
    // Client order ID
    params["clientOrderId"] = order.getClientOrderId();
    
    return params;
}

// Parse order from JSON (common implementation - derived classes can specialize)
models::ExecutionReport parseOrderResponse(const std::string& json, const models::Order& originalOrder) {
    // This is just a placeholder. In a real implementation, you would parse the JSON
    // and extract order details to create an models::ExecutionReport.
    models::ExecutionReport report(originalOrder.getOrderId());
    report.setClientOrderId(originalOrder.getClientOrderId());
    report.setSymbol(originalOrder.getTradingPair().toString());
    report.setSide(originalOrder.getSide());
    report.setType(originalOrder.getType());
    report.setQuantity(originalOrder.getQuantity());
    
    // The derived class would populate these from the JSON response
    // report.setExchangeOrderId(...);
    // report.setStatus(...);
    // report.setFilledQuantity(...);
    // report.setPrice(...);
    // report.setAverageFillPrice(...);
    
    return report;
}

// Helper for exponential backoff retry logic
template<typename Func>
std::future<models::ExecutionReport> retryWithBackoff(
    Func operation, int maxRetries, std::chrono::milliseconds initialDelay) {
    
    return std::async(std::launch::async, [=]() {
        models::ExecutionReport result;
        bool success = false;
        std::chrono::milliseconds delay = initialDelay;
        std::exception_ptr lastException;
        
        for (int attempt = 0; attempt <= maxRetries && !success; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(delay);
                delay *= 2; // Exponential backoff
            }
            
            try {
                result = operation();
                success = true;
            } catch (const std::exception& e) {
                lastException = std::current_exception();
                // Could log the exception here
            }
        }
        
        if (!success && lastException) {
            std::rethrow_exception(lastException);
        }
        
        return result;
    });
}

// Helper for exponential backoff retry logic with a different return type
template<typename ResultType, typename Func>
std::future<ResultType> retryWithBackoff(
    Func operation, int maxRetries, std::chrono::milliseconds initialDelay) {
    
    return std::async(std::launch::async, [=]() {
        ResultType result;
        bool success = false;
        std::chrono::milliseconds delay = initialDelay;
        std::exception_ptr lastException;
        
        for (int attempt = 0; attempt <= maxRetries && !success; ++attempt) {
            if (attempt > 0) {
                std::this_thread::sleep_for(delay);
                delay *= 2; // Exponential backoff
            }
            
            try {
                result = operation();
                success = true;
            } catch (const std::exception& e) {
                lastException = std::current_exception();
                // Could log the exception here
            }
        }
        
        if (!success && lastException) {
            std::rethrow_exception(lastException);
        }
        
        return result;
    });
}

} // namespace util

} // namespace execution
} // namespace trading