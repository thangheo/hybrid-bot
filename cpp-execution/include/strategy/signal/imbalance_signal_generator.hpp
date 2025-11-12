/**
 * @file imbalance_signal_generator.hpp
 * @brief Advanced generator for trading signals based on detected order book imbalances
 * 
 * @details
 * The Imbalance Signal Generator converts detected order book imbalances
 * into actionable trading signals using sophisticated market-aware algorithms.
 * Features dynamic parameter adjustment, regime-aware signal quality assessment,
 * and multi-factor signal strength calculation.
 * 
 * Created on: May 13, 2025
 * Refactored: September 24, 2025
 */
#pragma once

#include "strategy/types/imbalance_event.hpp"
#include "strategy/types/trading_signal.hpp"
#include "strategy/types/config_types.hpp"
#include "strategy/signal/momentum_correlation.hpp"
#include "strategy/signal/threshold_manager.hpp"
#include "core/trading_signal.hpp"
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <functional>
#include <atomic>
#include <array>

using namespace trading::core;

namespace trading {
namespace strategy {

// Forward declarations
class MarketRegimeDetector;
// class VolatilityEstimator;
class SignalPerformanceTracker;

// Callback type for signal generation events
using SignalCallback = std::function<void(const TradingSignal&)>;


/**
 * @brief Signal strength components for transparent calculation
 */
struct SignalStrengthComponents {
    double baseStrength = 0.0;
    double typeAdjustment = 1.0;
    double confidenceBoost = 1.0;
    double regimeAdjustment = 1.0;
    double momentumAlignment = 1.0;
    double finalStrength = 0.0;
    
    std::string toString() const;
};

/**
 * @brief Enhanced signal quality metrics
 */
struct SignalQualityMetrics {
    double strengthComponent = 0.0;
    double confidenceComponent = 0.0;
    double predictionComponent = 0.0;
    double correlationComponent = 0.0;
    double regimeConsistency = 0.0;
    double historicalPerformance = 0.0;
    double finalQuality = 0.0;
    
    std::string toString() const;
};

/**
 * @class ImbalanceSignalGenerator
 * @brief Advanced generator for trading signals from order book imbalances
 */
class ImbalanceSignalGenerator {
public:
    /**
     * @brief Default constructor
     */
    ImbalanceSignalGenerator();
    
    /**
     * @brief Constructor with configuration
     * @param config Signal generator configuration
     */
    explicit ImbalanceSignalGenerator(const SignalGeneratorConfig& config);
    
    /**
     * @brief Destructor
     */
    virtual ~ImbalanceSignalGenerator() = default;
    
    // Core functionality
    /**
     * @brief Initialize the signal generator with configuration
     * @param config Signal generator configuration
     * @return True if initialization successful, false otherwise
     */
    bool initialize(const SignalGeneratorConfig& config);
    
    /**
     * @brief Process newly detected imbalances
     * @param imbalances Vector of imbalance events to process
     * @return Number of signals generated
     */
    int processImbalances(const std::vector<ImbalanceEvent>& imbalances);
    
    /**
     * @brief Process a single imbalance event
     * @param imbalance Imbalance event to process
     * @return True if signal was generated, false otherwise
     */
    bool processImbalance(const ImbalanceEvent& imbalance);
    
    /**
     * @brief Generate trading signals from processed imbalances
     * @return Vector of generated trading signals
     */
    std::vector<TradingSignal> generateSignals();
    
    // Configuration and management
    /**
     * @brief Configure signal generation parameters
     * @param params Signal generation parameters
     */
    void configureSignalParameters(const SignalGeneratorConfig& params);
    
    /**
     * @brief Update parameters dynamically
     * @param params New parameters to apply
     */
    void updateParameters(const ParameterSet& params);
    
    /**
     * @brief Get current configuration
     * @return Current configuration
     */
    SignalGeneratorConfig getConfig() const;
    
    /**
     * @brief Get current state information
     * @return State information string
     */
    std::string getState() const;
    
    // Callback management
    /**
     * @brief Register a callback for signal generation
     * @param callback Function to call when signal is generated
     * @return Callback ID that can be used to unregister
     */
    size_t registerSignalCallback(const SignalCallback& callback);
    
    /**
     * @brief Unregister a previously registered callback
     * @param callbackId ID of the callback to unregister
     * @return True if callback was found and unregistered
     */
    bool unregisterSignalCallback(size_t callbackId);
    
    // State management
    /**
     * @brief Clear existing imbalance data and pending signals
     */
    void clear();
    
    /**
     * @brief Get the total count of signals generated
     * @return Count of signals generated since initialization
     */
    uint64_t getSignalCount() const;
    
    // Component management
    /**
     * @brief Set the momentum correlation component for signal enhancement
     * @param momentumCorrelation Pointer to momentum correlation component
     */
    void setMomentumCorrelation(MomentumCorrelationPtr momentumCorrelation);
    //setSignalDecayModel
    void setSignalDecayModel(SignalDecayModelPtr signalDecayModel);
    /**
     * @brief Set the threshold manager for adaptive thresholding
     * @param thresholdManager Pointer to threshold manager component
     */
    void setThresholdManager(ThresholdManagerPtr thresholdManager);
    
    /**
     * @brief Set market regime detector for adaptive behavior
     * @param regimeDetector Pointer to market regime detector
     */
    void setMarketRegimeDetector(std::shared_ptr<MarketRegimeDetector> regimeDetector);
    
    /**
     * @brief Set volatility estimator for dynamic calculations
     * @param volatilityEstimator Pointer to volatility estimator
     */
    // void setVolatilityEstimator(std::shared_ptr<VolatilityEstimator> volatilityEstimator);
    
    /**
     * @brief Set signal performance tracker for adaptive improvements
     * @param performanceTracker Pointer to performance tracker
     */
    void setSignalPerformanceTracker(std::shared_ptr<SignalPerformanceTracker> performanceTracker);
    /**
     * @brief Set the StrategyId to tag generated signals with
     * @param strategyId Strategy identifier (id + version)
     */
    void setStrategyId(const core::StrategyId& strategyId);

private:
  // volatilityEstimator_
    // Configuration and state
    SignalGeneratorConfig config_;
    std::vector<ImbalanceEvent> pendingImbalances_;
    std::vector<TradingSignal> generatedSignals_;
    std::atomic<uint64_t> signalCount_;
    
    // Associated components
    MomentumCorrelationPtr momentumCorrelation_;
    ThresholdManagerPtr thresholdManager_;
    std::shared_ptr<MarketRegimeDetector> regimeDetector_;
    // std::shared_ptr<VolatilityEstimator> volatilityEstimator_;
    std::shared_ptr<SignalPerformanceTracker> performanceTracker_;
    // signalDecayModel_
    SignalDecayModelPtr signalDecayModel_;
    // Callback management
    std::map<size_t, SignalCallback> callbacks_;
    size_t nextCallbackId_;
    // Separate mutex for protecting callbacks to avoid deadlocks when
    // notifySignalCallbacks is called while the main mutex_ is held.
    mutable std::mutex callbacksMutex_;
    
    // Thread safety
    mutable std::mutex mutex_;
    // Optional strategy identifier to tag generated signals. If empty/invalid,
    // generator will not override the signal's own value.
    core::StrategyId strategyId_;
    
    // Core signal generation methods
    /**
     * @brief Create a trading signal from an imbalance event
     * @param imbalance Source imbalance event
     * @return Generated trading signal
     */
    TradingSignal createSignalFromImbalance(
        const ImbalanceEvent& imbalance,
        MomentumCorrelationPtr momentumCopy = nullptr,
        ThresholdManagerPtr thresholdCopy = nullptr,
        std::shared_ptr<MarketRegimeDetector> regimeDetectorCopy = nullptr,
        std::shared_ptr<SignalPerformanceTracker> perfTrackerCopy = nullptr,
        SignalGeneratorConfig configCopy = SignalGeneratorConfig());
    
    /**
     * @brief Check if an imbalance passes signal generation filters
     * @param imbalance Imbalance event to check
     * @return True if imbalance passes filters
     */
    bool passesSignalFilters(const ImbalanceEvent& imbalance) const;
    
    /**
     * @brief Advanced signal strength calculation with market regime awareness
     * @param imbalance Source imbalance event
     * @return Signal strength components and final value
     */
    SignalStrengthComponents calculateSignalStrength(const ImbalanceEvent& imbalance) const;
    
    /**
     * @brief Calculate comprehensive signal quality metrics
     * @param signal Generated signal
     * @param predictedMove Momentum prediction
     * @param correlation Momentum correlation
     * @return Quality metrics and final score
     */
    SignalQualityMetrics calculateSignalQuality(
        const TradingSignal& signal, 
        double predictedMove, 
        double correlation
    ) const;
    
    /**
     * @brief Calculate adaptive target size based on multiple factors
     * @param imbalance Source imbalance
     * @param strength Signal strength
     * @param confidence Signal confidence
     * @param quality Signal quality
     * @param volatility Optional externally-provided volatility estimate (if <=0 the generator will
     *        fall back to searching for a supporting metric named "volatility" inside the
     *        ImbalanceEvent). This value is used to scale the target size conservatively.
     * @return Optimal target size as a fraction in [0.0, 1.0]. This represents the fraction
     *         of the USD notional (position size) the strategy should allocate for this signal —
     *         it is NOT an asset quantity.
     */
    double calculateTargetSize(
        const ImbalanceEvent& imbalance, 
        double strength, 
        double confidence, 
        double quality,
        double volatility = 0.0
    ) const;
    
    /**
     * @brief Calculate signal time-to-live with volatility adjustment
     * @param imbalance Source imbalance
     * @param deltaPrice Expected price movement
     * @return Signal TTL
     */
    std::chrono::milliseconds calculateSignalTTL(
        const ImbalanceEvent& imbalance, 
        double deltaPrice
    ) const;
    
    /**
     * @brief Generate sophisticated price targets
     * @param imbalance Source imbalance event
     * @return Vector of price targets with probabilities
     */
    std::vector<PriceTarget> generatePriceTargets(const ImbalanceEvent& imbalance) const;
    
    // Momentum and prediction handling
    /**
     * @brief Calculate confidence multiplier based on momentum correlation
     * @param absCorrelation Absolute correlation value
     * @param aligned Whether prediction aligns with signal
     * @return Confidence multiplier
     */
    double calculateConfidenceMultiplier(double absCorrelation, bool aligned) const;
    
    /**
     * @brief Check if momentum prediction aligns with signal direction
     * @param signalDirection Signal direction
     * @param predictedMove Predicted price movement
     * @return True if aligned
     */
    bool checkPredictionAlignment(core::Direction signalDirection, double predictedMove) const;
    
    /**
     * @brief Handle conflicts between signal and momentum prediction
     * @param signal Signal to modify
     * @param imbalance Source imbalance
     * @param predictedMove Predicted movement
     * @param correlation Momentum correlation
     */
    void handlePredictionConflict(
        TradingSignal& signal, 
        const ImbalanceEvent& imbalance,
        double predictedMove, 
        double correlation
    );
    
    // Market regime specific methods
    /**
     * @brief Get imbalance type scaling factor for current market regime
     * @param type Imbalance type
     * @param regime Current market regime
     * @return Scaling factor
     */
    double getImbalanceTypeScaling(ImbalanceType type, MarketRegimeType regime) const;
    
    /**
     * @brief Get regime-specific confidence adjustment
     * @param regime Current market regime
     * @param baseConfidence Base confidence level
     * @return Adjusted confidence
     */
    double getRegimeConfidenceAdjustment(MarketRegimeType regime, double baseConfidence) const;
    
    /**
     * @brief Determine current market regime
     * @param imbalance Source imbalance for context
     * @return Current market regime
     */
    MarketRegimeType getCurrentMarketRegime(const ImbalanceEvent& imbalance) const;
    
    // Utility methods
    /**
     * @brief Generate unique signal ID
     * @return Unique signal identifier
     */
    core::SignalId generateSignalId() const;
    
    /**
     * @brief Notify all registered callbacks about a new signal
     * @param signal Generated signal
     */
    void notifySignalCallbacks(const TradingSignal& signal);
    
    /**
     * @brief Get historical performance adjustment for signal type
     * @param type Imbalance type
     * @return Performance-based adjustment factor
     */
    double getHistoricalPerformanceAdjustment(ImbalanceType type) const;
    
    // Static lookup tables for regime-aware scaling
    static const std::array<std::array<double, 7>, 19> REGIME_TYPE_SCALING_MATRIX;
    static const std::array<double, 7> REGIME_BASE_ADJUSTMENTS;
    // static const std::array<std::string, 7> REGIME_NAMES;
};

// Type aliases
using ImbalanceSignalGeneratorPtr = std::shared_ptr<ImbalanceSignalGenerator>;
using ImbalanceSignalGeneratorConstPtr = std::shared_ptr<const ImbalanceSignalGenerator>;

} // namespace strategy
} // namespace trading