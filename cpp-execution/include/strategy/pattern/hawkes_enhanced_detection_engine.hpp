/**
 * @file hawkes_enhanced_detection_engine.hpp
 * @brief Enhanced imbalance detection engine using Hawkes-Markov model
 * 
 * @details
 * This class integrates the standard ImbalanceDetectionEngine with the 
 * HawkesMarkovDetector to provide enhanced imbalance detection with
 * better temporal modeling.
 * 
 * Created on: May 8, 2025
 */
#pragma once

#include "strategy/pattern/imbalance_detection_engine.hpp"
#include "strategy/pattern/hawkes_markov_detector.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include <memory>

namespace trading {
namespace strategy {

/**
 * @class HawkesEnhancedDetectionEngine
 * @brief Enhanced imbalance detection engine that integrates Hawkes-Markov model
 */
class HawkesEnhancedDetectionEngine {
public:
    /**
     * @brief Constructor
     */
    HawkesEnhancedDetectionEngine();
    
    /**
     * @brief Destructor
     */
    virtual ~HawkesEnhancedDetectionEngine() = default;
    
    /**
     * @brief Initialize the enhanced engine
     * @param imbalanceConfig Configuration for standard imbalance detection
     * @param hawkesConfig Configuration for Hawkes-Markov model
     * @return True if initialization successful, false otherwise
     */
    bool initialize(const ImbalanceDetectionConfig& imbalanceConfig,
                   const HawkesMarkovConfig& hawkesConfig);
    
    /**
     * @brief Process a new order book update
     * @param orderBook The order book to analyze
     * @param metrics Pre-calculated metrics from the order book
     * @return True if processing was successful, false otherwise
     */
    bool processOrderBook(const data_ingestion::OrderBook& orderBook,
                         const OrderBookMetrics& metrics);
    
    /**
     * @brief Get all detected imbalance events
     * @return Vector of imbalance events
     */
    std::vector<ImbalanceEvent> getDetectedImbalances() const;
    
    /**
     * @brief Register callback for imbalance events
     * @param callback Function to call when imbalance is detected
     * @return Callback ID
     */
    size_t registerImbalanceCallback(const ImbalanceCallback& callback);
    
    /**
     * @brief Get the engine status
     * @return Status as a string
     */
    std::string getStatus() const;
    
private:
    // Component instances
    ImbalanceDetectionEnginePtr imbalanceEngine_;
    HawkesMarkovDetectorPtr hawkesDetector_;
    
    // Event history for Hawkes model
    std::vector<OrderBookEvent> recentTrades_;
    
    // Event transformation methods
    // convertToHawkesEvent
    OrderBookEvent convertToHawkesEvent(const data_ingestion::OrderBookUpdate& update);

    ImbalanceEvent enhanceWithHawkes(const ImbalanceEvent& event, const HawkesSignal& signal);
    
    // Signal callback from Hawkes model
    void onHawkesSignal(const HawkesSignal& signal);
    
    // Last signals from each component
    std::vector<ImbalanceEvent> combinedImbalances_;
    std::vector<HawkesSignal> hawkesSignals_;
    mutable std::mutex mutex_;
};

// Type alias for convenience
using HawkesEnhancedDetectionEnginePtr = std::shared_ptr<HawkesEnhancedDetectionEngine>;

} // namespace strategy
} // namespace trading