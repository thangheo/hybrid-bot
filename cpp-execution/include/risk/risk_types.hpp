/**
 * Common type definitions used across risk management modules
 *
 * File: risk_types.hpp (Fixed version)
 * Created: 2025-04-29
 */

#pragma once

#include "core/market_types.hpp"
#include "core/direction.hpp"

#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <cstdint>
#include <execution/models/order.hpp>

using OrderType = trading::execution::models::OrderType;
using OrderSide = trading::execution::models::OrderSide;

namespace trading {
namespace risk {
using namespace trading::execution::models;
// Forward declarations
class Position;
class OrderRequest;
class ValidationResult;
class ThresholdSet;
class RiskParameters;
class PerformanceMetrics;

/**
 * Type for timestamp representation
 */
using Timestamp = core::Timestamp;
using MarketDataUpdate = core::MarketDataUpdate;    
using MarketData = core::MarketData;
using OrderSide = core::Direction;

/**
 * Struct for trading pair (symbol)
 */
struct TradingPair {
    std::string base;
    std::string quote;
    std::string symbol;
    
    TradingPair() = default;
    
    TradingPair(const std::string& sym) : symbol(sym) {
        // Try to split symbol into base/quote
        size_t delimiterPos = sym.find('/');
        if (delimiterPos != std::string::npos) {
            base = sym.substr(0, delimiterPos);
            quote = sym.substr(delimiterPos + 1);
        }
    }
    
    TradingPair(const std::string& b, const std::string& q, const std::string& sym = "") 
        : base(b), quote(q), symbol(sym.empty() ? b + "/" + q : sym) {}
    
    std::string getSymbol() const {
        return symbol.empty() ? (base + "/" + quote) : symbol;
    }
    
    bool operator==(const TradingPair& other) const {
        return getSymbol() == other.getSymbol();
    }
    
    bool operator<(const TradingPair& other) const {
        return getSymbol() < other.getSymbol();
    }
};

/**
 * Enum for different types of risk violations
 */
enum class ViolationType {
    POSITION_LIMIT,
    EXPOSURE_LIMIT,
    ORDER_SIZE_LIMIT,
    PRICE_DEVIATION,
    DRAWDOWN_LIMIT,
    RATE_LIMIT,
    COLLATERAL_REQUIREMENT,
    EXCHANGE_CONSTRAINT,
    OTHER
};

/**
 * Enum for risk manager mode/state
 */
enum class RiskManagerMode {
    NORMAL,
    HEIGHTENED_RISK,
    EMERGENCY,
    MAINTENANCE
};

/**
 * Enum for circuit breaker state
 */
enum class CircuitBreakerState {
    OFF,
    WARNING,
    TRIGGERED,
    COOLING
};

/**
 * Helper function to convert OrderType to string
 */
inline std::string toString(OrderType type) {
    switch (type) {
        case OrderType::MARKET: return "MARKET";
        case OrderType::LIMIT: return "LIMIT";
        case OrderType::STOP: return "STOP";
        case OrderType::STOP_LIMIT: return "STOP_LIMIT";
        case OrderType::TAKE_PROFIT: return "TAKE_PROFIT";
        case OrderType::TAKE_PROFIT_LIMIT: return "TAKE_PROFIT_LIMIT";
        // case OrderType::TRAILING_STOP: return "TRAILING_STOP";
        default: return "UNKNOWN";
    }
}

/**
 * Helper function to convert ViolationType to string
 */
inline std::string toString(ViolationType type) {
    switch (type) {
        case ViolationType::POSITION_LIMIT: return "POSITION_LIMIT";
        case ViolationType::EXPOSURE_LIMIT: return "EXPOSURE_LIMIT";
        case ViolationType::ORDER_SIZE_LIMIT: return "ORDER_SIZE_LIMIT";
        case ViolationType::PRICE_DEVIATION: return "PRICE_DEVIATION";
        case ViolationType::DRAWDOWN_LIMIT: return "DRAWDOWN_LIMIT";
        case ViolationType::RATE_LIMIT: return "RATE_LIMIT";
        case ViolationType::COLLATERAL_REQUIREMENT: return "COLLATERAL_REQUIREMENT";
        case ViolationType::EXCHANGE_CONSTRAINT: return "EXCHANGE_CONSTRAINT";
        case ViolationType::OTHER: return "OTHER";
        default: return "UNKNOWN";
    }
}
inline OrderType orderTypeFromString(const std::string& str) {
    if (str == "MARKET") return OrderType::MARKET;
    if (str == "LIMIT") return OrderType::LIMIT;
    if (str == "STOP") return OrderType::STOP;
    if (str == "STOP_LIMIT") return OrderType::STOP_LIMIT;
    if (str == "TAKE_PROFIT") return OrderType::TAKE_PROFIT;
    if (str == "TAKE_PROFIT_LIMIT") return OrderType::TAKE_PROFIT_LIMIT;
    // if (str == "TRAILING_STOP") return OrderType::TRAILING_STOP;
    return OrderType::MARKET; // Default fallback
}

/**
 * Struct to represent a risk violation (RESTORED ORIGINAL FORMAT)
 */
struct RiskViolation {
    ViolationType type;           // Keep original enum field name
    std::string description;
    double limitValue;
    double actualValue;
    
    RiskViolation() : type(ViolationType::OTHER), limitValue(0.0), actualValue(0.0) {}
    
    RiskViolation(ViolationType vType, const std::string& desc, double limit, double actual)
        : type(vType), description(desc), limitValue(limit), actualValue(actual) {}
};

/**
 * Order request structure (FIXED TYPES)
 */
struct OrderRequest {
    std::string orderId;
    std::string clientOrderId;
    core::ExchangeId exchangeId;
    std::string tradingPair;        // Keep as string for now
    OrderSide side;
    OrderType type;                 // Changed from string to OrderType enum
    double quantity;
    double price;
    std::string timeInForce;
    std::string strategyId;
    std::string signalId;
    Timestamp timestamp;
    std::map<std::string, std::string> params;
    
    OrderRequest() 
        : side(OrderSide::BUY), type(OrderType::MARKET), quantity(0.0), price(0.0),
          timestamp(std::chrono::system_clock::now()) {}
};

/**
 * Struct for strategy identification
 */
struct StrategyId {
    std::string id;
    std::string version;
    
    bool operator==(const StrategyId& other) const {
        return id == other.id && version == other.version;
    }
    
    bool operator<(const StrategyId& other) const {
        return (id == other.id) ? (version < other.version) : (id < other.id);
    }
};

/**
 * Custom exceptions for Risk Management System
 */
class RiskException : public std::runtime_error {
public:
    explicit RiskException(const std::string& message) : std::runtime_error(message) {}
};

class ValidationException : public RiskException {
public:
    explicit ValidationException(const std::string& message) : RiskException(message) {}
};

class ConfigurationException : public RiskException {
public:
    explicit ConfigurationException(const std::string& message) : RiskException(message) {}
};

class DataException : public RiskException {
public:
    explicit DataException(const std::string& message) : RiskException(message) {}
};

} // namespace risk
} // namespace trading