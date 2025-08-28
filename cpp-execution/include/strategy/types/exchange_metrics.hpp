// exchange_metrics.hpp
#pragma once

#include <map>
#include <string>
#include <cstddef>

namespace trading {
namespace strategy {

/**
 * @struct ExchangeFingerprint
 * @brief A compact representation of exchange characteristics for quick comparison
 * 
 * This structure provides discretized metrics that allow for rapid comparison
 * of exchanges without detailed analysis.
 */
struct ExchangeFingerprint {
    int updateFrequencyBucket;   // Discrete bucket for update frequency
    int spreadBucket;            // Discrete bucket for typical spread
    int depthBucket;             // Discrete bucket for book depth
    int liquidityBucket;         // Discrete bucket for liquidity level
    
    // Overload equality operator for comparison
    bool operator==(const ExchangeFingerprint& other) const {
        return updateFrequencyBucket == other.updateFrequencyBucket &&
               spreadBucket == other.spreadBucket &&
               depthBucket == other.depthBucket &&
               liquidityBucket == other.liquidityBucket;
    }
    
    // Hash function for using in unordered containers
    struct Hash {
        std::size_t operator()(const ExchangeFingerprint& fp) const {
            std::size_t seed = 0;
            seed ^= fp.updateFrequencyBucket + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= fp.spreadBucket + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= fp.depthBucket + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            seed ^= fp.liquidityBucket + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
};

/**
 * @struct ExchangeMetrics
 * @brief Stores key metrics for an exchange that are used for parameter calibration
 * 
 * This structure captures essential characteristics of an exchange's behavior
 * that affect trading strategy performance, such as order book update frequency,
 * typical spread patterns, market depth, and liquidity.
 */
struct ExchangeMetrics {
    // Order book update characteristics
    double updateFrequency;       // Average updates per second
    double updateConsistency;     // Measure of consistency in update timing (0-1)
    
    // Market microstructure
    double averageSpread;         // Average bid-ask spread as decimal (e.g., 0.0005 = 5bps)
    double spreadVolatility;      // Standard deviation of spread
    double bookDepth;             // Effective depth of the order book
    double priceGranularity;      // Minimum price increment
    
    // Liquidity metrics
    double averageLiquidity;      // Average volume available at best 5 levels
    double liquidityImbalance;    // Typical bid/ask volume imbalance (>1 means more bids)
    double liquidityRefreshRate;  // How quickly liquidity replenishes after trades
    
    // Market impact and behavior
    double marketImpact;          // Typical price impact per unit volume (normalized 0-1)
    double orderExecutionLatency; // Average latency for order execution in milliseconds
    double cancelRatio;           // Ratio of cancelled to executed orders
    
    // Exchange-specific anomalies
    double quoteInstability;      // Frequency of quote flickering (0-1)
    double anomalyFrequency;      // Frequency of unusual events like micro-gaps (0-1)
    
    // Time-based patterns
    bool hasSignificantTimePatterns; // Whether exchange exhibits time-of-day effects
    std::map<int, double> hourlyActivityFactor; // Activity factor by hour (0-23)
    
    // Constructor with reasonable defaults
    ExchangeMetrics() :
        updateFrequency(10.0),
        updateConsistency(0.8),
        averageSpread(0.0005),
        spreadVolatility(0.0002),
        bookDepth(20.0),
        priceGranularity(0.00001),
        averageLiquidity(100.0),
        liquidityImbalance(1.0),
        liquidityRefreshRate(0.7),
        marketImpact(0.5),
        orderExecutionLatency(50.0),
        cancelRatio(0.75),
        quoteInstability(0.2),
        anomalyFrequency(0.1),
        hasSignificantTimePatterns(false) {
        // Initialize hourly activity factors with neutral values
        for (int hour = 0; hour < 24; ++hour) {
            hourlyActivityFactor[hour] = 1.0;
        }
    }
    
    // Reset to default values
    void reset() {
        *this = ExchangeMetrics();
    }
    
    // Calculate a general "quality" score for the exchange (higher is better)
    double calculateQualityScore() const {
        return (updateConsistency * 0.2 +
                (1.0 - averageSpread) * 0.2 +
                std::min(1.0, bookDepth / 30.0) * 0.15 +
                std::min(1.0, averageLiquidity / 200.0) * 0.15 +
                (1.0 - marketImpact) * 0.1 +
                (1.0 - quoteInstability) * 0.1 +
                (1.0 - anomalyFrequency) * 0.1);
    }
};

} // namespace strategy
} // namespace trading