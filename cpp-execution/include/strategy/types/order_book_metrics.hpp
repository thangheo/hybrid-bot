/**
 * @file strategy/types/order_book_metrics.hpp
 * @brief Order book metrics structure
 * 
 * This file defines the OrderBookMetrics structure used throughout
 * the strategy component.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include "core/types.hpp"
#include "core/market_types.hpp"

namespace trading {
namespace strategy {

/**
 * Order book metrics calculated by the analyzer
 */
struct OrderBookMetrics {
    core::Timestamp timestamp;
    uint64_t sequenceNumber;
    core::ExchangeId exchangeId;
    core::Symbol symbol;  // Changed from tradingPair for consistency
    
    
    // Map of metrics for flexible access
    std::map<std::string, double> metrics;
    
    // Price metrics
    double mid_price = 0.0;
    double spread = 0.0;
    double spread_percent = 0.0;
    
    // Volume metrics
    double total_bid_volume = 0.0;
    double total_ask_volume = 0.0;
    double bid_ask_volume_ratio = 1.0;
    
    // Depth metrics
    std::vector<double> bid_volumes_by_level;
    std::vector<double> ask_volumes_by_level;
    std::vector<double> cumulative_bid_volumes;
    std::vector<double> cumulative_ask_volumes;
    
    // Imbalance metrics
    double volume_imbalance = 0.0;            // Overall volume imbalance
    double weighted_volume_imbalance = 0.0;   // Price-weighted volume imbalance
    double depth_imbalance = 0.0;             // Imbalance in market depth
    double pressure_imbalance = 0.0;          // Combined pressure imbalance
    
    // Level metrics
    double bid_density = 0.0;                 // Density of orders on bid side
    double ask_density = 0.0;                 // Density of orders on ask side
    double price_level_imbalance = 0.0;       // Imbalance in number of price levels
    
    // Derived metrics
    double market_directional_coefficient = 0.0;  // Market direction indicator
    double liquidity_profile_score = 0.0;         // Score of overall liquidity
    double volatility_expectation = 0.0;          // Expected near-term volatility
    double realized_volatility = 0.0;           // Realized volatility based on recent data
    double liquidity_pressure = 0.0;            // Pressure from liquidity imbalances
    // Historical metrics
    double price_velocity = 0.0;              // Rate of price change
    double volume_velocity = 0.0;             // Rate of volume change
    double spread_velocity = 0.0;             // Rate of spread change
    
    // Time-series metrics
    std::map<std::string, std::vector<double>> time_series_metrics;
    
    // Constructor to initialize metrics map
    OrderBookMetrics() {
        updateMetricsMap();
    }
    
    // Update metrics map with current values
    void updateMetricsMap() {
        metrics["mid_price"] = mid_price;
        metrics["spread"] = spread;
        metrics["spread_percent"] = spread_percent;
        
        // Map standard volume metrics to both original and expected names
        metrics["total_bid_volume"] = total_bid_volume;
        metrics["total_ask_volume"] = total_ask_volume;
        metrics["bid_volume"] = total_bid_volume;  // Additional mapping for compatibility
        metrics["ask_volume"] = total_ask_volume;  // Additional mapping for compatibility
        metrics["bid_ask_volume_ratio"] = bid_ask_volume_ratio;
        
        // Imbalance metrics
        metrics["volume_imbalance"] = volume_imbalance;
        metrics["weighted_volume_imbalance"] = weighted_volume_imbalance;
        metrics["depth_imbalance"] = depth_imbalance;
        metrics["pressure_imbalance"] = pressure_imbalance;
        
        // Level metrics
        metrics["bid_density"] = bid_density;
        metrics["ask_density"] = ask_density;
        metrics["price_level_imbalance"] = price_level_imbalance;
        metrics["bid_level_count"] = bid_volumes_by_level.size();  // Derived from vector size
        metrics["ask_level_count"] = ask_volumes_by_level.size();  // Derived from vector size
        
        // Derived metrics
        metrics["market_directional_coefficient"] = market_directional_coefficient;
        metrics["liquidity_profile_score"] = liquidity_profile_score;
        metrics["volatility_expectation"] = volatility_expectation;
        
        // Historical metrics
        metrics["price_velocity"] = price_velocity;
        metrics["volume_velocity"] = volume_velocity;
        metrics["spread_velocity"] = spread_velocity;
        
        // Additional metrics that might be referenced in the code
        metrics["bid_flow"] = 0.0;  // Fill in with actual values if available
        metrics["ask_flow"] = 0.0;  // Fill in with actual values if available
        metrics["average_spread_percent"] = 0.0;  // Fill in with actual values if available
        metrics["volatility"] = volatility_expectation;  // Map to existing value
    }
    
    // Method to set a metric value
    void setMetric(const std::string& name, double value) {
        metrics[name] = value;
        
        // Also update the corresponding class member if it exists
        if (name == "mid_price") mid_price = value;
        else if (name == "spread") spread = value;
        else if (name == "spread_percent") spread_percent = value;
        else if (name == "bid_volume" || name == "total_bid_volume") total_bid_volume = value;
        else if (name == "ask_volume" || name == "total_ask_volume") total_ask_volume = value;
        else if (name == "bid_ask_volume_ratio") bid_ask_volume_ratio = value;
        else if (name == "volume_imbalance") volume_imbalance = value;
        else if (name == "weighted_volume_imbalance") weighted_volume_imbalance = value;
        else if (name == "depth_imbalance") depth_imbalance = value;
        else if (name == "pressure_imbalance") pressure_imbalance = value;
        else if (name == "bid_density") bid_density = value;
        else if (name == "ask_density") ask_density = value;
        else if (name == "price_level_imbalance") price_level_imbalance = value;
        else if (name == "market_directional_coefficient") market_directional_coefficient = value;
        else if (name == "liquidity_profile_score") liquidity_profile_score = value;
        else if (name == "volatility_expectation") volatility_expectation = value;
        else if (name == "price_velocity") price_velocity = value;
        else if (name == "volume_velocity") volume_velocity = value;
        else if (name == "spread_velocity") spread_velocity = value;
    }
    
    // Get a specific metric by name
    double getMetric(const std::string& name, double defaultValue = 0.0) const {
        auto it = metrics.find(name);
        if (it != metrics.end()) {
            return it->second;
        }
        return defaultValue;
    }
    
    // Convert to map for easy access (now just returns a copy of the metrics map)
    std::map<std::string, double> toMap() const {
        return metrics;
    }
};

} // namespace strategy
} // namespace trading