/**
 * @file strategy_engine.cpp
 * @brief Implementation of the Strategy Engine for Order Imbalance Strategy
 * 
 * Created on: May 5, 2025
 */
#include "strategy/engine/strategy_engine.hpp"

#include "common/event_bus.hpp"
#include "common/trading_events.hpp"
#include "common/logger.hpp"

 #include <nlohmann/json.hpp>
 #include <fstream>
 #include <chrono>
 #include <algorithm>
 #include <sstream>
 #include <iomanip>
#include <iostream>
#include <logger.hpp>

 namespace trading {
 namespace strategy {
 
 using json = nlohmann::json;
 using namespace std::chrono_literals;
 
 // Constructor
 StrategyEngine::StrategyEngine(const std::string& strategyId)
     : strategyId_(strategyId), 
       strategyVersion_("1.0.0"),
       state_(StrategyState::CREATED),
       lastUpdateTime_(std::chrono::system_clock::now()) {
     
     // Initialize with empty components - will be properly set up in initialize()
     LOG_INFO("Strategy Engine created: {}", strategyId_);
 }
 
 // Destructor
 StrategyEngine::~StrategyEngine() {
     // Ensure cleanup of any running threads or resources
     if (state_ == StrategyState::RUNNING) {
         stop();
     }
     LOG_INFO("Strategy Engine destroyed: {}", strategyId_);
 }
 
 // Initialize the strategy engine with configuration
 bool StrategyEngine::initialize(const StrategyConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (state_ != StrategyState::CREATED && state_ != StrategyState::STOPPED) {
        LOG_ERROR("Cannot initialize strategy engine in current state: {}", 
                  static_cast<int>(state_.load()));
        return false;
    }
    
    try {
        config_ = config;
        
        // Initialize components
        orderBookAnalyzer_ = std::make_shared<OrderBookAnalyzerManager>(config_.orderBookAnalyzerConfig);
        imbalanceDetector_ = std::make_shared<ImbalanceDetectionEngine>(config_.imbalanceDetectionConfig);
        liquidityMonitor_ = std::make_shared<LiquidityFlowMonitor>(config_.liquidityFlowConfig);
        volumeProfileAnalyzer_ = std::make_shared<VolumeProfileAnalyzer>(config_.volumeProfileConfig);
        signalGenerator_ = std::make_shared<ImbalanceSignalGenerator>(config_.signalGeneratorConfig);
        momentumCorrelator_ = std::make_shared<MomentumCorrelation>(config_.momentumCorrelationConfig);
        thresholdManager_ = std::make_shared<ThresholdManager>(config_.thresholdManagerConfig);
        // signalDecayModel_ = std::make_shared<SignalDecayModel>(config_.signalDecayConfig);
        signalDecayModel_ = std::make_shared<SignalDecayModel>(DecayModelType::EXPONENTIAL);
        exchangeCalibration_ = std::make_shared<ExchangeCalibration>(config_.exchangeCalibrationConfig);
        adaptiveMechanism_ = std::make_shared<AdaptiveMechanism>(config_.adaptiveMechanismConfig);
        
        // Set up component dependencies
        signalGenerator_->setMomentumCorrelation(momentumCorrelator_);
        signalGenerator_->setThresholdManager(thresholdManager_);
               
        imbalanceSubscriptionId_ = common::EventBus::getInstance().subscribe<events::ImbalanceDetectedEvent>(
            [this](const events::ImbalanceDetectedEvent& event) {
                this->processImbalanceEvents({event.imbalance});
            }
        );
        //subscribe to signal generated events
        signalGeneratedSubscriptionId_ = common::EventBus::getInstance().subscribe<events::SignalGeneratedEvent>(
            [this](const events::SignalGeneratedEvent& event)
            {
                this->handleSignalGenerated(event.signal);
            }
        );
        // Remove signal generated event subscription to avoid circular dependencies
        // We use direct callbacks from the signal generator instead

        // Initialize trading pairs
        for (const auto& pair : config_.tradingPairs) {
            if (!initializeTradingPair(pair.first, pair.second)) {
                LOG_WARN("Failed to initialize trading pair: {} on exchange: {}", 
                         pair.first, pair.second.toString());
                continue;
            }
        }
        
        // Load default parameters
        loadDefaultParameters();
        
        state_ = StrategyState::INITIALIZED;
        LOG_INFO("Strategy Engine initialized: {}", strategyId_);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Exception during strategy initialization: {}", e.what());
        return false;
    }
}
void StrategyEngine::handleSignalGenerated(const TradingSignal& signal) {
    // Debug: Check signal when received
    auto expiration_received = std::chrono::duration_cast<std::chrono::milliseconds>(signal.expirationTime.time_since_epoch()).count();
    // LOG_DEBUG("handleSignalGenerated - Signal {} expirationTime: {}ms", signal.id, expiration_received);
    
    std::unique_lock<std::recursive_mutex> lock(mutex_, std::defer_lock);
    if (!lock.try_lock()) {
        // Queue the signal for later processing
        std::lock_guard<std::mutex> qlock(signalQueueMutex_);
        queuedSignals_.push_back(signal);
        // LOG_WARN("Could not acquire lock in handleSignalGenerated - queued signal {}", signal.id);
        return;
    }
    activeSignals_[signal.id] = signal;
    for (const auto& [id, callback] : signalCallbacks_) {
        try {
            callback(signal);
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in signal callback {}: {}", id, e.what());
        }
    }
}

// Call this periodically (e.g. in your maintenance thread) to process queued signals
void StrategyEngine::processQueuedSignals() {
    std::vector<TradingSignal> signalsToProcess;
    {
        std::lock_guard<std::mutex> qlock(signalQueueMutex_);
        signalsToProcess.swap(queuedSignals_);
    }
    for (const auto& signal : signalsToProcess) {
        handleSignalGenerated(signal);
    }
}
 // Start the strategy engine
 bool StrategyEngine::start() {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     
     if (state_ != StrategyState::INITIALIZED && state_ != StrategyState::STOPPED) {
         LOG_ERROR("Cannot start strategy engine in current state: {}", 
                          static_cast<int>(state_.load()));
         return false;
     }
     
     try {
         // Start the signal maintenance thread
         runMaintenanceThread_ = true;
         signalMaintenanceThread_ = std::make_unique<std::thread>(&StrategyEngine::signalMaintenanceWorker, this);
         
         state_ = StrategyState::RUNNING;
         LOG_INFO("Strategy Engine started: {}", strategyId_);
         return true;
     } catch (const std::exception& e) {
         LOG_ERROR("Exception during strategy start: {}", e.what());
         return false;
     }
 }
 
 // Stop the strategy engine
 bool StrategyEngine::stop() {
     {
         std::lock_guard<std::recursive_mutex> lock(mutex_);
         
         if (state_ != StrategyState::RUNNING) {
             LOG_ERROR("Cannot stop strategy engine in current state: {}", 
                             static_cast<int>(state_.load()));
             return false;
         }
         
         // Stop the signal maintenance thread
         runMaintenanceThread_ = false;
     }
     
     // Join the thread outside the lock to avoid deadlock
     if (signalMaintenanceThread_ && signalMaintenanceThread_->joinable()) {
         signalMaintenanceThread_->join();
     }
     
     {
         std::lock_guard<std::recursive_mutex> lock(mutex_);
         state_ = StrategyState::STOPPED;
         LOG_INFO("Strategy Engine stopped: {}", strategyId_);
     }
     
     return true;
 }
 
 // Process new order book data
 bool StrategyEngine::processOrderBook(const data_ingestion::OrderBook& orderBook) {
     // Quick check if we're in the right state without locking
     if (state_ != StrategyState::RUNNING) {
         return false;
     }
     if (!validateOrderBook(orderBook)) {
         return false;
     }
     
     try {
         // Process through the analysis pipeline (no locking needed for these)
         orderBookAnalyzer_->processOrderBook(orderBook);
         
         // Get the metrics from the analyzer
         auto metrics = orderBookAnalyzer_->getMetrics(orderBook.getSymbol(), orderBook.getExchangeId());
         //print bids and asks and volumes       

         // Process imbalance detection

        OrderBookDelta delta = orderBook.getOrderBookDelta();
        if (!delta.isEmpty()) {
            liquidityMonitor_->processOrderBookDelta(delta);
        }
         // Update the volume profile analyzer
         volumeProfileAnalyzer_->updateVolumeProfile(orderBook, {});
         
         // Detect imbalances
         imbalanceDetector_->analyzeMetrics(metrics);
         
         // Prepare data for locked section
         auto symbol = orderBook.getSymbol();
         ExchangeId exchangeId = orderBook.getExchangeId();
         std::string pairId = orderBook.getExchangeId().toString() + ":" + orderBook.getSymbol().toString();
         auto currentTime = std::chrono::system_clock::now();
         double currentPrice = orderBook.getMidPrice();
         //print exchange and symbol
         // Variables to store data for processing outside the lock
         bool shouldUpdateCorrelation = false;
         PriceMovement movement;
         
         // Update the trading pair state - LOCK HERE (minimize lock duration)
         {
             std::lock_guard<std::recursive_mutex> lock(mutex_);
             auto it = tradingPairStates_.find(pairId);
             if (it != tradingPairStates_.end()) {
                 double previousPrice = it->second.lastPrice;
                 it->second.lastUpdateTime = currentTime;
                 it->second.previousPrice = it->second.lastPrice;
                 it->second.lastPrice = currentPrice;
                 
                 // Prepare movement data for correlation analysis (process outside lock)
                 if (previousPrice > 0.0 && currentPrice > 0.0 && previousPrice != currentPrice) {
                     shouldUpdateCorrelation = true;
                     movement.symbol = symbol;
                     movement.time_start = it->second.lastUpdateTime;
                     movement.time_end = currentTime;
                     movement.price_before = previousPrice;
                     movement.price_after = currentPrice;
                     movement.absolute_change = movement.price_after - movement.price_before;
                     movement.percent_change = (movement.absolute_change / movement.price_before) * 100.0;
                 }
             }
         }
         
         // Update momentum correlation OUTSIDE the lock to avoid blocking other threads
         if (shouldUpdateCorrelation) {
             momentumCorrelator_->updateCorrelationModel(metrics, movement);
         }
         // Record metrics for performance tracking
         recordMetrics();
         
         return true;
     } catch (const std::exception& e) {
         LOG_ERROR("Exception processing order book for {}: {}", 
                          orderBook.getSymbol(), e.what());
         return false;
     }
 }
 
 // Get all active trading signals
 std::vector<TradingSignal> StrategyEngine::getActiveSignals() const {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     
     std::vector<TradingSignal> signals;
     signals.reserve(activeSignals_.size());
     
     for (const auto& [id, signal] : activeSignals_) {
         signals.push_back(signal);
     }
     
     return signals;
 }
 
 // Register callback for new trading signals
 size_t StrategyEngine::registerSignalCallback(const SignalCallback& callback) {
     std::lock_guard<std::recursive_mutex> lock(callbackMutex_);
     
     size_t id = nextCallbackId_++;
     signalCallbacks_[id] = callback;
     
     return id;
 }
 
 // Unregister a previously registered callback
 bool StrategyEngine::unregisterSignalCallback(size_t callbackId) {
     std::lock_guard<std::recursive_mutex> lock(callbackMutex_);
     
     auto it = signalCallbacks_.find(callbackId);
     if (it != signalCallbacks_.end()) {
         signalCallbacks_.erase(it);
         return true;
     }
     
     return false;
 }
 
 // Update strategy parameters
 bool StrategyEngine::updateParameters(const ParameterSet& params) {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     
     try {
         // Update our internal parameters
         parameters_ = params;
         
         // Propagate to sub-components
         orderBookAnalyzer_->updateParameters(params);
         imbalanceDetector_->updateParameters(params);
         liquidityMonitor_->updateParameters(params);
         volumeProfileAnalyzer_->updateParameters(params);
         signalGenerator_->updateParameters(params);
         momentumCorrelator_->updateParameters(params);
         thresholdManager_->updateParameters(params);
         signalDecayModel_->updateParameters(params);
         
         LOG_INFO("Strategy parameters updated: {}", strategyId_);
         return true;
     } catch (const std::exception& e) {
         LOG_ERROR("Exception updating parameters: {}", e.what());
         return false;
     }
 }
 
 // Get current strategy parameters
 ParameterSet StrategyEngine::getParameters() const {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     return parameters_;
 }
 
 // Get strategy status as JSON string
 std::string StrategyEngine::getStatus() const {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     
     json status;
     status["strategyId"] = strategyId_;
     status["strategyVersion"] = strategyVersion_;
     status["state"] = static_cast<int>(state_.load());
     status["activeSignalsCount"] = activeSignals_.size();
     status["tradingPairsCount"] = tradingPairStates_.size();
     
     // Add trading pair statuses
     json pairsStatus = json::array();
     for (const auto& [pairId, state] : tradingPairStates_) {
         json pairStatus;
         pairStatus["pairId"] = pairId;
         pairStatus["state"] = static_cast<int>(state.state);
         pairStatus["lastPrice"] = state.lastPrice;
         
         // Format last update time
         auto timeT = std::chrono::system_clock::to_time_t(state.lastUpdateTime);
         std::stringstream ss;
         ss << std::put_time(std::localtime(&timeT), "%Y-%m-%d %H:%M:%S");
         pairStatus["lastUpdateTime"] = ss.str();
         
         pairsStatus.push_back(pairStatus);
     }
     status["tradingPairs"] = pairsStatus;
     
     // Add performance metrics
     json performanceJson;
     performanceJson["signalsGenerated"] = performance_.signalsGenerated;
     performanceJson["signalsExecuted"] = performance_.signalsExecuted;
     performanceJson["successfulSignals"] = performance_.successfulSignals;
     performanceJson["failedSignals"] = performance_.failedSignals;
     performanceJson["totalPnl"] = performance_.totalPnl;
     performanceJson["averageSignalStrength"] = performance_.averageSignalStrength;
     performanceJson["averageSignalDuration"] = performance_.averageSignalDuration.count();
     
     status["performance"] = performanceJson;
     
     return status.dump(2);
 }
 
 // Get strategy performance metrics
 StrategyPerformance StrategyEngine::getPerformance() const {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     return performance_;
 }
 
 // Manually trigger signal evaluation
//  bool StrategyEngine::triggerSignalEvaluation() {
//      if (state_ != StrategyState::RUNNING) {
//          LOG_ERROR("Cannot trigger signal evaluation in current state: {}", 
//                           static_cast<int>(state_.load()));
//          return false;
//      }
     
//      try {
//          // Get latest metrics from analyzers
//          auto metrics = orderBookAnalyzer_->getMetrics();
//          auto liquidityMetrics = liquidityMonitor_->getLiquidityMetrics();
         
//          // Detect imbalances
//          auto isImbalances = imbalanceDetector_->analyzeMetrics(metrics);
//          // Get detected imbalances
//          auto imbalances = imbalanceDetector_->getDetectedImbalances();
         
//          // Process any detected imbalances
//          processImbalanceEvents(imbalances);
         
//          return true;
//      } catch (const std::exception& e) {
//          LOG_ERROR("Exception during signal evaluation: {}", e.what());
//          return false;
//      }
//  }
 
 // Record signal execution result for feedback
 bool StrategyEngine::recordSignalResult(const std::string& signalId, const SignalExecutionResult& result) {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     
     try {
         // Find the signal
         auto it = activeSignals_.find(signalId);
         if (it == activeSignals_.end()) {
            // LOG_WARN("Received result for unknown signal: {} - but still updating performance metrics", signalId);
            // Still update performance metrics even if signal is not in active list
            updatePerformanceMetrics(result);
            return true; // Changed to true so we still track performance
         }
         
         // Record the signal performance for feedback
         recordSignalPerformance(signalId, result);
         
         // Update performance metrics
         updatePerformanceMetrics(result);
         
         // If signal is complete, remove it from active signals
         if (result.status == SignalExecutionStatus::COMPLETED || 
             result.status == SignalExecutionStatus::FAILED ||
             result.status == SignalExecutionStatus::EXPIRED) {
             activeSignals_.erase(it);
         }
         
         // Use the execution result to adapt the strategy
         if (adaptiveMechanism_) {
             adaptiveMechanism_->updateSignalResult(signalId, result);
             
             // Periodically apply adaptive adjustments
             auto now = std::chrono::system_clock::now();
             if (now - lastUpdateTime_ > std::chrono::hours(1)) {
                 performAdaptiveAdjustments();
                 lastUpdateTime_ = now;
             }
         }
         
         LOG_INFO("Recorded result for signal {}: status={}, pnl={}", 
                         signalId, static_cast<int>(result.status), result.pnl);
         return true;
     } catch (const std::exception& e) {
         LOG_ERROR("Exception recording signal result: {}", e.what());
         return false;
     }
 }
 
 //
 // Private methods
 //
 
TradingSignal StrategyEngine::enhanceSignalWithEventContext(
    const TradingSignal& baseSignal, 
    const ImbalanceEvent& sourceEvent) const{
    
    // Create a copy to avoid mutating the original
    TradingSignal enhancedSignal = baseSignal;
    
    // Add correlation data from momentum correlator
    if (momentumCorrelator_) {
        double expectedPriceImpact = momentumCorrelator_->predictPriceMovement(sourceEvent);
        enhancedSignal.expectedPriceImpact = expectedPriceImpact;
        
        // Add momentum correlation metrics
        std::string imbalanceTypeStr = imbalanceTypeToString(sourceEvent.type);
        double correlation = momentumCorrelator_->getCorrelationCoefficient(imbalanceTypeStr);
        enhancedSignal.setMetric("momentumCorrelation", correlation);
        enhancedSignal.setMetric("predictedPriceMove", expectedPriceImpact);
    }
    
    // Apply signal decay model
    if (signalDecayModel_) {
        enhancedSignal.decayRate = signalDecayModel_->calculateDecayRate(sourceEvent.type);
        
        // Debug TTL value
        uint64_t ttl_value = signalDecayModel_->getSignalTTL(sourceEvent.type);
        auto current_time = std::chrono::system_clock::now();
        
        // LOG_DEBUG("Signal TTL value: {} ms, Current time: {}", ttl_value, 
        //           std::chrono::duration_cast<std::chrono::milliseconds>(current_time.time_since_epoch()).count());
        
        // Set expiration time (assuming TTL is in milliseconds)
        if (enhancedSignal.expirationTime.time_since_epoch().count() == 0) {
            enhancedSignal.expirationTime = current_time + std::chrono::milliseconds(ttl_value);
            enhancedSignal.generation_time = current_time;
        }
        // LOG_DEBUG("Signal generation time: {}", 
        //           std::chrono::duration_cast<std::chrono::milliseconds>(enhancedSignal.generation_time.time_since_epoch()).count());
        // LOG_DEBUG("Signal expiration time: {}", 
        //           std::chrono::duration_cast<std::chrono::milliseconds>(enhancedSignal.expirationTime.time_since_epoch()).count());
    }
    
    // Add threshold manager data
    if (thresholdManager_) {
        double sensitivity = thresholdManager_->getGlobalSensitivity();
        enhancedSignal.setMetric("thresholdSensitivity", sensitivity);
        
        // Adjust strength based on sensitivity
        if (sensitivity != 1.0) {
            enhancedSignal.strength = std::min(1.0, enhancedSignal.strength * sensitivity);
        }
    }
    
    // Copy event context into signal
    enhancedSignal.sourceEventId = sourceEvent.eventId;
    enhancedSignal.setMetric("sourceEventStrength", sourceEvent.strength);
    enhancedSignal.setMetric("sourceEventConfidence", static_cast<double>(sourceEvent.confidence));
    
    // Copy supporting metrics from the original imbalance event
    for (const auto& [metricName, metricValue] : sourceEvent.supportingMetrics) {
        enhancedSignal.setMetric("event_" + metricName, metricValue);
    }
    
    return enhancedSignal;
}
void StrategyEngine::processImbalanceEvents(const std::vector<ImbalanceEvent>& events) {
    if (events.empty()) return;
    
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    // Process all events at once for efficiency
    signalGenerator_->processImbalances(events);
    auto allSignals = signalGenerator_->generateSignals();
    
    // Clear the signal generator's state to prevent accumulation
    signalGenerator_->clear();
    
    for (const auto& signal : allSignals) {
        try {
            // Find the source event for this signal
            auto sourceEvent = findSourceEvent(signal, events);
            if (sourceEvent) {
                auto enhancedSignal = enhanceSignalWithEventContext(signal, *sourceEvent);
                activeSignals_[enhancedSignal.id] = enhancedSignal;
                notifySignalCallbacks(enhancedSignal);
            } else {
                LOG_WARN("Could not correlate signal {} to source event", signal.id);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Exception processing signal: {}", e.what());
        }
    }
}

std::optional<ImbalanceEvent> StrategyEngine::findSourceEvent(
    const TradingSignal& signal, 
    const std::vector<ImbalanceEvent>& events)const {
    
    for (const auto& event : events) {
        if (signal.sourceEventId == event.eventId ||
            (signal.symbol == event.symbol && 
             signal.exchangeId == event.exchangeId &&
             signal.direction == event.direction)) {
            return event;
        }
    }
    return std::nullopt;
}

 void StrategyEngine::maintainSignals() {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     
     auto now = std::chrono::system_clock::now();
    //  convert now to uint64_t
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
     // Apply signal decay
     for (auto& [id, signal] : activeSignals_) {
         double decayedStrength = signalDecayModel_->calculateRemainingStrength(signal, now_ms);
         signal.strength = decayedStrength;
     }
     
     // Remove expired signals
     pruneExpiredSignals();
 }
 
 void StrategyEngine::updateStrategyState() {
     // This method would contain logic to update the overall strategy state
     // based on market conditions, signal performance, etc.
 }
 
 void StrategyEngine::notifySignalCallbacks(const TradingSignal& signal) {
   
    //publish event instead of callbacks to avoid deadlocks
    // Debug: Check signal before publishing
    auto expiration_before = std::chrono::duration_cast<std::chrono::milliseconds>(signal.expirationTime.time_since_epoch()).count();
    // LOG_DEBUG("Before EventBus publish - Signal {} expirationTime: {}ms", signal.id, expiration_before);
    
    common::EventBus::getInstance().publish(events::SignalGeneratedEvent{signal});
 }
 
 void StrategyEngine::recordMetrics() {
     // This would record various metrics about strategy performance
     // for monitoring and optimization purposes
 }
 
 bool StrategyEngine::validateOrderBook(const data_ingestion::OrderBook& orderBook) const {
     // Validate that the order book is for a trading pair we're interested in
     auto symbol = orderBook.getSymbol();
     ExchangeId exchangeId = orderBook.getExchangeId();
     std::string pairId = exchangeId.toString() + ":" + symbol.toString();
     // Check if we're tracking this pair - LOCK HERE
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     return tradingPairStates_.find(pairId) != tradingPairStates_.end();
 }
 

 void StrategyEngine::signalMaintenanceWorker() {
     LOG_INFO("Signal maintenance thread started for strategy: {}", strategyId_);
     while (runMaintenanceThread_) {
         try {
             // Maintain signals - apply decay, prune expired ones
             maintainSignals();
             // Process any queued signals
             processQueuedSignals();
             // Update strategy state
             updateStrategyState();
             // Sleep for a bit
             std::this_thread::sleep_for(100ms);
         } catch (const std::exception& e) {
             LOG_ERROR("Exception in signal maintenance thread: {}", e.what());
             // Continue the loop even if there's an exception
         }
     }
     LOG_INFO("Signal maintenance thread stopped for strategy: {}", strategyId_);
 }
 
 void StrategyEngine::pruneExpiredSignals() {
     auto now = std::chrono::system_clock::now();
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
     
     for (auto it = activeSignals_.begin(); it != activeSignals_.end(); ) {
         const auto& signal = it->second;
         
         // Check if signal has expired
         if (!signalDecayModel_->isSignalValid(signal, now_ms)) {
            //  LOG_INFO("Signal expired: {}", signal.id);
             
             // Remove the signal
             it = activeSignals_.erase(it);
         } else {
             ++it;
         }
     }
 }
 
 void StrategyEngine::loadDefaultParameters() {
     // Load default parameters from config
     parameters_ = config_.defaultParameters;
     
     // Propagate to sub-components
     orderBookAnalyzer_->updateParameters(parameters_);
     imbalanceDetector_->updateParameters(parameters_);
     liquidityMonitor_->updateParameters(parameters_);
     volumeProfileAnalyzer_->updateParameters(parameters_);
     signalGenerator_->updateParameters(parameters_);
     momentumCorrelator_->updateParameters(parameters_);
     thresholdManager_->updateParameters(parameters_);
     signalDecayModel_->updateParameters(parameters_);
 }
 
 void StrategyEngine::performAdaptiveAdjustments() {
     if (!adaptiveMechanism_) {
         return;
     }
     
     try {
         // Get adjustment recommendations
         auto adjustments = adaptiveMechanism_->getAdjustmentRecommendations();
         
         if (!adjustments.empty()) {
             // Apply adjustments to parameters
             for (const auto& adjustment : adjustments) {
                 auto paramName = adjustment.first;
                 auto paramValue = adjustment.second;
                 
                 // Update our parameter set
                 if (parameters_.numeric_params.find(paramName) != parameters_.numeric_params.end()) {
                     parameters_.numeric_params[paramName] = paramValue;
                 }
             }
             
             // Propagate updated parameters to components
             orderBookAnalyzer_->updateParameters(parameters_);
             imbalanceDetector_->updateParameters(parameters_);
             liquidityMonitor_->updateParameters(parameters_);
             volumeProfileAnalyzer_->updateParameters(parameters_);
             signalGenerator_->updateParameters(parameters_);
             momentumCorrelator_->updateParameters(parameters_);
             thresholdManager_->updateParameters(parameters_);
             signalDecayModel_->updateParameters(parameters_);
             
             LOG_INFO("Applied {} adaptive adjustments to strategy {}", 
                           adjustments.size(), strategyId_);
         }
     } catch (const std::exception& e) {
         LOG_ERROR("Exception during adaptive adjustments: {}", e.what());
     }
 }
 
bool StrategyEngine::initializeTradingPair(const std::string& symbol, ExchangeId exchangeId) {
    std::string pairId = exchangeId.toString() + ":" + symbol;    
     
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     // Check if already initialized
     if (tradingPairStates_.find(pairId) != tradingPairStates_.end()) {
         return true;
     }
     
     // Create new state for this pair
     TradingPairState state;
     state.symbol = symbol;
     state.exchangeId = exchangeId;
     state.state = TradingPairState::ACTIVE;
     state.lastUpdateTime = std::chrono::system_clock::now();
     state.lastPrice = 0.0;
     state.previousPrice = 0.0;
     
     // Store in our map
     tradingPairStates_[pairId] = state;
     
     LOG_INFO("Initialized trading pair: {} on exchange: {}", 
                    symbol, exchangeId.toString());
     return true;
 }
 
 void StrategyEngine::updateTradingPairState(const std::string& symbol, TradingPairState state) {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     for (auto& [pairId, pairState] : tradingPairStates_) {
         if (pairState.symbol == symbol) {
             pairState = state;
         }
     }
 }
 
 void StrategyEngine::updatePerformanceMetrics(const SignalExecutionResult& result) {
     performance_.signalsExecuted++;
     
     if (result.status == SignalExecutionStatus::COMPLETED) {
         performance_.successfulSignals++;
     } else if (result.status == SignalExecutionStatus::FAILED) {
         performance_.failedSignals++;
     }
     
     performance_.totalPnl += result.pnl;
     
     // Update average signal duration
     auto signalDuration = result.completionTime - result.startTime;
    //  performance_.averageSignalDuration =
    //      (performance_.averageSignalDuration * (performance_.signalsExecuted - 1) + signalDuration) / 
    //      performance_.signalsExecuted;
        if (performance_.signalsExecuted == 1) {
            performance_.averageSignalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(signalDuration);
        } else {
            performance_.averageSignalDuration += 
                std::chrono::duration_cast<std::chrono::milliseconds>(signalDuration - performance_.averageSignalDuration) / performance_.signalsExecuted;
        }
        
        // Update average signal strength
        if (result.signalStrength > 0.0) {
            performance_.averageSignalStrength = 
                (performance_.averageSignalStrength * (performance_.signalsExecuted - 1) + result.signalStrength) / 
                performance_.signalsExecuted;
        }
        
        // LOG_INFO("Updated performance metrics: totalPnl={}, successfulSignals={}, failedSignals={}", 
        //                 performance_.totalPnl, performance_.successfulSignals, performance_.failedSignals);
 }
 
 void StrategyEngine::recordSignalPerformance(const std::string& signalId, const SignalExecutionResult& result) {
     // This would be used to record detailed performance data for later analysis
     // Could store to a database or other persistent storage
 }
 
 } // namespace strategy
 } // namespace trading
  // End of file