/**
 * @file strategy_engine.hpp
 * @brief Main orchestration component for the Order Imbalance Strategy
 * 
 * @details
 * The Strategy Engine is the central coordinator for the order imbalance trading strategy.
 * It orchestrates the flow of order book data through various analyzers, imbalance detection,
 * signal generation, and optimization components to produce actionable trading signals.
 * 
 * Created on: May 13, 2025
 */
#pragma once
#include "strategy/types.hpp"
#include "strategy/signal/signal_decay_model.hpp"
#include "strategy/pattern/imbalance_detection_engine.hpp"
#include "strategy/core_analysis/order_book_analyzer.hpp"
#include "strategy/core_analysis/liquidity_flow_monitor.hpp"
#include "strategy/core_analysis/volume_profile_analyzer.hpp"
#include "strategy/signal/imbalance_signal_generator.hpp"
#include "strategy/signal/momentum_correlation.hpp"
#include "strategy/signal/threshold_manager.hpp"
#include "strategy/optimization/exchange_calibration.hpp"
#include "strategy/optimization/adaptive_mechanism.hpp"
#include "data_ingestion/orderbook/order_book.hpp"

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>
#include <chrono>
#include "core/types.hpp"
namespace trading {
namespace strategy {

/**
 * @class StrategyEngine
 * @brief Main orchestration component for the Order Imbalance Strategy
 * 
 * This class coordinates all the components of the strategy, manages the flow
 * of order book data, and produces trading signals based on detected imbalances.
 */
class StrategyEngine {
public:
    /**
     * @brief Constructor
     * @param strategyId Unique identifier for this strategy instance
     */
    explicit StrategyEngine(const StrategyId& strategyId);

    /**
     * @brief Register a logistic regression signal generator
     * @param generator The signal generator to register
     */
    // void registerLogisticRegressionSignalGenerator(
    //     std::shared_ptr<LogisticRegressionSignalGenerator> generator);
    
    /**
     * @brief Destructor
     */
    virtual ~StrategyEngine();
    
    /**
     * @brief Initialize the strategy engine
     * @param config Strategy engine configuration
     * @return True if initialization successful, false otherwise
     */
    bool initialize(const StrategyConfig& config);
    /* a function to get the signal expiration */
    /**
     * @brief Start the strategy engine
     * @return True if started successfully, false otherwise
     */
    bool start();
    
    /**
     * @brief Stop the strategy engine
     * @return True if stopped successfully, false otherwise
     */
    bool stop();
    
    /**
     * @brief Process new order book data
     * @param orderBook The order book to process
     * @return True if processing was successful, false otherwise
     */
    bool processOrderBook(const data_ingestion::OrderBook& orderBook);
    void handleSignalGenerated(const TradingSignal& signal) ;

    /**
     * @brief Get all active trading signals
     * @return Vector of active trading signals
     */
    std::vector<TradingSignal> getActiveSignals() const;
    
    /**
     * @brief Register callback for new trading signals
     * @param callback Function to call when new signal is generated
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
     * @brief Update strategy parameters
     * @param params New parameters to apply
     * @return True if parameters were updated successfully, false otherwise
     */
    bool updateParameters(const ParameterSet& params);
    
    /**
     * @brief Get current strategy parameters
     * @return Current parameter set
     */
    ParameterSet getParameters() const;
    
    /**
     * @brief Get strategy status
     * @return Status information as a JSON string
     */
    std::string getStatus() const;
    
    /**
     * @brief Get strategy performance metrics
     * @return Performance metrics
     */
    StrategyPerformance getPerformance() const;
    
    /**
     * @brief Manually trigger signal evaluation
     * @return True if evaluation was triggered, false otherwise
     */
    bool triggerSignalEvaluation();
    
    /**
     * @brief Record signal execution result for feedback
     * @param signalId ID of the executed signal
     * @param result Execution result details
     * @return True if feedback was recorded successfully, false otherwise
     */
    bool recordSignalResult(const std::string& signalId, const SignalExecutionResult& result);

private:
    // Strategy identification
    StrategyId strategyId_;
    std::string strategyVersion_;
    size_t eventSubscriptionId_;
    size_t imbalanceSubscriptionId_;
    size_t signalGeneratedSubscriptionId_; //subscription id for signal generated events from event bus
    //event bus
    // size_t eventSubscriptionId_ = 0;  
    // signalQueueMutex_
    std::mutex signalQueueMutex_;
    // std::vector<TradingSignal> signalQueue_;
    std::vector<TradingSignal> queuedSignals_;
    // Component instances
    OrderBookAnalyzerPtr orderBookAnalyzer_;
    ImbalanceDetectionEnginePtr imbalanceDetector_;
    LiquidityFlowMonitorPtr liquidityMonitor_;
    VolumeProfileAnalyzerPtr volumeProfileAnalyzer_;
    ImbalanceSignalGeneratorPtr signalGenerator_;
    MomentumCorrelationPtr momentumCorrelator_;
    ThresholdManagerPtr thresholdManager_;
    SignalDecayModelPtr signalDecayModel_;
    ExchangeCalibrationPtr exchangeCalibration_;
    AdaptiveMechanismPtr adaptiveMechanism_;
    
    // Strategy state
    StrategyConfig config_;
    ParameterSet parameters_;
    std::atomic<StrategyState> state_{StrategyState::INITIALIZED};
    std::chrono::system_clock::time_point lastUpdateTime_;
    std::map<std::string, TradingSignal> activeSignals_;
    StrategyPerformance performance_;
    
    // Callback management
    std::map<size_t, SignalCallback> signalCallbacks_;
    size_t nextCallbackId_ = 1;
    
    // Thread safety
    mutable std::recursive_mutex mutex_;
    std::recursive_mutex callbackMutex_;
    //previousOrderBook_
    // data_ingestion::OrderBook previousOrderBook_;
    // Periodic processing
    std::unique_ptr<std::thread> signalMaintenanceThread_;
    std::atomic<bool> runMaintenanceThread_{false};
    
    TradingSignal enhanceSignalWithEventContext(const TradingSignal& signal, const ImbalanceEvent& event) const;
    void processQueuedSignals();
    // Internal processing methods
    void processImbalanceEvents(const std::vector<ImbalanceEvent>& events);
    void maintainSignals();
    void updateStrategyState();
    void notifySignalCallbacks(const TradingSignal& signal);
    void recordMetrics();
    bool validateOrderBook(const data_ingestion::OrderBook& orderBook) const;
    void signalMaintenanceWorker();
    void pruneExpiredSignals();
    void loadDefaultParameters();
    std::optional<ImbalanceEvent> findSourceEvent(
    const TradingSignal& signal, 
    const std::vector<ImbalanceEvent>& events) const;

    // Optimization methods
    void performAdaptiveAdjustments();
    
    // Trading pair management
    std::map<std::string, TradingPairState> tradingPairStates_;
    bool initializeTradingPair(const std::string& symbol, core::ExchangeId exchangeId);
    void updateTradingPairState(const std::string& symbol, TradingPairState state);
    
    // Performance tracking
    void updatePerformanceMetrics(const SignalExecutionResult& result);
    void recordSignalPerformance(const std::string& signalId, const SignalExecutionResult& result);
    
    // Additional components
    // std::shared_ptr<LogisticRegressionSignalGenerator> logistic_regression_signal_generator_;
};

// Type aliases
using StrategyEnginePtr = std::shared_ptr<StrategyEngine>;
using StrategyEngineConstPtr = std::shared_ptr<const StrategyEngine>;

} // namespace strategy
} // namespace trading