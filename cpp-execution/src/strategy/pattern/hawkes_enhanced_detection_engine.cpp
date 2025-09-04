/**
 * @file hawkes_enhanced_detection_engine.cpp
 * @brief Implementation of enhanced imbalance detection with Hawkes-Markov model
 * 
 * Created on: May 8, 2025
 */

 #include "strategy/pattern/hawkes_enhanced_detection_engine.hpp"
 #include <iostream>
 #include <algorithm>
 
 namespace trading {
 namespace strategy {
 
 // Constructor
 HawkesEnhancedDetectionEngine::HawkesEnhancedDetectionEngine() {
     // Create component instances
     imbalanceEngine_ = std::make_shared<ImbalanceDetectionEngine>();
     hawkesDetector_ = std::make_shared<HawkesMarkovDetector>();
 }
 
 // Initialize the enhanced engine
 bool HawkesEnhancedDetectionEngine::initialize(const ImbalanceDetectionConfig& imbalanceConfig,
                                             const HawkesMarkovConfig& hawkesConfig) {
     // Initialize both components
     bool imbalanceInitOk = imbalanceEngine_->initialize(imbalanceConfig);
     bool hawkesInitOk = hawkesDetector_->initialize(hawkesConfig);
     
     // Register callback to receive Hawkes signals
     hawkesDetector_->registerSignalCallback(
         std::bind(&HawkesEnhancedDetectionEngine::onHawkesSignal, this, std::placeholders::_1));
     
     return imbalanceInitOk && hawkesInitOk;
 }
 
 // Process a new order book update
 bool HawkesEnhancedDetectionEngine::processOrderBook(const data_ingestion::OrderBook& orderBook,
                                                   const OrderBookMetrics& metrics) {
     // Process with standard imbalance engine
     bool imbalanceResult = imbalanceEngine_->analyzeMetrics(metrics);
     
     // Convert to Hawkes event
     OrderBookEvent event = convertToHawkesEvent(orderBook.getSnapshot());
     
     // Store in recent trades history (simplified)
     recentTrades_.push_back(event);
     if (recentTrades_.size() > 100) {  // Keep only last 100 trades
         recentTrades_.erase(recentTrades_.begin());
     }
     
     // Process with Hawkes detector
     double processingTime = hawkesDetector_->processEvent(event, orderBook, recentTrades_);
     
     // Make trading decision based on current price
     double currentPrice = (orderBook.getMidPrice() > 0) ? 
                          orderBook.getMidPrice() : 
                          (orderBook.getBestBid().price + orderBook.getBestAsk().price) / 2.0;
     
     // Get current timestamp
     double currentTime = std::chrono::duration_cast<std::chrono::duration<double>>(
         std::chrono::high_resolution_clock::now().time_since_epoch()).count();
     
     // Get Hawkes signal
     HawkesSignal signal = hawkesDetector_->makeDecision(currentPrice, currentTime);
     
     return imbalanceResult;
 }
 
 // Convert order book update to Hawkes event
 OrderBookEvent HawkesEnhancedDetectionEngine::convertToHawkesEvent(const data_ingestion::OrderBookUpdate& update) {
     OrderBookEvent event;
     
     // Determine event type based on order book state
     // This is a simplified heuristic - in a real system you'd have more sophisticated logic
     
     auto bids = update.bids;
     auto asks = update.asks;
     
     double bidVolume = 0.0;
     double askVolume = 0.0;
     
     for (const auto& level : bids) {
         bidVolume += level.quantity;
     }
     
     for (const auto& level : asks) {
         askVolume += level.quantity;
     }
     
     // Determine event type based on imbalance
     if (bidVolume > askVolume * 1.5) {
         event.type = 2;  // Large buy pressure
     } else if (askVolume > bidVolume * 1.5) {
         event.type = 3;  // Large sell pressure
     } else if (bidVolume > askVolume * 1.1) {
         event.type = 0;  // Small buy pressure
     } else if (askVolume > bidVolume * 1.1) {
         event.type = 1;  // Small sell pressure
     } else {
         // Check for cancellations (would require tracking previous state)
         event.type = (bidVolume > askVolume) ? 0 : 1;  // Default to small buy/sell
     }
     
     // Set event properties
     event.timestamp = std::chrono::duration_cast<std::chrono::duration<double>>(
         std::chrono::high_resolution_clock::now().time_since_epoch()).count();
     event.volume = bidVolume + askVolume;
     event.exchangeId = update.exchangeId;
     event.symbol = update.symbol;
     
     return event;
 }
 
 // Enhance imbalance event with Hawkes signal
 ImbalanceEvent HawkesEnhancedDetectionEngine::enhanceWithHawkes(
     const ImbalanceEvent& event, const HawkesSignal& signal) {
     
     ImbalanceEvent enhanced = event;
     
     // Only enhance if both signals are in the same direction
     if ((event.direction == Direction::BUY && signal.direction == Direction::BUY) ||
         (event.direction == Direction::SELL && signal.direction == Direction::SELL)) {
         
         // Boost confidence based on Hawkes signal
         double newConfidenceValue = std::min(0.95, core::confidenceLevelToValue(event.confidence) + 0.1 * signal.strength);
         enhanced.confidence = core::valueToConfidenceLevel(newConfidenceValue);
         
         // Boost strength
         enhanced.strength = std::min(event.strength * 1.2, event.strength + signal.strength * 0.5);
         
         // Add Hawkes metrics to supporting metrics
         enhanced.supportingMetrics["hawkes_strength"] = signal.strength;
         enhanced.supportingMetrics["hawkes_confidence"] = signal.confidence;
     }
     
     return enhanced;
 }
 
 // Callback handler for Hawkes signals
 void HawkesEnhancedDetectionEngine::onHawkesSignal(const HawkesSignal& signal) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Get existing imbalance events
     auto events = imbalanceEngine_->getDetectedImbalances();
     
     // Update combined imbalances
     combinedImbalances_.clear();
     
     // Process each imbalance event
     for (const auto& event : events) {
         // If directions match, enhance the event with Hawkes signal
         if ((event.direction == Direction::BUY && signal.direction == Direction::BUY) ||
             (event.direction == Direction::SELL && signal.direction == Direction::SELL)) {
             
             combinedImbalances_.push_back(enhanceWithHawkes(event, signal));
         } else {
             combinedImbalances_.push_back(event);
         }
     }
     
     // If Hawkes detected something not in standard events, create a new event
     if (!events.empty() && signal.direction != Direction::NEUTRAL) {
         bool found = false;
         for (const auto& event : events) {
             if (event.direction == signal.direction) {
                 found = true;
                 break;
             }
         }
         
         if (!found) {
             // Create a new imbalance event from Hawkes signal
             ImbalanceEvent newEvent;
             newEvent.type = ImbalanceType::VOLUME_IMBALANCE;  // Default type
             newEvent.direction = signal.direction;
             newEvent.strength = signal.strength;
            //  newEvent.confidence = signal.confidence;
             newEvent.confidence = core::valueToConfidenceLevel(signal.confidence);
             newEvent.detectionTime = std::chrono::system_clock::now();
             newEvent.eventId = 0;  // To be assigned
             newEvent.supportingMetrics["hawkes_origin"] = 1.0;
             
             combinedImbalances_.push_back(newEvent);
         }
     }
 }
 
 // Get all detected imbalance events
 std::vector<ImbalanceEvent> HawkesEnhancedDetectionEngine::getDetectedImbalances() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // If we have combined results, return those
     if (!combinedImbalances_.empty()) {
         return combinedImbalances_;
     }
     
     // Otherwise, return standard imbalance events
     return imbalanceEngine_->getDetectedImbalances();
 }
 
 // Register callback for imbalance events
 size_t HawkesEnhancedDetectionEngine::registerImbalanceCallback(const ImbalanceCallback& callback) {
     // Pass through to standard imbalance engine
     return imbalanceEngine_->registerImbalanceCallback(callback);
 }
 
 // Get the engine status
 std::string HawkesEnhancedDetectionEngine::getStatus() const {
     std::string status = "HawkesEnhancedDetectionEngine Status:\n";
     status += "=== Standard Imbalance Engine ===\n";
     status += imbalanceEngine_->getStatus();
     status += "\n=== Hawkes-Markov Detector ===\n";
     status += hawkesDetector_->getStatus();
     status += "\n=== Combined Metrics ===\n";
     status += "  Combined imbalances: " + std::to_string(combinedImbalances_.size()) + "\n";
     
     return status;
 }
 
 } // namespace strategy
 } // namespace trading