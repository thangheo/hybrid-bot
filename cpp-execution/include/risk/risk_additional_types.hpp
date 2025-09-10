/**
 * Additional types for risk management system
 *
 * File: risk_additional_types.hpp
 * Created: 2025-05-23
 */

#pragma once

#include "risk_types.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <optional>

namespace trading {
namespace risk {

/**
 * Market data structure for risk management
 */
// struct MarketData {
//     std::string symbol;
//     ExchangeId exchangeId;
//     double price = 0.0;
//     double volume = 0.0;
//     double volatility = 0.0;
//     double bidPrice = 0.0;
//     double askPrice = 0.0;
//     double spread = 0.0;
//     std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::now();
    
//     MarketData() = default;
    
//     MarketData(const core::Symbol& symbol, const core::ExchangeId& exchangeId, double price)
//         : symbol(symbol), exchangeId(exchangeId), price(price) {}
    
//     MarketData(const core::Symbol& symbol, const core::ExchangeId& exchangeId, 
//                double price, double volume, double volatility)
//         : symbol(symbol), exchangeId(exchangeId), price(price), volume(volume), volatility(volatility) {}
    
//     // Calculate spread percentage
//     double getSpreadPercent() const {
//         if (price > 0 && spread > 0) {
//             return (spread / price) * 100.0;
//         }
//         return 0.0;
//     }
    
//     // Check if market data is recent (within last N seconds)
//     bool isRecent(int maxAgeSeconds = 60) const {
//         auto now = std::chrono::system_clock::now();
//         auto age = std::chrono::duration_cast<std::chrono::seconds>(now - timestamp);
//         return age.count() <= maxAgeSeconds;
//     }
// };

/**
 * Result structure for rebalancing operations
 */
struct RebalancingResult {
    bool success = false;
    std::vector<std::string> ordersCreated;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    double totalRebalancedValue = 0.0;
    double estimatedCost = 0.0;
    int assetsRebalanced = 0;
    std::chrono::system_clock::time_point executionTime = std::chrono::system_clock::now();
    std::chrono::milliseconds executionDuration{0};
    
    RebalancingResult() = default;
    
    explicit RebalancingResult(bool success) : success(success) {}
    
    RebalancingResult(bool success, const std::vector<std::string>& orders)
        : success(success), ordersCreated(orders) {}
    
    // Add an error message
    void addError(const std::string& error) {
        errors.push_back(error);
        success = false; // Any error makes the operation unsuccessful
    }
    
    // Add a warning message
    void addWarning(const std::string& warning) {
        warnings.push_back(warning);
    }
    
    // Add a created order
    void addOrder(const std::string& orderId) {
        ordersCreated.push_back(orderId);
    }
    
    // Get summary string
    std::string getSummary() const {
        std::string summary = success ? "SUCCESS" : "FAILED";
        summary += " - " + std::to_string(ordersCreated.size()) + " orders created";
        if (!errors.empty()) {
            summary += ", " + std::to_string(errors.size()) + " errors";
        }
        if (!warnings.empty()) {
            summary += ", " + std::to_string(warnings.size()) + " warnings";
        }
        return summary;
    }
};

/**
 * Balance information for an asset on an exchange
 */
struct Balance {
    std::string asset;
    core::ExchangeId exchangeId;
    double available = 0.0;
    double locked = 0.0;
    double total = 0.0;
    std::chrono::system_clock::time_point lastUpdate = std::chrono::system_clock::now();
    
    Balance() = default;
    
    Balance(const std::string& asset, const core::ExchangeId& exchangeId, 
            double available, double locked = 0.0)
        : asset(asset), exchangeId(exchangeId), available(available), locked(locked) {
        total = available + locked;
    }
    
    // Update balance amounts
    void updateBalance(double newAvailable, double newLocked = 0.0) {
        available = newAvailable;
        locked = newLocked;
        total = available + locked;
        lastUpdate = std::chrono::system_clock::now();
    }
    
    // Check if balance is sufficient for a trade
    bool isSufficientFor(double requiredAmount) const {
        return available >= requiredAmount;
    }
};

/**
 * Portfolio summary information
 */
struct PortfolioSummary {
    double totalValue = 0.0;
    double totalPnl = 0.0;
    double totalUnrealizedPnl = 0.0;
    double totalRealizedPnl = 0.0;
    int totalPositions = 0;
    int activePositions = 0;
    std::vector<std::string> topAssets; // Top assets by allocation
    std::chrono::system_clock::time_point lastUpdate = std::chrono::system_clock::now();
    
    PortfolioSummary() = default;
    
    // Calculate total return percentage
    double getTotalReturnPercent() const {
        double initialValue = totalValue - totalPnl;
        if (initialValue > 0) {
            return (totalPnl / initialValue) * 100.0;
        }
        return 0.0;
    }
    
    // Update summary with new data
    void updateSummary(double value, double pnl, int positions) {
        totalValue = value;
        totalPnl = pnl;
        totalPositions = positions;
        lastUpdate = std::chrono::system_clock::now();
    }
};

} // namespace risk
} // namespace trading