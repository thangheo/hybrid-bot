/**
 * @file imbalance_detection_engine.hpp
 * @brief Engine for detecting significant order book imbalances
 * 
 * @details
 * The Imbalance Detection Engine identifies significant imbalances in order book data
 * that may indicate short-term price movements. It applies various detection algorithms
 * to identify patterns like bid/ask volume imbalances, sudden liquidity changes,
 * and other market microstructure patterns.
 * 
 * Created on: May 5, 2025
 */
#pragma once

#include "strategy/pattern/analyzer.hpp"
#include "strategy/types.hpp"

// Forward-declare Bayesian model to avoid pulling heavy Eigen headers into this public header
namespace trading { namespace perception { class BayesianTradingModel; } }

#include "data_ingestion/orderbook/order_book.hpp"
#include "perception/bayesian_trading_adapter.hpp"
#include "strategy/core_analysis/regime_subscriber.hpp"
#include <vector>
#include <memory>
#include <map>
#include <unordered_map>
#include <string>
#include <mutex>
#include <functional>
#include <deque>
#include <atomic>



namespace trading {
namespace strategy {
    
struct DetectorResult {
    ImbalanceType type;
    // bool isImbalance = false;
    double imbalanceValue = 0.0; // e.g., volume ratio, spread change
    double strength = 0.0;
    Direction direction = Direction::BUY;
    bool shouldEmitEvent = false; // true nếu cần gọi createImbalanceEvent
};

/**
 * @class ImbalanceDetectionEngine
 * @brief Engine for detecting order book imbalances
 * 
 * This class implements algorithms to detect meaningful order book imbalances
 * that may predict short-term price movements. It processes order book metrics
 * and identifies patterns that cross configured thresholds.
 */
class ImbalanceDetectionEngine {
public:
    /**
     * @brief Constructor
     */
    ImbalanceDetectionEngine();
    ImbalanceDetectionEngine(const ImbalanceDetectionConfig& config); 
    /**
     * @brief Destructor
     */
    virtual ~ImbalanceDetectionEngine();
    // Start the background detector worker (optional). If you call this, the
    // engine will offload detectors to a worker thread instead of running them
    // on the caller thread.
    void startDetectorWorker(size_t threads = 0);
    
    /**
     * @brief Initialize the detection engine with configuration
     * @param config Configuration parameters
     * @return True if initialization successful, false otherwise
     */
    bool initialize(const ImbalanceDetectionConfig& config);
    
    /**
     * @brief Analyze order book metrics to detect imbalances
     * @param metrics The order book metrics to analyze
     * @return True if analysis was successful, false otherwise
     */
    bool analyzeMetrics(const OrderBookMetrics& metrics);
    
    /**
     * @brief Get all detected imbalance events
     * @return Vector of imbalance events
     */
    std::vector<ImbalanceEvent> getDetectedImbalances() const;
    
    /**
     * @brief Check if a specific type of imbalance has been detected
     * @param type The type of imbalance to check for
     * @return True if imbalance of specified type detected, false otherwise
     */
    bool hasSignificantImbalance(ImbalanceType type) const;
    
    /**
     * @brief Set detection thresholds
     * @param thresholds The thresholds to set
     */
    void setThresholds(const ThresholdSet& thresholds);
    trading::core::ConfidenceLevel valueToConfidenceLevel(double value) ;

    /**
     * @brief Get current detection thresholds
     * @return Current thresholds
     */
    ThresholdSet getThresholds() const;
    
    /**
     * @brief Register callback for imbalance detection events
     * @param callback Function to call when imbalance is detected
     * @return Callback ID that can be used to unregister
     */
    size_t registerImbalanceCallback(const ImbalanceCallback& callback);
    
    /**
     * @brief Unregister a previously registered callback
     * @param callbackId ID of the callback to unregister
     * @return True if callback was found and unregistered, false otherwise
     */
    bool unregisterImbalanceCallback(size_t callbackId);
    
    /**
     * @brief Clear all detected imbalances
     */
    void clearImbalances();
    
    /**
     * @brief Get the engine status
     * @return Status as a string
     */
    std::string getStatus() const;

    /**
     * These additional functions complete the ImbalanceDetectionEngine implementation
     * Add these to the imbalance_detection_engine.cpp file
     */

    //updateParameters
    virtual void updateParameters(const ParameterSet& params);

private:
    //number of regimes
    static constexpr int NUMBER_OF_REGIMES = sizeof(MarketRegimeType) - 1; // Exclude UNKNOWN
    double calculateOrderCountDropRatio(const OrderBookMetrics& metrics);
    double calculateMarketImpactDecayRatio(const OrderBookMetrics& metrics) ;
    double calculateVolatilityDeclineRatio(const OrderBookMetrics& metrics) ;
    double calculateFlowNormalizationRatio(const OrderBookMetrics& metrics) ;
    double calculatePressureReliefRatio(const OrderBookMetrics& metrics);
    double getCurrentVolumeConcentration(const OrderBookMetrics& metrics);
    Direction determineDisappearanceDirection(const OrderBookMetrics& metrics);
    double calculatePriceReversionRatio(const OrderBookMetrics& metrics) ;
    double calculateLiquidityDropRatio(const OrderBookMetrics& metrics) ;
    double calculateSpreadExpansionRatio(const OrderBookMetrics& metrics) ;
    double calculateVolumeReductionRatio(const OrderBookMetrics& metrics) ;
    double calculateDepthReductionRatio(const OrderBookMetrics& metrics) ;


    Direction determineDirection(const OrderBookMetrics& metrics);
    double getVolumeConcentrationStrength(const OrderBookMetrics& metrics);
    // Additional private methods
    uint64_t generateEventId();
    double calculateImbalanceConfidence(ImbalanceType type, double strength, const OrderBookMetrics& metrics);
    double calculateImbalanceStrength(double volumeRatio, double threshold);
    
    // Optimal exit strategy methods (replaced old transition tracking)
    // Exit logic now handled by OptimalExitStrategy class

    // Configuration
    ImbalanceDetectionConfig config_;
    ThresholdSet thresholds_;
    // Bayesian model to aggregate detector strengths into a single signal
    std::shared_ptr<trading::perception::BayesianTradingModel> bayes_model_;
    std::chrono::steady_clock::time_point lastSaveTime_;
    // lastMetrics_
    std::optional<OrderBookMetrics> lastMetrics_;
    // Detection state
    std::vector<ImbalanceEvent> detectedImbalances_;
    std::map<ImbalanceType, bool> imbalanceState_;
    std::map<std::string, double> metricHistory_;
    
    // Detection algorithms for all supported imbalance types
    DetectorResult detectVolumeImbalance(const OrderBookMetrics& metrics);
    DetectorResult detectPriceLevelImbalance(const OrderBookMetrics& metrics);
    DetectorResult detectLiquidityFlowImbalance(const OrderBookMetrics& metrics);
    DetectorResult detectAbnormalSpread(const OrderBookMetrics& metrics);
    DetectorResult detectMarketDepthChange(const OrderBookMetrics& metrics);
    DetectorResult detectLargeOrderAppearance(const OrderBookMetrics& metrics);
    DetectorResult detectLargeOrderDisappearance(const OrderBookMetrics& metrics);
    DetectorResult detectOrderFlowImbalance(const OrderBookMetrics& metrics);
    DetectorResult detectPriceLevelConcentration(const OrderBookMetrics& metrics);
    DetectorResult detectLiquidityImbalance(const OrderBookMetrics& metrics);
    DetectorResult detectTrendContinuation(const OrderBookMetrics& metrics);
    DetectorResult detectTrendReversal(const OrderBookMetrics& metrics);
    DetectorResult detectTradeImbalance(const OrderBookMetrics& metrics);
    DetectorResult detectCancellationImbalance(const OrderBookMetrics& metrics);
    DetectorResult detectPriceVolatilityImbalance(const OrderBookMetrics& metrics);
    DetectorResult detectHiddenLiquidityImbalance(const OrderBookMetrics& metrics);
    DetectorResult detectCustomImbalance(const OrderBookMetrics& metrics);
    bool checkVolumeConcentration(const OrderBookMetrics& metrics) ;
    bool detectSpreadPersistence(const OrderBookMetrics& metrics) ;
    
    // PRODUCTION FIX: Thread-safe labeling statistics
    struct LabelingStats {
        std::atomic<size_t> totalLabels{0};
        std::atomic<size_t> buyLabels{0};
        std::atomic<size_t> sellLabels{0}; 
        std::atomic<size_t> neutralLabels{0};
    };
    LabelingStats labelingStats_;
    
    void trackLabelingEvent(core::Direction labelDirection, const std::string& symbol, 
                          double normalizedReturn, std::chrono::milliseconds delay);
    bool detectDepthVolumeInconsistency(const OrderBookMetrics& metrics) ;
    bool detectVolatilitySuppression(const OrderBookMetrics& metrics) ;
    bool detectHiddenLiquidityPatterns(const OrderBookMetrics& metrics) ;


    double calculateVariance(const std::vector<double>& data) ;
    // bool detectQuoteStuffing(const OrderBookMetrics& metrics) ;
    // bool
    // detectFlashCrashPattern
    bool detectCorrelationAnomalies(const OrderBookMetrics& metrics) ;
    core::Direction determineImbalanceDirection(ImbalanceType type, const OrderBookMetrics& metrics);
    // detectMarketImpactResistance
    bool detectMarketImpactResistance(const OrderBookMetrics& metrics) ;
    // detectFlashCrashPattern
    // bool detectFlashCrashPattern(const OrderBookMetrics& metrics) ;
    bool detectDensityPressureMismatch(const OrderBookMetrics& metrics) ;
    bool detectVelocityAnomalies(const OrderBookMetrics& metrics) ;
    // Event ID generation
    // Imbalance evaluation
    double calculateImbalanceStrength(ImbalanceType type, const OrderBookMetrics& metrics);
    void createImbalanceEvent(ImbalanceType type, double strength, core::Direction direction, const OrderBookMetrics& metrics);
    //create a specific imbalance event for custom bayes signal
    void createImbalanceEventForBayesModel(ImbalanceType type, double strength ,core::Direction direction, 
        const OrderBookMetrics& metrics, double bayesProbability, double uncertainty = 0.0);
    // Detected imbalances
    // Callback management
    std::map<size_t, ImbalanceCallback> callbacks_;
    size_t nextCallbackId_;
    void notifyImbalanceCallbacks(const ImbalanceEvent& event);
    // Control whether detected events are published to the system (EventBus/callbacks)
public:
    void setPublishEvents(bool enable) { publishEvents_.store(enable); }
    bool publishEventsEnabled() const { return publishEvents_.load(); }
    
    // Thread safety
    mutable std::mutex mutex_;
    // Per-(exchange,symbol) last observed volume ratio to avoid cross-symbol interference
    // and to allow per-instrument change gating in a multi-threaded environment.
    std::map<std::pair<core::ExchangeId, core::Symbol>, double> lastVolumeRatio_;
    mutable std::mutex lastVolumeMutex_;
    // Background detector worker(s) to offload expensive detection
    // Support multiple worker threads to avoid single-thread bottleneck
    std::vector<std::thread> detectorWorkerThreads_;
    mutable std::mutex detectorQueueMutex_;
    std::condition_variable detectorQueueCv_;
    std::deque<OrderBookMetrics> detectorQueue_;
    std::atomic<bool> detectorWorkerRunning_{false};
    size_t detectorQueueMax_ = 1024*16; // default max queue size
    // For rate-limiting the 'queue full' warning
    mutable std::atomic<long long> lastQueueFullWarnMs_{0};

public:
    // Query if engine is currently under backpressure (queue at or above capacity)
    bool isUnderBackpressure() const;
    // Get current queue size (for instrumentation)
    size_t getDetectorQueueSize() const;
    // Get configured queue capacity
    size_t getDetectorQueueMax() const { return detectorQueueMax_; }

    // When false, detected events are constructed but not published to EventBus
    std::atomic<bool> publishEvents_{true};

    // Bayesian perception model for fusing detector outputs
    // Adapter handle to a perception bayesian model (opaque, implemented in perception)
    trading::perception::BayesHandle bayes_handle_ = nullptr;
    // Mutex to protect bayes_handle_ and related operations
    mutable std::mutex bayesMutex_;
    // True after initialize() successfully created/allocated the bayes handle
    std::atomic<bool> bayesInitialized_{false};
    // Buffer for pending training examples (features captured at prediction time)
    struct PendingTraining {
        core::Timestamp timestamp;
        std::vector<double> features; // uses imbalanceValue ordering
        double midPrice;
        double vwap = 0.0; // optional VWAP snapshot at capture
        double sma = 0.0;  // optional simple moving average snapshot at capture
        double referenceValue = 0.0; // chosen reference value used for labeling (vwap/sma/micro/mid)
        int referenceType = 0; // 0=mid,1=vwap,2=sma,3=micro
        // Add symbol context so training can be applied to the correct per-symbol model
        core::ExchangeId exchangeId;
        core::Symbol symbol;
    };
    std::deque<PendingTraining> pendingTraining_;
    mutable std::mutex pendingTrainingMutex_;

    // Labeling configuration (milliseconds to wait before labelling a prediction)
    std::chrono::milliseconds labelingDelay_{50000};
    // Increased buffer to hold samples while labelingDelay is long (e.g., 60s)
    // Default enlarged to avoid dropping samples: ~65k
    size_t maxPendingTraining_ = 65536;

    // Training warmup gating: start time and enabled flag
    std::chrono::steady_clock::time_point trainingStartTime_ = std::chrono::steady_clock::now();
    std::atomic<bool> trainingEnabled_{false};
    // Minimum orderbook levels required before training can start (from config)
    int minOrderbookLevels_ = 0;

    // Optional model persistence path (can be set via env BAYES_MODEL_PATH)
    std::string bayesModelPath_;
    // If configured, use this directory to store per-symbol model files. Derived from bayesModelPath_
    std::string bayesModelDirectory_;
    // Per-symbol Bayes handles (opaque handles from perception adapter)
    // Now support NUMBER_OF_REGIMES handles per symbol (one per regime)
    std::unordered_map<std::string, std::array<trading::perception::BayesHandle, NUMBER_OF_REGIMES>> bayesHandles_;
    mutable std::mutex bayesHandlesMutex_;

    // Pointer to the global regime subscriber (optional). Set by caller (StrategyEngine)
    trading::strategy::core_analysis::RegimeSubscriber* regime_subscriber_ = nullptr;

    // Optional callback to check authoritative position for a symbol
    // Return: optional<double> signed position quantity if available, nullopt if unknown
    std::function<std::optional<double>(const core::ExchangeId&, const core::Symbol&)> positionLookup_ = nullptr;

public:
    // Set a callback that allows the engine to ask for an authoritative (cached) position
    // If not set, the engine preserves legacy behavior (no authoritative check)
    void setPositionLookupCallback(const std::function<std::optional<double>(const core::ExchangeId&, const core::Symbol&)>& cb) {
        positionLookup_ = cb;
    }

public:
    // Set the RegimeSubscriber so the engine can snapshot regime probabilities at capture time and weight predictions
    void setRegimeSubscriber(trading::strategy::core_analysis::RegimeSubscriber* sub) { regime_subscriber_ = sub; }

    // Configure minimum bayes model updates required before emitting trades
    void setBayesMinUpdatesToTrade(int v) { bayesMinUpdatesToTrade_ = v; }

    // Helpers for per-symbol model management
    // Return an array of NUMBER_OF_REGIMES handles (one per regime). Null entries allowed.
    std::array<trading::perception::BayesHandle, NUMBER_OF_REGIMES> getBayesHandlesForSymbol(const core::ExchangeId &exch, const core::Symbol &sym);
    // Legacy compatibility: return a single handle (regime 0) or create one
    trading::perception::BayesHandle getBayesHandleForSymbol(const core::ExchangeId &exch, const core::Symbol &sym);
    bool saveBayesForSymbol(trading::perception::BayesHandle h, const core::ExchangeId &exch, const core::Symbol &sym);
    // If configured, use this directory to store per-symbol model files. Derived from bayesModelPath_
    
    // Per-symbol Bayes handles (opaque handles from perception adapter)
    // Save-after-updates counter (decremented on each bayes_update)
    int bayesSaveAfterUpdates_ = 0;
    // Configured save interval (ms) for periodic saves
    std::chrono::milliseconds bayesSaveInterval_{60 * 60 * 1000};
    // Require model to have at least this many updates before saving (protect against saving untrained model)
    int requireModelReadyUpdates_ = 100;
    // Minimum number of model updates required before allowing the model to trigger trades
    int bayesMinUpdatesToTrade_ = 15000;
    // Minimum interval between saves (debounce)
    std::chrono::milliseconds bayesMinSaveInterval_{30000};
    // Runtime counter for updates since last save
    int updatesSinceLastSave_ = 0;
    
    // PRODUCTION FIX: Thread-safe labeling statistics (instance-based)
    // struct LabelingStats {
    //     std::atomic<size_t> totalLabels{0};
    //     std::atomic<size_t> buyLabels{0};
    //     std::     std::atomic<size_t> neutralLabels{0};
    // };
    // LabelingStats labelingStats_;
    
    // Optimal exit strategy integration (replaced old signal state tracking)
    // Position management now handled by OptimalExitStrategy class
};

// Type alias
using ImbalanceDetectionEnginePtr = std::shared_ptr<ImbalanceDetectionEngine>;
using ImbalanceDetectionEngineConstPtr = std::shared_ptr<const ImbalanceDetectionEngine>;

} // namespace strategy
} // namespace trading

// Forward declaration for AdaptiveExitStrategy
// #include "strategy/exit/adaptive_exit_strategy.hpp"
// Avoid including heavy exit strategy header here; forward-declare the class
namespace trading { namespace strategy { class AdaptiveExitStrategy; } }
// Global accessor to allow sharing a single AdaptiveExitStrategy instance across
// components. Use setGlobalAdaptiveExitStrategy() at startup to register the
// shared_ptr created by Trading_system so ImbalanceDetectionEngine uses the
// same instance as the rest of the system.
namespace trading {
namespace strategy {
    void setGlobalAdaptiveExitStrategy(const std::shared_ptr<AdaptiveExitStrategy>& ptr);
    std::shared_ptr<AdaptiveExitStrategy> getGlobalAdaptiveExitStrategy();
} // namespace strategy
} // namespace trading