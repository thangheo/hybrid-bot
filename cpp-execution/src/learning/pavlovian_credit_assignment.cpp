/**
 * @file pavlovian_credit_assignment.cpp
 * @brief Implementation of Pavlovian Temporal Credit Assignment Algorithm
 * 
 * @author Trading System Team
 * @date September 13, 2025
 */

#include "learning/pavlovian_credit_assignment.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>
#include <fstream>
#include <sstream>
#include <set>

using json = nlohmann::json;

namespace trading {
namespace learning {

PavlovianCreditAssignment::PavlovianCreditAssignment(const PTCAConfig& config)
    : config_(config), totalTimesteps_(0), lastAutoSave_(core::now()) {
    
    if (!config_.isValid()) {
        throw std::invalid_argument("Invalid PTCA configuration provided");
    }
    
    // Try to load existing model if path exists
    if (!config_.saveFilePath.empty()) {
        loadModel(config_.saveFilePath);
    }
}

PavlovianCreditAssignment::~PavlovianCreditAssignment() {
    if (config_.enableAutoSave && !config_.saveFilePath.empty()) {
        saveModel(config_.saveFilePath);
    }
}

void PavlovianCreditAssignment::addTimestep(const std::vector<LearningEvent>& events, 
                                           const OutcomeMeasure& outcome) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    totalTimesteps_++;
    
    // Calculate reinforcement strength from outcome
    double reinforcement = calculateReinforcementStrength(outcome.value);
    
    // Skip processing if reinforcement is zero (neutral outcome)
    if (std::abs(reinforcement) < 1e-9) {
        return;
    }
    
    // Create active event set for competitive inhibition
    std::vector<LearningEvent> activeEvents;
    std::copy_if(events.begin(), events.end(), std::back_inserter(activeEvents),
                [](const LearningEvent& e) { return e.isActive; });
    
    // Process each event
    for (const auto& event : events) {
        const std::string& eventId = event.eventId;
        
        // Initialize event stats if not exists
        if (eventStats_.find(eventId) == eventStats_.end()) {
            eventStats_[eventId] = EventStats{};
            eventStats_[eventId].learningRate = config_.baseLearningRate;
        }
        
        EventStats& stats = eventStats_[eventId];
        
        if (event.isActive) {
            // Calculate weighted historical score
            double weightedScore = calculateWeightedHistoricalScore(stats, outcome.timestamp);
            
            // Update event statistics with reinforcement
            updateEventStats(eventId, event, reinforcement, weightedScore);
        }
        
        // Store recent score for future weighted calculations
        stats.recentScores.push_back(event.rawScore);
        stats.scoreTimes.push_back(event.timestamp);
        
        // Keep only recent scores within lookback window
        while (stats.recentScores.size() > config_.lookbackWindow) {
            stats.recentScores.erase(stats.recentScores.begin());
            stats.scoreTimes.erase(stats.scoreTimes.begin());
        }
        
        stats.totalActivations++;
    }
    
    // Apply extinction learning to all events
    applyExtinctionLearning(outcome.timestamp);
    
    // Apply competitive inhibition
    if (!activeEvents.empty()) {
        applyCompetitiveInhibition(activeEvents, std::abs(reinforcement));
    }
    
    // Update adaptive learning rates
    updateAdaptiveLearningRates();
    
    // Check if auto-save is needed
    checkAutoSave();
}

void PavlovianCreditAssignment::addEvents(const std::vector<LearningEvent>& events) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    currentEvents_ = events;
}

void PavlovianCreditAssignment::addOutcome(const OutcomeMeasure& outcome) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!currentEvents_.empty()) {
        addTimestep(currentEvents_, outcome);
        currentEvents_.clear();
    }
}

double PavlovianCreditAssignment::getEventScore(const std::string& eventId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = eventStats_.find(eventId);
    return (it != eventStats_.end()) ? it->second.accumulatedScore : 0.0;
}

std::map<std::string, double> PavlovianCreditAssignment::getAllEventScores() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    std::map<std::string, double> scores;
    for (const auto& [eventId, stats] : eventStats_) {
        scores[eventId] = stats.accumulatedScore;
    }
    return scores;
}

EventStats PavlovianCreditAssignment::getEventStats(const std::string& eventId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = eventStats_.find(eventId);
    return (it != eventStats_.end()) ? it->second : EventStats{};
}

double PavlovianCreditAssignment::getSignalRecommendation(const strategy::TradingSignal& signal) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    LearningEvent learningEvent = signalToLearningEvent(signal);
    auto it = eventStats_.find(learningEvent.eventId);
    
    if (it == eventStats_.end()) {
        return 0.0; // No data, neutral recommendation
    }
    
    const EventStats& stats = it->second;
    
    // Base recommendation on accumulated score
    double baseScore = stats.accumulatedScore;
    
    // Apply signal strength weighting
    double weightedScore = baseScore * signal.strength * signal.confidence;
    
    // Apply direction adjustment
    if (signal.direction == core::Direction::SELL) {
        weightedScore = -weightedScore;
    }
    
    // Normalize to [-1, 1] range using tanh
    return std::tanh(weightedScore);
}

PavlovianCreditAssignment::TradingDecision 
PavlovianCreditAssignment::generateTradingDecision(const core::Symbol& symbol,
                                                  const std::vector<LearningEvent>& currentEvents) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    TradingDecision decision;
    decision.eventContributions.clear();
    
    double totalBuyScore = 0.0;
    double totalSellScore = 0.0;
    double totalWeight = 0.0;
    std::vector<std::string> activeReasons;
    
    for (const auto& event : currentEvents) {
        if (!event.isActive) continue;
        
        auto it = eventStats_.find(event.eventId);
        if (it == eventStats_.end()) continue;
        
        const EventStats& stats = it->second;
        double eventScore = stats.accumulatedScore;
        double weight = event.rawScore;
        
        // Store individual contribution
        decision.eventContributions[event.eventId] = eventScore * weight;
        
        // Accumulate weighted scores
        if (event.direction == core::Direction::BUY) {
            totalBuyScore += eventScore * weight;
            if (eventScore > 0.1) {
                activeReasons.push_back("Positive " + eventTypeToString(event.type) + " signal");
            }
        } else if (event.direction == core::Direction::SELL) {
            totalSellScore += eventScore * weight;
            if (eventScore > 0.1) {
                activeReasons.push_back("Positive " + eventTypeToString(event.type) + " signal");
            }
        }
        
        totalWeight += std::abs(weight);
    }
    
    // Calculate net score and confidence
    double netScore = totalBuyScore - totalSellScore;
    if (totalWeight > 0.0) {
        netScore /= totalWeight;
    }
    
    decision.expectedOutcome = netScore;
    decision.confidence = std::abs(netScore);
    
    // Determine direction based on score
    const double decisionThreshold = 0.1;
    if (netScore > decisionThreshold) {
        decision.recommendedDirection = core::Direction::BUY;
    } else if (netScore < -decisionThreshold) {
        decision.recommendedDirection = core::Direction::SELL;
    } else {
        decision.recommendedDirection = core::Direction::NEUTRAL;
    }
    
    // Generate reasoning text
    if (decision.recommendedDirection != core::Direction::NEUTRAL) {
        std::ostringstream reasoning;
        reasoning << "PTCA recommends " << core::directionToString(decision.recommendedDirection) 
                 << " with confidence " << std::fixed << std::setprecision(2) << decision.confidence;
        if (!activeReasons.empty()) {
            reasoning << " based on: ";
            for (size_t i = 0; i < activeReasons.size(); ++i) {
                if (i > 0) reasoning << ", ";
                reasoning << activeReasons[i];
            }
        }
        decision.reasoning = reasoning.str();
    } else {
        decision.reasoning = "PTCA suggests holding - insufficient signal strength";
    }
    
    return decision;
}

bool PavlovianCreditAssignment::saveModel(const std::string& filePath) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    std::string path = filePath.empty() ? config_.saveFilePath : filePath;
    if (path.empty()) return false;
    
    try {
        json modelData;
        
        // Save configuration
        json configJson;
        configJson["lookbackWindow"] = config_.lookbackWindow;
        configJson["temporalDecayFactor"] = config_.temporalDecayFactor;
        configJson["weakThreshold"] = config_.weakThreshold;
        configJson["strongThreshold"] = config_.strongThreshold;
        configJson["baseLearningRate"] = config_.baseLearningRate;
        configJson["extinctionLambda"] = config_.extinctionLambda;
        configJson["competitiveInhibition"] = config_.competitiveInhibition;
        configJson["adaptiveLearningFactor"] = config_.adaptiveLearningFactor;
        modelData["config"] = configJson;
        
        // Save event statistics
        json eventStatsJson;
        for (const auto& [eventId, stats] : eventStats_) {
            json statsJson;
            statsJson["accumulatedScore"] = stats.accumulatedScore;
            statsJson["learningRate"] = stats.learningRate;
            statsJson["conditioningCount"] = stats.conditioningCount;
            statsJson["lastReinforcement"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                stats.lastReinforcement.time_since_epoch()).count();
            statsJson["totalPositiveReinforcement"] = stats.totalPositiveReinforcement;
            statsJson["totalNegativeReinforcement"] = stats.totalNegativeReinforcement;
            statsJson["totalActivations"] = stats.totalActivations;
            
            eventStatsJson[eventId] = statsJson;
        }
        modelData["eventStats"] = eventStatsJson;
        
        // Save metadata
        modelData["totalTimesteps"] = totalTimesteps_;
        modelData["lastSave"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            core::now().time_since_epoch()).count();
        modelData["version"] = "1.0";
        
        // Write to file
        std::ofstream file(path);
        file << modelData.dump(2);
        file.close();
        
        return true;
        
    } catch (const std::exception& e) {
        // Log error but don't throw
        return false;
    }
}

bool PavlovianCreditAssignment::loadModel(const std::string& filePath) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    std::string path = filePath.empty() ? config_.saveFilePath : filePath;
    if (path.empty()) return false;
    
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;
        
        json modelData;
        file >> modelData;
        file.close();
        
        // Load event statistics
        if (modelData.contains("eventStats")) {
            eventStats_.clear();
            
            for (const auto& [eventId, statsJson] : modelData["eventStats"].items()) {
                EventStats stats;
                stats.accumulatedScore = statsJson.value("accumulatedScore", 0.0);
                stats.learningRate = statsJson.value("learningRate", config_.baseLearningRate);
                stats.conditioningCount = statsJson.value("conditioningCount", 0);
                
                if (statsJson.contains("lastReinforcement")) {
                    auto ms = std::chrono::milliseconds(statsJson["lastReinforcement"]);
                    stats.lastReinforcement = core::Timestamp(ms);
                }
                
                stats.totalPositiveReinforcement = statsJson.value("totalPositiveReinforcement", 0.0);
                stats.totalNegativeReinforcement = statsJson.value("totalNegativeReinforcement", 0.0);
                stats.totalActivations = statsJson.value("totalActivations", 0);
                
                eventStats_[eventId] = stats;
            }
        }
        
        // Load metadata
        totalTimesteps_ = modelData.value("totalTimesteps", 0);
        
        return true;
        
    } catch (const std::exception& e) {
        return false;
    }
}

void PavlovianCreditAssignment::reset() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    eventStats_.clear();
    currentEvents_.clear();
    totalTimesteps_ = 0;
    lastAutoSave_ = core::now();
}

void PavlovianCreditAssignment::updateConfig(const PTCAConfig& newConfig) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (!newConfig.isValid()) {
        throw std::invalid_argument("Invalid PTCA configuration provided");
    }
    
    // Check if major parameters changed (requires reset)
    bool needsReset = (config_.lookbackWindow != newConfig.lookbackWindow ||
                      config_.temporalDecayFactor != newConfig.temporalDecayFactor);
    
    config_ = newConfig;
    
    if (needsReset) {
        reset();
    }
}

PavlovianCreditAssignment::LearningStats PavlovianCreditAssignment::getLearningStats() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    LearningStats stats;
    stats.totalTimesteps = totalTimesteps_;
    stats.totalEvents = eventStats_.size();
    stats.lastUpdate = core::now();
    
    double totalReinforcement = 0.0;
    size_t totalOutcomes = 0;
    
    for (const auto& [eventId, eventStats] : eventStats_) {
        stats.eventTypeCounts[eventId] = eventStats.totalActivations;
        totalReinforcement += eventStats.totalPositiveReinforcement + 
                             std::abs(eventStats.totalNegativeReinforcement);
        totalOutcomes += eventStats.conditioningCount;
    }
    
    stats.totalOutcomes = totalOutcomes;
    stats.averageReinforcement = (totalOutcomes > 0) ? totalReinforcement / totalOutcomes : 0.0;
    
    return stats;
}

// Private methods implementation

double PavlovianCreditAssignment::calculateReinforcementStrength(double outcomeValue) const {
    if (outcomeValue >= config_.strongThreshold) {
        return 2.0;
    } else if (outcomeValue >= config_.weakThreshold) {
        return 1.0;
    } else if (outcomeValue <= -config_.strongThreshold) {
        return -2.0;
    } else if (outcomeValue <= -config_.weakThreshold) {
        return -1.0;
    } else {
        return 0.0;
    }
}

double PavlovianCreditAssignment::calculateWeightedHistoricalScore(
    const EventStats& stats, core::Timestamp currentTime) const {
    
    if (stats.recentScores.empty()) {
        return 0.0;
    }
    
    double weightedSum = 0.0;
    double totalWeight = 0.0;
    
    for (size_t i = 0; i < stats.recentScores.size(); ++i) {
        auto timeDiff = currentTime - stats.scoreTimes[i];
        auto steps = std::chrono::duration_cast<std::chrono::seconds>(timeDiff).count();
        
        double weight = std::pow(config_.temporalDecayFactor, steps);
        weightedSum += stats.recentScores[i] * weight;
        totalWeight += weight;
    }
    
    return (totalWeight > 0.0) ? weightedSum / totalWeight : 0.0;
}

void PavlovianCreditAssignment::updateEventStats(const std::string& eventId,
                                                const LearningEvent& event,
                                                double reinforcement,
                                                double weightedScore) {
    EventStats& stats = eventStats_[eventId];
    
    // Update accumulated score: A(i,t) = A(i,t-1) + R(t) × W(i,t) × α(i)
    stats.accumulatedScore += reinforcement * weightedScore * stats.learningRate;
    
    // Update conditioning statistics
    stats.conditioningCount++;
    stats.lastReinforcement = event.timestamp;
    
    if (reinforcement > 0) {
        stats.totalPositiveReinforcement += reinforcement;
    } else {
        stats.totalNegativeReinforcement += reinforcement;
    }
}

void PavlovianCreditAssignment::applyExtinctionLearning(core::Timestamp currentTime) {
    for (auto& [eventId, stats] : eventStats_) {
        auto timeSinceReinforcement = currentTime - stats.lastReinforcement;
        auto stepsSince = std::chrono::duration_cast<std::chrono::seconds>(timeSinceReinforcement).count();
        
        if (stepsSince > static_cast<int64_t>(config_.lookbackWindow)) {
            double extinctionFactor = std::exp(-config_.extinctionLambda * stepsSince);
            stats.accumulatedScore *= extinctionFactor;
        }
    }
}

void PavlovianCreditAssignment::applyCompetitiveInhibition(
    const std::vector<LearningEvent>& activeEvents, double reinforcementMagnitude) {
    
    std::set<std::string> activeEventIds;
    for (const auto& event : activeEvents) {
        activeEventIds.insert(event.eventId);
    }
    
    for (auto& [eventId, stats] : eventStats_) {
        if (activeEventIds.find(eventId) == activeEventIds.end()) {
            // This event is inactive, apply competitive inhibition
            stats.accumulatedScore -= reinforcementMagnitude * config_.competitiveInhibition;
        }
    }
}

void PavlovianCreditAssignment::updateAdaptiveLearningRates() {
    for (auto& [eventId, stats] : eventStats_) {
        // Adaptive learning rate: α(i,t) = α₀(i) × (1 + ρ × conditioning_count(i))⁻¹
        stats.learningRate = config_.baseLearningRate / 
                           (1.0 + config_.adaptiveLearningFactor * stats.conditioningCount);
    }
}

void PavlovianCreditAssignment::checkAutoSave() {
    if (!config_.enableAutoSave || config_.saveFilePath.empty()) {
        return;
    }
    
    auto timeSinceLastSave = core::now() - lastAutoSave_;
    if (timeSinceLastSave >= config_.autoSaveInterval) {
        if (saveModel(config_.saveFilePath)) {
            lastAutoSave_ = core::now();
        }
    }
}

LearningEvent PavlovianCreditAssignment::signalToLearningEvent(
    const strategy::TradingSignal& signal) const {
    
    LearningEvent event;
    event.type = imbalanceTypeToEventType(signal.sourceType);
    event.eventId = eventTypeToString(event.type) + "_" + signal.symbol.toString() + "_" + 
                    core::directionToString(signal.direction);
    event.timestamp = signal.generationTime;
    event.isActive = true;
    event.rawScore = signal.strength * signal.confidence * signal.quality;
    event.symbol = signal.symbol;
    event.direction = signal.direction;
    
    return event;
}

EventType PavlovianCreditAssignment::imbalanceTypeToEventType(
    strategy::ImbalanceType imbalanceType) const {
    
    switch (imbalanceType) {
        case strategy::ImbalanceType::VOLUME_IMBALANCE:
            return EventType::VOLUME_IMBALANCE_SIGNAL;
        case strategy::ImbalanceType::PRICE_LEVEL_IMBALANCE:
            return EventType::PRICE_LEVEL_SIGNAL;
        case strategy::ImbalanceType::LIQUIDITY_REMOVAL:
            return EventType::LIQUIDITY_REMOVAL_SIGNAL;
        case strategy::ImbalanceType::ORDER_FLOW_IMBALANCE:
            return EventType::ORDER_FLOW_SIGNAL;
        case strategy::ImbalanceType::LARGE_ORDER_APPEARANCE:
        case strategy::ImbalanceType::LARGE_ORDER_DISAPPEARANCE:
            return EventType::LARGE_ORDER_SIGNAL;
        case strategy::ImbalanceType::BID_ASK_SPREAD_CHANGE:
        case strategy::ImbalanceType::ABNORMAL_SPREAD:
            return EventType::SPREAD_CHANGE_SIGNAL;
        case strategy::ImbalanceType::MARKET_DEPTH_CHANGE:
            return EventType::MARKET_DEPTH_SIGNAL;
        default:
            return EventType::CUSTOM_SIGNAL;
    }
}

std::string PavlovianCreditAssignment::eventTypeToString(EventType type) const {
    switch (type) {
        case EventType::VOLUME_IMBALANCE_SIGNAL: return "volume_imbalance";
        case EventType::PRICE_LEVEL_SIGNAL: return "price_level";
        case EventType::LIQUIDITY_REMOVAL_SIGNAL: return "liquidity_removal";
        case EventType::ORDER_FLOW_SIGNAL: return "order_flow";
        case EventType::LARGE_ORDER_SIGNAL: return "large_order";
        case EventType::SPREAD_CHANGE_SIGNAL: return "spread_change";
        case EventType::MARKET_DEPTH_SIGNAL: return "market_depth";
        case EventType::CUSTOM_SIGNAL: return "custom";
        default: return "unknown";
    }
}

EventType PavlovianCreditAssignment::eventTypeFromString(const std::string& str) const {
    if (str == "volume_imbalance") return EventType::VOLUME_IMBALANCE_SIGNAL;
    if (str == "price_level") return EventType::PRICE_LEVEL_SIGNAL;
    if (str == "liquidity_removal") return EventType::LIQUIDITY_REMOVAL_SIGNAL;
    if (str == "order_flow") return EventType::ORDER_FLOW_SIGNAL;
    if (str == "large_order") return EventType::LARGE_ORDER_SIGNAL;
    if (str == "spread_change") return EventType::SPREAD_CHANGE_SIGNAL;
    if (str == "market_depth") return EventType::MARKET_DEPTH_SIGNAL;
    return EventType::CUSTOM_SIGNAL;
}

// Factory and helper functions

std::unique_ptr<PavlovianCreditAssignment> createTradingPTCA(const std::string& modelPath) {
    PTCAConfig config;
    
    // Trading-optimized configuration
    config.lookbackWindow = 100;           // Look back 100 signals
    config.temporalDecayFactor = 0.98;     // Slower decay for trading signals
    config.weakThreshold = 0.02;           // 2% threshold for weak signals
    config.strongThreshold = 0.05;         // 5% threshold for strong signals
    config.baseLearningRate = 0.05;        // Conservative learning rate
    config.extinctionLambda = 0.001;       // Slow extinction
    config.competitiveInhibition = 0.01;   // Light competitive inhibition
    config.adaptiveLearningFactor = 0.05;  // Moderate adaptation
    config.saveFilePath = modelPath;
    config.autoSaveInterval = std::chrono::minutes(15); // Save every 15 minutes
    config.enableAutoSave = true;
    
    return std::make_unique<PavlovianCreditAssignment>(config);
}

std::vector<LearningEvent> createLearningEventsFromSignals(
    const std::vector<strategy::TradingSignal>& signals) {
    
    std::vector<LearningEvent> events;
    events.reserve(signals.size());
    
    for (const auto& signal : signals) {
        LearningEvent event;
        event.type = EventType::CUSTOM_SIGNAL; // Will be determined by signal type
        event.eventId = signal.id;
        event.timestamp = signal.generationTime;
        event.isActive = true;
        event.rawScore = signal.strength * signal.confidence * signal.quality;
        event.symbol = signal.symbol;
        event.direction = signal.direction;
        
        events.push_back(event);
    }
    
    return events;
}

OutcomeMeasure createPnLOutcome(double pnlValue, const core::Symbol& symbol) {
    OutcomeMeasure outcome;
    outcome.timestamp = core::now();
    outcome.value = pnlValue;
    outcome.outcomeType = "pnl";
    outcome.symbol = symbol;
    return outcome;
}

OutcomeMeasure createPriceMovementOutcome(double priceChange, 
                                        const core::Symbol& symbol,
                                        std::chrono::milliseconds timeframe) {
    OutcomeMeasure outcome;
    outcome.timestamp = core::now();
    outcome.value = priceChange;
    outcome.outcomeType = "price_movement_" + std::to_string(timeframe.count()) + "ms";
    outcome.symbol = symbol;
    return outcome;
}

} // namespace learning
} // namespace trading
