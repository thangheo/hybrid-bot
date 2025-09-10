/**
 * Common type definitions used across risk management modules
 *
 * File: risk_types.hpp (Fixed version)
 * Created: 2025-04-29
 */

#pragma once

#include "core/market_types.hpp"
#include "core/types.hpp"
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
#include <core/market_types.hpp>
using namespace trading::core;
using namespace trading;
using namespace trading::execution::models;
using TradingPair = trading::core::Symbol;

namespace trading {
namespace risk {

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
    SYSTEM_ERROR,
    TIMEOUT,
    OTHER
};

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

using OrderType = trading::execution::models::OrderType;
using ExchangeId = trading::core::ExchangeId;
// Forward declarations
// class ValidationResult;
class ThresholdSet;
// class RiskParameters;
class PerformanceMetrics;

using StrategyId = trading::core::StrategyId;

/**
 * Type for timestamp representation
 */
using Timestamp = core::Timestamp;
using MarketDataUpdate = core::MarketDataUpdate;    
using MarketData = core::MarketData;
using OrderSide = core::Direction;


// Forward declarations for missing types
struct ValidationResult {
    std::string signalId;
    bool approved = false;
    double adjustedSize = 0.0;
    uint64_t timestamp = 0;
    std::vector<RiskViolation> violations;
    
    ValidationResult() = default;
};

struct RiskParameters {
    core::ExchangeId exchangeId;
    TradingPair tradingPair;
    core::StrategyId strategyId;
    double maxPositionSize = 0.0;
    double maxExposure = 0.0;
    double maxOrderSize = 0.0;
    double minOrderSize = 0.0;
    double maxDrawdown = 0.0; // as a fraction, e.g. 0.2 for 20%
    uint64_t lastUpdated = 0; // epoch time in milliseconds
    
    RiskParameters() = default;
};


/**
 * Position class representing a trading position
 */
class Position {
public:
    Position(const ExchangeId& exchangeId, const TradingPair& tradingPair, 
            const StrategyId& strategyId, double quantity, double averageEntryPrice);
    
    // Getters
    const ExchangeId& getExchangeId() const;
    const TradingPair& getTradingPair() const;
    const StrategyId& getStrategyId() const;
    double getQuantity() const;
    double getAverageEntryPrice() const;
    double getCurrentPrice() const;
    double getUnrealizedPnl() const;
    double getRealizedPnl() const;
    Timestamp getLastUpdated() const;
    
    // Setters
    void setCurrentPrice(double price);
    void setUnrealizedPnl(double pnl);
    void setRealizedPnl(double pnl);
    void setLastUpdated(Timestamp timestamp);
    
    // Operations
    void updatePosition(double quantity, double price);
    double getNotionalValue() const;
    
private:
    ExchangeId m_exchangeId;
    TradingPair m_tradingPair;
    StrategyId m_strategyId;
    double m_quantity;
    double m_averageEntryPrice;
    double m_currentPrice;
    double m_unrealizedPnl;
    double m_realizedPnl;
    Timestamp m_lastUpdated;
    
    // Helper method
    void updateUnrealizedPnl();
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
 * Order request structure (FIXED TYPES)
 */
struct OrderRequest {
    std::string orderId;
    std::string clientOrderId;
    core::ExchangeId exchangeId;
    TradingPair tradingPair;        // Keep as string for now
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