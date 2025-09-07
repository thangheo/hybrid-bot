/**
 * @file hawkes_parameters.hpp
 * @brief Hawkes process parameters for order imbalance prediction
 * 
 * @author Trading System Team
 * @date September 07, 2025
 */

#pragma once

#include <vector>
#include <chrono>
#include <map>
#include <string>

namespace trading {
namespace strategy {

/**
 * Types of imbalance events for Hawkes processes
 */
enum class HawkesEventType {
    VOLUME_IMBALANCE,    // Volume-based imbalances
    PRICE_IMBALANCE,     // Price level imbalances  
    LIQUIDITY_IMBALANCE  // Liquidity flow imbalances
};

/**
 * Cross-excitation matrix for multi-dimensional Hawkes processes
 * Represents how events of one type influence the intensity of other types
 */
struct CrossExcitationMatrix {
    // Self and cross-excitement factors [from_type][to_type]
    std::map<HawkesEventType, std::map<HawkesEventType, double>> alphaMatrix;
    
    // Decay rates for cross-excitation [from_type][to_type]
    std::map<HawkesEventType, std::map<HawkesEventType, double>> betaMatrix;
    
    CrossExcitationMatrix() {
        // Initialize with default values
        for (auto fromType : {HawkesEventType::VOLUME_IMBALANCE, 
                              HawkesEventType::PRICE_IMBALANCE, 
                              HawkesEventType::LIQUIDITY_IMBALANCE}) {
            for (auto toType : {HawkesEventType::VOLUME_IMBALANCE, 
                                HawkesEventType::PRICE_IMBALANCE, 
                                HawkesEventType::LIQUIDITY_IMBALANCE}) {
                if (fromType == toType) {
                    // Self-excitement
                    alphaMatrix[fromType][toType] = 0.3;
                    betaMatrix[fromType][toType] = 2.0;
                } else {
                    // Cross-excitement (weaker)
                    alphaMatrix[fromType][toType] = 0.1;
                    betaMatrix[fromType][toType] = 1.5;
                }
            }
        }
    }
    
    /**
     * Get excitement factor from one event type to another
     */
    double getAlpha(HawkesEventType from, HawkesEventType to) const {
        auto fromIt = alphaMatrix.find(from);
        if (fromIt != alphaMatrix.end()) {
            auto toIt = fromIt->second.find(to);
            if (toIt != fromIt->second.end()) {
                return toIt->second;
            }
        }
        return 0.0;
    }
    
    /**
     * Get decay rate from one event type to another
     */
    double getBeta(HawkesEventType from, HawkesEventType to) const {
        auto fromIt = betaMatrix.find(from);
        if (fromIt != betaMatrix.end()) {
            auto toIt = fromIt->second.find(to);
            if (toIt != fromIt->second.end()) {
                return toIt->second;
            }
        }
        return 1.0;
    }
    
    /**
     * Set excitement parameters between event types
     */
    void setParameters(HawkesEventType from, HawkesEventType to, double alpha, double beta) {
        alphaMatrix[from][to] = alpha;
        betaMatrix[from][to] = beta;
    }
};

/**
 * Enhancement factors for signal generation based on Hawkes predictions
 */
struct HawkesEnhancementFactors {
    double strengthMultiplier = 1.5;      // Multiply signal strength by this when clustering detected
    double ttlExtensionFactor = 2.0;      // Extend TTL by this factor for persistent signals
    double clusteringThreshold = 0.7;     // Intensity threshold to consider clustering
    double persistenceThreshold = 0.6;    // Minimum persistence for TTL extension
    double maxStrengthBoost = 2.0;        // Maximum allowed strength multiplier
    double maxTtlExtension = 5.0;         // Maximum allowed TTL extension factor
    
    // Confidence adjustments
    double highClusteringConfidenceBoost = 0.2;   // Add to confidence when high clustering
    double lowPersistenceConfidencePenalty = 0.1; // Subtract when low persistence
    
    /**
     * Validate enhancement factors
     */
    bool isValid() const {
        return strengthMultiplier >= 1.0 && strengthMultiplier <= maxStrengthBoost &&
               ttlExtensionFactor >= 1.0 && ttlExtensionFactor <= maxTtlExtension &&
               clusteringThreshold >= 0.0 && clusteringThreshold <= 1.0 &&
               persistenceThreshold >= 0.0 && persistenceThreshold <= 1.0 &&
               maxStrengthBoost >= 1.0 && maxTtlExtension >= 1.0 &&
               highClusteringConfidenceBoost >= 0.0 &&
               lowPersistenceConfidencePenalty >= 0.0;
    }
};

/**
 * Complete Hawkes process parameters for order imbalance prediction
 */
struct HawkesParameters {
    // Base intensities for each event type (μ parameter)
    std::map<HawkesEventType, double> baseIntensity;
    
    // Cross-excitation matrix
    CrossExcitationMatrix crossExcitation;
    
    // Enhancement factors for signal generation
    HawkesEnhancementFactors enhancementFactors;
    
    // Time window for event history
    std::chrono::minutes historyWindow{60};
    
    // Maximum number of events to keep in history (memory bound)
    size_t maxHistorySize = 1000;
    
    // Minimum event strength to include in Hawkes process
    double minEventStrength = 0.1;
    
    // Time resolution for intensity calculations (affects performance vs accuracy)
    std::chrono::milliseconds timeResolution{100};
    
    // Clustering detection parameters
    double intensityClusteringThreshold = 2.0;  // Multiple of base intensity
    std::chrono::seconds clusteringTimeWindow{30}; // Window to look for clustering
    
    // Persistence calculation parameters
    std::chrono::seconds persistenceHorizon{300}; // How far ahead to predict
    double persistenceDecayFactor = 0.8;         // Factor for persistence decay
    
    // Performance optimization settings
    bool enableCaching = true;                    // Cache intensity calculations
    std::chrono::milliseconds cacheValidityPeriod{50}; // Cache validity
    bool enablePrecomputation = true;            // Precompute common values
    
    /**
     * Default constructor with reasonable defaults
     */
    HawkesParameters() {
        // Set default base intensities
        baseIntensity[HawkesEventType::VOLUME_IMBALANCE] = 0.5;
        baseIntensity[HawkesEventType::PRICE_IMBALANCE] = 0.3;
        baseIntensity[HawkesEventType::LIQUIDITY_IMBALANCE] = 0.4;
    }
    
    /**
     * Get base intensity for an event type
     */
    double getBaseIntensity(HawkesEventType eventType) const {
        auto it = baseIntensity.find(eventType);
        return (it != baseIntensity.end()) ? it->second : 0.1; // Default fallback
    }
    
    /**
     * Set base intensity for an event type
     */
    void setBaseIntensity(HawkesEventType eventType, double intensity) {
        baseIntensity[eventType] = std::max(0.0, intensity);
    }
    
    /**
     * Validate all parameters
     */
    bool isValid() const {
        // Check base intensities are positive
        for (const auto& [type, intensity] : baseIntensity) {
            if (intensity <= 0.0) return false;
        }
        
        // Check time parameters
        if (historyWindow.count() <= 0 || maxHistorySize == 0) return false;
        if (timeResolution.count() <= 0) return false;
        if (clusteringTimeWindow.count() <= 0) return false;
        if (persistenceHorizon.count() <= 0) return false;
        
        // Check threshold parameters
        if (minEventStrength < 0.0 || minEventStrength > 1.0) return false;
        if (intensityClusteringThreshold <= 0.0) return false;
        if (persistenceDecayFactor <= 0.0 || persistenceDecayFactor > 1.0) return false;
        
        // Check cache parameters
        if (cacheValidityPeriod.count() <= 0) return false;
        
        // Validate enhancement factors
        return enhancementFactors.isValid();
    }
    
    /**
     * Apply scaling factor to all alpha parameters (for calibration)
     */
    void scaleAlphaParameters(double scaleFactor) {
        if (scaleFactor <= 0.0) return;
        
        for (auto& [fromType, toMap] : crossExcitation.alphaMatrix) {
            for (auto& [toType, alpha] : toMap) {
                alpha *= scaleFactor;
            }
        }
    }
    
    /**
     * Apply scaling factor to all beta parameters (for calibration)
     */
    void scaleBetaParameters(double scaleFactor) {
        if (scaleFactor <= 0.0) return;
        
        for (auto& [fromType, toMap] : crossExcitation.betaMatrix) {
            for (auto& [toType, beta] : toMap) {
                beta *= scaleFactor;
            }
        }
    }
    
    /**
     * Reset to default values
     */
    void resetToDefaults() {
        *this = HawkesParameters();
    }
    
    /**
     * Create a copy with modified enhancement factors
     */
    HawkesParameters withEnhancementFactors(const HawkesEnhancementFactors& factors) const {
        HawkesParameters copy = *this;
        copy.enhancementFactors = factors;
        return copy;
    }
};

/**
 * Convert imbalance type string to HawkesEventType enum
 */
inline HawkesEventType stringToHawkesEventType(const std::string& typeStr) {
    if (typeStr == "volume_imbalance" || typeStr == "VOLUME_IMBALANCE") {
        return HawkesEventType::VOLUME_IMBALANCE;
    } else if (typeStr == "price_imbalance" || typeStr == "PRICE_IMBALANCE") {
        return HawkesEventType::PRICE_IMBALANCE;
    } else if (typeStr == "liquidity_imbalance" || typeStr == "LIQUIDITY_IMBALANCE") {
        return HawkesEventType::LIQUIDITY_IMBALANCE;
    }
    return HawkesEventType::VOLUME_IMBALANCE; // Default fallback
}

/**
 * Convert HawkesEventType enum to string
 */
inline std::string hawkesEventTypeToString(HawkesEventType eventType) {
    switch (eventType) {
        case HawkesEventType::VOLUME_IMBALANCE:    return "volume_imbalance";
        case HawkesEventType::PRICE_IMBALANCE:     return "price_imbalance";
        case HawkesEventType::LIQUIDITY_IMBALANCE: return "liquidity_imbalance";
        default:                                   return "unknown";
    }
}

} // namespace strategy
} // namespace trading