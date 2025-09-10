/**
 * @file signal_decay_model.hpp
 * @brief Models decay of trading signal relevance over time
 * 
 * @details
 * This component implements various decay models to represent how
 * trading signals lose strength and relevance over time.
 * 
 * Created on: May 6, 2025
 */
#pragma once

#include "strategy/types.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <memory>

namespace trading {
namespace strategy {

/**
 * @brief Enumeration of available decay model types
 */
enum class DecayModelType {
    EXPONENTIAL,      // Exponential decay function
    LINEAR,           // Linear decay function
    STEP,             // Step-wise decay function
    CONDITIONAL,      // Conditional decay based on market events
    CUSTOM            // Custom decay function
};

/**
 * @brief Outcome of a historical trading signal
 */
struct SignalOutcome {
    TradingSignal signal;           // The original signal
    bool executed;                   // Whether the signal was acted upon
    bool reached_target;            // Whether price reached target
    uint64_t outcome_time;          // When the outcome was determined
    double price_movement;          // Actual price movement that occurred
    double profit_loss;             // P&L if the signal was traded
};

/**
 * @brief Parameters for signal decay models
 */
struct DecayParameters {
    double base_decay_rate;            // Base rate of decay (per time unit)
    uint64_t time_unit_ms;             // Milliseconds per time unit
    std::vector<double> step_intervals; // Intervals for step decay
    std::vector<double> step_values;    // Values for step decay
    uint64_t min_ttl_ms;               // Minimum signal lifetime 
    double min_valid_strength;         // Minimum strength to be considered valid
};


/**
 * @brief Market event types that could affect signal validity
 */
enum class MarketEventType {
    PRICE_SPIKE,      // Sudden price movement
    LARGE_TRADE,      // Unusually large trade execution
    LIQUIDITY_CHANGE, // Significant change in order book liquidity
    TREND_CHANGE,     // Change in market trend
    NEWS_RELEASE,     // Market-relevant news
    EXCHANGE_EVENT    // Exchange-specific event (maintenance, etc.)
};

/**
 * @brief Market event data
 */
using MarketEventData = std::map<std::string, double>;

/**
 * @brief Market event information
 */
struct MarketEvent {
    MarketEventType type;         // Event type
    MarketEventData data;         // Event data
    uint64_t timestamp;           // Event timestamp
    ExchangeId exchangeId;      // Exchange identifier
    core::Symbol tradingPair;     // Trading pair
};


/**
 * @class SignalDecayModel
 * @brief Models how trading signals lose strength over time
 */
class SignalDecayModel {
public:
    /**
     * @brief Constructor
     * @param model_type The type of decay model to use
     */
    explicit SignalDecayModel(DecayModelType model_type = DecayModelType::EXPONENTIAL);
    SignalDecayModel(const SignalDecayConfig& config); 

    /**
     * @brief Destructor
     */
    virtual ~SignalDecayModel() = default;
    
    /**
     * @brief Configure the decay parameters
     * @param params Decay configuration parameters
     */
    virtual void configureDecayParameters(const DecayParameters& params);
    
    /**
     * @brief Calculate remaining signal strength based on time elapsed
     * @param signal The original trading signal
     * @param current_time Current timestamp
     * @return Remaining signal strength (0.0-1.0)
     */
    virtual double calculateRemainingStrength(const TradingSignal& signal, uint64_t current_time);
    
    /**
     * @brief Check if a signal is still valid
     * @param signal The trading signal to check
     * @param current_time Current timestamp
     * @return True if the signal is still valid
     */
    virtual bool isSignalValid(const TradingSignal& signal, uint64_t current_time);
    
    /**
     * @brief Set a custom decay function
     * @param decay_func Custom function that calculates remaining strength
     */
    void setCustomDecayFunction(std::function<double(const TradingSignal&, uint64_t)> decay_func);
    
    /**
     * @brief Get the current decay model type
     * @return The decay model type
     */
    DecayModelType getModelType() const;
    
    /**
     * @brief Set the decay model type
     * @param model_type The decay model to use
     */
    void setModelType(DecayModelType model_type);
    
    /**
     * @brief Register a market event that could invalidate signals
     * @param event_type Type of market event
     * @param event_data Additional event data
     */
    void registerMarketEvent(MarketEventType event_type, const MarketEventData& event_data);
    
    /**
     * @brief Calibrate decay rates based on historical signal performance
     * @param signal_history Vector of historical signals with outcomes
     */
    void calibrateFromHistory(const std::vector<SignalOutcome>& signal_history);
    // updateParameters
    /**
     * @brief Update parameters for the decay model
     * @param params New parameters
     */
    void updateParameters(const ParameterSet& params);
    // calculateDecayRate
    /**
     * @brief Calculate decay rate for a specific imbalance type
     * @param imbalance_type The type of imbalance
     * @return Decay rate for this type of signal
     */
    double calculateDecayRate(ImbalanceType imbalance_type) const;
    // getSignalTTL
    /**
     * @brief Get the time-to-live (TTL) for a specific imbalance type
     * @param imbalance_type The type of imbalance
     * @return TTL in milliseconds
     */
    uint64_t getSignalTTL(ImbalanceType imbalance_type) const;
private:
    DecayModelType model_type_;
    DecayParameters params_;
    std::function<double(const TradingSignal&, uint64_t)> custom_decay_func_;
    std::map<ImbalanceType, double> type_specific_decay_rates_;
    std::map<ImbalanceType, uint64_t> type_specific_ttl_;
    std::vector<MarketEvent> recent_market_events_;
    double volatility_multiplier_ = 1.0;
    void updateVolatilityAdjustment(double current_volatility);
    /**
     * @brief Calculate exponential decay
     * @param signal The trading signal
     * @param current_time Current timestamp
     * @return Remaining signal strength
     */
    double exponentialDecay(const TradingSignal& signal, uint64_t current_time);
    std::map<core::ExchangeId, double> exchange_decay_multipliers_;
    bool validateConfiguration() const;

    /**
     * @brief Calculate linear decay
     * @param signal The trading signal
     * @param current_time Current timestamp
     * @return Remaining signal strength
     */
    double linearDecay(const TradingSignal& signal, uint64_t current_time);
    
    /**
     * @brief Calculate step-wise decay
     * @param signal The trading signal
     * @param current_time Current timestamp
     * @return Remaining signal strength
     */
    double stepDecay(const TradingSignal& signal, uint64_t current_time);
    
    /**
     * @brief Calculate conditional decay based on market events
     * @param signal The trading signal
     * @param current_time Current timestamp
     * @return Remaining signal strength
     */
    double conditionalDecay(const TradingSignal& signal, uint64_t current_time);
    
    /**
     * @brief Check if market events invalidate the signal
     * @param signal The trading signal
     * @return True if market events invalidate the signal
     */
    bool isInvalidatedByMarketEvents(const TradingSignal& signal);
    
    /**
     * @brief Get decay rate specific to the signal's imbalance type
     * @param signal The trading signal
     * @return Decay rate for this type of signal
     */
    double getTypeSpecificDecayRate(const TradingSignal& signal);
};

} // namespace strategy
} // namespace trading