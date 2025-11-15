/**
 * @file config_types.hpp
 * @brief Configuration structures for strategy components
 * 
 * This file defines configuration structures needed for various strategy components.
 * 
 * @author Trading System Team
 * @date May 13, 2025
 */

 #pragma once

#include "../types.hpp"
#include "core/types.hpp"
 #include <string>
 #include <core/market_types.hpp>
 namespace trading {
 namespace strategy {
 
 /**
  * Configuration for OrderBookAnalyzer
  */
 struct OrderBookAnalyzerConfig {
    core::ExchangeId exchangeId; 
    core::Symbol tradingPair;
     int depth =20;                        // Default depth to analyze
     int historySize = 1000;                // Size of history to maintain
     double priceWeightFactor = 0.5;        // Weight factor for price-weighted metrics
     // Default price weight factor
        double volumeWeightFactor = 0.5;       // Weight factor for volume-weighted metrics
        double imbalanceThreshold = 0.6;       // Threshold for imbalance detection
        double liquidityPressureThreshold = 0.7; // Threshold for liquidity pressure detection
        double orderFlowThreshold = 0.5;       // Threshold for order flow detection
    // ADD THESE MISSING ONES:
        double priceGroupingFactor = 0.0001;
        int minSamplesForStability = 5;
        double emaAlpha = 0.2;
     OrderBookAnalyzerConfig() = default;
 };
 
 /**
  * Configuration for ImbalanceDetection
  */
 struct ImbalanceDetectionConfig {
    // Labeling delay in milliseconds for online training buffer
     // Strategy identification
     std::string strategy_id;                     // Unique identifier for the strategy
     std::string version = "1.0";                 // Version of the strategy
     double densityChangeThreshold = 0.5;        // Threshold for density change detection
     // Core thresholds with defaults
     double volumeImbalanceThreshold = 0.7;       // Threshold for volume imbalance detection
     double priceLevelImbalanceThreshold = 0.6;   // Threshold for price level imbalance
     double liquidityRemovalThreshold = 0.5;      // Threshold for liquidity removal detection
    //  spreadChangeThreshold_
    double spreadChangeThreshold = 0.5;         // Threshold for bid-ask spread change
    double orderFlowImbalanceThreshold = 0.6;    // Threshold for order flow imbalance
    double marketDepthChangeThreshold = 0.4;     // Threshold for market depth change
    // levelVolumeChangeThreshold
    double levelVolumeChangeThreshold = 0.3;     // Threshold for volume change at price levels
    double liquidityFlowImbalanceThreshold = 0.6; // Threshold for liquidity flow imbalance
    double abnormalSpreadThreshold = 0.5;        // Threshold for abnormal spread detection
    double liquidityImbalanceThreshold = 0.5;    // Threshold for general liquidity imbalance
    double trendContinuationThreshold = 0.4;     // Threshold for trend continuation detection
    double trendReversalThreshold = 0.4;         // Threshold for trend reversal detection
    double volumeSpikeThreshold = 0.6;           // Threshold for volume spike detection
    double priceSpikeThreshold = 0.6;            // Threshold for price spike detection
    double liquidityPressureThreshold = 0.5;     // Threshold for liquidity pressure changes
    // Window and history settings (shorter default windows for low-latency orderbook signals)
    int detectionWindowMs = 1500;                // Detection window in milliseconds (1.5s)
     int maxHistorySize = 1000;                   // Maximum history size for metrics
     
     // Advanced configuration
     double global_sensitivity = 1.0;             // Global sensitivity for all thresholds
     
     // Dynamic configuration maps
     std::map<std::string, double> thresholds;    // Thresholds for different imbalance types   
     std::map<std::string, double> parameters;    // General parameters for the strategy
     std::map<std::string, std::string> string_parameters; // String parameters
    // Bayesian model persistence path (optional)
    // If empty, persistence is disabled. Set via configuration to enable.
    std::string bayesModelPath = "";
    // Labeling delay in milliseconds for online training buffer
    int labelingDelayMs = 1500;
    // Warmup / gating for online training: delay (ms) to wait after startup
    // before allowing training to begin. If 0, no delay-based gating is applied.
    int trainingStartDelayMs = 0;
    // Minimum number of non-empty order-book price levels on each side required
    // before allowing training to begin. If 0, no depth-based gating is applied.
    int minOrderbookLevels = 0;
    // Persistence tuning: interval (ms) between automatic saves (default 1h)
    // Set to 0 to disable interval-based saves.
    int bayesSaveIntervalMs = 60 * 60 * 1000; // 1 hour
    // Persistence tuning: save after N updates. Set to 0 to disable.
    int bayesSaveAfterUpdates = 50000;         // Save after 50000 updates
    // Require this many model updates before allowing persistent saves (to avoid saving untrained models)
    int requireModelReadyUpdates = 100;       // do not save until model has >= 100 updates
    // Minimum milliseconds between saves (debounce)
    int bayesMinSaveIntervalMs = 600000;       // 30 seconds debounce between saves
    // Minimum model updates required before allowing trades
    int bayesMinUpdatesToTrade = 5000;         // Default 5000 updates required
    // Minimum profitable return required to consider a label/trade (decimal, e.g., 0.003 = 0.3%)
    double min_profitable_return = 0.003;
     
     ImbalanceDetectionConfig() = default;
     
     // Helper method to get threshold with global sensitivity applied
     double getAdjustedThreshold(const std::string& name, double default_value) const {
         auto it = thresholds.find(name);
         if (it != thresholds.end()) {
             return it->second * global_sensitivity;
         }
         return default_value * global_sensitivity;
     }
     
     // Helper to get parameter
     double getParameter(const std::string& name, double default_value) const {
         auto it = parameters.find(name);
         if (it != parameters.end()) {
             return it->second;
         }
         return default_value;
     }
     
     // Helper to get string parameter
     std::string getStringParameter(const std::string& name, const std::string& default_value) const {
         auto it = string_parameters.find(name);
         if (it != string_parameters.end()) {
             return it->second;
         }
         return default_value;
     }
 };
 
 /**
  * Configuration for LiquidityFlowMonitor
  */
 struct LiquidityFlowConfig {
        double significance_threshold = 0.5;
        double execution_rate_factor = 0.2;
        double modification_rate_factor = 0.5;
        
        int historySize = 1000;                // Size of history to maintain
        double flowImbalanceThreshold = 0.6;   // Threshold for flow imbalance detection
        core::ExchangeId exchangeId;             // Exchange ID
        core::Symbol tradingPair;             // Trading pair
        double largeOrderThreshold = 5.0;     // Threshold for large orders
        int historyWindowSeconds = 60;       // Window size for historical analysis
        double largeOrderVolume = 0.0;       // Volume of large orders
        double cancellationRatio = 0.0;     // Ratio of cancellations
        double pressureIndex = 0.0;         // Pressure index for liquidity flow
        double flowImbalance = 0.0;         // Flow imbalance metric
        double typicalOrderSize = 1.0;      // Typical order size for calculations
        std::chrono::system_clock::time_point lastUpdateTime; // Last update time
        uint64_t lastSequenceNumber = 0;   // Last sequence number for order book updates
       
            
        // History management
        static constexpr size_t DEFAULT_MAX_HISTORY_SIZE = 100;  // Max snapshots to keep
        static constexpr int DEFAULT_HISTORY_WINDOW_SECONDS = 300;  // 5 minutes max
        static constexpr size_t DEFAULT_CLEANUP_THRESHOLD = 120;
        
        size_t max_history_size = DEFAULT_MAX_HISTORY_SIZE;
        int history_window_seconds = DEFAULT_HISTORY_WINDOW_SECONDS;
        size_t cleanup_threshold = DEFAULT_CLEANUP_THRESHOLD;
        
        // Memory limits
        size_t max_memory_usage_mb = 50;  // Max 50MB for history
        bool enable_auto_cleanup = true;
        std::chrono::minutes cleanup_interval{2};



        //params
        ParameterSet params;             // Parameters for liquidity flow analysis
        // large_order_threshold
     LiquidityFlowConfig() = default;
 };
 
 /**
  * Configuration for VolumeProfileAnalyzer
  */
 struct VolumeProfileConfig {
     int timeWindowMs = 60000;              // Time window for volume profile in milliseconds
     int priceLevels = 20;                  // Number of price levels to track
    //  exchangeId
        core::ExchangeId exchangeId;               // Exchange ID
        core::Symbol tradingPair;               // Trading pair
        double rangeSizePercent = 0.01;        // Size of each price range as percentage of price
        // timeFrameMinutes
        int timeFrameMinutes = 60;            // Time frame in minutes

        // params
        ParameterSet params;                  // Parameters for volume profile analysis
        //numPriceRanges  
        int numPriceRanges = 20;            // Number of price ranges to track
        // price_ranges
        // std::vector<PriceRange> price_ranges; // Price ranges for volume profile
     
     VolumeProfileConfig() = default;
 };
 
 /**
  * Configuration for ImbalanceSignalGenerator
  */
 struct SignalGeneratorConfig {
     double minSignalStrength = 0.01;        // Minimum signal strength to generate
     double confidenceThreshold = 0.5;      // Minimum confidence for signal generation
     uint64_t signalTtlMs = 500;           // Time-to-live for signals in milliseconds
    // Timeout for external prediction/predictor calls in microseconds.
    // If predictor does not return within this interval, generator will
    // fall back to a safe default to avoid stalling signal creation.
    // A value of 0 disables the timeout and does a blocking call.
    std::uint32_t predictTimeoutUs = 2000; // default 2000us (2ms)
     double minPredictionThreshold = 0.01;   // Minimum prediction threshold to consider
     double qualityBase = 0.5;              // Base quality score
     double qualityFactor = 0.5;            // Factor for quality calculation
     double minCorrelationForOverride = 0.5; // Minimum correlation to override signal
     bool enableCorrelationAdjustment = true; // Enable correlation-based confidence adjustment
     double conflictConfidencePenalty = 0.5; // Penalty factor for conflicting predictions
     SignalGeneratorConfig() = default;
 };
 
 /**
  * Configuration for MomentumCorrelation
  */
 struct MomentumCorrelationConfig {
     int correlationWindowMs = 10000;       // Window for correlation analysis in milliseconds
     int predictionHorizonMs = 5000;        // Prediction horizon in milliseconds
     
     MomentumCorrelationConfig() = default;
 };
 
 /**
  * Configuration for ThresholdManager
  */
 struct ThresholdManagerConfig {
     double globalSensitivity = 1.0;        // Global sensitivity factor
     double volatilityAdjustmentFactor = 0.5; // Factor for volatility adjustment
     
     ThresholdManagerConfig() = default;
 };
 
 /**
  * Configuration for SignalDecayModel
  */
 struct SignalDecayConfig {
     double defaultDecayRate = 0.1;         // Default decay rate per second
     double minSignalStrength = 0.05;       // Minimum signal strength before expiration
     
     SignalDecayConfig() = default;
 };
 
 /**
  * Configuration for ExchangeCalibration
  */
//  struct ExchangeCalibrationConfig {
//      bool enableAutoCalibration = true;     // Enable automatic calibration
//      int calibrationIntervalMs = 3600000;   // Calibration interval in milliseconds (1 hour)
//      // Calibration threshold for parameter transfer
//         double calibrationThreshold = 0.7;     // Threshold for parameter transfer
//         // Calibration parameters
//         std::map<std::string, double> calibrationParameters; // Parameters to calibrate
//         // Calibration history
//         std::map<std::string, std::chrono::system_clock::time_point> calibrationHistory; // History of calibrations
//         // baseParameters
//         ParameterSet baseParameters;          // Base parameters for calibration
//         // config.exchanges for     std::map<ExchangeId, ExchangeMetrics> exchangeProfiles_;
//         std::map<core::ExchangeId, core::ExchangeMetrics> exchanges; // Exchanges to calibrate
//      ExchangeCalibrationConfig() = default;
//  };
 struct ExchangeCalibrationConfig {
    bool enableAutoCalibration = true;
    int calibrationIntervalMs = 3600000;
    double calibrationThreshold = 0.7;
    std::map<std::string, double> calibrationParameters;
    std::map<std::string, std::chrono::system_clock::time_point> calibrationHistory;
    ParameterSet baseParameters;
    std::vector<core::ExchangeId> exchanges; // exchangeID just a std::string
    ExchangeCalibrationConfig() = default;
};
 /**
  * Configuration for AdaptiveMechanism
  */
 struct AdaptiveMechanismConfig {
     bool enableAdaptation = true;          // Enable adaptation
     double adaptationRate = 0.05;          // Rate of parameter adaptation
     int adaptationIntervalMs = 3600000;    // Adaptation interval in milliseconds (1 hour)
     
     AdaptiveMechanismConfig() = default;
 };
//  regime detector config
    struct RegimeDetectorConfig {
        double sensitivity = 1.0;               // Sensitivity for regime detection
        int detectionWindowMs = 60000;         // Detection window in milliseconds
        RegimeDetectorConfig() = default;
    };
    
 // The main StrategyConfig should have these fields:
 struct StrategyConfig {
     core::StrategyId strategy_id;                      // Unique identifier for the strategy
     std::string version;                               // Version of the strategy
     ParameterSet defaultParameters;                    // Default parameters
     
     // Component configurations
    OrderBookAnalyzerConfig orderBookAnalyzerConfig;
    ImbalanceDetectionConfig imbalanceDetectionConfig;
    LiquidityFlowConfig liquidityFlowConfig;
    VolumeProfileConfig volumeProfileConfig;
    SignalGeneratorConfig signalGeneratorConfig;
    MomentumCorrelationConfig momentumCorrelationConfig;
    RegimeDetectorConfig regimeDetectorConfig;
    ThresholdManagerConfig thresholdManagerConfig;
    SignalDecayConfig signalDecayConfig;
    ExchangeCalibrationConfig exchangeCalibrationConfig;
    AdaptiveMechanismConfig adaptiveMechanismConfig;
     
     // Map of trading pairs to monitor (symbol -> exchange ID)
    std::map<core::Symbol, core::ExchangeId> tradingPairs;
    // Runtime intervals (milliseconds) - tunable at runtime via config
    // These govern internal polling/loop intervals. Default to 100ms for
    // low-latency orderbook-driven strategies.
    int marketDataUpdaterIntervalMs = 200; // interval between market-data updater cycles
    int exitMonitorIntervalMs = 200;       // interval between exit-monitor wakeups
     // Constructor
     StrategyConfig() : strategy_id(core::StrategyId("integrated_strategy", "1.0.0")), version("1.0.0") {}

     StrategyConfig(const core::StrategyId& id, const std::string& ver = "1.0.0")
         : strategy_id(id), version(ver) {}
 };
 
 /**
  * Extended StrategyConfig with component-specific configurations
  * This can be removed as the StrategyConfig now contains all needed fields
  */
 /*
 struct ExtendedStrategyConfig : public StrategyConfig {
     ExtendedStrategyConfig() = default;
     
     // Constructor that takes base StrategyConfig
     ExtendedStrategyConfig(const StrategyConfig& baseConfig) 
         : StrategyConfig(baseConfig) {}
 };
 */
 
 // Remove this since the main StrategyConfig now has all the needed fields
 // No need for an extended version that adds the same fields
 
 } // namespace strategy
 } // namespace trading