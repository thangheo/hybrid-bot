/**
 * @file ptca_integration_manager.hpp
 * @brief Integration manager for PTCA learning in the trading system
 * 
 * This file provides a manager class that integrates the Pavlovian Temporal
 * Credit Assignment algorithm into your existing trading system.
 * 
 * @author Trading System Team
 * @date September 13, 2025
 */

#pragma once

#include "strategy/types.hpp"
#include "learning/pavlovian_credit_assignment.hpp"
#include "execution/models/order.hpp"
#include "execution/models/execution_report.hpp"
#include <memory>
#include <chrono>
#include <unordered_map>
#include <queue>
#include <thread>
#include <atomic>
#include <mutex>

using namespace trading::execution::models;
using namespace trading::strategy;

namespace trading {
namespace learning {

/**
 * Outcome tracking for signals
 */
struct SignalOutcomeTracker {
    strategy::TradingSignal originalSignal;
    core::Timestamp signalTime;
    core::Timestamp executionTime;
    bool wasExecuted = false;
    double executionPrice = 0.0;
    double currentPrice = 0.0;
    double currentPnL = 0.0;
    bool outcomeReported = false;
    
    SignalOutcomeTracker() = default;
    explicit SignalOutcomeTracker(const strategy::TradingSignal& signal)
        : originalSignal(signal), signalTime(signal.generationTime) {}
};

/**
 * Configuration for PTCA integration
 */
struct PTCAIntegrationConfig {
    // Learning parameters
    std::chrono::minutes outcomeEvaluationDelay{5};     // Wait 5 minutes to evaluate outcomes
    std::chrono::minutes signalTrackingWindow{30};      // Track signals for 30 minutes max
    double minimumPnLThreshold = 0.2 / 100;                 // Minimum P&L to consider as outcome as 0.2%

    // Market data integration
    bool useRealTimePriceUpdates = true;
    std::chrono::seconds priceUpdateInterval{10};       // Update prices every 10 seconds
    
    // Model persistence
    std::string modelSaveDirectory = "ptca_models/";
    std::chrono::minutes modelSaveInterval{30};         // Save model every 30 minutes
    bool enableContinuousLearning = true;
    
    // Performance tracking
    size_t maxSignalsToTrack = 1000;                    // Limit memory usage
    bool enableDetailedLogging = true;
    
    // Thresholds for different outcome types
    double priceMovementThreshold = 0.005;              // 0.5% price movement significance
    double pnlSuccessThreshold = 0.01;                  // 1% P&L success threshold
    double strongOutcomeThreshold = 0.03;               // 3% strong outcome threshold
};

/**
 * Main integration manager for PTCA learning
 */
class PTCAIntegrationManager {
public:
    /**
     * Constructor
     */
    explicit PTCAIntegrationManager(const PTCAIntegrationConfig& config = PTCAIntegrationConfig{});
    
    /**
     * Destructor
     */
    ~PTCAIntegrationManager();
    
    /**
     * Start the integration manager
     */
    void start();
    
    /**
     * Stop the integration manager
     */
    void stop();
    
    /**
     * Register a new trading signal for learning
     * This should be called from your signal callback
     */
    void registerSignal(const strategy::TradingSignal& signal);
    
    /**
     * Register order execution (when a signal results in an order)
     * This helps track which signals were actually acted upon
     */
    void registerExecution(const std::string& signalId, 
                          const ExecutionReport& executionReport);
    
    /**
     * Update market price for a symbol (for P&L calculation)
     */
    void updateMarketPrice(const core::Symbol& symbol, double price);
    
    /**
     * Get recommendation for a new signal based on learned patterns
     * Returns confidence score (-1.0 to 1.0)
     */
    double getSignalRecommendation(const strategy::TradingSignal& signal);
    
    /**
     * Generate trading decision based on current market state
     */
    PavlovianCreditAssignment::TradingDecision 
    generateTradingDecision(const core::Symbol& symbol, 
                           const std::vector<strategy::TradingSignal>& currentSignals);
    
    /**
     * Get learning statistics
     */
    struct IntegrationStats {
        size_t totalSignalsTracked = 0;
        size_t signalsWithOutcomes = 0;
        size_t executedSignals = 0;
        double averageSignalPnL = 0.0;
        double learningAccuracy = 0.0;
        size_t currentlyTrackedSignals = 0;
        core::Timestamp lastModelSave;
        
        PavlovianCreditAssignment::LearningStats ptcaStats;
    };
    
    IntegrationStats getStats() const;
    
    /**
     * Get the underlying PTCA instance for advanced operations
     */
    PavlovianCreditAssignment& getPTCA() { return *ptca_; }
    const PavlovianCreditAssignment& getPTCA() const { return *ptca_; }
    
    /**
     * Force save the model
     */
    bool saveModel(const std::string& filename = "");
    
    /**
     * Load a previously saved model
     */
    bool loadModel(const std::string& filename);
    
    /**
     * Reset all learning data
     */
    void reset();

private:
    PTCAIntegrationConfig config_;
    std::unique_ptr<PavlovianCreditAssignment> ptca_;
    
    // Signal tracking
    std::unordered_map<std::string, SignalOutcomeTracker> activeSignals_;
    std::unordered_map<std::string, double> currentPrices_;
    mutable std::recursive_mutex signalMutex_;
    
    // Background processing
    std::thread processingThread_;
    std::atomic<bool> running_{false};
    
    // Statistics
    size_t totalSignalsTracked_ = 0;
    size_t signalsWithOutcomes_ = 0;
    size_t executedSignals_ = 0;
    double totalPnL_ = 0.0;
    core::Timestamp lastModelSave_;
    
    /**
     * Background processing loop
     */
    void processingLoop();
    
    /**
     * Evaluate outcomes for signals that are ready
     */
    void evaluateSignalOutcomes();
    
    /**
     * Calculate P&L for a signal
     */
    double calculateSignalPnL(const SignalOutcomeTracker& tracker);
    
    /**
     * Create learning event from trading signal
     */
    LearningEvent createLearningEvent(const strategy::TradingSignal& signal);
    
    /**
     * Create outcome measure from signal tracker
     */
    OutcomeMeasure createOutcome(const SignalOutcomeTracker& tracker);
    
    /**
     * Clean up old signals
     */
    void cleanupOldSignals();
    
    /**
     * Save model if needed
     */
    void checkModelSave();
    
    /**
     * Get filename for model based on timestamp
     */
    std::string generateModelFilename() const;
};

/**
 * Global instance access (singleton pattern)
 * This makes it easy to integrate into your existing callback system
 */
class PTCAManager {
public:
    static PTCAIntegrationManager& getInstance() {
        static PTCAIntegrationManager instance;
        return instance;
    }
    
    static void initialize(const PTCAIntegrationConfig& config) {
        getInstance().~PTCAIntegrationManager();
        new (&getInstance()) PTCAIntegrationManager(config);
    }
    
private:
    PTCAManager() = default;
};

} // namespace learning
} // namespace trading

/**
 * INTEGRATION EXAMPLE - How to integrate into your main.cpp:
 * 
 * // 1. In your main function, initialize PTCA:
 * trading::learning::PTCAIntegrationConfig ptcaConfig;
 * ptcaConfig.modelSaveDirectory = "models/ptca/";
 * ptcaConfig.enableContinuousLearning = true;
 * trading::learning::PTCAManager::initialize(ptcaConfig);
 * trading::learning::PTCAManager::getInstance().start();
 * 
 * // 2. Modify your signal callback to include PTCA learning:
 * strategyEngine_->registerSignalCallback([this](const strategy::TradingSignal& signal) {
 *     signalsGenerated_++;
 *     
 *     // Register signal with PTCA for learning
 *     trading::learning::PTCAManager::getInstance().registerSignal(signal);
 *     
 *     // Get recommendation from PTCA
 *     double ptcaRecommendation = trading::learning::PTCAManager::getInstance()
 *         .getSignalRecommendation(signal);
 *     
 *     // Apply PTCA recommendation as a filter
 *     if (ptcaRecommendation < -0.3) {
 *         // PTCA strongly recommends against this signal
 *         return;
 *     }
 *     
 *     // Convert strategy signal to execution signal for risk validation
 *     execution::TradingSignal execSignal;
 *     execSignal.id = signal.id;
 *     execSignal.symbol = signal.symbol;
 *     // ... rest of your existing code
 * });
 * 
 * // 3. When orders are executed, register with PTCA:
 * // In your execution completion callback:
 * void onExecutionComplete(const execution::ExecutionReport& report) {
 *     // Your existing logic...
 *     
 *     // Register execution with PTCA
 *     trading::learning::PTCAManager::getInstance()
 *         .registerExecution(report.getOrderId(), report);
 * }
 * 
 * // 4. Update market prices for P&L calculation:
 * // In your market data callback:
 * void onMarketDataUpdate(const core::Symbol& symbol, double price) {
 *     trading::learning::PTCAManager::getInstance()
 *         .updateMarketPrice(symbol, price);
 * }
 * 
 * // 5. At shutdown, stop PTCA:
 * trading::learning::PTCAManager::getInstance().stop();
 */
