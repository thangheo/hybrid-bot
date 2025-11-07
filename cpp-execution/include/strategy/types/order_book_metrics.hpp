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
    double midPrice = 0.0;
    double bidPrice = 0.0;
    double askPrice = 0.0;
    double spread = 0.0;
    double spreadPercent = 0.0;
    // Flow metrics
    double bidFlow = 0.0;
    double askFlow = 0.0;
    
    // Volume metrics
    double totalBidVolume  = 0.0;
    double totalAskVolume = 0.0;
    double bidAskVolumeRatio = 1.0;
    double totalVolume = 0.0; // totalBidVolume + totalAskVolume
    
    
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
    double realizedVolatility = 0.0;           // Realized volatility based on recent data
    double volatilityPerUnitTime = 0.0; // Volatility normalized by time
    double liquidityPressure = 0.0;            // Pressure from liquidity imbalances
    
    // New volatility metrics
    double realize_volatility_1m = 0.0;        // 1-minute realized volatility
    double rv_weighted = 0.0;                  // Volume-weighted realized volatility
    double rv_adjusted = 0.0;                  // Microstructure noise adjusted RV
    double rv_smoothed = 0.0;                  // EWMA smoothed RV
    double spread_volatility = 0.0;            // Spread volatility
    double trend_ratio = 0.0;                  // Trend vs mean-reversion ratio
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
        metrics["midPrice"] = midPrice;
        metrics["bidPrice"] = bidPrice;
        metrics["askPrice"] = askPrice;
        metrics["spread"] = spread;
        metrics["spreadPercent"] = spreadPercent;

        // Map flow metrics
        metrics["bidFlow"] = bidFlow;
        metrics["askFlow"] = askFlow;
        //

        // Map standard volume metrics to both original and expected names
        metrics["totalBidVolume"] = totalBidVolume;
        metrics["totalAskVolume"] = totalAskVolume;
        metrics["bidVolume"] = totalBidVolume ;  // Additional mapping for compatibility
        metrics["askVolume"] = totalAskVolume;  // Additional mapping for compatibility
        metrics["bidAskVolumeRatio"] = bidAskVolumeRatio;
        metrics["totalVolume"] = totalVolume;
        //bidVolumesByLevel and askVolumesByLevel are vectors, not single values
        
        

        // Imbalance metrics
        metrics["volumeImbalance"] = volumeImbalance;
        metrics["weightedVolumeImbalance"] = weightedVolumeImbalance;
        metrics["depthImbalance"] = depthImbalance;
        metrics["pressureImbalance"] = pressureImbalance;
        
        // Level metrics
        metrics["bidDensity"] = bidDensity;
        metrics["askDensity"] = askDensity;
        metrics["priceLevelImbalance"] = priceLevelImbalance;
        metrics["bidLevelCount"] = static_cast<double>(bidVolumesByLevel.size());  // Derived from vector size
        metrics["askLevelCount"] = static_cast<double>(askVolumesByLevel.size());  // Derived from vector size
        
        // Derived metrics
        metrics["marketDirectionalCoefficient"] = marketDirectionalCoefficient;
        metrics["liquidityProfileScore"] = liquidityProfileScore;
        metrics["volatilityExpectation"] = volatilityExpectation;
        metrics["realizedVolatility"] = realizedVolatility;
        metrics["liquidityPressure"] = liquidityPressure;
        metrics["volatilityPerUnitTime"] = volatilityPerUnitTime;
        
        // New volatility metrics
        metrics["realize_volatility_1m"] = realize_volatility_1m;
        metrics["rv_weighted"] = rv_weighted;
        metrics["rv_adjusted"] = rv_adjusted;
        metrics["rv_smoothed"] = rv_smoothed;
        metrics["spread_volatility"] = spread_volatility;
        metrics["trend_ratio"] = trend_ratio;
        
        // Historical metrics
        metrics["priceVelocity"] = priceVelocity;
        metrics["volumeVelocity"] = volumeVelocity;
        metrics["spreadVelocity"] = spreadVelocity;
        
        // Additional metrics that might be referenced in the code
        metrics["bidFlow"] = 0.0;  // Fill in with actual values if available
        metrics["askFlow"] = 0.0;  // Fill in with actual values if available
        metrics["averageSpreadPercent"] = 0.0;  // Fill in with actual values if available
        metrics["volatility"] = volatilityExpectation;  // Map to existing value
    }
    
    // Method to set a metric value
    void setMetric(const std::string& name, double value) {
        metrics[name] = value;
        
        // Also update the corresponding class member if it exists
        if (name == "midPrice") midPrice = value;
        // bidPrice and askPrice should store here
        else if (name == "bidPrice") bidPrice = value;
        else if (name == "askPrice") askPrice = value;
        else if (name == "spread") spread = value;
        else if (name == "spreadPercent") spreadPercent = value;
        else if (name == "bidVolume" || name == "totalBidVolume") totalBidVolume  = value;
        else if (name == "askVolume" || name == "totalAskVolume") totalAskVolume = value;
        else if (name == "bidAskVolumeRatio") bidAskVolumeRatio = value;
        else if (name == "volumeImbalance") volumeImbalance  = value;
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
        else if (name == "realizedVolatility") realizedVolatility = value;
        else if (name == "volatilityPerUnitTime") volatilityPerUnitTime = value;
        else if (name == "liquidityPressure") liquidityPressure = value;
        else if (name == "bidFlow") bidFlow = value;
        else if (name == "askFlow") askFlow = value;
        else if (name == "averageSpreadPercent") metrics["averageSpreadPercent"] = value;
        else if (name == "volatility") volatilityExpectation = value; // Map to existing value
        else if (name == "realize_volatility_1m") realize_volatility_1m = value;
        else if (name == "rv_weighted") rv_weighted = value;
        else if (name == "rv_adjusted") rv_adjusted = value;
        else if (name == "rv_smoothed") rv_smoothed = value;
        else if (name == "spread_volatility") spread_volatility = value;
        else if (name == "trend_ratio") trend_ratio = value;
    }
    
    // Get a specific metric by name
    double getMetric(const std::string& name, double defaultValue = 0.0) const {
        // Prefer direct member values for known keys so callers get up-to-date values
        if (name == "midPrice") return midPrice;
        else if (name == "bidPrice") return bidPrice;
        else if (name == "askPrice") return askPrice;
        if (name == "spread") return spread;
        if (name == "spreadPercent") return spreadPercent;
        if (name == "totalBidVolume" || name == "bidVolume") return totalBidVolume;
        if (name == "totalAskVolume" || name == "askVolume") return totalAskVolume;
        if (name == "bidAskVolumeRatio") return bidAskVolumeRatio;
        if (name == "volumeImbalance") return volumeImbalance;
        if (name == "weightedVolumeImbalance") return weightedVolumeImbalance;
        if (name == "depthImbalance") return depthImbalance;
        if (name == "pressureImbalance") return pressureImbalance;
        if (name == "bidDensity") return bidDensity;
        if (name == "askDensity") return askDensity;
        if (name == "priceLevelImbalance") return priceLevelImbalance;
        if (name == "marketDirectionalCoefficient") return marketDirectionalCoefficient;
        if (name == "liquidityProfileScore") return liquidityProfileScore;
        if (name == "volatilityExpectation") return volatilityExpectation;
        if (name == "realizedVolatility") return realizedVolatility;
        if (name == "volatilityPerUnitTime") return volatilityPerUnitTime;
        if (name == "liquidityPressure") return liquidityPressure;
        if (name == "priceVelocity") return priceVelocity;
        if (name == "volumeVelocity") return volumeVelocity;
        if (name == "spreadVelocity") return spreadVelocity;
        if (name == "bidLevelCount") return static_cast<double>(bidVolumesByLevel.size());
        if (name == "askLevelCount") return static_cast<double>(askVolumesByLevel.size());
        if (name == "bidFlow") return bidFlow;
        if (name == "askFlow") return askFlow;
        if (name == "averageSpreadPercent") { return metrics.at("averageSpreadPercent"); }
        if (name == "volatility") return volatilityExpectation; // Map to existing value
        // Fallback to metrics map for other keys

        auto it = metrics.find(name);
        if (it != metrics.end()) return it->second;
        return defaultValue;
    }
    
    // Convert to map for easy access (now just returns a copy of the metrics map)
    std::map<std::string, double> toMap() const {
        return metrics;
    }

    // Const reference accessor to the internal metrics map to avoid copies
    const std::map<std::string, double>& metricsMap() const {
        return metrics;
    }
};


} // namespace strategy
} // namespace trading