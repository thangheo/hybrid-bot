// adaptive_mechanism.hpp
#pragma once

// #include "adaptation_rules.hpp"
// #include "strategy/types/parameter_set.hpp"
// #include "strategy/types/performance_metrics.hpp"
#include "strategy/types.hpp"
#include "core/types.hpp"
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <functional>
#include <mutex>
#include <chrono>

namespace trading {
namespace strategy {

/**
 * Enumeration of adaptation types
 */
enum class AdaptationType {
    PARAMETER_DRIFT,     // Gradual parameter changes based on performance
    MARKET_REGIME,       // Adapt to different market regimes
    PARAMETER_BLENDING,  // Blend parameters from different sources
    ANTI_FRAGILITY       // Implement anti-fragile behavior
};

/**
 * Class representing the current adaptation status
 */
struct AdaptationStatus {
    bool active;
    std::chrono::system_clock::time_point lastAdaptationTime;
    std::map<std::string, double> parameterChanges;
    std::string currentRegime;
    double adaptationConfidence;
    
    AdaptationStatus() : active(false), adaptationConfidence(0.0) {}
};

/**
 * Class implementing self-adaptation mechanisms for the strategy
 */
class AdaptiveMechanism {
public:
    // Constructor
    AdaptiveMechanism();
    ~AdaptiveMechanism() = default;
    AdaptiveMechanism(const AdaptiveMechanismConfig& config); 
    // Update performance metrics to inform adaptation
    void updatePerformanceMetrics(const PerformanceMetrics& metrics);
    // processMetrics
    void processMetrics(const PerformanceMetrics& metrics);
    // Apply adaptations based on collected metrics
    void applyAdaptations();
    
    // Get current adaptation status
    AdaptationStatus getStatus() const;
    
    // Configure adaptation rules
    void configureAdaptationRules(const AdaptationRules& rules);
    
    // Enable/disable specific adaptation types
    void enableAdaptationType(AdaptationType type, bool enable);
    
    // Set base parameters (starting point for adaptations)
    void setBaseParameters(const ParameterSet& params);
    
    // Get adapted parameters
    ParameterSet getAdaptedParameters() const;
    
    // Reset adaptation state
    void reset();
    //getAdjustmentRecommendations
    std::map<std::string, double> getAdjustmentRecommendations() const;
    // Register callbacks
    using AdaptationCallback = std::function<void(const ParameterSet&)>;
    void registerAdaptationCallback(AdaptationCallback callback);
    
    // Market regime detection
    std::string detectMarketRegime(const core::MarketState& state) const;
    //updateSignalResult
    void updateSignalResult(const std::string& signalId, const SignalExecutionResult& result);
private:
    // Adaptation rules
    AdaptationRules rules_;
    // Base and adapted parameters
    ParameterSet baseParameters_;
    ParameterSet adaptedParameters_;
    
    // Performance history
    std::vector<PerformanceMetrics> performanceHistory_;
    
    // Adaptation status
    AdaptationStatus status_;
    
    // Enabled adaptation types
    std::map<AdaptationType, bool> enabledTypes_;
    
    // Safety bounds for parameter changes
    std::map<std::string, std::pair<double, double>> parameterBounds_;
    
    // Implementation of different adaptation strategies
    void applyParameterDrift();
    void applyMarketRegimeAdaptation();
    void applyParameterBlending();
    void applyAntiFragility();
    
    // Determine performance attribution for targeted adaptation
    std::map<std::string, double> attributePerformance() const;
    
    // Check if adaptation is necessary
    bool isAdaptationNeeded() const;
    
    // Reversion mechanisms for failed adaptations
    void checkForFailedAdaptation();
    void revertToSafeParameters();
    
    // Callbacks
    AdaptationCallback adaptationCallback_;
    
    // Thread safety
    mutable std::mutex mutex_;
    //config_
    AdaptiveMechanismConfig config_;
};

} // namespace strategy
} // namespace trading