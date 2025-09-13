/**
 * @file pavlovian_credit_assignment.hpp
 * @brief Pavlovian Temporal Credit Assignment Algorithm implementation for trading system
 * 
 * This implements the PTCA algorithm from the paper to learn which events
 * contribute positively or negatively to trading outcomes in real-time.
 * 
 * @author Trading System Team
 * @date September 13, 2025
 */

#pragma once

#include "core/time.hpp"
#include "core/market_types.hpp"
#include "strategy/types.hpp"
#include "strategy/types/imbalance_type.hpp"
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <mutex>
#include <fstream>
#include <nlohmann/json.hpp>

namespace trading {
namespace learning {

/**
 * Event types that can be tracked by PTCA
 */
enum class EventType {
    VOLUME_IMBALANCE_SIGNAL,
    PRICE_LEVEL_SIGNAL,
    LIQUIDITY_REMOVAL_SIGNAL,
    ORDER_FLOW_SIGNAL,
    LARGE_ORDER_SIGNAL,
    SPREAD_CHANGE_SIGNAL,
    MARKET_DEPTH_SIGNAL,
    CUSTOM_SIGNAL
};

/**
 * Individual event data point for PTCA learning
 */
struct LearningEvent {
    EventType type;
    std::string eventId;
    core::Timestamp timestamp;
    bool isActive;              // Binary event state (0 or 1)
    double rawScore;            // Raw score/strength of the event
    core::Symbol symbol;        // Trading pair
    core::ExchangeId exchange;  // Exchange
    core::Direction direction;  // Buy/sell direction
    
    LearningEvent() 
        : type(EventType::CUSTOM_SIGNAL), isActive(false), rawScore(0.0) {}
        
    LearningEvent(EventType t, const std::string& id, bool active, double score)
        : type(t), eventId(id), timestamp(core::now()), isActive(active), rawScore(score) {}
};

/**
 * Outcome measurement for PTCA reinforcement
 */
struct OutcomeMeasure {
    core::Timestamp timestamp;
    double value;               // Outcome value (e.g., P&L, price movement, success rate)
    std::string outcomeType;    // Type of outcome being measured
    core::Symbol symbol;        // Associated symbol
    
    OutcomeMeasure() : value(0.0) {}
    OutcomeMeasure(double val, const std::string& type) 
        : timestamp(core::now()), value(val), outcomeType(type) {}
};

/**
 * PTCA configuration parameters
 */
struct PTCAConfig {
    // Core parameters
    size_t lookbackWindow = 50;         // n in the algorithm
    double temporalDecayFactor = 0.95;  // γ (gamma) 
    double weakThreshold = 0.1;         // θ_weak
    double strongThreshold = 0.3;       // θ_strong
    
    // Learning parameters  
    double baseLearningRate = 0.1;      // α_0 base learning rate
    double extinctionLambda = 0.01;     // λ extinction rate
    double competitiveInhibition = 0.05; // β competitive inhibition
    double adaptiveLearningFactor = 0.1; // ρ adaptive learning
    
    // Persistence parameters
    std::string saveFilePath = "ptca_model.json";
    std::chrono::minutes autoSaveInterval{30};
    bool enableAutoSave = true;
    
    // Validation
    bool isValid() const {
        return lookbackWindow > 0 && 
               temporalDecayFactor > 0.0 && temporalDecayFactor <= 1.0 &&
               weakThreshold > 0.0 && strongThreshold > weakThreshold &&
               baseLearningRate > 0.0 && baseLearningRate <= 1.0;
    }
};

/**
 * Event learning statistics and accumulated scores
 */
struct EventStats {
    double accumulatedScore = 0.0;      // A(i,t) in algorithm
    double learningRate = 0.1;          // α(i) current learning rate  
    size_t conditioningCount = 0;       // Number of times reinforced
    core::Timestamp lastReinforcement; // Last time this event was reinforced
    std::vector<double> recentScores;   // Historical scores for weighted calculation
    std::vector<core::Timestamp> scoreTimes; // Timestamps for recent scores
    
    // Performance tracking
    double totalPositiveReinforcement = 0.0;
    double totalNegativeReinforcement = 0.0;
    size_t totalActivations = 0;
    
    EventStats() : lastReinforcement(core::now()) {}
};

/**
 * Main Pavlovian Temporal Credit Assignment class
 */
class PavlovianCreditAssignment {
public:
    /**
     * Constructor with configuration
     */
    explicit PavlovianCreditAssignment(const PTCAConfig& config = PTCAConfig{});
    
    /**
     * Destructor - saves model if auto-save enabled
     */
    ~PavlovianCreditAssignment();
    
    /**
     * Add a new timestep with events and outcome
     * This is the core learning function
     */
    void addTimestep(const std::vector<LearningEvent>& events, 
                    const OutcomeMeasure& outcome);
    
    /**
     * Add events only (outcome will be provided later)
     */
    void addEvents(const std::vector<LearningEvent>& events);
    
    /**
     * Add outcome for the most recent events
     */
    void addOutcome(const OutcomeMeasure& outcome);
    
    /**
     * Get current accumulated score for an event type
     */
    double getEventScore(const std::string& eventId) const;
    
    /**
     * Get all event scores as a map
     */
    std::map<std::string, double> getAllEventScores() const;
    
    /**
     * Get event statistics for detailed analysis
     */
    EventStats getEventStats(const std::string& eventId) const;
    
    /**
     * Get recommendation for a trading signal based on learned patterns
     * Returns confidence score (-1.0 to 1.0) for the signal
     */
    double getSignalRecommendation(const strategy::TradingSignal& signal) const;
    
    /**
     * Generate trading decision based on current market state and learned patterns
     */
    struct TradingDecision {
        core::Direction recommendedDirection = core::Direction::NEUTRAL;
        double confidence = 0.0;            // Confidence in the decision (-1.0 to 1.0)
        double expectedOutcome = 0.0;       // Expected outcome value
        std::string reasoning;              // Human-readable reasoning
        std::map<std::string, double> eventContributions; // Individual event contributions
    };
    
    TradingDecision generateTradingDecision(const core::Symbol& symbol,
                                          const std::vector<LearningEvent>& currentEvents) const;
    
    /**
     * Save learned model to file
     */
    bool saveModel(const std::string& filePath = "") const;
    
    /**
     * Load learned model from file
     */
    bool loadModel(const std::string& filePath = "");
    
    /**
     * Reset all learned parameters
     */
    void reset();
    
    /**
     * Get configuration
     */
    const PTCAConfig& getConfig() const { return config_; }
    
    /**
     * Update configuration (will reset if major parameters change)
     */
    void updateConfig(const PTCAConfig& newConfig);
    
    /**
     * Get learning statistics
     */
    struct LearningStats {
        size_t totalTimesteps = 0;
        size_t totalEvents = 0;
        size_t totalOutcomes = 0;
        double averageReinforcement = 0.0;
        core::Timestamp lastUpdate;
        std::map<std::string, size_t> eventTypeCounts;
    };
    
    LearningStats getLearningStats() const;

private:
    PTCAConfig config_;
    std::map<std::string, EventStats> eventStats_;
    std::vector<LearningEvent> currentEvents_;  // Events waiting for outcome
    size_t totalTimesteps_;
    core::Timestamp lastAutoSave_;
    mutable std::recursive_mutex mutex_;
    
    /**
     * Calculate reinforcement strength based on outcome value
     */
    double calculateReinforcementStrength(double outcomeValue) const;
    
    /**
     * Calculate weighted historical score for an event
     */
    double calculateWeightedHistoricalScore(const EventStats& stats, 
                                          core::Timestamp currentTime) const;
    
    /**
     * Update event statistics with reinforcement
     */
    void updateEventStats(const std::string& eventId, 
                         const LearningEvent& event,
                         double reinforcement,
                         double weightedScore);
    
    /**
     * Apply extinction learning to inactive events
     */
    void applyExtinctionLearning(core::Timestamp currentTime);
    
    /**
     * Apply competitive inhibition
     */
    void applyCompetitiveInhibition(const std::vector<LearningEvent>& activeEvents,
                                   double reinforcementMagnitude);
    
    /**
     * Update adaptive learning rates
     */
    void updateAdaptiveLearningRates();
    
    /**
     * Auto-save model if needed
     */
    void checkAutoSave();
    
    /**
     * Convert trading signal to learning event
     */
    LearningEvent signalToLearningEvent(const strategy::TradingSignal& signal) const;
    
    /**
     * Convert imbalance type to event type
     */
    EventType imbalanceTypeToEventType(strategy::ImbalanceType imbalanceType) const;
    
    /**
     * Convert event type to string for serialization
     */
    std::string eventTypeToString(EventType type) const;
    
    /**
     * Convert string to event type for deserialization
     */
    EventType eventTypeFromString(const std::string& str) const;
};

/**
 * Factory function to create PTCA with default trading-optimized configuration
 */
std::unique_ptr<PavlovianCreditAssignment> createTradingPTCA(
    const std::string& modelPath = "trading_ptca_model.json");

/**
 * Helper function to create learning events from trading signals
 */
std::vector<LearningEvent> createLearningEventsFromSignals(
    const std::vector<strategy::TradingSignal>& signals);

/**
 * Helper function to create outcome measure from P&L data
 */
OutcomeMeasure createPnLOutcome(double pnlValue, const core::Symbol& symbol);

/**
 * Helper function to create outcome measure from price movement
 */
OutcomeMeasure createPriceMovementOutcome(double priceChange, 
                                        const core::Symbol& symbol,
                                        std::chrono::milliseconds timeframe);

} // namespace learning
} // namespace trading
