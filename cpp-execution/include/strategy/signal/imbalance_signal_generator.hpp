/**
 * @file imbalance_signal_generator.hpp
 * @brief Generator for trading signals based on detected order book imbalances
 * 
 * @details
 * The Imbalance Signal Generator converts detected order book imbalances
 * into actionable trading signals. It evaluates the strength, direction,
 * and confidence of imbalance events to generate signals with appropriate
 * parameters for trading decisions.
 * 
 * Created on: May 13, 2025
 */
#pragma once

#include "strategy/types/imbalance_event.hpp"
#include "strategy/types/trading_signal.hpp"
#include "strategy/types/config_types.hpp"
#include "strategy/signal/momentum_correlation.hpp"
#include "strategy/signal/threshold_manager.hpp"
#include <vector>
#include <memory>
#include <map>
#include <mutex>
#include <functional>
#include <atomic>

namespace trading {
namespace strategy {

// Callback type for signal generation events
using SignalCallback = std::function<void(const TradingSignal&)>;

/**
 * @class ImbalanceSignalGenerator
 * @brief Generates trading signals from detected order book imbalances
 */
class ImbalanceSignalGenerator {
public:
    /**
     * @brief Constructor
     */
    ImbalanceSignalGenerator();
    ImbalanceSignalGenerator(const SignalGeneratorConfig& config);
    // ImbalanceSignalGenerator(const ImbalanceSignalGeneratorConfig& config);
    
    /**
     * @brief Destructor
     */
    virtual ~ImbalanceSignalGenerator() = default;
    
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
    
    /**
     * @brief Configure signal generation parameters
     * @param params Signal generation parameters
     */
    void configureSignalParameters(const SignalGeneratorConfig& params);
    
    /**
     * @brief Register a callback for signal generation
     * @param callback Function to call when signal is generated
     * @return Callback ID that can be used to unregister
     */
    size_t registerSignalCallback(const SignalCallback& callback);
    
    /**
     * @brief Unregister a previously registered callback
     * @param callbackId ID of the callback to unregister
     * @return True if callback was found and unregistered, false otherwise
     */
    bool unregisterSignalCallback(size_t callbackId);
    
    /**
     * @brief Clear existing imbalance data and pending signals
     */
    void clear();
    
    /**
     * @brief Get the total count of signals generated
     * @return Count of signals generated since initialization
     */
    uint64_t getSignalCount() const;
    
    /**
     * @brief Set the momentum correlation component for signal enhancement
     * @param momentum_correlation Pointer to momentum correlation component
     */
    void setMomentumCorrelation(MomentumCorrelationPtr momentum_correlation);
    
    /**
     * @brief Set the threshold manager for adaptive thresholding
     * @param threshold_manager Pointer to threshold manager component
     */
    void setThresholdManager(ThresholdManagerPtr threshold_manager);
  //updateParameters
    /**
     * @brief Update parameters for the signal generator
     * @param params New parameters to apply
     */
    void updateParameters(const ParameterSet& params);
    
    /**
     * @brief Get the current configuration of the signal generator
     * @return Current configuration
     */
    const SignalGeneratorConfig& getConfig() const;
    
    /**
     * @brief Get the current state of the signal generator
     * @return Current state
     */
    std::string getState() const;
private:
    // Configuration
    SignalGeneratorConfig config_;
    
    // State
    std::vector<ImbalanceEvent> pendingImbalances_;
    std::vector<TradingSignal> generatedSignals_;
    std::atomic<uint64_t> signalCount_;
    
    // Associated components
    MomentumCorrelationPtr momentumCorrelation_;
    ThresholdManagerPtr thresholdManager_;
    
    // Signal generation methods
    TradingSignal createSignalFromImbalance(const ImbalanceEvent& imbalance);
    bool passesSignalFilters(const ImbalanceEvent& imbalance) const;
    double calculateSignalStrength(const ImbalanceEvent& imbalance) const;
    std::vector<PriceTarget> generatePriceTargets(const ImbalanceEvent& imbalance) const;
    core::SignalId generateSignalId() const;
    
    // Callback management
    std::map<size_t, SignalCallback> callbacks_;
    size_t nextCallbackId_;
    void notifySignalCallbacks(const TradingSignal& signal);
    
    // Thread safety
    mutable std::mutex mutex_;
};

// Type alias
using ImbalanceSignalGeneratorPtr = std::shared_ptr<ImbalanceSignalGenerator>;
using ImbalanceSignalGeneratorConstPtr = std::shared_ptr<const ImbalanceSignalGenerator>;

} // namespace strategy
} // namespace trading