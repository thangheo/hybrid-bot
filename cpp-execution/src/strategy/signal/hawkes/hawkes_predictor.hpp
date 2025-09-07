/**
 * @file hawkes_predictor.hpp
 * @brief Hawkes process predictor for order imbalance clustering and persistence
 * 
 * @author Trading System Team
 * @date September 07, 2025
 */

#pragma once

#include "hawkes_parameters.hpp"
#include "strategy/types.hpp"
#include "core/time.hpp"
#include "core/direction.hpp"
#include <deque>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <algorithm>
#include <cmath>

namespace trading {
namespace strategy {

/**
 * Historical event for Hawkes process calculation
 */
struct HawkesEvent {
    core::Timestamp timestamp;
    HawkesEventType eventType;
    core::Direction direction;
    double strength;
    std::string exchangeId;
    std::string tradingPair;
    
    HawkesEvent() = default;
    
    HawkesEvent(core::Timestamp ts, HawkesEventType type, core::Direction dir, 
                double str, const std::string& exchange, const std::string& pair)
        : timestamp(ts), eventType(type), direction(dir), strength(str),
          exchangeId(exchange), tradingPair(pair) {}
    
    /**
     * Create from ImbalanceEvent
     */
//      enum class ImbalanceType {
//      VOLUME_IMBALANCE,              // Significant difference between bid and ask volume
//      PRICE_LEVEL_IMBALANCE,         // Imbalance across multiple price levels
//      LIQUIDITY_REMOVAL,             // Sudden removal of liquidity from one side
//      ORDER_FLOW_IMBALANCE,          // Directional bias in order submissions
//      LARGE_ORDER_APPEARANCE,        // Large orders appearing in the book
//      LARGE_ORDER_DISAPPEARANCE,     // Large orders being removed from the book
//      PRICE_LEVEL_CONCENTRATION,     // Concentration of volume at specific price levels
//      BID_ASK_SPREAD_CHANGE,         // Significant change in the bid-ask spread
//      MARKET_DEPTH_CHANGE,           // Significant change in market depth
//      LIQUIDITY_FLOW_IMBALANCE,      // Imbalance in liquidity flow
//      ABNORMAL_SPREAD,               // Abnormal spread between bid and ask prices
//      LIQUIDITY_IMBALANCE,           // General liquidity imbalance
//      TREND_CONTINUATION,            // Continuation of a price trend
//      TREND_REVERSAL,                // Reversal of a price trend
//      CUSTOM                         // Custom imbalance type defined by strategy
//  };

    static HawkesEvent fromImbalanceEvent(const ImbalanceEvent& event) {
        HawkesEventType hawkesType = HawkesEventType::VOLUME_IMBALANCE;
        
        // Map imbalance type to Hawkes event type
        if (event.type == ImbalanceType::VOLUME_IMBALANCE) {
            hawkesType = HawkesEventType::VOLUME_IMBALANCE;
        } else if (event.type == ImbalanceType::PRICE_LEVEL_IMBALANCE) {
            hawkesType = HawkesEventType::PRICE_IMBALANCE;
        } else if (event.type == ImbalanceType::LIQUIDITY_IMBALANCE) {
            hawkesType = HawkesEventType::LIQUIDITY_IMBALANCE;
        }
        
    // core::Timestamp timestamp;
    // HawkesEventType eventType;
    // core::Direction direction;
    // double strength;
    // std::string exchangeId;
    // std::string tradingPair;
        return HawkesEvent(
            static_cast<core::Timestamp>(event.detectionTime),
            hawkesType,
            event.direction,
            event.strength,
            event.exchangeId.toString(),
            event.symbol.toString() // Assuming Symbol has a toString() method
            // static_cast<std::string>(event.getTradingPair())
        );
    }
};

/**
 * Cached intensity calculation result
 */
struct IntensityCache {
    core::Timestamp calculationTime;
    std::unordered_map<HawkesEventType, double> intensities;
    double overallIntensity = 0.0;
    
    bool isValid(const std::chrono::milliseconds& validityPeriod) const {
        auto now = std::chrono::system_clock::now();
        auto age = std::chrono::duration_cast<std::chrono::milliseconds>(now - calculationTime);
        return age <= validityPeriod;
    }
};

/**
 * Hawkes process predictor for order imbalance events
 * 
 * Implements multi-dimensional Hawkes processes to predict:
 * - Event clustering probability
 * - Signal persistence
 * - Cross-excitation between different imbalance types
 */
class HawkesPredictor {
public:
    /**
     * Constructor
     */
    explicit HawkesPredictor(const HawkesParameters& params = HawkesParameters());
    
    /**
     * Destructor
     */
    ~HawkesPredictor() = default;
    
    // Non-copyable but movable
    HawkesPredictor(const HawkesPredictor&) = delete;
    HawkesPredictor& operator=(const HawkesPredictor&) = delete;
    HawkesPredictor(HawkesPredictor&&) = default;
    HawkesPredictor& operator=(HawkesPredictor&&) = default;
    
    /**
     * Update with new imbalance event
     * Thread-safe
     */
    void updateWithImbalanceEvent(const ImbalanceEvent& event);
    
    /**
     * Get clustering probability for a given direction and strength
     * Returns value between 0.0 and 1.0
     */
    double getClusteringProbability(core::Direction direction, double strength);
    
    /**
     * Predict persistence of a trading signal
     * Returns value between 0.0 and 1.0 indicating expected persistence
     */
    double predictPersistence(const TradingSignal& signal);
    
    /**
     * Get current overall intensity across all event types
     */
    double getCurrentIntensity();
    
    /**
     * Get current intensity for specific event type
     */
    double getCurrentIntensity(HawkesEventType eventType);
    
    /**
     * Configure Hawkes parameters
     * Thread-safe
     */
    void configureParameters(const HawkesParameters& params);
    
    /**
     * Get current parameters (copy)
     */
    HawkesParameters getParameters() const;
    
    /**
     * Clear all event history
     */
    void clearHistory();
    
    /**
     * Get number of events in history
     */
    size_t getHistorySize() const;
    
    /**
     * Get statistics about the predictor state
     */
    struct PredictorStats {
        size_t totalEvents = 0;
        size_t eventsInWindow = 0;
        double averageIntensity = 0.0;
        double maxIntensity = 0.0;
        core::Timestamp lastEventTime;
        std::unordered_map<HawkesEventType, size_t> eventTypeCount;
        std::unordered_map<core::Direction, size_t> directionCount;
    };
    
    PredictorStats getStats() const;
    
    /**
     * Check if predictor is ready (has sufficient event history)
     */
    bool isReady() const;
    
    /**
     * Precompute values for performance optimization
     * Called automatically when parameters change
     */
    void precomputeValues();

private:
    // Configuration
    HawkesParameters parameters_;
    mutable std::mutex parameterMutex_;
    
    // Event history
    std::deque<HawkesEvent> eventHistory_;
    mutable std::mutex historyMutex_;
    
    // Performance optimization
    mutable std::optional<IntensityCache> intensityCache_;
    mutable std::mutex cacheMutex_;
    
    // Hash function for pair
    template<typename T1, typename T2>
    struct PairHash {
        std::size_t operator()(const std::pair<T1, T2>& p) const {
            auto h1 = std::hash<T1>{}(p.first);
            auto h2 = std::hash<T2>{}(p.second);
            return h1 ^ (h2 << 1);
        }
    };
    
    // Precomputed values for performance
    struct PrecomputedValues {
        std::unordered_map<HawkesEventType, double> baseIntensityLookup;
        std::unordered_map<std::pair<HawkesEventType, HawkesEventType>, double, 
                          PairHash<HawkesEventType, HawkesEventType>> alphaBetaProducts;
        bool isValid = false;
    } precomputedValues_;
    
    /**
     * Calculate intensity at current time for all event types
     * Thread-safe with caching
     */
    std::unordered_map<HawkesEventType, double> calculateCurrentIntensities() const;
    
    /**
     * Calculate intensity for specific event type at given time
     */
    double calculateIntensity(HawkesEventType targetType, core::Timestamp targetTime) const;
    
    /**
     * Calculate conditional intensity contribution from past events
     */
    double calculateConditionalIntensity(HawkesEventType targetType, 
                                       core::Timestamp targetTime,
                                       const std::deque<HawkesEvent>& events) const;
    
    /**
     * Clean up old events outside the history window
     */
    void cleanupOldEvents();
    
    /**
     * Get events within a specific time window
     */
    std::vector<HawkesEvent> getEventsInWindow(core::Timestamp startTime, 
                                              core::Timestamp endTime) const;
    
    /**
     * Calculate clustering probability based on recent intensity
     */
    double calculateClusteringProbability(const std::unordered_map<HawkesEventType, double>& intensities,
                                         core::Direction direction) const;
    
    /**
     * Calculate persistence based on intensity decay prediction
     */
    double calculatePersistence(const std::unordered_map<HawkesEventType, double>& intensities,
                               const TradingSignal& signal) const;
    
    /**
     * Map trading signal to Hawkes event type
     */
    HawkesEventType mapSignalToEventType(const TradingSignal& signal) const;
    
    /**
     * Calculate exponential decay factor
     */
    inline double calculateDecayFactor(double beta, 
                                     const std::chrono::duration<double>& timeDiff) const {
        return std::exp(-beta * timeDiff.count());
    }
    
    /**
     * Validate cached intensities
     */
    bool isCacheValid() const;
    
    /**
     * Update intensity cache
     */
    void updateCache(const std::unordered_map<HawkesEventType, double>& intensities) const;
    
    /**
     * Get direction-specific events
     */
    std::vector<HawkesEvent> getDirectionEvents(core::Direction direction, 
                                               const std::deque<HawkesEvent>& events) const;
    
    /**
     * Calculate weighted intensity based on event strength
     */
    double calculateWeightedIntensity(const HawkesEvent& event, 
                                     HawkesEventType targetType,
                                     core::Timestamp targetTime) const;
};

// Implementation of thread-safe statistics collection
inline HawkesPredictor::PredictorStats HawkesPredictor::getStats() const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    PredictorStats stats;
    stats.totalEvents = eventHistory_.size();
    
    // Calculate events in current window
    auto now = std::chrono::system_clock::now();
    auto windowStart = now - parameters_.historyWindow;
    
    for (const auto& event : eventHistory_) {
        if (event.timestamp >= windowStart) {
            stats.eventsInWindow++;
            stats.eventTypeCount[event.eventType]++;
            stats.directionCount[event.direction]++;
            
            if (stats.lastEventTime < event.timestamp) {
                stats.lastEventTime = event.timestamp;
            }
        }
    }
    
    // Calculate intensity statistics
    auto intensities = calculateCurrentIntensities();
    double totalIntensity = 0.0;
    double maxIntensity = 0.0;
    
    for (const auto& [type, intensity] : intensities) {
        totalIntensity += intensity;
        maxIntensity = std::max(maxIntensity, intensity);
    }
    
    stats.averageIntensity = intensities.empty() ? 0.0 : totalIntensity / intensities.size();
    stats.maxIntensity = maxIntensity;
    
    return stats;
}

} // namespace strategy
} // namespace trading