/**
 * @file imbalance_signal_generator.hpp (Complete Implementation)
 * @brief Enhanced signal generator with Hawkes process prediction capabilities
 * 
 * @author Trading System Team
 * @date September 07, 2025
 */

#pragma once

#include "strategy/types.hpp"
#include "strategy/signal/hawkes_predictor.hpp"
#include "strategy/signal/hawkes_parameters.hpp"
#include "core/time.hpp"
#include "core/direction.hpp"
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <optional>
#include <map>
#include <string>
#include <atomic>
#include <deque>
#include <iomanip>

namespace trading {
namespace strategy {

/**
 * Configuration for signal generation
 */
struct SignalGenerationConfig {
    double minSignalStrength = 0.1;         // Minimum strength to generate signal
    double maxSignalStrength = 1.0;         // Maximum allowed signal strength
    std::chrono::milliseconds baseTtl{5000}; // Base TTL for signals
    std::chrono::milliseconds maxTtl{30000}; // Maximum allowed TTL
    
    // Signal filtering
    bool enableDuplicateFiltering = true;    // Filter duplicate signals
    std::chrono::seconds duplicateWindow{10}; // Window for duplicate detection
    
    // Signal enhancement thresholds
    double strengthBoostThreshold = 0.6;     // Minimum strength for enhancement
    double confidenceBoostThreshold = 0.7;   // Minimum confidence for enhancement
    
    // Rate limiting
    int maxSignalsPerSecond = 10;           // Maximum signals per second
    int maxSignalsPerMinute = 100;          // Maximum signals per minute
    
    bool isValid() const {
        return minSignalStrength >= 0.0 && minSignalStrength <= 1.0 &&
               maxSignalStrength >= minSignalStrength && maxSignalStrength <= 1.0 &&
               baseTtl.count() > 0 && maxTtl >= baseTtl &&
               duplicateWindow.count() > 0 &&
               strengthBoostThreshold >= 0.0 && strengthBoostThreshold <= 1.0 &&
               confidenceBoostThreshold >= 0.0 && confidenceBoostThreshold <= 1.0 &&
               maxSignalsPerSecond > 0 && maxSignalsPerMinute > 0;
    }
};

/**
 * Enhanced ImbalanceSignalGenerator with Hawkes process capabilities
 * 
 * Generates trading signals from detected imbalances and optionally enhances
 * them using Hawkes process predictions for clustering and persistence.
 */
class ImbalanceSignalGenerator {
public:
    /**
     * Constructor
     */
    explicit ImbalanceSignalGenerator(const SignalGenerationConfig& config = SignalGenerationConfig());
    
    /**
     * Destructor
     */
    ~ImbalanceSignalGenerator() = default;
    
    // Non-copyable but movable
    ImbalanceSignalGenerator(const ImbalanceSignalGenerator&) = delete;
    ImbalanceSignalGenerator& operator=(const ImbalanceSignalGenerator&) = delete;
    ImbalanceSignalGenerator(ImbalanceSignalGenerator&&) = default;
    ImbalanceSignalGenerator& operator=(ImbalanceSignalGenerator&&) = default;
    
    /**
     * Process detected imbalances and add to signal generation queue
     * Thread-safe
     */
    void processImbalances(const std::vector<ImbalanceEvent>& imbalances);
    
    /**
     * Generate trading signals from processed imbalances
     * Returns vector of generated signals
     */
    std::vector<TradingSignal> generateSignals();
    
    /**
     * Configure signal generation parameters
     * Thread-safe
     */
    void configureSignalParameters(const SignalGenerationConfig& config);
    
    /**
     * Enable/disable Hawkes process enhancement
     * Thread-safe
     */
    void enableHawkesEnhancement(bool enable);
    
    /**
     * Configure Hawkes process parameters
     * Only effective if Hawkes enhancement is enabled
     * Thread-safe
     */
    void configureHawkesParameters(const HawkesParameters& params);
    
    /**
     * Check if Hawkes enhancement is enabled
     */
    bool isHawkesEnhancementEnabled() const;
    
    /**
     * Get current Hawkes parameters (if enhancement is enabled)
     */
    std::optional<HawkesParameters> getHawkesParameters() const;
    
    /**
     * Register a callback for new signals
     * Callback is called immediately when signals are generated
     */
    void registerSignalCallback(const std::function<void(const TradingSignal&)>& callback);
    
    /**
     * Clear all pending imbalances and reset state
     */
    void clear();
    
    /**
     * Get statistics about signal generation
     */
    struct GenerationStats {
        size_t totalImbalancesProcessed = 0;
        size_t totalSignalsGenerated = 0;
        size_t hawkesEnhancedSignals = 0;
        size_t filteredDuplicates = 0;
        size_t rateLimitedSignals = 0;
        double averageSignalStrength = 0.0;
        double averageEnhancementFactor = 0.0;
        core::Timestamp lastSignalTime;
        std::map<core::Direction, size_t> signalsByDirection;
        std::map<std::string, size_t> signalsByType;
    };
    
    GenerationStats getStats() const {
        std::lock_guard<std::mutex> lock(statsMutex_);
        return stats_;
    }
    
    /**
     * Reset statistics
     */
    void resetStats() {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats_ = GenerationStats();
    }
    
    /**
     * Check if generator is ready to produce quality signals
     */
    bool isReady() const {
        // Check if we have processed some imbalances
        {
            std::lock_guard<std::mutex> lock(statsMutex_);
            if (stats_.totalImbalancesProcessed < 10) {
                return false;
            }
        }
        
        // If Hawkes is enabled, check if it's ready
        if (hawkesEnabled_.load()) {
            return withHawkesPredictor([](const HawkesPredictor& predictor) {
                return predictor.isReady();
            });
        }
        
        return true;
    }
    
    /**
     * Get current signal generation rate (signals per second)
     */
    double getCurrentSignalRate() const {
        std::lock_guard<std::mutex> lock(rateLimitMutex_);
        
        auto now = std::chrono::system_clock::now();
        auto oneSecondAgo = now - std::chrono::seconds(1);
        
        // Count signals in last second
        size_t signalsInLastSecond = 0;
        for (const auto& timestamp : rateLimitState_.recentSignals) {
            if (timestamp >= oneSecondAgo) {
                signalsInLastSecond++;
            }
        }
        
        return static_cast<double>(signalsInLastSecond);
    }

private:
    // Configuration
    SignalGenerationConfig config_;
    mutable std::mutex configMutex_;
    
    // Hawkes enhancement
    std::atomic<bool> hawkesEnabled_{false};
    std::unique_ptr<HawkesPredictor> hawkesPredictor_;
    mutable std::mutex hawkesMutex_;
    
    // Imbalance processing queue
    std::vector<ImbalanceEvent> pendingImbalances_;
    mutable std::mutex imbalancesMutex_;
    
    // Signal callbacks
    std::vector<std::function<void(const TradingSignal&)>> signalCallbacks_;
    mutable std::mutex callbacksMutex_;
    
    // Statistics
    mutable GenerationStats stats_;
    mutable std::mutex statsMutex_;
    
    // Rate limiting
    struct RateLimitState {
        std::deque<core::Timestamp> recentSignals;
        size_t signalsThisSecond = 0;
        size_t signalsThisMinute = 0;
        core::Timestamp lastSecond;
        core::Timestamp lastMinute;
    } rateLimitState_;
    mutable std::mutex rateLimitMutex_;
    
    // Duplicate filtering
    std::vector<TradingSignal> recentSignals_;
    mutable std::mutex recentSignalsMutex_;
    
    /**
     * Generate base signal from imbalance event
     */
    TradingSignal generateBaseSignal(const ImbalanceEvent& imbalance) const;
    
    /**
     * Enhance signal using Hawkes predictions (if enabled)
     */
    TradingSignal enhanceSignalWithHawkes(const TradingSignal& baseSignal) const;
    
    /**
     * Apply signal enhancements based on Hawkes predictions
     */
    void applyHawkesEnhancements(TradingSignal& signal, 
                                double clusteringProbability,
                                double persistence) const;
    
    /**
     * Check if signal should be filtered due to rate limiting
     */
    bool isRateLimited() const;
    
    /**
     * Update rate limiting state
     */
    void updateRateLimitState();
    
    /**
     * Check if signal is duplicate of recent signal
     */
    bool isDuplicateSignal(const TradingSignal& signal) const;
    
    /**
     * Add signal to recent signals for duplicate filtering
     */
    void addToRecentSignals(const TradingSignal& signal);
    
    /**
     * Clean up old signals from recent signals list
     */
    void cleanupRecentSignals();
    
    /**
     * Validate signal before generation
     */
    bool validateSignal(const TradingSignal& signal) const;
    
    /**
     * Normalize signal strength to valid range
     */
    double normalizeSignalStrength(double strength) const {
        return std::clamp(strength, config_.minSignalStrength, config_.maxSignalStrength);
    }
    
    /**
     * Calculate signal TTL based on strength and persistence
     */
    std::chrono::milliseconds calculateSignalTtl(double strength, double persistence = 0.0) const {
        // Base TTL scaled by strength
        auto baseTtl = config_.baseTtl;
        double strengthFactor = strength * 2.0; // 0.5 strength = 1x TTL, 1.0 strength = 2x TTL
        
        // Apply persistence factor if available
        double persistenceFactor = (persistence > 0.0) ? (1.0 + persistence) : 1.0;
        
        auto calculatedTtl = std::chrono::duration_cast<std::chrono::milliseconds>(
            baseTtl * strengthFactor * persistenceFactor);
        
        return std::clamp(calculatedTtl, baseTtl, config_.maxTtl);
    }
    
    /**
     * Generate unique signal ID
     */
    std::string generateSignalId() const;
    
    /**
     * Notify signal callbacks
     */
    void notifySignalCallbacks(const TradingSignal& signal) const;
    
    /**
     * Update statistics
     */
    void updateStats(const TradingSignal& signal, bool hawkesEnhanced) const;
    
    /**
     * Map imbalance type to signal metrics
     */
    std::map<std::string, double> createSignalMetrics(const ImbalanceEvent& imbalance,
                                                     double clusteringProbability = 0.0,
                                                     double persistence = 0.0) const;
    
    /**
     * Determine signal direction from imbalance
     */
    core::Direction determineSignalDirection(const ImbalanceEvent& imbalance) const {
        return imbalance.direction;
    }
    
    /**
     * Calculate target size based on imbalance strength
     */
    double calculateTargetSize(const ImbalanceEvent& imbalance) const {
        // Base size scaled by imbalance strength
        double baseSize = 1.0; // Default base size
        return baseSize * imbalance.strength;
    }
    
    /**
     * Create Hawkes predictor with current parameters
     */
    void createHawkesPredictor();
    
    /**
     * Thread-safe access to Hawkes predictor
     */
    template<typename Func>
    auto withHawkesPredictor(Func&& func) const -> decltype(func(*hawkesPredictor_)) {
        std::lock_guard<std::mutex> lock(hawkesMutex_);
        if (hawkesEnabled_ && hawkesPredictor_) {
            return func(*hawkesPredictor_);
        }
        // Return default value for the return type
        using ReturnType = decltype(func(*hawkesPredictor_));
        if constexpr (std::is_same_v<ReturnType, void>) {
            return;
        } else {
            return ReturnType{};
        }
    }
    
    // Statistics helpers
    void updateDirectionStats(core::Direction direction) const;
    void updateTypeStats(const std::string& type) const;
    void updateAverages() const;
};

// Additional inline implementations for performance-critical methods

inline bool ImbalanceSignalGenerator::isHawkesEnhancementEnabled() const {
    return hawkesEnabled_.load();
}

} // namespace strategy
} // namespace trading