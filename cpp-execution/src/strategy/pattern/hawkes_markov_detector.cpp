/**
 * @file hawkes_markov_detector.cpp
 * @brief Implementation of Hawkes-Markov model for order book imbalance detection
 * 
 * @details
 * Implements a Hawkes-Markov model to enhance imbalance detection by
 * modeling the temporal dynamics of order book events.
 * 
 * Created on: May 8, 2025
 */

 #include "strategy/pattern/hawkes_markov_detector.hpp"
 #include "data_ingestion/orderbook/order_book_types.hpp"
 #include <algorithm>
 #include <cmath>
 #include <iostream>
 #include <numeric>
 #include <chrono>
 
 namespace trading {
 namespace strategy {
 
 // Constructor
 HawkesMarkovDetector::HawkesMarkovDetector() 
     : currentState_(0), previousState_(0), lastPrediction_(0), nextCallbackId_(0) {
     // Initialize with empty state
 }
 
 // Initialize the detector with configuration
 bool HawkesMarkovDetector::initialize(const HawkesMarkovConfig& config) {
     std::lock_guard<std::mutex> lock(mutex_);
     config_ = config;
     
     // Initialize transition matrix (initially uniform)
     size_t numStates = config_.numStates;
     transitionMatrix_.resize(numStates);
     for (auto& row : transitionMatrix_) {
         row.resize(numStates, 1.0 / numStates);
     }
     
     // Initialize decay lookup tables
     initializeExpLookup();
     
     // Clear buffers
     eventBuffer_.clear();
     predictionErrors_.clear();
     predictions_.clear();
     actualOutcomes_.clear();
     
     return true;
 }
 
 // Initialize exponential decay lookup tables
 void HawkesMarkovDetector::initializeExpLookup() {
     // Create lookup tables for 3 seconds with 1ms resolution
     double maxTime = config_.timeWindow;
     double resolution = 0.001;  // 1ms
     size_t tableSize = static_cast<size_t>(maxTime / resolution) + 1;
     
     expLookupShort_.resize(tableSize);
     expLookupMedium_.resize(tableSize);
     expLookupLong_.resize(tableSize);
     
     for (size_t i = 0; i < tableSize; ++i) {
         double t = i * resolution;
         expLookupShort_[i] = std::exp(-config_.betaShort * t);
         expLookupMedium_[i] = std::exp(-config_.betaMedium * t);
         expLookupLong_[i] = std::exp(-config_.betaLong * t);
     }
 }
 
// Get decay factor from lookup table
double HawkesMarkovDetector::getDecayFactor(double dt, const std::string& decayType) const {
    if (dt >= config_.timeWindow) {
        return 0.0;
    }
    
    size_t idx = std::min(static_cast<size_t>(dt * 1000), expLookupShort_.size() - 1);
    
    if (decayType == "short") {
        return expLookupShort_[idx];
    } else if (decayType == "medium") {
        return expLookupMedium_[idx];
    } else if (decayType == "long") {
        return expLookupLong_[idx];
    } else {
        return 0.0;
    }
}
 
 // Process a new order book event
 double HawkesMarkovDetector::processEvent(const OrderBookEvent& event,
                                         const data_ingestion::OrderBook& orderBook,
                                         const std::vector<OrderBookEvent>& recentTrades) {
     auto startTime = std::chrono::high_resolution_clock::now();
     std::lock_guard<std::mutex> lock(mutex_);
     
     // 1. Update event buffer
     eventBuffer_.push_back(event);
     if (eventBuffer_.size() > config_.eventWindowSize) {
         eventBuffer_.pop_front();
     }
     
     // 2. Calculate current market state
     previousState_ = currentState_;
     currentState_ = calculateMarketState(orderBook, recentTrades);
     
     // 3. Update Markov transition matrix
     if (previousState_ < config_.numStates) {
         double alpha = config_.markovLearningRate;
         transitionMatrix_[previousState_][currentState_] = 
             (1 - alpha) * transitionMatrix_[previousState_][currentState_] + alpha;
         
         // Normalize row to sum to 1
         double rowSum = std::accumulate(transitionMatrix_[previousState_].begin(), 
                                         transitionMatrix_[previousState_].end(), 0.0);
         if (rowSum > 0) {
             for (auto& value : transitionMatrix_[previousState_]) {
                 value /= rowSum;
             }
         }
     }
     
     // 4. Track prediction performance
     if (previousState_ < config_.numStates) {
         // Store prediction results
         predictions_.push_back(lastPrediction_);
         actualOutcomes_.push_back(currentState_);
         
         // Keep buffer at reasonable size
         if (predictions_.size() > 100) {
             predictions_.pop_front();
             actualOutcomes_.pop_front();
         }
         
         // Calculate prediction error
         double error = (lastPrediction_ != currentState_) ? 1.0 : 0.0;
         predictionErrors_.push_back(error);
         
         // Keep buffer at reasonable size
         if (predictionErrors_.size() > 100) {
             predictionErrors_.pop_front();
         }
         
         // Check for market regime change
         if (predictionErrors_.size() >= 50) {
             std::vector<double> recentErrors(predictionErrors_.end() - 50, predictionErrors_.end());
             double errorRate = std::accumulate(recentErrors.begin(), recentErrors.end(), 0.0) / recentErrors.size();
             
             if (errorRate > config_.marketRegimeThreshold) {
                 // Market regime change detected, increase learning rate
                 config_.markovLearningRate = std::min(
                     0.5, config_.markovLearningRate * 2.0);
             } else {
                 // Market stable, gradually reduce learning rate
                 config_.markovLearningRate = std::max(
                     0.05, config_.markovLearningRate * 0.99);
             }
         }
     }
     
     // 5. Make prediction for next state
     lastPrediction_ = predictNextState();
     
     // Calculate processing time
     auto endTime = std::chrono::high_resolution_clock::now();
     std::chrono::duration<double, std::milli> duration = endTime - startTime;
     
     return duration.count();
 }
 
 // Calculate the current market state based on order book and trades
 uint32_t HawkesMarkovDetector::calculateMarketState(const data_ingestion::OrderBook& orderBook,
                                                  const std::vector<OrderBookEvent>& recentTrades) {
     // Get bid and ask data from order book
     auto bids = orderBook.getBids(5);  // Top 5 levels
     auto asks = orderBook.getAsks(5);  // Top 5 levels
     
     // 1. Calculate order book imbalance
     double bidVolume = 0.0;
     double askVolume = 0.0;
     
     for (const auto& level : bids) {
         bidVolume += level.getQuantity();
     }
     
     for (const auto& level : asks) {
         askVolume += level.getQuantity();
     }
     
     int imbalance;
     if (bidVolume > askVolume * 1.5) {
         imbalance = 0;  // Strong buy pressure
     } else if (askVolume > bidVolume * 1.5) {
         imbalance = 2;  // Strong sell pressure
     } else {
         imbalance = 1;  // Balanced
     }
     
     // 2. Calculate recent price trend
     int trend = 1;  // Default: sideways
     
     if (recentTrades.size() >= 20) {
         std::vector<double> prices;
         for (size_t i = recentTrades.size() - 20; i < recentTrades.size(); ++i) {
             // Assuming volume-weighted price is available
             prices.push_back(recentTrades[i].volume);
         }
         
         double firstPrices = std::accumulate(prices.begin(), prices.begin() + 10, 0.0) / 10.0;
         double lastPrices = std::accumulate(prices.begin() + 10, prices.end(), 0.0) / 10.0;
         
         if (lastPrices > firstPrices * 1.002) {
             trend = 0;  // Uptrend
         } else if (firstPrices > lastPrices * 1.002) {
             trend = 2;  // Downtrend
         }
     }
     
     // 3. Calculate volatility
     int volatility = 0;  // Default: low volatility
     
     if (recentTrades.size() >= 30) {
         std::vector<double> prices;
         for (size_t i = recentTrades.size() - 30; i < recentTrades.size(); ++i) {
             prices.push_back(recentTrades[i].volume);
         }
         
         std::vector<double> returns(prices.size() - 1);
         for (size_t i = 0; i < prices.size() - 1; ++i) {
             returns[i] = (prices[i + 1] - prices[i]) / prices[i];
         }
         
         // Calculate standard deviation
         double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
         double sq_sum = std::inner_product(returns.begin(), returns.end(), returns.begin(), 0.0,
                                           std::plus<>(), [mean](double a, double b) {
                                               return (a - mean) * (b - mean);
                                           });
         double vol = std::sqrt(sq_sum / returns.size()) * 100.0;  // Convert to percentage
         
         if (vol > 0.1) {
             volatility = 2;  // High volatility
         } else if (vol > 0.05) {
             volatility = 1;  // Medium volatility
         }
     }
     
     // 4. Market depth
     int depth = (bidVolume + askVolume < 100.0) ? 1 : 0;  // 1 = shallow, 0 = deep
     
     // 5. Trade speed
     int tradeSpeed = 0;  // Default: slow
     
     if (recentTrades.size() >= 20) {
         double oldestTime = recentTrades[recentTrades.size() - 20].timestamp;
         double newestTime = recentTrades[recentTrades.size() - 1].timestamp;
         
         if (newestTime - oldestTime < 5.0) {
             tradeSpeed = 2;  // Fast
         } else if (newestTime - oldestTime < 15.0) {
             tradeSpeed = 1;  // Medium
         }
     }
     
     // Combine features to create a unique state ID
     uint32_t stateId = static_cast<uint32_t>(
         imbalance * std::pow(3, 4) +
         trend * std::pow(3, 3) +
         volatility * std::pow(3, 2) +
         depth * std::pow(3, 1) +
         tradeSpeed * std::pow(3, 0)
     );
     
     // Map to [0, numStates-1] range
     return std::min(stateId, config_.numStates - 1);
 }
 
 // Predict the next state based on the current state
 uint32_t HawkesMarkovDetector::predictNextState() {
     if (currentState_ >= config_.numStates) {
         return 0;  // Default state if current state is invalid
     }
     
     // Find the state with highest transition probability
     const auto& row = transitionMatrix_[currentState_];
     return static_cast<uint32_t>(std::distance(row.begin(), 
                                              std::max_element(row.begin(), row.end())));
 }
 double HawkesMarkovDetector::calculateHawkesIntensity(uint8_t eventType, double currentTime) const {
 // Calculate Hawkes intensity for a specific event type
//  double HawkesMarkovDetector::calculateHawkesIntensity(uint8_t eventType, double currentTime) {
     double intensity = config_.hawkesBaseIntensity;
     
     if (eventBuffer_.empty()) {
         return intensity;
     }
     
     // Calculate influence from previous events
     for (const auto& event : eventBuffer_) {
         double dt = currentTime - event.timestamp;
         
         if (dt <= 0 || dt >= config_.timeWindow) {
             continue;
         }
         
         // Calculate composite decay from three components
         double decayShort = getDecayFactor(dt, "short");
         double decayMedium = getDecayFactor(dt, "medium");
         double decayLong = getDecayFactor(dt, "long");
         
         // Calculate excitation factor
         double excitation = (
             config_.alphaShort * decayShort +
             config_.alphaMedium * decayMedium +
             config_.alphaLong * decayLong
         );
         
         // Add to total intensity
         intensity += config_.excitationMatrix[event.type][eventType] * excitation;
     }
     
     return std::max(0.0, intensity);
 }
 
 // Make a trading decision based on current model state
 HawkesSignal HawkesMarkovDetector::makeDecision(double currentPrice, double currentTime) {
     std::lock_guard<std::mutex> lock(mutex_);
     
     // Default signal (no signal)
     HawkesSignal signal;
     signal.direction = Direction::NEUTRAL;
     signal.strength = 0.0;
     signal.confidence = 0.0;
     signal.timestamp = currentTime;
     signal.reason = "No significant signal detected";
     
     // Check if we have enough data
     if (currentState_ >= config_.numStates || eventBuffer_.empty()) {
         return signal;
     }
     
     // 1. Predict next market state
     uint32_t nextState = predictNextState();
     double stateConfidence = transitionMatrix_[currentState_][nextState];
     
     // 2. Calculate Hawkes intensities for large buy and sell events
     double buyIntensity = calculateHawkesIntensity(2, currentTime);  // Large buy orders
     double sellIntensity = calculateHawkesIntensity(3, currentTime); // Large sell orders
     
     // 3. Calculate signal strengths
     double buySignal = 0.0;
     double sellSignal = 0.0;
     
     // State-based component
     if (nextState < config_.numStates / 3) {
         // Lower states indicate bullish conditions
         buySignal += 0.6 * stateConfidence;
     } else if (nextState > 2 * config_.numStates / 3) {
         // Higher states indicate bearish conditions
         sellSignal += 0.6 * stateConfidence;
     }
     
     // Hawkes intensity component
     double totalIntensity = buyIntensity + sellIntensity;
     if (totalIntensity > 0) {
         buySignal += 0.4 * (buyIntensity / totalIntensity);
         sellSignal += 0.4 * (sellIntensity / totalIntensity);
     }
     
     // 4. Determine final signal
     if (buySignal > config_.predictionThreshold) {
         signal.direction = Direction::BUY;
         signal.strength = buySignal;
         signal.confidence = 0.5 + (buySignal - config_.predictionThreshold);
         signal.reason = "Strong buy imbalance detected";
         
         // Notify callbacks
         notifySignalCallbacks(signal);
     } else if (sellSignal > config_.predictionThreshold) {
         signal.direction = Direction::SELL;
         signal.strength = sellSignal;
         signal.confidence = 0.5 + (sellSignal - config_.predictionThreshold);
         signal.reason = "Strong sell imbalance detected";
         
         // Notify callbacks
         notifySignalCallbacks(signal);
     }
     
     return signal;
 }
 
 // Get current market state
 int32_t HawkesMarkovDetector::getCurrentState() const {
     std::lock_guard<std::mutex> lock(mutex_);
     return (currentState_ < config_.numStates) ? static_cast<int32_t>(currentState_) : -1;
 }
 
 // Get prediction accuracy
 double HawkesMarkovDetector::getPredictionAccuracy() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     if (predictions_.empty() || actualOutcomes_.empty() || predictions_.size() != actualOutcomes_.size()) {
         return 0.0;
     }
     
     size_t correct = 0;
     for (size_t i = 0; i < predictions_.size(); ++i) {
         if (predictions_[i] == actualOutcomes_[i]) {
             correct++;
         }
     }
     
     return static_cast<double>(correct) / predictions_.size();
 }
 
 // Register callback for signals
 size_t HawkesMarkovDetector::registerSignalCallback(
     const std::function<void(const HawkesSignal&)>& callback) {
     std::lock_guard<std::mutex> lock(mutex_);
     size_t id = nextCallbackId_++;
     callbacks_[id] = callback;
     return id;
 }
 
 // Unregister a callback
 bool HawkesMarkovDetector::unregisterSignalCallback(size_t callbackId) {
     std::lock_guard<std::mutex> lock(mutex_);
     auto it = callbacks_.find(callbackId);
     if (it != callbacks_.end()) {
         callbacks_.erase(it);
         return true;
     }
     return false;
 }
 
 // Notify callbacks about a new signal
 void HawkesMarkovDetector::notifySignalCallbacks(const HawkesSignal& signal) {
     // Copy callbacks to prevent deadlocks
     std::map<size_t, std::function<void(const HawkesSignal&)>> callbacksCopy;
     {
         std::lock_guard<std::mutex> lock(mutex_);
         callbacksCopy = callbacks_;
     }
     
     // Notify all callbacks
     for (const auto& cb : callbacksCopy) {
         try {
             cb.second(signal);
         } catch (const std::exception& e) {
             std::cerr << "Exception in Hawkes-Markov signal callback: " << e.what() << std::endl;
         }
     }
 }
 
 // Get the detector status
 std::string HawkesMarkovDetector::getStatus() const {
     std::lock_guard<std::mutex> lock(mutex_);
     
     std::string status = "HawkesMarkovDetector Status:\n";
     status += "  Current state: " + std::to_string(currentState_) + "\n";
     status += "  Event buffer size: " + std::to_string(eventBuffer_.size()) + "\n";
     status += "  Learning rate: " + std::to_string(config_.markovLearningRate) + "\n";
     status += "  Prediction accuracy: " + std::to_string(getPredictionAccuracy()) + "\n";
     
     // Add Hawkes intensities for main event types
     double currentTime = std::chrono::duration_cast<std::chrono::duration<double>>(
         std::chrono::high_resolution_clock::now().time_since_epoch()).count();
     status += "  Buy intensity: " + std::to_string(calculateHawkesIntensity(2, currentTime)) + "\n";
     status += "  Sell intensity: " + std::to_string(calculateHawkesIntensity(3, currentTime)) + "\n";
     
     return status;
 }
 
 } // namespace strategy
 } // namespace trading