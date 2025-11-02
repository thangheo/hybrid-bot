// core/trading_signal.hpp
/**
 * @file trading_signal_base.hpp
 * @brief Base trading signal structure
 * 
 * This file defines the common base structure for trading signals
 * used across all system components.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include "core/time.hpp"
 #include "core/direction.hpp"
 #include "core/market_types.hpp"
 #include "core/types.hpp"
 #include "strategy/types/imbalance_type.hpp"
 #include <string>
#include <vector>
#include <unordered_map>
 #include <map>
 using namespace trading::common;
 namespace trading {
 namespace core {
 
using ImbalanceType = trading::strategy::ImbalanceType;

 /**
  * Represents a price target for a trading signal
  */
 struct PriceTarget {
    double percentage;            // Percentage target
    double price;                  // Target price level
    double probability;            // Probability of reaching this target
    core::Duration expected_time;  // Expected time to reach target
    double size;                   // Size associated with this target
    PriceTarget() : price(0.0), probability(0.0), expected_time(core::Duration::zero()) {}
     
     PriceTarget(double p, double prob, const core::Duration& time) 
         : price(p), probability(prob), expected_time(time) {}
 };
 
 /**
  * Base trading signal structure with common properties
  * used across all system components
  */
 /**
 * Unified TradingSignal struct for both strategy and execution components
 */
struct TradingSignal {
    // Core identification
    core::SignalId id;                                // Unique identifier (replaces signal_id)
    uint64_t sourceEventId;                           // Source event ID
    uint64_t correlationId;                           // Correlation ID
    
    // Signal properties
    core::Direction direction;                        // Buy or sell direction
    double quality;                                   // Signal quality (0.0-1.0)
    double strength;                                  // Signal strength (0.0-1.0)
    double confidence;                                // Confidence level (0.0-1.0)
    
    // Timing and lifecycle
    core::Timestamp generationTime;                   // When the signal was generated
    core::Duration ttl;                               // Time to live (TTL) for the signal
    core::Timestamp expirationTime;                  // Expiration time for the signal
    
    // Trading details
    core::Symbol symbol;                              // Trading pair symbol
    core::ExchangeId exchangeId;                      // Exchange identifier
    double targetSize;                                // Suggested position size (from execution)
    double quantity;                                  // Quantity to trade (from execution)
    std::vector<PriceTarget> targets;                 // Price targets
    double price;                                     // Price at which to execute the order
    // Strategy information
    StrategyId strategyId;                            // Strategy identifier
    ImbalanceType sourceType;                         // Type of imbalance that generated this signal
    
    // Advanced properties
    double decayRate;                                 // Decay rate for the signal
    double expectedPriceImpact;                       // Expected price impact of the signal
    std::unordered_map<std::string, double> signalMetrics;     // Metrics associated with this signal
    
    /**
     * Default constructor
     */
    TradingSignal()
        : id(""), 
          sourceEventId(0),
          correlationId(0),
          direction(core::Direction::NEUTRAL), 
          quality(0.0),
          strength(0.0), 
          confidence(0.0),
          generationTime(core::Timestamp()), 
          ttl(std::chrono::milliseconds(5000)), // Default 5 seconds
          expirationTime(core::Timestamp()),
          exchangeId(core::ExchangeId::unknown()),
          targetSize(0.0),
          strategyId(StrategyId()),
          sourceType(ImbalanceType::VOLUME_IMBALANCE),
          decayRate(0.0),
          expectedPriceImpact(0.0) {}
    
    /**
     * Constructor with required fields
     */
    TradingSignal(
        const core::SignalId& sid, 
        core::Direction d, 
        double s, 
        const core::Timestamp& gt
    )
        : id(sid), 
          sourceEventId(0),
          correlationId(0),
          direction(d), 
          quality(0.5),
          strength(s), 
          confidence(0.5),
          generationTime(gt), 
          ttl(std::chrono::milliseconds(5000)), 
          expirationTime(core::Timestamp()),
          exchangeId(core::ExchangeId::unknown()),
          targetSize(0.0),
          strategyId(StrategyId()),
          sourceType(ImbalanceType::VOLUME_IMBALANCE),
          decayRate(0.0),
          expectedPriceImpact(0.0) {}
    
    // Use defaulted copy/move semantics. We explicitly mark moves as noexcept
    // so containers can rely on noexcept move operations (avoids fallback to copies).
    TradingSignal(const TradingSignal& other) = default;
    TradingSignal& operator=(const TradingSignal& other) = default;
    TradingSignal(TradingSignal&& other) noexcept = default;
    TradingSignal& operator=(TradingSignal&& other) noexcept = default;
    // getPrice
    double getPrice() const { return price; }
    // setPrice
    void setPrice(double p) { price = p; }
    /**
     * Reserve internal containers to avoid repeated reallocations during bulk operations.
     * Call this before heavy push_back/emplace operations.
     */
    void reserveContainers(size_t expectedTargets = 0, size_t expectedMetrics = 0) {
        if (expectedTargets > 0) targets.reserve(expectedTargets);
        if (expectedMetrics > 0) signalMetrics.reserve(expectedMetrics);
    }
    
    // Getters (maintaining compatibility with execution interface)
    const std::string& getSignalId() const { return id; }
    core::Symbol getTradingPair() const { return symbol; }
    core::Timestamp getGenerationTime() const { return generationTime; }
    std::chrono::milliseconds getTtl() const { return std::chrono::duration_cast<std::chrono::milliseconds>(ttl); }
    core::Direction getDirection() const { return direction; }
    double getStrength() const { return strength; }
    double getTargetSize() const { return targetSize; }
    double getQuantity() const { return targetSize*price; } // assuming quantity = size * price
    const core::ExchangeId& getExchangeId() const { return exchangeId; }
    std::chrono::milliseconds getSignalTtl() const { 
        return std::chrono::duration_cast<std::chrono::milliseconds>(ttl); 
    }
    const StrategyId& getStrategyId() const { return strategyId; }
    const std::string& getStrategyVersion() const { return strategyId.version; }
    const std::unordered_map<std::string, double>& getSignalMetrics() const { return signalMetrics; }
    
    // Setters
    void setId(const std::string& newId) { this->id = newId; }
    void setExchangeId(const core::ExchangeId& newExchangeId) { this->exchangeId = newExchangeId; }
    void setDirection(core::Direction d) { this->direction = d; }
    void setStrength(double s) { this->strength = s; }
    void setStrategyId(const StrategyId& stId) { this->strategyId = stId; }
    void setTradingPair(const core::Symbol& tradingPair) { symbol = tradingPair; }
    // set target size
    void setTargetSize(double size) { this->targetSize = size; }
    void setTtl(const core::Duration& duration) { this->ttl = duration; }
    /**
     * Check if signal is still valid based on TTL
     */
    bool isValid(const core::Timestamp& currentTime) const {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - generationTime).count();
        return elapsed < ttl.count();
    }

    /**
     * Calculate remaining strength based on signal decay
     */
    double getRemainingStrength(const core::Timestamp& currentTime, double decayRateOverride = 0.0) const {
        double effectiveDecayRate = (decayRateOverride > 0.0) ? decayRateOverride : decayRate;
        
        if (effectiveDecayRate <= 0.0) {
            return isValid(currentTime) ? strength : 0.0;
        }
        
        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            currentTime - generationTime).count();
        
        // Exponential decay formula: strength * e^(-decay_rate * elapsed_time)
        double decayFactor = std::exp(-effectiveDecayRate * elapsedMs / 1000.0);
        return strength * decayFactor;
    }
    
    /**
     * Get a metric value with a default if not found
     */
    double getMetric(const std::string& name, double defaultValue = 0.0) const {
        auto it = signalMetrics.find(name);
        return (it != signalMetrics.end()) ? it->second : defaultValue;
    }
    
    /**
     * Add or update a metric
     */
    void setMetric(std::string name, double value) {
        // take name by value and move into the map to avoid an extra copy
        signalMetrics.insert_or_assign(std::move(name), value);
    }
    // metric with string value
    void setMetric(std::string name, const std::string& value) {
        // For string metrics, we can store a hash or length as a double representation
        signalMetrics.insert_or_assign(std::move(name), static_cast<double>(value.length()));
    }

    /**
     * Check if this is a buy signal
     */
    bool isBuySignal() const { return direction == core::Direction::BUY; }
    
    /**
     * Check if this is a sell signal
     */
    bool isSellSignal() const { return direction == core::Direction::SELL; }
};
 
 } // namespace core
 } // namespace trading