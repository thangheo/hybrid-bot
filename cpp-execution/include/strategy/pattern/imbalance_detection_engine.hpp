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

#include "data_ingestion/orderbook/order_book.hpp"
#include <vector>
#include <memory>
#include <map>
#include <string>
#include <mutex>
#include <functional>

namespace trading {
namespace strategy {

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
    virtual ~ImbalanceDetectionEngine() = default;
    
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
    // Additional private methods
    uint64_t generateEventId();
    double calculateImbalanceConfidence(ImbalanceType type, double strength, const OrderBookMetrics& metrics);
    // Configuration
    ImbalanceDetectionConfig config_;
    ThresholdSet thresholds_;
    
    // Detection state
    std::vector<ImbalanceEvent> detectedImbalances_;
    std::map<ImbalanceType, bool> imbalanceState_;
    std::map<std::string, double> metricHistory_;
    
    // Detection algorithms
    bool detectVolumeImbalance(const OrderBookMetrics& metrics);
    bool detectPriceLevelImbalance(const OrderBookMetrics& metrics);
    bool detectLiquidityFlowImbalance(const OrderBookMetrics& metrics);
    bool detectAbnormalSpread(const OrderBookMetrics& metrics);
    
    // Imbalance evaluation
    double calculateImbalanceStrength(ImbalanceType type, const OrderBookMetrics& metrics);
    core::Direction determineImbalanceDirection(ImbalanceType type, const OrderBookMetrics& metrics);
    void createImbalanceEvent(ImbalanceType type, double strength, core::Direction direction, const OrderBookMetrics& metrics);
    
    // Callback management
    std::map<size_t, ImbalanceCallback> callbacks_;
    size_t nextCallbackId_;
    void notifyImbalanceCallbacks(const ImbalanceEvent& event);
    
    // Thread safety
    mutable std::mutex mutex_;
};

// Type alias
using ImbalanceDetectionEnginePtr = std::shared_ptr<ImbalanceDetectionEngine>;
using ImbalanceDetectionEngineConstPtr = std::shared_ptr<const ImbalanceDetectionEngine>;

} // namespace strategy
} // namespace trading