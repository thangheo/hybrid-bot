/**
 * @file hawkes_predictor.cpp
 * @brief Implementation of Hawkes process predictor
 * 
 * @author Trading System Team
 * @date September 07, 2025
 */

#include "strategy/signal/hawkes/hawkes_predictor.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace trading {
namespace strategy {

HawkesPredictor::HawkesPredictor(const HawkesParameters& params) 
    : parameters_(params) {
    if (!parameters_.isValid()) {
        parameters_ = HawkesParameters(); // Use defaults if invalid
    }
    precomputeValues();
}

void HawkesPredictor::updateWithImbalanceEvent(const ImbalanceEvent& event) {
    // Convert to Hawkes event
    auto hawkesEvent = HawkesEvent::fromImbalanceEvent(event);
    
    // Filter by minimum strength
    if (hawkesEvent.strength < parameters_.minEventStrength) {
        return;
    }
    
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        
        // Add to history (maintain chronological order)
        eventHistory_.push_back(hawkesEvent);
        
        // Sort if needed (should rarely be needed with real-time data)
        if (eventHistory_.size() > 1 && 
            eventHistory_[eventHistory_.size()-1].timestamp < 
            eventHistory_[eventHistory_.size()-2].timestamp) {
            std::sort(eventHistory_.begin(), eventHistory_.end(),
                     [](const HawkesEvent& a, const HawkesEvent& b) {
                         return a.timestamp < b.timestamp;
                     });
        }
        
        // Cleanup old events periodically
        if (eventHistory_.size() % 100 == 0) {
            cleanupOldEvents();
        }
    }
    
    // Invalidate cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        intensityCache_.reset();
    }
}

double HawkesPredictor::getClusteringProbability(core::Direction direction, double strength) {
    auto intensities = calculateCurrentIntensities();
    return calculateClusteringProbability(intensities, direction);
}

double HawkesPredictor::predictPersistence(const TradingSignal& signal) {
    auto intensities = calculateCurrentIntensities();
    return calculatePersistence(intensities, signal);
}

double HawkesPredictor::getCurrentIntensity() {
    auto intensities = calculateCurrentIntensities();
    
    double totalIntensity = 0.0;
    for (const auto& [type, intensity] : intensities) {
        totalIntensity += intensity;
    }
    
    return totalIntensity;
}

double HawkesPredictor::getCurrentIntensity(HawkesEventType eventType) {
    auto intensities = calculateCurrentIntensities();
    auto it = intensities.find(eventType);
    return (it != intensities.end()) ? it->second : parameters_.getBaseIntensity(eventType);
}

void HawkesPredictor::configureParameters(const HawkesParameters& params) {
    {
        std::lock_guard<std::mutex> lock(parameterMutex_);
        parameters_ = params.isValid() ? params : HawkesParameters();
    }
    
    precomputeValues();
    
    // Invalidate cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        intensityCache_.reset();
    }
    
    // Cleanup history based on new parameters
    cleanupOldEvents();
}

HawkesParameters HawkesPredictor::getParameters() const {
    std::lock_guard<std::mutex> lock(parameterMutex_);
    return parameters_;
}

void HawkesPredictor::clearHistory() {
    std::lock_guard<std::mutex> lock(historyMutex_);
    eventHistory_.clear();
    
    // Invalidate cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        intensityCache_.reset();
    }
}

size_t HawkesPredictor::getHistorySize() const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    return eventHistory_.size();
}

bool HawkesPredictor::isReady() const {
    std::lock_guard<std::mutex> lock(historyMutex_);
    
    // Need at least 10 events and some recent activity
    if (eventHistory_.size() < 10) {
        return false;
    }
    
    // Check for recent activity (within last 10 minutes)
    auto now = std::chrono::system_clock::now();
    auto recentThreshold = now - std::chrono::minutes(10);
    
    auto it = std::find_if(eventHistory_.rbegin(), eventHistory_.rend(),
                          [recentThreshold](const HawkesEvent& event) {
                              return event.timestamp >= recentThreshold;
                          });
    
    return it != eventHistory_.rend();
}

void HawkesPredictor::precomputeValues() {
    if (!parameters_.enablePrecomputation) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(parameterMutex_);
    
    // Precompute base intensity lookup
    precomputedValues_.baseIntensityLookup.clear();
    for (const auto& [type, intensity] : parameters_.baseIntensity) {
        precomputedValues_.baseIntensityLookup[type] = intensity;
    }
    
    // Precompute alpha * beta products for efficiency
    precomputedValues_.alphaBetaProducts.clear();
    for (auto fromType : {HawkesEventType::VOLUME_IMBALANCE, 
                         HawkesEventType::PRICE_IMBALANCE, 
                         HawkesEventType::LIQUIDITY_IMBALANCE}) {
        for (auto toType : {HawkesEventType::VOLUME_IMBALANCE, 
                           HawkesEventType::PRICE_IMBALANCE, 
                           HawkesEventType::LIQUIDITY_IMBALANCE}) {
            double alpha = parameters_.crossExcitation.getAlpha(fromType, toType);
            double beta = parameters_.crossExcitation.getBeta(fromType, toType);
            precomputedValues_.alphaBetaProducts[{fromType, toType}] = alpha * beta;
        }
    }
    
    precomputedValues_.isValid = true;
}

std::unordered_map<HawkesEventType, double> HawkesPredictor::calculateCurrentIntensities() const {
    // Check cache first
    if (parameters_.enableCaching && isCacheValid()) {
        std::lock_guard<std::mutex> lock(cacheMutex_);
        if (intensityCache_ && intensityCache_->isValid(parameters_.cacheValidityPeriod)) {
            return intensityCache_->intensities;
        }
    }
    
    auto now = std::chrono::system_clock::now();
    std::unordered_map<HawkesEventType, double> intensities;
    
    // Calculate for each event type
    for (auto eventType : {HawkesEventType::VOLUME_IMBALANCE, 
                          HawkesEventType::PRICE_IMBALANCE, 
                          HawkesEventType::LIQUIDITY_IMBALANCE}) {
        intensities[eventType] = calculateIntensity(eventType, now);
    }
    
    // Update cache
    if (parameters_.enableCaching) {
        updateCache(intensities);
    }
    
    return intensities;
}

double HawkesPredictor::calculateIntensity(HawkesEventType targetType, core::Timestamp targetTime) const {
    // Base intensity
    double intensity = parameters_.getBaseIntensity(targetType);
    
    // Add conditional intensity from past events
    std::lock_guard<std::mutex> lock(historyMutex_);
    intensity += calculateConditionalIntensity(targetType, targetTime, eventHistory_);
    
    return std::max(0.0, intensity);
}

double HawkesPredictor::calculateConditionalIntensity(HawkesEventType targetType, 
                                                     core::Timestamp targetTime,
                                                     const std::deque<HawkesEvent>& events) const {
    double conditionalIntensity = 0.0;
    
    // Only consider events within the history window
    auto windowStart = targetTime - parameters_.historyWindow;
    
    for (const auto& event : events) {
        if (event.timestamp < windowStart || event.timestamp >= targetTime) {
            continue;
        }
        
        // Calculate time difference
        auto timeDiff = std::chrono::duration<double>(targetTime - event.timestamp);
        
        // Get cross-excitation parameters
        double alpha = parameters_.crossExcitation.getAlpha(event.eventType, targetType);
        double beta = parameters_.crossExcitation.getBeta(event.eventType, targetType);
        
        if (alpha > 0.0 && beta > 0.0) {
            // Weight by event strength
            double weightedAlpha = alpha * event.strength;
            
            // Calculate exponential decay
            double decayFactor = calculateDecayFactor(beta, timeDiff);
            
            // Add contribution
            conditionalIntensity += weightedAlpha * decayFactor;
        }
    }
    
    return conditionalIntensity;
}

void HawkesPredictor::cleanupOldEvents() {
    auto now = std::chrono::system_clock::now();
    auto cutoffTime = now - parameters_.historyWindow;
    
    // Remove events older than history window
    auto it = std::remove_if(eventHistory_.begin(), eventHistory_.end(),
                            [cutoffTime](const HawkesEvent& event) {
                                return event.timestamp < cutoffTime;
                            });
    
    eventHistory_.erase(it, eventHistory_.end());
    
    // Enforce maximum history size
    if (eventHistory_.size() > parameters_.maxHistorySize) {
        size_t excess = eventHistory_.size() - parameters_.maxHistorySize;
        eventHistory_.erase(eventHistory_.begin(), eventHistory_.begin() + excess);
    }
}

std::vector<HawkesEvent> HawkesPredictor::getEventsInWindow(core::Timestamp startTime, 
                                                           core::Timestamp endTime) const {
    std::vector<HawkesEvent> windowEvents;
    
    for (const auto& event : eventHistory_) {
        if (event.timestamp >= startTime && event.timestamp <= endTime) {
            windowEvents.push_back(event);
        }
    }
    
    return windowEvents;
}

double HawkesPredictor::calculateClusteringProbability(
    const std::unordered_map<HawkesEventType, double>& intensities,
    core::Direction direction) const {
    
    // Calculate overall intensity
    double totalIntensity = 0.0;
    double baseIntensitySum = 0.0;
    
    for (const auto& [type, intensity] : intensities) {
        totalIntensity += intensity;
        baseIntensitySum += parameters_.getBaseIntensity(type);
    }
    
    if (baseIntensitySum <= 0.0) {
        return 0.0;
    }
    
    // Intensity ratio compared to base
    double intensityRatio = totalIntensity / baseIntensitySum;
    
    // Direction-specific adjustment
    double directionWeight = 1.0;
    {
        std::lock_guard<std::mutex> lock(historyMutex_);
        auto recentEvents = getDirectionEvents(direction, eventHistory_);
        
        if (!recentEvents.empty()) {
            // Recent activity in same direction increases clustering probability
            auto now = std::chrono::system_clock::now();
            auto recentWindow = now - parameters_.clusteringTimeWindow;
            
            size_t recentCount = std::count_if(recentEvents.begin(), recentEvents.end(),
                                              [recentWindow](const HawkesEvent& event) {
                                                  return event.timestamp >= recentWindow;
                                              });
            
            directionWeight = 1.0 + (recentCount * 0.2); // 20% boost per recent same-direction event
        }
    }
    
    // Calculate probability using sigmoid function
    double clusteringScore = (intensityRatio - 1.0) * directionWeight;
    double probability = 1.0 / (1.0 + std::exp(-clusteringScore));
    
    // Apply threshold
    if (intensityRatio < parameters_.intensityClusteringThreshold) {
        probability *= 0.5; // Reduce probability if below threshold
    }
    
    return std::clamp(probability, 0.0, 1.0);
}

double HawkesPredictor::calculatePersistence(
    const std::unordered_map<HawkesEventType, double>& intensities,
    const TradingSignal& signal) const {
    
    HawkesEventType signalType = mapSignalToEventType(signal);
    
    // Get current intensity for signal type
    auto it = intensities.find(signalType);
    double currentIntensity = (it != intensities.end()) ? 
        it->second : parameters_.getBaseIntensity(signalType);
    
    // Predict intensity decay over persistence horizon
    auto futureTime = std::chrono::system_clock::now() + parameters_.persistenceHorizon;
    double futureIntensity = calculateIntensity(signalType, futureTime);
    
    // Calculate persistence as ratio of future to current intensity
    double rawPersistence = (currentIntensity > 0.0) ? futureIntensity / currentIntensity : 0.0;
    
    // Apply decay factor
    double persistence = rawPersistence * parameters_.persistenceDecayFactor;
    
    // Boost persistence for strong signals
    if (signal.strength > 0.7) {
        persistence *= 1.2;
    }
    
    return std::clamp(persistence, 0.0, 1.0);
}

HawkesEventType HawkesPredictor::mapSignalToEventType(const TradingSignal& signal) const {
    // Use signal metrics to determine event type
    const auto& metrics = signal.signalMetrics;

    // Check for specific metric indicators
    if (metrics.find("volumeImbalance ") != metrics.end()) {
        return HawkesEventType::VOLUME_IMBALANCE;
    } else if (metrics.find("price_imbalance") != metrics.end()) {
        return HawkesEventType::PRICE_IMBALANCE;
    } else if (metrics.find("liquidity_imbalance") != metrics.end()) {
        return HawkesEventType::LIQUIDITY_IMBALANCE;
    }
    
    // Default mapping based on signal strength
    if (signal.strength > 0.8) {
        return HawkesEventType::PRICE_IMBALANCE; // Strong signals likely price-related
    } else if (signal.strength > 0.5) {
        return HawkesEventType::VOLUME_IMBALANCE; // Medium signals likely volume-related
    } else {
        return HawkesEventType::LIQUIDITY_IMBALANCE; // Weak signals likely liquidity-related
    }
}

bool HawkesPredictor::isCacheValid() const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    return intensityCache_ && intensityCache_->isValid(parameters_.cacheValidityPeriod);
}

void HawkesPredictor::updateCache(const std::unordered_map<HawkesEventType, double>& intensities) const {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    
    intensityCache_ = IntensityCache();
    intensityCache_->calculationTime = std::chrono::system_clock::now();
    intensityCache_->intensities = intensities;
    
    // Calculate overall intensity
    intensityCache_->overallIntensity = 0.0;
    for (const auto& [type, intensity] : intensities) {
        intensityCache_->overallIntensity += intensity;
    }
}

std::vector<HawkesEvent> HawkesPredictor::getDirectionEvents(
    core::Direction direction, 
    const std::deque<HawkesEvent>& events) const {
    
    std::vector<HawkesEvent> directionEvents;
    
    for (const auto& event : events) {
        if (event.direction == direction) {
            directionEvents.push_back(event);
        }
    }
    
    return directionEvents;
}

double HawkesPredictor::calculateWeightedIntensity(const HawkesEvent& event, 
                                                  HawkesEventType targetType,
                                                  core::Timestamp targetTime) const {
    // Get parameters
    double alpha = parameters_.crossExcitation.getAlpha(event.eventType, targetType);
    double beta = parameters_.crossExcitation.getBeta(event.eventType, targetType);
    
    if (alpha <= 0.0 || beta <= 0.0) {
        return 0.0;
    }
    
    // Calculate time difference
    auto timeDiff = std::chrono::duration<double>(targetTime - event.timestamp);
    
    // Apply strength weighting and decay
    double weightedAlpha = alpha * event.strength;
    double decayFactor = calculateDecayFactor(beta, timeDiff);
    
    return weightedAlpha * decayFactor;
}

} // namespace strategy
} // namespace trading