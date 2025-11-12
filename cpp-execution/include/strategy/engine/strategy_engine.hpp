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
#include "strategy/engine/meta_filter.hpp"
#include "strategy/engine/shock_detector.hpp"
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
#include "strategy/core_analysis/regime_subscriber.hpp"
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
     * @brief Set minimum volatility threshold for trading
     * @param threshold Minimum volatility (e.g., 0.0005 for 0.05%)
     */
    void setVolatilityThreshold(double threshold) { volatilityThreshold_ = threshold; }
    
    /**
     * @brief Get current volatility threshold
     * @return Current volatility threshold
     */
    double getVolatilityThreshold() const { return volatilityThreshold_; }
    
    /**
     * @brief Set volatility threshold for specific symbol
     * @param symbol Symbol name (e.g., "BTCUSDT")
     * @param threshold Volatility threshold for this symbol
     */
    void setSymbolVolatilityThreshold(const std::string& symbol, double threshold);
    
    /**
     * @brief Get volatility threshold for specific symbol
     * @param symbol Symbol name
     * @return Symbol-specific threshold or default if not set
     */
    double getSymbolVolatilityThreshold(const std::string& symbol) const;
    
    /**
     * @brief Load symbol volatility thresholds from config file
     * @param configPath Path to JSON config file
     * @return True if loaded successfully
     */
    bool loadSymbolVolatilityConfig(const std::string& configPath);
    
    /**
     * @brief Load symbol-specific volatility thresholds from config file
     * @param configPath Path to JSON configuration file
     * @return True if loaded successfully, false otherwise
     */
    // bool loadSymbolVolatilityConfig(const std::string& configPath);

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

    /**
     * @brief Update MetaFilter configuration
     * @param config New MetaFilter configuration
     */
    void updateMetaFilterConfig(const engine::MetaFilterConfig& config);

    /**
     * @brief Update ShockDetector configuration  
     * @param config New ShockDetector configuration
     */
    void updateShockDetectorConfig(const engine::ShockDetectorConfig& config);

    /**
     * @brief Get MetaFilter statistics
     * @return JSON string with MetaFilter statistics
     */
    std::string getMetaFilterStatistics() const;

    /**
     * @brief Get ShockDetector statistics
     * @return JSON string with ShockDetector statistics
     */
    std::string getShockDetectorStatistics() const;

    /**
     * @brief Emergency exit callback type for liquidating positions
     * @param symbol Symbol to liquidate
     * @param reason Reason for emergency exit
     */
    using EmergencyExitCallback = std::function<bool(const std::string& symbol, const std::string& reason)>;

    /**
     * @brief Register callback for emergency position liquidation
     * @param callback Function to call when emergency exit is needed
     */
    void registerEmergencyExitCallback(const EmergencyExitCallback& callback);

    // Expose imbalance detector for integration wiring (e.g., to set position lookup callback)
    std::shared_ptr<ImbalanceDetectionEngine> getImbalanceDetector() const { return imbalanceDetector_; }

private:
    // Strategy identification
    StrategyId strategyId_;
    std::string strategyVersion_;
    size_t eventSubscriptionId_;
    size_t imbalanceSubscriptionId_;
    size_t signalGeneratedSubscriptionId_; //subscription id for signal generated events from event bus
    size_t metricsUpdatedSubscriptionId_; //subscription id for metrics updated events from event bus
    // size_t eventSubscriptionId_ = 0;
    // signalQueueMutex_
    std::mutex signalQueueMutex_;
    // std::vector<TradingSignal> signalQueue_;
    std::vector<TradingSignal> queuedSignals_;
    // Component instances
    OrderBookAnalyzerPtr orderBookAnalyzer_;
    ImbalanceDetectionEnginePtr imbalanceDetector_;
    LiquidityFlowMonitorPtr liquidityMonitor_;
    // Keep a separate VolumeProfileAnalyzer per trading pair (exchange:symbol)
    std::map<std::string, VolumeProfileAnalyzerPtr> volumeProfileAnalyzers_;
    // Store latest volume profile metrics for each symbol for signal enhancement
    std::map<std::string, std::map<std::string, double>> volumeProfileMetrics_;
    ImbalanceSignalGeneratorPtr signalGenerator_;
    MomentumCorrelationPtr momentumCorrelator_;

    // regimeDetector_
    OrderbookRegimeDetectorPtr regimeDetector_;
    ThresholdManagerPtr thresholdManager_;
    SignalDecayModelPtr signalDecayModel_;
    ExchangeCalibrationPtr exchangeCalibration_;
    AdaptiveMechanismPtr adaptiveMechanism_;
    
    // Signal filtering components
    std::unique_ptr<engine::MetaFilter> meta_filter_;
    std::unique_ptr<engine::ShockDetector> shock_detector_;
    
    // Strategy state
    StrategyConfig config_;
    ParameterSet parameters_;
    std::atomic<StrategyState> state_{StrategyState::INITIALIZED};
    std::chrono::system_clock::time_point lastUpdateTime_;
    std::map<std::string, TradingSignal> activeSignals_;
    StrategyPerformance performance_;
    
    // Volatility filtering
    double volatilityThreshold_ = 0.0005; // Default 0.05%
    
    // Volume Profile metrics storage (symbol -> metrics map)
    // std::map<std::string, std::map<std::string, double>> volumeProfileMetrics_;
    
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

    // Signal filtering methods
    bool applySignalFilters(const TradingSignal& signal) const;
    void updateShockDetectorPrice(const std::string& symbol, double price) const;
    // void checkEmergencyExit(const std::string& symbol) const;

    // Emergency exit callback
    mutable std::mutex emergency_callback_mutex_;
    EmergencyExitCallback emergency_exit_callback_;

    // Optimization methods
    void performAdaptiveAdjustments();
    
    // Trading pair management
    std::map<std::string, TradingPairState> tradingPairStates_;
    bool initializeTradingPair(const core::Symbol& symbol, core::ExchangeId exchangeId);
    void updateTradingPairState(const core::Symbol& symbol, TradingPairState state);
    
    // Performance tracking
    void updatePerformanceMetrics(const SignalExecutionResult& result);
    void recordSignalPerformance(const std::string& signalId, const SignalExecutionResult& result);
    
    // Symbol-specific volatility thresholds
    std::map<std::string, double> symbolVolatilityThresholds_; // Per-symbol thresholds
    
    // Additional components
    // std::shared_ptr<LogisticRegressionSignalGenerator> logistic_regression_signal_generator_;
    // Regime subscriber removed: regime information is now taken from
    // OrderBookMetrics.metrics["marketRegime"] injected by OrderBookAnalyzer.

public:
    // Volume Profile Access Methods
    /**
     * Get volume profile analyzer for a specific symbol
     * @param symbol The trading symbol
     * @param exchangeId The exchange ID  
     * @return Shared pointer to volume profile analyzer or nullptr if not found
     */
    VolumeProfileAnalyzerPtr getVolumeProfileAnalyzer(const core::Symbol& symbol, const core::ExchangeId& exchangeId) const;
    
    /**
     * Get current volume profile metrics for a symbol
     * @param symbol The trading symbol as string
     * @return Map of metric names to values
     */
    std::map<std::string, double> getVolumeProfileMetrics(const std::string& symbol) const;
    
    /**
     * Check if price is near significant volume profile levels
     * @param symbol The trading symbol
     * @param price Current price to check
     * @param tolerance Tolerance percentage (default 0.5%)
     * @return True if near POC, value area edges, or VWAP
     */
    bool isNearVolumeProfileLevel(const std::string& symbol, double price, double tolerance = 0.005) const;
    
    /**
     * Get volume profile signal enhancement factor
     * @param symbol The trading symbol  
     * @param price Current price
     * @return Enhancement factor (1.0 = no enhancement, >1.0 = boost, <1.0 = reduction)
     */
    double getVolumeProfileSignalFactor(const std::string& symbol, double price) const;
    
    /**
     * Calculate adaptive threshold based on volume profile context
     * @param symbol The trading symbol
     * @param baseThreshold Base imbalance threshold
     * @param currentPrice Current price (optional, will fetch if not provided)
     * @return Adjusted threshold based on volume density and context
     */
    double calculateAdaptiveThreshold(const std::string& symbol, double baseThreshold, double currentPrice = 0.0) const;
    
    /**
     * Check if imbalance strength meets volume-profile-adjusted threshold
     * @param symbol The trading symbol
     * @param imbalanceStrength Raw imbalance strength
     * @param baseThreshold Base threshold for comparison
     * @return True if imbalance is significant enough given volume context
     */
    bool isImbalanceSignificant(const std::string& symbol, double imbalanceStrength, double baseThreshold) const;

    // Query latest regime info for a symbol (thread-safe). Returns true if symbol exists.
    bool getRegimeInfo(const std::string& symbol, trading::strategy::core_analysis::RegimeInfo& out) const;
};

// Type aliases
using StrategyEnginePtr = std::shared_ptr<StrategyEngine>;
using StrategyEngineConstPtr = std::shared_ptr<const StrategyEngine>;

} // namespace strategy
} // namespace trading