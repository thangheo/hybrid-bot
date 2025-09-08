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
    double spreadPercent = 0.0;
    
    // Volume metrics
    double totalBidVolume  = 0.0;
    double totalAskVolume = 0.0;
    double bidAskVolumeRatio = 1.0;
    
    // Depth metrics
    std::vector<double> bidVolumesByLevel;
    std::vector<double> askVolumesByLevel;
    std::vector<double> cumulativeBidVolumes;
    std::vector<double> cumulativeAskVolumes;
    
    // Imbalance metrics
    double volumeImbalance  = 0.0;            // Overall volume imbalance
    double weightedVolumeImbalance = 0.0;   // Price-weighted volume imbalance
    double depthImbalance = 0.0;             // Imbalance in market depth
    double pressureImbalance = 0.0;          // Combined pressure imbalance
    
    // Level metrics
    double bidDensity = 0.0;                 // Density of orders on bid side
    double askDensity = 0.0;                 // Density of orders on ask side
    double priceLevelImbalance = 0.0;       // Imbalance in number of price levels
    
    // Derived metrics
    double marketDirectionalCoefficient = 0.0;  // Market direction indicator
    double liquidityProfileScore = 0.0;         // Score of overall liquidity
    double volatilityExpectation = 0.0;          // Expected near-term volatility
    double realized_volatility = 0.0;           // Realized volatility based on recent data
    double volatility_per_unit_time = 0.0; // Volatility normalized by time
    double liquidity_pressure = 0.0;            // Pressure from liquidity imbalances
    // Historical metrics
    double priceVelocity = 0.0;              // Rate of price change
    double volumeVelocity = 0.0;             // Rate of volume change
    double spreadVelocity = 0.0;             // Rate of spread change
    
    // Time-series metrics
    std::map<std::string, std::vector<double>> timeSeriesMetrics;
    
    // Constructor to initialize metrics map
    OrderBookMetrics() {
        updateMetricsMap();
    }
    
    // Update metrics map with current values
    void updateMetricsMap() {
        metrics["mid_price"] = mid_price;
        metrics["spread"] = spread;
        metrics["spreadPercent"] = spreadPercent;
        
        // Map standard volume metrics to both original and expected names
        metrics["totalBidVolume "] = totalBidVolume ;
        metrics["totalAskVolume"] = totalAskVolume;
        metrics["bid_volume"] = totalBidVolume ;  // Additional mapping for compatibility
        metrics["ask_volume"] = totalAskVolume;  // Additional mapping for compatibility
        metrics["bidAskVolumeRatio"] = bidAskVolumeRatio;
        
        // Imbalance metrics
        metrics["volumeImbalance "] = volumeImbalance ;
        metrics["weightedVolumeImbalance"] = weightedVolumeImbalance;
        metrics["depthImbalance"] = depthImbalance;
        metrics["pressureImbalance"] = pressureImbalance;
        
        // Level metrics
        metrics["bidDensity"] = bidDensity;
        metrics["askDensity"] = askDensity;
        metrics["priceLevelImbalance"] = priceLevelImbalance;
        metrics["bid_level_count"] = bidVolumesByLevel.size();  // Derived from vector size
        metrics["ask_level_count"] = askVolumesByLevel.size();  // Derived from vector size
        
        // Derived metrics
        metrics["marketDirectionalCoefficient"] = marketDirectionalCoefficient;
        metrics["liquidityProfileScore"] = liquidityProfileScore;
        metrics["volatilityExpectation"] = volatilityExpectation;
        metrics["realized_volatility"] = realized_volatility;
        metrics["liquidity_pressure"] = liquidity_pressure;
        metrics["volatility_per_unit_time"] = volatility_per_unit_time;
        
        // Historical metrics
        metrics["priceVelocity"] = priceVelocity;
        metrics["volumeVelocity"] = volumeVelocity;
        metrics["spreadVelocity"] = spreadVelocity;
        
        // Additional metrics that might be referenced in the code
        metrics["bid_flow"] = 0.0;  // Fill in with actual values if available
        metrics["ask_flow"] = 0.0;  // Fill in with actual values if available
        metrics["average_spread_percent"] = 0.0;  // Fill in with actual values if available
        metrics["volatility"] = volatilityExpectation;  // Map to existing value
    }
    
    // Method to set a metric value
    void setMetric(const std::string& name, double value) {
        metrics[name] = value;
        
        // Also update the corresponding class member if it exists
        if (name == "mid_price") mid_price = value;
        else if (name == "spread") spread = value;
        else if (name == "spreadPercent") spreadPercent = value;
        else if (name == "bid_volume" || name == "totalBidVolume ") totalBidVolume  = value;
        else if (name == "ask_volume" || name == "totalAskVolume") totalAskVolume = value;
        else if (name == "bidAskVolumeRatio") bidAskVolumeRatio = value;
        else if (name == "volumeImbalance ") volumeImbalance  = value;
        else if (name == "weightedVolumeImbalance") weightedVolumeImbalance = value;
        else if (name == "depthImbalance") depthImbalance = value;
        else if (name == "pressureImbalance") pressureImbalance = value;
        else if (name == "bidDensity") bidDensity = value;
        else if (name == "askDensity") askDensity = value;
        else if (name == "priceLevelImbalance") priceLevelImbalance = value;
        else if (name == "marketDirectionalCoefficient") marketDirectionalCoefficient = value;
        else if (name == "liquidityProfileScore") liquidityProfileScore = value;
        else if (name == "volatilityExpectation") volatilityExpectation = value;
        else if (name == "priceVelocity") priceVelocity = value;
        else if (name == "volumeVelocity") volumeVelocity = value;
        else if (name == "spreadVelocity") spreadVelocity = value;
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