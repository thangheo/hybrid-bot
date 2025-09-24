/**
 * @file imbalance_detection_engine.cpp
 * @brief Implementation of the imbalance detection engine
 * 
 * @details
 * Implements algorithms for detecting significant order book imbalances
 * that may predict short-term price movements.
 * 
 * Created on: May 5, 2025
 */

 #include "strategy/pattern/imbalance_detection_engine.hpp"
 #include <common/event_bus.hpp>
 #include <common/trading_events.hpp>
 #include <algorithm>
 #include <cmath>
 #include <iostream>
 #include <chrono>
 #include <sstream>
#include <logger.hpp>
 namespace trading {
 namespace strategy {
 
 // Constructor
 ImbalanceDetectionEngine::ImbalanceDetectionEngine() : nextCallbackId_(0) {

    //  VOLUME_IMBALANCE,              // Significant difference between bid and ask volume
    //  PRICE_LEVEL_IMBALANCE,         // Imbalance across multiple price levels
    //  LIQUIDITY_REMOVAL,             // Sudden removal of liquidity from one side
    //  ORDER_FLOW_IMBALANCE,          // Directional bias in order submissions
    //  LARGE_ORDER_APPEARANCE,        // Large orders appearing in the book
    //  LARGE_ORDER_DISAPPEARANCE,     // Large orders being removed from the book
    //  PRICE_LEVEL_CONCENTRATION,     // Concentration of volume at specific price levels
    //  BID_ASK_SPREAD_CHANGE,         // Significant change in the bid-ask spread
    //  MARKET_DEPTH_CHANGE,           // Significant change in market depth
    //  LIQUIDITY_FLOW_IMBALANCE,      // Imbalance in liquidity flow
    //  ABNORMAL_SPREAD,               // Abnormal spread between bid and ask prices
    //  LIQUIDITY_IMBALANCE,           // General liquidity imbalance
    //  TREND_CONTINUATION,            // Continuation of a price trend
    //  TREND_REVERSAL,                // Reversal of a price trend
     // Initialize imbalance state map for all imbalance types
    imbalanceState_[ImbalanceType::VOLUME_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::PRICE_LEVEL_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::LIQUIDITY_FLOW_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::ABNORMAL_SPREAD] = false;
        // Add other imbalance types as needed
    imbalanceState_[ImbalanceType::LIQUIDITY_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::LIQUIDITY_REMOVAL] = false;
    imbalanceState_[ImbalanceType::ORDER_FLOW_IMBALANCE] = false;
    imbalanceState_[ImbalanceType::LARGE_ORDER_APPEARANCE] = false;
    imbalanceState_[ImbalanceType::LARGE_ORDER_DISAPPEARANCE] = false;
    imbalanceState_[ImbalanceType::PRICE_LEVEL_CONCENTRATION] = false;
    imbalanceState_[ImbalanceType::BID_ASK_SPREAD_CHANGE] = false;
    imbalanceState_[ImbalanceType::MARKET_DEPTH_CHANGE] = false;
    imbalanceState_[ImbalanceType::TREND_CONTINUATION] = false;
    imbalanceState_[ImbalanceType::TREND_REVERSAL] = false;
 }
 ImbalanceDetectionEngine::ImbalanceDetectionEngine(const ImbalanceDetectionConfig& config){
    
     config_ = config;
 }

 // Initialize the detection engine with configuration
 bool ImbalanceDetectionEngine::initialize(const ImbalanceDetectionConfig& config) {
     std::lock_guard<std::mutex> lock(mutex_);
     config_ = config;
     
     // Initialize thresholds with defaults if not specified
     if (thresholds_.empty()) {
         // Set default thresholds if none provided
         thresholds_.setThreshold("volumeImbalance ", 2.0); // Volume imbalance threshold
         thresholds_.setThreshold("emaVolumeImbalance", 1.5); // EMA volume imbalance threshold
         thresholds_.setThreshold("priceLevelImbalance", 1.5); // Price level imbalance threshold
         thresholds_.setThreshold("liquidity_flow_imbalance", 3.0); // Liquidity flow imbalance threshold
         thresholds_.setThreshold("liquidity_removal", 1.5); // Liquidity removal threshold
         thresholds_.setThreshold("priceLevelImbalance", 1.5); // Price level imbalance threshold
         thresholds_.setThreshold("abnormal_spread", 2.5); // Abnormal spread threshold
        thresholds_.setThreshold("liquidity_flow_imbalance", 3.0); // Liquidity flow imbalance threshold
     }
     
     // Clear any existing state
     clearImbalances();
     
     return true;
 }
 
//  // Analyze order book metrics to detect imbalances
 bool ImbalanceDetectionEngine::analyzeMetrics(const OrderBookMetrics& metrics) {
    // Only hold the main mutex briefly to update shared history and to initialize lastMetrics_.
    // Do NOT hold the mutex while running the detectors below — detectors may also lock the same
    // mutex and holding it here causes a deadlock when detect* functions attempt to acquire it.
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //
        // Store metrics history for time-series analysis
        for (const auto& metric : metrics.metrics) {
            metricHistory_[metric.first] = metric.second;
        }

        // Initialize lastMetrics_ on first call and return early - detectors need a previous snapshot
        if (!lastMetrics_) {
            lastMetrics_ = metrics;
            return false;
        }
    }

    // Validate metrics without holding the main mutex
    if (metrics.bidVolumesByLevel.empty() || metrics.askVolumesByLevel.empty()) {
        std::cerr << "Invalid metrics: empty bid or ask volumes." << std::endl;
        return false;
    }

    // Call detectors without holding the main mutex to avoid lock-order inversion / deadlocks.
    // Instrumentation: measure each detector's duration to find blocking or slow operations.
    auto detStart = std::chrono::steady_clock::now();
    {
        LOG_DEBUG("ImbalanceDetectionEngine::analyzeMetrics - running detectors for {}:{}",
                  metrics.exchangeId.toString(), metrics.symbol.toString());
    }

    bool volumeImbalance = false;
    bool priceLevelImbalance = false;
    bool liquidityFlowImbalance = false;
    bool abnormalSpread = false;
    bool marketDepthChange = false;

    // Helper lambda to run and time a detector
    auto runDetector = [&](const std::string& name, auto&& fn) -> bool {
        auto t0 = std::chrono::steady_clock::now();
        bool result = fn();
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        if (ms > 50) {
            LOG_WARN("Detector {} took {} ms for {}:{}", name, ms, metrics.exchangeId.toString(), metrics.symbol.toString());
        } else {
            LOG_DEBUG("Detector {} took {} ms for {}:{}", name, ms, metrics.exchangeId.toString(), metrics.symbol.toString());
        }
        return result;
    };

    // volumeImbalance = runDetector("detectVolumeImbalance", [&]() { return detectVolumeImbalance(metrics); });
    //we skipp each to see which one is blocking
    // volumeImbalance = detectVolumeImbalance(metrics); // this is the one blocking
    volumeImbalance = runDetector("detectVolumeImbalance", [&]() { return detectVolumeImbalance(metrics); });

    priceLevelImbalance = runDetector("detectPriceLevelImbalance", [&]() { return detectPriceLevelImbalance(metrics); });
    liquidityFlowImbalance = runDetector("detectLiquidityFlowImbalance", [&]() { return detectLiquidityFlowImbalance(metrics); });
    abnormalSpread = runDetector("detectAbnormalSpread", [&]() { return detectAbnormalSpread(metrics); });
    marketDepthChange = runDetector("detectMarketDepthChange", [&]() { return detectMarketDepthChange(metrics); });

    // Update last snapshot after all detectors ran (keeps detectors comparing to the same previous snapshot)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastMetrics_ = metrics;
    }

    // Return true if any imbalance was detected
    return volumeImbalance || priceLevelImbalance || liquidityFlowImbalance || abnormalSpread || marketDepthChange;
 }
 // Add this debugging version of analyzeMetrics with timeout detection
// bool ImbalanceDetectionEngine::analyzeMetrics(const OrderBookMetrics& metrics) {
//     // Add timeout detection to identify where we're hanging
//     auto start = std::chrono::steady_clock::now();
    
//     LOG_DEBUG("analyzeMetrics: Starting analysis for {}:{}", 
//               metrics.exchangeId.toString(), metrics.symbol.toString());
    
//     // Only hold the main mutex briefly to update shared history and to initialize lastMetrics_.
//     {
//         std::lock_guard<std::mutex> lock(mutex_);
//         LOG_DEBUG("analyzeMetrics: Acquired main mutex");
        
//         // Store metrics history for time-series analysis
//         for (const auto& metric : metrics.metrics) {
//             metricHistory_[metric.first] = metric.second;
//         }

//         // Initialize lastMetrics_ on first call and return early
//         if (!lastMetrics_) {
//             lastMetrics_ = metrics;
//             LOG_DEBUG("analyzeMetrics: First call, initialized lastMetrics_");
//             return false;
//         }
//         LOG_DEBUG("analyzeMetrics: Released main mutex");
//     }

//     // Validate metrics without holding the main mutex
//     if (metrics.bidVolumesByLevel.empty() || metrics.askVolumesByLevel.empty()) {
//         LOG_WARN("Invalid metrics: empty bid or ask volumes");
//         return false;
//     }

//     LOG_DEBUG("analyzeMetrics: Starting detector calls");
    
//     // Helper lambda to run and time a detector with timeout detection
//     auto runDetectorWithTimeout = [&](const std::string& name, auto&& fn) -> bool {
//         auto detectorStart = std::chrono::steady_clock::now();
//         LOG_DEBUG("analyzeMetrics: Starting detector {}", name);
        
//         bool result = false;
//         try {
//             result = fn();
//             auto detectorEnd = std::chrono::steady_clock::now();
//             auto detectorMs = std::chrono::duration_cast<std::chrono::milliseconds>(detectorEnd - detectorStart).count();
            
//             if (detectorMs > 100) {
//                 LOG_WARN("Detector {} took {} ms - potential performance issue", name, detectorMs);
//             } else {
//                 LOG_DEBUG("Detector {} completed in {} ms", name, detectorMs);
//             }
//         } catch (const std::exception& e) {
//             LOG_ERROR("Detector {} threw exception: {}", name, e.what());
//             result = false;
//         }
        
//         return result;
//     };

//     bool volumeImbalance = runDetectorWithTimeout("detectVolumeImbalance", 
//         [&]() { return detectVolumeImbalance(metrics); });
        
//     // Check if we're taking too long overall
//     auto elapsed = std::chrono::steady_clock::now() - start;
//     if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > 500) {
//         LOG_ERROR("analyzeMetrics: Taking too long! Elapsed: {} ms", 
//                   std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
//     }

//     bool priceLevelImbalance = runDetectorWithTimeout("detectPriceLevelImbalance", 
//         [&]() { return detectPriceLevelImbalance(metrics); });
        
//     bool liquidityFlowImbalance = runDetectorWithTimeout("detectLiquidityFlowImbalance", 
//         [&]() { return detectLiquidityFlowImbalance(metrics); });
        
//     bool abnormalSpread = runDetectorWithTimeout("detectAbnormalSpread", 
//         [&]() { return detectAbnormalSpread(metrics); });
        
//     bool marketDepthChange = runDetectorWithTimeout("detectMarketDepthChange", 
//         [&]() { return detectMarketDepthChange(metrics); });

//     // Update last snapshot after all detectors ran
//     {
//         std::lock_guard<std::mutex> lock(mutex_);
//         lastMetrics_ = metrics;
//         LOG_DEBUG("analyzeMetrics: Updated lastMetrics_");
//     }

//     auto totalElapsed = std::chrono::steady_clock::now() - start;
//     auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(totalElapsed).count();
    
//     if (totalMs > 200) {
//         LOG_WARN("analyzeMetrics: Total time {} ms - performance concern", totalMs);
//     } else {
//         LOG_DEBUG("analyzeMetrics: Completed in {} ms", totalMs);
//     }

//     return volumeImbalance || priceLevelImbalance || liquidityFlowImbalance || abnormalSpread || marketDepthChange;
// }

 // Get all detected imbalance events
 std::vector<ImbalanceEvent> ImbalanceDetectionEngine::getDetectedImbalances() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return detectedImbalances_;
 }
 
 // Check if a specific type of imbalance has been detected
 bool ImbalanceDetectionEngine::hasSignificantImbalance(ImbalanceType type) const {
     std::lock_guard<std::mutex> lock(mutex_);
     auto it = imbalanceState_.find(type);
     return (it != imbalanceState_.end()) ? it->second : false;
 }
 
 // Set detection thresholds
 void ImbalanceDetectionEngine::setThresholds(const ThresholdSet& thresholds) {
     std::lock_guard<std::mutex> lock(mutex_);
     thresholds_ = thresholds;
 }
 
 // Get current detection thresholds
 ThresholdSet ImbalanceDetectionEngine::getThresholds() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return thresholds_;
 }
 
 // Register callback for imbalance detection events
 size_t ImbalanceDetectionEngine::registerImbalanceCallback(const ImbalanceCallback& callback) {
     std::lock_guard<std::mutex> lock(mutex_);
     size_t id = nextCallbackId_++;
     callbacks_[id] = callback;
     return id;
 }
 
 // Unregister a previously registered callback
 bool ImbalanceDetectionEngine::unregisterImbalanceCallback(size_t callbackId) {
     std::lock_guard<std::mutex> lock(mutex_);
     auto it = callbacks_.find(callbackId);
     if (it != callbacks_.end()) {
         callbacks_.erase(it);
         return true;
     }
     return false;
 }
 
 // Clear all detected imbalances
 void ImbalanceDetectionEngine::clearImbalances() {
     detectedImbalances_.clear();
     
     // Reset imbalance state for all types
     for (auto& pair : imbalanceState_) {
         pair.second = false;
     }
 }
 
 // Get the engine status
 std::string ImbalanceDetectionEngine::getStatus() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Build a simple status string
     std::string status = "ImbalanceDetectionEngine Status:\n";
     status += "  Active imbalances: " + std::to_string(detectedImbalances_.size()) + "\n";
     
     // Add status for each imbalance type
     for (const auto& pair : imbalanceState_) {
         std::string typeName;
         switch (pair.first) {
             case ImbalanceType::VOLUME_IMBALANCE:
                 typeName = "VOLUME_IMBALANCE";
                 break;
             case ImbalanceType::PRICE_LEVEL_IMBALANCE:
                 typeName = "PRICE_LEVEL_IMBALANCE";
                 break;
             case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE:
                 typeName = "LIQUIDITY_FLOW_IMBALANCE";
                 break;
             case ImbalanceType::ABNORMAL_SPREAD:
                 typeName = "ABNORMAL_SPREAD";
                 break;
            case ImbalanceType::LIQUIDITY_IMBALANCE:
                 typeName = "LIQUIDITY_IMBALANCE";
                 break;
            case ImbalanceType::LIQUIDITY_REMOVAL:
                 typeName = "LIQUIDITY_REMOVAL";
                 break;
            case ImbalanceType::ORDER_FLOW_IMBALANCE:
                 typeName = "ORDER_FLOW_IMBALANCE";
                 break;
            case ImbalanceType::LARGE_ORDER_APPEARANCE:
                 typeName = "LARGE_ORDER_APPEARANCE";
                 break;
            case ImbalanceType::LARGE_ORDER_DISAPPEARANCE:
                 typeName = "LARGE_ORDER_DISAPPEARANCE";
                 break;
            case ImbalanceType::PRICE_LEVEL_CONCENTRATION:
                 typeName = "PRICE_LEVEL_CONCENTRATION";
                 break;
            case ImbalanceType::BID_ASK_SPREAD_CHANGE:
                 typeName = "BID_ASK_SPREAD_CHANGE";
                 break;
            case ImbalanceType::MARKET_DEPTH_CHANGE:
                 typeName = "MARKET_DEPTH_CHANGE";
                 break;
            case ImbalanceType::TREND_CONTINUATION:
                 typeName = "TREND_CONTINUATION";
                 break;
            case ImbalanceType::TREND_REVERSAL:
                 typeName = "TREND_REVERSAL";
                 break;
             case ImbalanceType::CUSTOM:
                 typeName = "CUSTOM";
                 break;
             default:
                 typeName = "UNKNOWN";
                 break;
         }
         status += "  " + typeName + ": " + (pair.second ? "ACTIVE" : "INACTIVE") + "\n";
     }
     
     return status;
 }
   
 void ImbalanceDetectionEngine::updateParameters(const ParameterSet& params) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Define default values for each threshold
    const double DEFAULT_VOLUME_IMBALANCE = 2.0;
    const double DEFAULT_PRICE_LEVEL_IMBALANCE = 1.5;
    const double DEFAULT_LIQUIDITY_FLOW_IMBALANCE = 3.0;
    const double DEFAULT_ABNORMAL_SPREAD = 2.5;
    
    // Update volume imbalance threshold
    if (params.numeric_params.find("volume_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("volume_imbalance_threshold", DEFAULT_VOLUME_IMBALANCE);
        thresholds_.setThreshold("volumeImbalance ", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::VOLUME_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::VOLUME_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::VOLUME_IMBALANCE, imbalanceThreshold);
        }
    }
    
    // Update price level imbalance threshold
    if (params.numeric_params.find("price_level_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("price_level_imbalance_threshold", DEFAULT_PRICE_LEVEL_IMBALANCE);
        thresholds_.setThreshold("priceLevelImbalance", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::PRICE_LEVEL_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::PRICE_LEVEL_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::PRICE_LEVEL_IMBALANCE, imbalanceThreshold);
        }
    }
    
    // Update liquidity flow imbalance threshold
    if (params.numeric_params.find("liquidity_flow_imbalance_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("liquidity_flow_imbalance_threshold", DEFAULT_LIQUIDITY_FLOW_IMBALANCE);
        thresholds_.setThreshold("liquidity_flow_imbalance", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE, imbalanceThreshold);
        }
    }
    
    // Update abnormal spread threshold
    if (params.numeric_params.find("abnormal_spread_threshold") != params.numeric_params.end()) {
        double threshold = params.getDouble("abnormal_spread_threshold", DEFAULT_ABNORMAL_SPREAD);
        thresholds_.setThreshold("abnormal_spread", threshold);
        
        // If thresholds_ also has a map for ImbalanceType enum
        if (thresholds_.type_thresholds.find(ImbalanceType::ABNORMAL_SPREAD) != thresholds_.type_thresholds.end()) {
            ImbalanceThreshold imbalanceThreshold = thresholds_.getTypeThreshold(ImbalanceType::ABNORMAL_SPREAD);
            imbalanceThreshold.activation_threshold = threshold;
            thresholds_.setTypeThreshold(ImbalanceType::ABNORMAL_SPREAD, imbalanceThreshold);
        }
    }
    
    // Update global sensitivity if provided
    if (params.numeric_params.find("global_sensitivity") != params.numeric_params.end()) {
        thresholds_.global_sensitivity = params.getDouble("global_sensitivity", 1.0);
    }
}
 // PRIVATE METHODS

double ImbalanceDetectionEngine::calculateImbalanceStrength(double volumeRatio, double threshold) {
    double strength;
    if (volumeRatio > threshold) {
        strength = (volumeRatio - threshold) / threshold;
    } else if (volumeRatio < 1.0 / threshold) {
        strength = (1.0/threshold - volumeRatio) / (1.0/threshold);
    } else {
        strength = 0.0;
    }
    
    strength = std::min(1.0, std::max(0.0, strength)); // Clamp [0,1]
    return strength;
}


double ImbalanceDetectionEngine::calculateImbalanceConfidence(ImbalanceType type, double strength, const OrderBookMetrics& metrics) {
    // Strength is already normalized (0-1), so use it directly
    double baseConfidence = std::clamp(strength, 0.0, 1.0);
    
    // Adjust confidence based on market conditions
    double liquidityFactor = (metrics.totalBidVolume  + metrics.totalAskVolume) / 2.0;
    double volatilityFactor = metrics.volatilityExpectation;
    
    // Simple heuristic adjustments
    double liquidityWeight = 0.1; // Reduced impact
    double volatilityWeight = 0.1; // Reduced impact
    
    double liquidityAdjustment = (liquidityFactor > 1000.0) ? liquidityWeight : (liquidityFactor / 1000.0) * liquidityWeight;
    double volatilityAdjustment = (volatilityFactor < 0.5) ? volatilityWeight : (1.0 - volatilityFactor) * volatilityWeight;
    
    double adjustedConfidence = baseConfidence + liquidityAdjustment - volatilityAdjustment;
    return std::clamp(adjustedConfidence, 0.0, 1.0);
}
bool ImbalanceDetectionEngine::detectVolumeImbalance(const OrderBookMetrics& metrics) {
    try {
        // Check if required metrics are available
        auto bidVolIt = metrics.metrics.find("bidVolume");
        auto askVolIt = metrics.metrics.find("askVolume");
        if (bidVolIt == metrics.metrics.end() || askVolIt == metrics.metrics.end()) {
            return false;
        }

        double bidVolume = bidVolIt->second;
        double askVolume = askVolIt->second;

        // Avoid division by zero
        if (askVolume < 1e-10) {
            askVolume = 1e-10;
        }

        // CRITICAL FIX: Read all shared state in ONE atomic operation
        double baseThreshold;
        bool currentlyImbalanced;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            try {
                baseThreshold = thresholds_.getTypeThreshold(ImbalanceType::VOLUME_IMBALANCE).activation_threshold;
            } catch (...) {
                baseThreshold = 2.0; // fallback
            }
            auto it = imbalanceState_.find(ImbalanceType::VOLUME_IMBALANCE);
            currentlyImbalanced = (it != imbalanceState_.end()) ? it->second : false;
        }

        // Calculate volume ratio with EMA blending
        double volumeRatio = bidVolume / askVolume;
        auto emaIt = metrics.metrics.find("emaVolumeImbalance");
        const double EMA_BLEND_ALPHA = 0.7;
        
        if (emaIt != metrics.metrics.end()) {
            double emaVal = emaIt->second;
            if (std::isfinite(emaVal) && emaVal > 0.0) {
                volumeRatio = EMA_BLEND_ALPHA * emaVal + (1.0 - EMA_BLEND_ALPHA) * (bidVolume / askVolume);
            }
        }

        double enterThreshold = baseThreshold;
        double exitThreshold = baseThreshold * 0.8;
        double effectiveThreshold = currentlyImbalanced ? exitThreshold : enterThreshold;
        bool isImbalance = (volumeRatio >= effectiveThreshold || volumeRatio <= 1.0 / effectiveThreshold);

        // CRITICAL FIX: Handle volume ratio history in ONE atomic operation
        std::pair<core::ExchangeId, core::Symbol> key{metrics.exchangeId, metrics.symbol};
        double lastVolumeRatio = volumeRatio; // default
        double ratioChange = 0.0;
        
        {
            std::lock_guard<std::mutex> lock(lastVolumeMutex_);
            auto it = lastVolumeRatio_.find(key);
            
            if (it == lastVolumeRatio_.end() || !(std::isfinite(it->second) && it->second > 0.0)) {
                // First time or invalid previous value - initialize
                lastVolumeRatio_[key] = volumeRatio;
                lastVolumeRatio = volumeRatio; // No change on first run
            } else {
                lastVolumeRatio = it->second;
                
                // Calculate change
                if (lastVolumeRatio > 0.0 && std::isfinite(lastVolumeRatio) && 
                    volumeRatio > 0.0 && std::isfinite(volumeRatio)) {
                    ratioChange = std::abs(std::log(volumeRatio) - std::log(lastVolumeRatio));
                }
                
                // Update the stored value
                lastVolumeRatio_[key] = volumeRatio;
            }
        }

        const double minChangeThreshold = 0.05;
        
        // Early return if currently imbalanced and change is too small
        if (ratioChange < minChangeThreshold && currentlyImbalanced) {
            return currentlyImbalanced;
        }

        // CRITICAL FIX: Update imbalance state in ONE atomic operation
        {
            std::lock_guard<std::mutex> lock(mutex_);
            imbalanceState_[ImbalanceType::VOLUME_IMBALANCE] = isImbalance;
        }

        // Generate event only if newly imbalanced or significant change
        if (isImbalance && (!currentlyImbalanced || ratioChange >= minChangeThreshold)) {
            double strength = calculateImbalanceStrength(volumeRatio, baseThreshold);
            Direction direction = (volumeRatio > 1.0) ? Direction::BUY : Direction::SELL;

            // CRITICAL FIX: Create event WITHOUT holding any locks
            createImbalanceEvent(ImbalanceType::VOLUME_IMBALANCE, strength, direction, metrics);
        }

        return isImbalance;
        
    } catch (const std::exception& ex) {
        // Use LOG_ERROR instead of std::cerr if available
        std::cerr << "Exception in detectVolumeImbalance: " << ex.what() << std::endl;
        return false;
    } catch (...) {
        std::cerr << "Unknown exception in detectVolumeImbalance" << std::endl;
        return false;
    }
}

// //a more thread safe approach?
// bool ImbalanceDetectionEngine::detectVolumeImbalance(const OrderBookMetrics& metrics) {
//     try {
        
//         // Check if required metrics are available
//         auto bidVolIt = metrics.metrics.find("bidVolume");
//         auto askVolIt = metrics.metrics.find("askVolume");
//         if (bidVolIt == metrics.metrics.end() || askVolIt == metrics.metrics.end()) {
//             return false;
//         }

//         double bidVolume = bidVolIt->second;
//         double askVolume = askVolIt->second;

//         // Avoid division by zero
//         if (askVolume < 1e-10) {
//             askVolume = 1e-10;
//         }

//         double volumeRatio = bidVolume / askVolume;

//         // Snapshot thresholds and current state under the main mutex to avoid races
//         double baseThreshold = 1.0;
//         bool currentlyImbalanced = false;
//         {
//             std::lock_guard<std::mutex> lock(mutex_);
//             baseThreshold = thresholds_.getTypeThreshold(ImbalanceType::VOLUME_IMBALANCE).activation_threshold;
//             auto it = imbalanceState_.find(ImbalanceType::VOLUME_IMBALANCE);
//             if (it != imbalanceState_.end()) currentlyImbalanced = it->second;
//         }

//         double enterThreshold = baseThreshold;
//         double exitThreshold = baseThreshold * 0.8; // hysteresis exit level
//         double effectiveThreshold = currentlyImbalanced ? exitThreshold : enterThreshold;

//         bool isImbalance = (volumeRatio >= effectiveThreshold || volumeRatio <= 1.0 / effectiveThreshold);

//         // ---- Thread-safe lastVolumeRatio storage ----
//         std::pair<core::ExchangeId, core::Symbol> key{metrics.exchangeId, metrics.symbol};

//         // If EMA is provided, blend it with raw ratio to avoid abrupt swaps.
//         auto emaIt = metrics.metrics.find("emaVolumeImbalance");
//         const double EMA_BLEND_ALPHA = 0.7; // weight for EMA (configurable later)
//         double rawRatio = bidVolume / askVolume; // recompute raw for blending clarity
//         if (emaIt != metrics.metrics.end()) {
//             double emaVal = emaIt->second;
//             // Only blend when emaVal is finite and positive
//             if (std::isfinite(emaVal) && emaVal > 0.0) {
//                 volumeRatio = EMA_BLEND_ALPHA * emaVal + (1.0 - EMA_BLEND_ALPHA) * rawRatio;
//             } else {
//                 volumeRatio = rawRatio;
//             }
//         } else {
//             volumeRatio = rawRatio;
//         }

//         // Access and update per-key last ratio under its mutex
//         double lastVolumeRatio = volumeRatio; // default to current to avoid spurious first-change
//         {
//             std::lock_guard<std::mutex> lock(lastVolumeMutex_);
//             auto it = lastVolumeRatio_.find(key);
//             if (it == lastVolumeRatio_.end() || !(std::isfinite(it->second) && it->second > 0.0)) {
//                 // Initialize to current reading so first comparison doesn't cause a spurious event
//                 lastVolumeRatio_[key] = volumeRatio;
//                 lastVolumeRatio = volumeRatio;
//             } else {
//                 lastVolumeRatio = it->second;
//             }
//         }

//         // Compute a symmetric change metric using log-delta which handles up/down ratio similarly
//         double minChangeThreshold = 0.05; // ~5% change (in log space it's approximate)
//         double ratioChange = 0.0;
//         if (lastVolumeRatio > 0.0 && std::isfinite(lastVolumeRatio) && volumeRatio > 0.0 && std::isfinite(volumeRatio)) {
//             ratioChange = std::abs(std::log(volumeRatio) - std::log(lastVolumeRatio));
//         }

//         // If currently imbalanced and change is too small, update last snapshot and keep the state
//         if (ratioChange < minChangeThreshold && currentlyImbalanced) {
//             std::lock_guard<std::mutex> lock(lastVolumeMutex_);
//             lastVolumeRatio_[key] = volumeRatio;
//             return currentlyImbalanced;
//         }

//         // Persist new last ratio
//         {
//             std::lock_guard<std::mutex> lock(lastVolumeMutex_);
//             lastVolumeRatio_[key] = volumeRatio;
//         }

//         // Update shared imbalance state under main mutex to avoid races
//         {
//             std::lock_guard<std::mutex> lock(mutex_);
//             imbalanceState_[ImbalanceType::VOLUME_IMBALANCE] = isImbalance;
//         }

//         // Only generate event if we're newly imbalanced or change is significant
//         if (isImbalance && (!currentlyImbalanced || ratioChange >= minChangeThreshold)) {
//             // Compute strength relative to base activation threshold for consistency
//             double strength = calculateImbalanceStrength(volumeRatio, baseThreshold);
//             Direction direction = (volumeRatio > 1.0) ? Direction::BUY : Direction::SELL;

//             // std::cout << "[VolumeImbalance] Ratio=" << volumeRatio
//             //         << ", Threshold=" << baseThreshold
//             //         << ", Strength=" << strength
//             //         << ", Direction=" << (direction == Direction::BUY ? "BUY" : "SELL")
//             //         << ", Change(log)=" << (ratioChange) << std::endl;

//             createImbalanceEvent(ImbalanceType::VOLUME_IMBALANCE, strength, direction, metrics);
//         }

//         return isImbalance;
//     } catch (const std::exception& ex) {
//         std::cerr << "Exception in detectVolumeImbalance: " << ex.what() << std::endl;
//         return false;
//     } catch (...) {
//         std::cerr << "Unknown exception in detectVolumeImbalance." << std::endl;
//         return false;
//     }
// }
 // Detect price level imbalance
 bool ImbalanceDetectionEngine::detectPriceLevelImbalance(const OrderBookMetrics& metrics) {
     // Check if required metrics are available
     auto bidLevelsIt = metrics.metrics.find("bidLevelCount");
     auto askLevelsIt = metrics.metrics.find("askLevelCount");
     
     if (bidLevelsIt == metrics.metrics.end() || askLevelsIt == metrics.metrics.end()) {
         return false; // Required metrics not available
     }
     
     double bidLevels = bidLevelsIt->second;
     double askLevels = askLevelsIt->second;
     
     // Avoid division by zero
     if (askLevels < 1e-10) {
         askLevels = 1e-10;
     }
     
     // Calculate price level ratio
     double levelRatio = bidLevels / askLevels;
     
     // Get threshold for price level imbalance
     double threshold = thresholds_.getTypeThreshold(ImbalanceType::PRICE_LEVEL_IMBALANCE).activation_threshold;
     
     // Detect imbalance if ratio exceeds threshold or is below its inverse
    //  bool isImbalance = (levelRatio >= threshold || levelRatio <= 1.0 / threshold);
     // Detect imbalance if ratio exceeds threshold or is below its inverse
    bool isImbalance = (levelRatio > threshold || levelRatio < 1.0 / threshold);
     // Update imbalance state
     imbalanceState_[ImbalanceType::PRICE_LEVEL_IMBALANCE] = isImbalance;
     
     // If imbalance detected, create an event
     if (isImbalance) {
        //  double strength = std::abs(levelRatio - 1.0);
        double strength;
        if (levelRatio > threshold) {
            strength = (levelRatio - threshold) / threshold; // Normalize above threshold
        } else if (levelRatio < 1.0 / threshold) {
            strength = (1.0/threshold - levelRatio) / (1.0/threshold); // Normalize below inverse threshold
        } else {
            strength = 0.0; // Should not happen as isImbalance is true
        }
        strength = std::min(1.0, strength); // Cap at 1.0
        
        // Ensure strength is never negative or invalid
        if (strength < 0.0 || !std::isfinite(strength)) {
            strength = 0.0;
        }
         Direction direction = (levelRatio > 1.0) ? Direction::BUY : Direction::SELL;
         
        //  std::cout<<"[TradingApp] ImbalanceDetectionEngine::detectPriceLevelImbalance: strength="<<strength<<", levelRatio="<<levelRatio<<", threshold="<<threshold<<"\n";
         createImbalanceEvent(ImbalanceType::PRICE_LEVEL_IMBALANCE, strength, direction, metrics);
     }
     
     return isImbalance;
 }
 
bool ImbalanceDetectionEngine::detectLiquidityFlowImbalance(const OrderBookMetrics& metrics) {
    // Check if required metrics are available
    auto bidFlowIt = metrics.metrics.find("bidFlow");
    auto askFlowIt = metrics.metrics.find("askFlow");
    if (bidFlowIt == metrics.metrics.end() || askFlowIt == metrics.metrics.end()) {
        return false; // Required metrics not available
    }

    double bidFlow = bidFlowIt->second;
    double askFlow = askFlowIt->second;

    // Calculate net and total flow
    double netFlow   = bidFlow - askFlow;
    double totalFlow = std::abs(bidFlow) + std::abs(askFlow);

    // If no meaningful flow, no imbalance
    if (totalFlow < 1e-10) {
        imbalanceState_[ImbalanceType::LIQUIDITY_FLOW_IMBALANCE] = false;
        return false;
    }

    // Flow imbalance ratio: 0 = balanced, 1 = one-sided
    double flowImbalance = std::abs(netFlow) / totalFlow;

    // Threshold should be configured directly in [0,1]
    double threshold = thresholds_
        .getTypeThreshold(ImbalanceType::LIQUIDITY_FLOW_IMBALANCE)
        .activation_threshold;
    threshold = std::clamp(threshold, 0.0, 1.0);

    bool isImbalance = (flowImbalance >= threshold);

    // Update imbalance state
    imbalanceState_[ImbalanceType::LIQUIDITY_FLOW_IMBALANCE] = isImbalance;

    if (isImbalance) {
        double strength = flowImbalance;
        // Normalize strength to [0,1]
        strength = std::min(1.0, strength / threshold);
        // Ensure strength is valid
        if (strength < 0.0 || !std::isfinite(strength)) {
            strength = 0.0;
        }
        strength = std::min(1.0, strength); // Cap at 1.0
        Direction direction = (netFlow > 0) ? Direction::BUY : Direction::SELL;

        std::cout << "[TradingApp] detectLiquidityFlowImbalance: "
                  << "flowImbalance=" << flowImbalance
                  << ", threshold=" << threshold << "\n";

        createImbalanceEvent(
            ImbalanceType::LIQUIDITY_FLOW_IMBALANCE,
            strength,
            direction,
            metrics
        );
    }

    return isImbalance;
}
 
 // Detect abnormal spread
 bool ImbalanceDetectionEngine::detectAbnormalSpread(const OrderBookMetrics& metrics) {
     // Check if required metrics are available
     auto spreadIt = metrics.metrics.find("spreadPercent");
     auto avgSpreadIt = metrics.metrics.find("averageSpreadPercent");
     
     if (spreadIt == metrics.metrics.end() || avgSpreadIt == metrics.metrics.end()) {
         return false; // Required metrics not available
     }
     
     double currentSpread = spreadIt->second;
     double averageSpread = avgSpreadIt->second;
     
     // Avoid division by zero
     if (averageSpread < 1e-10) {
         averageSpread = 1e-10;
     }
     
     // Calculate spread ratio
     double spreadRatio = currentSpread / averageSpread;
     
     // Get threshold for abnormal spread
     double threshold = thresholds_.getTypeThreshold(ImbalanceType::ABNORMAL_SPREAD).activation_threshold;
     
     // Detect imbalance if spread ratio exceeds threshold
     bool isImbalance = (spreadRatio >= threshold);
     
     // Update imbalance state
     imbalanceState_[ImbalanceType::ABNORMAL_SPREAD] = isImbalance;
     
     // If imbalance detected, create an event
     if (isImbalance) {
         double strength = spreadRatio - 1.0;
         //normalize strength to [0,1]
         strength = std::min(1.0, strength / (threshold - 1.0));
         // Ensure strength is never negative or invalid
         if (strength < 0.0 || !std::isfinite(strength)) {
             strength = 0.0;
         }
         strength = std::min(1.0, strength); // Cap at 1.0

         // Widening spread often indicates uncertainty or selling pressure
         // This is a simplified heuristic - in real systems, you'd use additional data
         Direction direction = Direction::SELL;
         
         // Check if there's additional data to determine direction more accurately
         auto bidVolumeIt = metrics.metrics.find("bidVolume");
         auto askVolumeIt = metrics.metrics.find("askVolume");
         
         if (bidVolumeIt != metrics.metrics.end() && askVolumeIt != metrics.metrics.end()) {
             double bidVolume = bidVolumeIt->second;
             double askVolume = askVolumeIt->second;
             
             // If bid volume is significantly higher, it might indicate buying pressure despite the wide spread
             if (bidVolume > askVolume * 1.5) {
                 direction = Direction::BUY;
             }
         }
         createImbalanceEvent(ImbalanceType::ABNORMAL_SPREAD, strength, direction, metrics);
     }
     
     return isImbalance;
 }
// bool ImbalanceDetectionEngine::detectMarketDepthChange(const OrderBookMetrics& metrics){
//     // Placeholder implementation

//     return false;
// }
bool ImbalanceDetectionEngine::detectMarketDepthChange(const OrderBookMetrics& metrics) {
    // Expect analyzeMetrics to provide a previous snapshot in lastMetrics_
    if (!lastMetrics_) {
        return false; // no previous snapshot to compare
    }

    const auto& prev = *lastMetrics_;
    bool depthChanged = false;

    // --- 1. Spread widening/narrowing ---
    double spreadDiff = std::abs(metrics.spread - prev.spread);
    if (spreadDiff > config_.spreadChangeThreshold) {
        depthChanged = true;
    }

    // --- 2. Volume imbalance shift ---
    double imbalanceDiff = std::abs(metrics.volumeImbalance - prev.volumeImbalance);
    if (imbalanceDiff > config_.volumeImbalanceThreshold) {
        depthChanged = true;
    }

    // --- 3. Depth distribution change ---
    auto checkVectorChange = [&](const std::vector<double>& cur,
                                 const std::vector<double>& old,
                                 double threshold) -> bool {
        size_t n = std::min(cur.size(), old.size());
        for (size_t i = 0; i < n; i++) {
            if (std::abs(cur[i] - old[i]) > threshold) {
                return true;
            }
        }
        return false;
    };

    if (checkVectorChange(metrics.bidVolumesByLevel, prev.bidVolumesByLevel, config_.levelVolumeChangeThreshold) ||
        checkVectorChange(metrics.askVolumesByLevel, prev.askVolumesByLevel, config_.levelVolumeChangeThreshold)) {
        depthChanged = true;
    }

    // --- 4. Liquidity pressure or density shifts ---
    double pressureDiff = std::abs(metrics.liquidityPressure - prev.liquidityPressure);
    if (pressureDiff > config_.liquidityPressureThreshold) {
        depthChanged = true;
    }

    return depthChanged;
}

bool ImbalanceDetectionEngine::detectLargeOrderAppearance(const OrderBookMetrics& metrics) {
    // Configuration thresholds - these should be configurable parameters
    static const double VOLUME_IMBALANCE_THRESHOLD = 0.3;        // 30% imbalance
    static const double WEIGHTED_IMBALANCE_THRESHOLD = 0.25;     // 25% weighted imbalance
    static const double DEPTH_IMBALANCE_THRESHOLD = 0.4;         // 40% depth imbalance
    static const double PRESSURE_IMBALANCE_THRESHOLD = 0.35;     // 35% pressure imbalance
    static const double BID_ASK_RATIO_THRESHOLD_HIGH = 2.0;      // 2:1 ratio or higher
    static const double BID_ASK_RATIO_THRESHOLD_LOW = 0.5;       // 1:2 ratio or lower
    static const double DENSITY_RATIO_THRESHOLD = 1.5;          // 50% difference in density
    static const double MARKET_DIRECTIONAL_THRESHOLD = 0.6;     // Strong directional signal
    static const double LIQUIDITY_PRESSURE_THRESHOLD = 0.4;     // High liquidity pressure
    
    // Counter for detection signals
    int detectionSignals = 0;
    const int REQUIRED_SIGNALS = 3;  // Need at least 3 signals for confirmation
    
    // 1. Check volume imbalance - indicates large orders affecting volume distribution
    double absVolumeImbalance = std::abs(metrics.volumeImbalance);
    if (absVolumeImbalance > VOLUME_IMBALANCE_THRESHOLD) {
        detectionSignals++;
    }
    
    // 2. Check weighted volume imbalance - price-weighted indicator is more sensitive
    double absWeightedImbalance = std::abs(metrics.weightedVolumeImbalance);
    if (absWeightedImbalance > WEIGHTED_IMBALANCE_THRESHOLD) {
        detectionSignals++;
    }
    
    // 3. Check depth imbalance - large orders create depth asymmetries
    double absDepthImbalance = std::abs(metrics.depthImbalance);
    if (absDepthImbalance > DEPTH_IMBALANCE_THRESHOLD) {
        detectionSignals++;
    }
    
    // 4. Check pressure imbalance - combined pressure indicator
    double absPressureImbalance = std::abs(metrics.pressureImbalance);
    if (absPressureImbalance > PRESSURE_IMBALANCE_THRESHOLD) {
        detectionSignals++;
    }
    
    // 5. Check bid-ask volume ratio for extreme values
    if (metrics.bidAskVolumeRatio > BID_ASK_RATIO_THRESHOLD_HIGH || 
        metrics.bidAskVolumeRatio < BID_ASK_RATIO_THRESHOLD_LOW) {
        detectionSignals++;
    }
    
    // 6. Check density imbalance - large orders create density asymmetries
    if (metrics.bidDensity > 0 && metrics.askDensity > 0) {
        double densityRatio = std::max(metrics.bidDensity / metrics.askDensity, 
                                     metrics.askDensity / metrics.bidDensity);
        if (densityRatio > DENSITY_RATIO_THRESHOLD) {
            detectionSignals++;
        }
    }
    
    // 7. Check market directional coefficient - strong directional bias
    double absMarketDirectional = std::abs(metrics.marketDirectionalCoefficient);
    if (absMarketDirectional > MARKET_DIRECTIONAL_THRESHOLD) {
        detectionSignals++;
    }
    
    // 8. Check liquidity pressure - high pressure indicates large order impact
    double absLiquidityPressure = std::abs(metrics.liquidityPressure);
    if (absLiquidityPressure > LIQUIDITY_PRESSURE_THRESHOLD) {
        detectionSignals++;
    }
    
    // 9. Check for significant level-by-level volume concentration
    bool hasConcentratedVolume = checkVolumeConcentration(metrics);
    if (hasConcentratedVolume) {
        detectionSignals++;
    }
    
    // 10. Additional check using custom metrics if available
    double bidFlow = metrics.getMetric("bidFlow", 0.0);
    double askFlow = metrics.getMetric("askFlow", 0.0);
    if (std::abs(bidFlow - askFlow) > 0.5) {  // Significant flow imbalance
        detectionSignals++;
    }
    
    // Return true if we have enough confirming signals
    return detectionSignals >= REQUIRED_SIGNALS;
}

// Helper function to check volume concentration at specific levels
// Note: Add this declaration to your ImbalanceDetectionEngine class header:
// bool checkVolumeConcentration(const OrderBookMetrics& metrics);

bool ImbalanceDetectionEngine::checkVolumeConcentration(const OrderBookMetrics& metrics) {
    const double CONCENTRATION_THRESHOLD = 0.6;  // 60% of volume in top levels
    
    // Check bid side concentration
    if (!metrics.bidVolumesByLevel.empty() && metrics.totalBidVolume > 0) {
        // Check if top 2 levels contain majority of volume
        double topBidVolume = 0.0;
        int levelsToCheck = std::min(2, static_cast<int>(metrics.bidVolumesByLevel.size()));
        
        for (int i = 0; i < levelsToCheck; ++i) {
            topBidVolume += metrics.bidVolumesByLevel[i];
        }
        
        if (topBidVolume / metrics.totalBidVolume > CONCENTRATION_THRESHOLD) {
            return true;
        }
    }
    
    // Check ask side concentration
    if (!metrics.askVolumesByLevel.empty() && metrics.totalAskVolume > 0) {
        double topAskVolume = 0.0;
        int levelsToCheck = std::min(2, static_cast<int>(metrics.askVolumesByLevel.size()));
        
        for (int i = 0; i < levelsToCheck; ++i) {
            topAskVolume += metrics.askVolumesByLevel[i];
        }
        
        if (topAskVolume / metrics.totalAskVolume > CONCENTRATION_THRESHOLD) {
            return true;
        }
    }
    
    return false;
}
bool ImbalanceDetectionEngine::detectLargeOrderDisappearance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectOrderFlowImbalance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectPriceLevelConcentration(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectLiquidityImbalance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectTrendContinuation(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectTrendReversal(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectTradeImbalance(const OrderBookMetrics& metrics)   {
    return false;
}
bool ImbalanceDetectionEngine::detectCancellationImbalance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectPriceVolatilityImbalance(const OrderBookMetrics& metrics){
    return false;
}

bool ImbalanceDetectionEngine::detectCustomImbalance(const OrderBookMetrics& metrics){
    return false;
}
bool ImbalanceDetectionEngine::detectHiddenLiquidityImbalance(const OrderBookMetrics& metrics) {
    // Configuration thresholds for hidden liquidity detection
    static const double VELOCITY_ANOMALY_THRESHOLD = 0.5;          // Significant velocity changes
    static const double PRESSURE_VELOCITY_CORRELATION = 0.3;      // Pressure vs velocity correlation
    static const double DEPTH_VOLUME_INCONSISTENCY = 0.4;         // Depth doesn't match volume
    static const double LIQUIDITY_PROFILE_ANOMALY = 0.6;          // Unusual liquidity profile
    static const double SPREAD_PERSISTENCE_THRESHOLD = 0.8;       // Spread remains stable despite volume
    static const double VOLATILITY_SUPPRESSION_THRESHOLD = 0.3;   // Volatility lower than expected
    static const double FLOW_RESISTANCE_THRESHOLD = 0.4;          // Resistance to price movement
    static const double MARKET_IMPACT_ANOMALY = 0.5;              // Lower than expected market impact
    
    int hiddenLiquiditySignals = 0;
    const int REQUIRED_SIGNALS = 2;  // Need fewer signals as hidden liquidity is subtle
    
    // 1. Velocity Anomaly Detection
    // Hidden liquidity often causes unusual velocity patterns
    if (detectVelocityAnomalies(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 2. Spread Persistence Analysis
    // Hidden liquidity can keep spreads stable despite volume changes
    if (detectSpreadPersistence(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 3. Depth-Volume Inconsistency
    // Visible depth doesn't match the apparent market strength
    if (detectDepthVolumeInconsistency(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 4. Liquidity Profile Anomaly
    // The liquidity profile suggests more depth than visible
    if (metrics.liquidityProfileScore > LIQUIDITY_PROFILE_ANOMALY && 
        std::abs(metrics.depthImbalance) < 0.2) {  // Low visible imbalance but high profile
        hiddenLiquiditySignals++;
    }
    
    // 5. Volatility Suppression
    // Hidden liquidity often suppresses expected volatility
    if (detectVolatilitySuppression(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 6. Market Impact Resistance
    // Price movements are smaller than volume would suggest
    if (detectMarketImpactResistance(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 7. Flow Imbalance vs Price Stability
    // Significant flow imbalance but price remains stable
    double bidFlow = metrics.getMetric("bidFlow", 0.0);
    double askFlow = metrics.getMetric("askFlow", 0.0);
    double flowImbalance = std::abs(bidFlow - askFlow) / (bidFlow + askFlow + 1e-10);
    
    if (flowImbalance > FLOW_RESISTANCE_THRESHOLD && 
        std::abs(metrics.priceVelocity) < 0.1) {  // High flow imbalance but low price movement
        hiddenLiquiditySignals++;
    }
    
    // 8. Density-Pressure Mismatch
    // Order density doesn't match liquidity pressure
    if (detectDensityPressureMismatch(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 9. Time-Series Pattern Analysis
    // Look for patterns in historical metrics that suggest hidden liquidity
    if (detectHiddenLiquidityPatterns(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    // 10. Cross-Metric Correlation Anomalies
    // Correlations between metrics are different than expected
    if (detectCorrelationAnomalies(metrics)) {
        hiddenLiquiditySignals++;
    }
    
    return hiddenLiquiditySignals >= REQUIRED_SIGNALS;
}

// Helper function to detect velocity anomalies
bool ImbalanceDetectionEngine::detectVelocityAnomalies(const OrderBookMetrics& metrics) {
    // Check for unusual patterns in price, volume, and spread velocities
    
    // Price velocity vs volume velocity inconsistency
    double priceVolRatio = std::abs(metrics.priceVelocity) / (std::abs(metrics.volumeVelocity) + 1e-10);
    if (priceVolRatio < 0.2) {  // Price moves much less than volume suggests
        return true;
    }
    
    // Spread velocity vs other velocities
    double spreadVolRatio = std::abs(metrics.spreadVelocity) / (std::abs(metrics.volumeVelocity) + 1e-10);
    if (spreadVolRatio < 0.3) {  // Spread stable despite volume changes
        return true;
    }
    
    // Check if liquidity pressure doesn't match velocity patterns
    if (std::abs(metrics.liquidityPressure) > 0.3 && std::abs(metrics.priceVelocity) < 0.1) {
        return true;
    }
    
    return false;
}

// Helper function to detect spread persistence
bool ImbalanceDetectionEngine::detectSpreadPersistence(const OrderBookMetrics& metrics) {
    // Hidden liquidity often keeps spreads tight despite volume imbalances
    
    // Check if spread is stable despite volume imbalance
    if (std::abs(metrics.volumeImbalance) > 0.3 && metrics.spreadPercent < 0.05) {
        return true;
    }
    
    // Check if spread velocity is low despite other changes
    if (std::abs(metrics.spreadVelocity) < 0.1 && 
        (std::abs(metrics.volumeVelocity) > 0.3 || std::abs(metrics.priceVelocity) > 0.2)) {
        return true;
    }
    
    return false;
}

// Helper function to detect depth-volume inconsistency
bool ImbalanceDetectionEngine::detectDepthVolumeInconsistency(const OrderBookMetrics& metrics) {
    // Compare visible depth with market behavior
    
    // Calculate apparent market strength vs visible depth
    double apparentStrength = std::abs(metrics.marketDirectionalCoefficient) * 
                             std::abs(metrics.liquidityPressure);
    
    double visibleDepthStrength = (metrics.totalBidVolume + metrics.totalAskVolume) * 
                                 (1.0 - std::abs(metrics.depthImbalance));
    
    // If apparent strength is much higher than visible depth suggests
    if (apparentStrength > 0.4 && visibleDepthStrength < 0.3) {
        return true;
    }
    
    // Check for density vs volume mismatch
    double avgDensity = (metrics.bidDensity + metrics.askDensity) / 2.0;
    double totalVolume = metrics.totalBidVolume + metrics.totalAskVolume;
    
    if (avgDensity > 0.7 && totalVolume < 0.3) {  // High density but low visible volume
        return true;
    }
    
    return false;
}

// Helper function to detect volatility suppression
bool ImbalanceDetectionEngine::detectVolatilitySuppression(const OrderBookMetrics& metrics) {
    // Hidden liquidity often suppresses volatility below expected levels
    
    // Compare realized vs expected volatility
    if (metrics.volatilityExpectation > 0.3 && metrics.realizedVolatility < 0.15) {
        return true;
    }
    
    // Check if volatility is low despite market pressure
    if (std::abs(metrics.liquidityPressure) > 0.4 && metrics.realizedVolatility < 0.2) {
        return true;
    }
    
    // Volatility per unit time is lower than pressure suggests
    double expectedVolatility = std::abs(metrics.liquidityPressure) * 0.5;
    if (metrics.volatilityPerUnitTime < expectedVolatility * 0.5) {
        return true;
    }
    
    return false;
}

// Helper function to detect market impact resistance
bool ImbalanceDetectionEngine::detectMarketImpactResistance(const OrderBookMetrics& metrics) {
    // Price impact is lower than volume/pressure would suggest
    
    // Calculate expected price impact based on volume imbalance
    double expectedImpact = std::abs(metrics.volumeImbalance) * 0.3;
    if (expectedImpact > 0.2 && std::abs(metrics.priceVelocity) < expectedImpact * 0.3) {
        return true;
    }
    
    // Check if weighted imbalance suggests more impact than realized
    double expectedWeightedImpact = std::abs(metrics.weightedVolumeImbalance) * 0.4;
    if (expectedWeightedImpact > 0.15 && std::abs(metrics.priceVelocity) < expectedWeightedImpact * 0.25) {
        return true;
    }
    
    return false;
}

// Helper function to detect density-pressure mismatch
bool ImbalanceDetectionEngine::detectDensityPressureMismatch(const OrderBookMetrics& metrics) {
    // Order density patterns don't match liquidity pressure
    
    double densityImbalance = (metrics.bidDensity - metrics.askDensity) / 
                             (metrics.bidDensity + metrics.askDensity + 1e-10);
    
    // Density suggests one direction but pressure suggests another
    if (std::abs(densityImbalance) > 0.3 && std::abs(metrics.liquidityPressure) > 0.3) {
        // Check if they point in opposite directions
        if ((densityImbalance > 0 && metrics.liquidityPressure < -0.1) ||
            (densityImbalance < 0 && metrics.liquidityPressure > 0.1)) {
            return true;
        }
    }
    
    return false;
}

// Helper function to detect hidden liquidity patterns in time series
bool ImbalanceDetectionEngine::detectHiddenLiquidityPatterns(const OrderBookMetrics& metrics) {
    // This would analyze patterns in the timeSeriesMetrics map
    // For now, implement basic pattern detection
    
    // Check if we have time series data
    if (metrics.timeSeriesMetrics.empty()) {
        return false;
    }
    
    // Look for patterns where metrics suggest hidden liquidity
    // This is a simplified implementation - in practice, you'd want more sophisticated pattern recognition
    
    auto spreadSeries = metrics.timeSeriesMetrics.find("spreadPercent");
    auto volumeSeries = metrics.timeSeriesMetrics.find("totalVolume");
    
    if (spreadSeries != metrics.timeSeriesMetrics.end() && 
        volumeSeries != metrics.timeSeriesMetrics.end()) {
        
        const auto& spreads = spreadSeries->second;
        const auto& volumes = volumeSeries->second;
        
        if (spreads.size() >= 3 && volumes.size() >= 3) {
            // Check for spread stability despite volume changes
            double spreadVariance = calculateVariance(spreads);
            double volumeVariance = calculateVariance(volumes);
            
            if (spreadVariance < 0.01 && volumeVariance > 0.1) {  // Stable spread, volatile volume
                return true;
            }
        }
    }
    
    return false;
}

// Helper function to detect correlation anomalies
bool ImbalanceDetectionEngine::detectCorrelationAnomalies(const OrderBookMetrics& metrics) {
    // Check for unusual correlations between metrics that might indicate hidden liquidity
    
    // Example: High market directional coefficient but low depth imbalance
    if (std::abs(metrics.marketDirectionalCoefficient) > 0.6 && 
        std::abs(metrics.depthImbalance) < 0.2) {
        return true;
    }
    
    // High liquidity profile score but moderate visible volume
    if (metrics.liquidityProfileScore > 0.7 && 
        (metrics.totalBidVolume + metrics.totalAskVolume) < 0.5) {
        return true;
    }
    
    return false;
}

// Utility function to calculate variance
double ImbalanceDetectionEngine::calculateVariance(const std::vector<double>& data) {
    if (data.empty()) return 0.0;
    
    double mean = std::accumulate(data.begin(), data.end(), 0.0) / data.size();
    double variance = 0.0;
    
    for (double value : data) {
        variance += (value - mean) * (value - mean);
    }
    
    return variance / data.size();
}
// Calculate imbalance strength based on type and metrics
 double ImbalanceDetectionEngine::calculateImbalanceStrength(ImbalanceType type, const OrderBookMetrics& metrics) {
     double strength = 0.0;
     
     switch (type) {
         case ImbalanceType::VOLUME_IMBALANCE: {
             auto bidVolIt = metrics.metrics.find("bidVolume");
             auto askVolIt = metrics.metrics.find("askVolume");
             
             if (bidVolIt != metrics.metrics.end() && askVolIt != metrics.metrics.end()) {
                 double bidVol = bidVolIt->second;
                 double askVol = askVolIt->second;
                 
                 if (askVol < 1e-10) askVol = 1e-10;
                 double ratio = bidVol / askVol;
                 
                 strength = std::abs(ratio - 1.0);
             }
             break;
         }
         case ImbalanceType::PRICE_LEVEL_IMBALANCE: {
             auto bidLevelsIt = metrics.metrics.find("bidLevelCount");
             auto askLevelsIt = metrics.metrics.find("askLevelCount");
             
             if (bidLevelsIt != metrics.metrics.end() && askLevelsIt != metrics.metrics.end()) {
                 double bidLevels = bidLevelsIt->second;
                 double askLevels = askLevelsIt->second;
                 
                 if (askLevels < 1e-10) askLevels = 1e-10;
                 double ratio = bidLevels / askLevels;
                 
                 strength = std::abs(ratio - 1.0);
             }
             break;
         }
         case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE: {
             auto bidFlowIt = metrics.metrics.find("bidFlow");
             auto askFlowIt = metrics.metrics.find("askFlow");
             
             if (bidFlowIt != metrics.metrics.end() && askFlowIt != metrics.metrics.end()) {
                 double bidFlow = bidFlowIt->second;
                 double askFlow = askFlowIt->second;
                 
                 double netFlow = bidFlow - askFlow;
                 double totalFlow = std::abs(bidFlow) + std::abs(askFlow);
                 
                 if (totalFlow < 1e-10) totalFlow = 1e-10;
                 strength = std::abs(netFlow) / totalFlow;
             }
             break;
         }
         case ImbalanceType::ABNORMAL_SPREAD: {
             auto spreadIt = metrics.metrics.find("spreadPercent");
             auto avgSpreadIt = metrics.metrics.find("averageSpreadPercent");
             
             if (spreadIt != metrics.metrics.end() && avgSpreadIt != metrics.metrics.end()) {
                 double currentSpread = spreadIt->second;
                 double averageSpread = avgSpreadIt->second;
                 
                 if (averageSpread < 1e-10) averageSpread = 1e-10;
                 strength = (currentSpread / averageSpread) - 1.0;
                 
                 // Clamp negative values to zero
                 if (strength < 0) strength = 0;
             }
             break;
         }
         default:
             break;
     }
     
     return strength;
 }
 
 //this function need to be rewrite carefully and follow below logic
 /**

function determineDirection(metrics):

    scoreBuy = 0
    scoreSell = 0

    # --- Volume imbalance ---
    if has(bidVolume, askVolume):
        ratio = (bidVolume - askVolume) / (bidVolume + askVolume)
        if ratio > +0.2: scoreBuy += 1
        if ratio < -0.2: scoreSell += 1

    # --- Price level imbalance ---
    if has(bidLevels, askLevels):
        ratio = (bidLevels - askLevels) / (bidLevels + askLevels)
        if ratio > +0.3: scoreBuy += 1
        if ratio < -0.3: scoreSell += 1

    # --- Liquidity removal ---
    if has(liquidityRemovalSide):
        if liquidityRemovalSide == "bid": scoreSell += 2
        if liquidityRemovalSide == "ask": scoreBuy += 2

    # --- Order flow imbalance (submissions) ---
    if has(bidOrdersSubmitted, askOrdersSubmitted):
        ratio = (bidOrdersSubmitted - askOrdersSubmitted) / (total)
        if ratio > +0.2: scoreBuy += 1
        if ratio < -0.2: scoreSell += 1

    # --- Large order appearance/disappearance ---
    if has(largeBidOrderAppeared): scoreBuy += 1
    if has(largeAskOrderAppeared): scoreSell += 1
    if has(largeBidOrderRemoved):  scoreSell += 1
    if has(largeAskOrderRemoved):  scoreBuy += 1

    # --- Price level concentration ---
    if has(concentratedBidLevels): scoreBuy += 1
    if has(concentratedAskLevels): scoreSell += 1

    # --- Spread changes ---
    if has(spreadChange):
        if spreadChange < 0: scoreBuy += 1   # tighter spread bullish
        if spreadChange > 0: scoreSell += 1  # wider spread bearish

    if has(spread) and spread > ABNORMAL_THRESHOLD:
        if bidVolume > askVolume * 1.5: scoreBuy += 1
        else: scoreSell += 1

    # --- Market depth change ---
    if has(bidDepthChange, askDepthChange):
        diff = bidDepthChange - askDepthChange
        if diff > +THRESH: scoreBuy += 1
        if diff < -THRESH: scoreSell += 1

    # --- Liquidity flow imbalance ---
    if has(bidFlow, askFlow):
        ratio = (bidFlow - askFlow) / (bidFlow + askFlow)
        if ratio > +0.1: scoreBuy += 2
        if ratio < -0.1: scoreSell += 2

    # --- Trend signals ---
    if has(trendStrength):
        if trendStrength > TREND_THRESHOLD: scoreBuy += 1
        if trendStrength < -TREND_THRESHOLD: scoreSell += 1

    if has(prevTrendStrength, trendStrength):
        if prevTrendStrength > 0 and trendStrength < -TREND_THRESHOLD:
            scoreSell += 2
        if prevTrendStrength < 0 and trendStrength > +TREND_THRESHOLD:
            scoreBuy += 2

    # --- Trade imbalance (executed trades) ---
    if has(marketBuyVolume, marketSellVolume):
        ratio = (marketBuyVolume - marketSellVolume) / (total)
        if ratio > +0.2: scoreBuy += 2
        if ratio < -0.2: scoreSell += 2

    # --- Cancellation imbalance ---
    if has(bidCancellations, askCancellations):
        ratio = (bidCancels - askCancels) / (total)
        if ratio > +0.2: scoreSell += 1    # buyers pulling liquidity
        if ratio < -0.2: scoreBuy += 1     # sellers pulling liquidity

    # --- Price volatility imbalance ---
    if has(volatility):
        if volatility > VOLATILITY_THRESHOLD:
            if lastMoveUp: scoreBuy += 1
            else: scoreSell += 1

    # --- Hidden liquidity imbalance (icebergs) ---
    if has(icebergOnBid): scoreBuy += 2
    if has(icebergOnAsk): scoreSell += 2

    # --- Custom ---
    if has(customSignal):
        if customSignal > 0: scoreBuy += 1
        if customSignal < 0: scoreSell += 1

    # --- Final decision ---
    if scoreBuy > scoreSell and scoreBuy >= MIN_CONFIDENCE:
        return BUY
    if scoreSell > scoreBuy and scoreSell >= MIN_CONFIDENCE:
        return SELL
    return NEUTRAL

  * 
  */

 // Determine imbalance direction based on type and metrics
 Direction ImbalanceDetectionEngine::determineImbalanceDirection(ImbalanceType type, const OrderBookMetrics& metrics) {
     Direction direction = Direction::NEUTRAL;
     
     switch (type) {
         case ImbalanceType::VOLUME_IMBALANCE: {
             auto bidVolIt = metrics.metrics.find("bidVolume");
             auto askVolIt = metrics.metrics.find("askVolume");
             
             if (bidVolIt != metrics.metrics.end() && askVolIt != metrics.metrics.end()) {
                 double bidVol = bidVolIt->second;
                 double askVol = askVolIt->second;
                 
                 direction = (bidVol > askVol) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::PRICE_LEVEL_IMBALANCE: {
             auto bidLevelsIt = metrics.metrics.find("bidLevelCount");
             auto askLevelsIt = metrics.metrics.find("askLevelCount");
             
             if (bidLevelsIt != metrics.metrics.end() && askLevelsIt != metrics.metrics.end()) {
                 double bidLevels = bidLevelsIt->second;
                 double askLevels = askLevelsIt->second;
                 
                 direction = (bidLevels > askLevels) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE: {
             auto bidFlowIt = metrics.metrics.find("bidFlow");
             auto askFlowIt = metrics.metrics.find("askFlow");
             
             if (bidFlowIt != metrics.metrics.end() && askFlowIt != metrics.metrics.end()) {
                 double bidFlow = bidFlowIt->second;
                 double askFlow = askFlowIt->second;
                 
                 direction = (bidFlow > askFlow) ? Direction::BUY : Direction::SELL;
             }
             break;
         }
         case ImbalanceType::ABNORMAL_SPREAD: {
             // For abnormal spread, direction determination is more complex
             // Default to SELL as widening spread often indicates selling pressure
             direction = Direction::SELL;
             
             // Check if there's additional data to determine direction more accurately
             auto bidVolumeIt = metrics.metrics.find("bidVolume");
             auto askVolumeIt = metrics.metrics.find("askVolume");
             
             if (bidVolumeIt != metrics.metrics.end() && askVolumeIt != metrics.metrics.end()) {
                 double bidVolume = bidVolumeIt->second;
                 double askVolume = askVolumeIt->second;
                 
                 // If bid volume is significantly higher, it might indicate buying pressure despite the wide spread
                 if (bidVolume > askVolume * 1.5) {
                     direction = Direction::BUY;
                 }
             }
             break;
        }
         case ImbalanceType::MARKET_DEPTH_CHANGE:
         //is this correct?
             direction = (metrics.metrics.at("depthChange") > 0) ? Direction::BUY : Direction::SELL;
             break;
         case ImbalanceType::TREND_CONTINUATION:
             direction = (metrics.metrics.at("trendStrength") > 0) ? Direction::BUY : Direction::SELL;
             break;
         case ImbalanceType::TREND_REVERSAL:
             direction = (metrics.metrics.at("trendStrength") < 0) ? Direction::BUY : Direction::SELL;
             break;
         case ImbalanceType::CUSTOM:
             direction = (metrics.metrics.at("customSignal") > 0) ? Direction::BUY : Direction::SELL;
             break;
         default:
             break;
    }
     return direction;
 }

 void ImbalanceDetectionEngine::createImbalanceEvent(ImbalanceType type, double strength, Direction direction, const OrderBookMetrics& metrics) {
    // Validate inputs
    if (strength < 0.0 || strength > 1.0) {
        LOG_WARN("Invalid strength value: {}", strength);
        return;
    }
    
    if (direction == Direction::NEUTRAL) {
        LOG_WARN("Cannot create imbalance event with NEUTRAL direction");
        return;
    }
    
    ImbalanceEvent event;
    event.type = type;
    event.strength = std::clamp(strength, 0.0, 1.0);
    event.direction = direction;
    event.detectionTime = std::chrono::system_clock::now();
    event.exchangeId = metrics.exchangeId;
    event.symbol = metrics.symbol;
    event.confidence = trading::core::valueToConfidenceLevel(calculateImbalanceConfidence(type, strength, metrics)); 
    event.eventId = generateEventId();
    
    // Copy relevant metrics
    for (const auto& metric : metrics.metrics) {
        event.supportingMetrics[metric.first] = metric.second;
    }
    
    // Choose ONE method: either local storage OR event bus, not both
    // Option 1: Local storage only (current caller pattern)
    // detectedImbalances_.push_back(event);
    
    // Option 2: Event bus only (better for decoupling)
    LOG_DEBUG("Publishing ImbalanceDetectedEvent: type={} strength={:.3f} symbol={} exchange={}",
              static_cast<int>(event.type), event.strength, event.symbol.toString(), event.exchangeId.toString());
    common::EventBus::getInstance().publish(events::ImbalanceDetectedEvent{event});
    LOG_DEBUG("Published ImbalanceDetectedEvent id={} for {}:{}", event.eventId, event.exchangeId.toString(), event.symbol.toString());
}
 void ImbalanceDetectionEngine::notifyImbalanceCallbacks(const ImbalanceEvent& event) {
    // Simply publish to event bus - no locks, no deadlocks
    common::EventBus::getInstance().publish(events::ImbalanceDetectedEvent{event});
}
 // Generate a unique event ID
 uint64_t ImbalanceDetectionEngine::generateEventId() {
     static uint64_t nextId = 0;
     return ++nextId;
 }
//  double ImbalanceDetectionEngine::calculateImbalanceConfidence(ImbalanceType type, double strength, const OrderBookMetrics& metrics) {
//     // Base confidence directly on normalized strength (0-1)
//     double baseConfidence = 0.5 + (strength * 0.5);  // Maps 0-1 strength to 0.5-1.0 confidence
    
//     // Adjust confidence based on market conditions if available
//     auto volatilityIt = metrics.metrics.find("volatility");
//     if (volatilityIt != metrics.metrics.end()) {
//         double volatility = volatilityIt->second;
        
//         // High volatility reduces confidence (but less aggressively)
//         if (volatility > 0.01) {  // 1% volatility threshold
//             double volatilityPenalty = std::min(0.3, volatility * 2.0);  // Cap penalty at 30%
//             baseConfidence *= (1.0 - volatilityPenalty);
            
//             // Ensure confidence stays in valid range
//             baseConfidence = std::max(0.5, baseConfidence);
//         }
//     }
    
//     // Round to 2 decimal places
//     return std::round(baseConfidence * 100.0) / 100.0;
// }
 
 } // namespace strategy
 } // namespace trading