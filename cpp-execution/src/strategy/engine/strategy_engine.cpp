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
#include <common/logger.hpp>
using namespace trading::core;
 namespace trading {
 namespace strategy {
 
 using json = nlohmann::json;
 using namespace std::chrono_literals;
 
 // Constructor
 StrategyEngine::StrategyEngine(const StrategyId& strategyId)
     : strategyId_(strategyId), 
       strategyVersion_("1.0.0"),
       state_(StrategyState::CREATED),
       lastUpdateTime_(std::chrono::system_clock::now()) {
     
     // Initialize with empty components - will be properly set up in initialize()
     LOG_INFO("Strategy Engine created: {}", strategyId_.toString());
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
        // we should use factory method to create order book analyzer manager with depth parameter for all pairs
        
        imbalanceDetector_ = std::make_shared<ImbalanceDetectionEngine>(config_.imbalanceDetectionConfig);
        // Ensure the detector is initialized so it creates/loads the bayes model and starts its worker
        try {
            if (!imbalanceDetector_->initialize(config_.imbalanceDetectionConfig)) {
                LOG_WARN("StrategyEngine: imbalanceDetector initialize() returned false");
            }
        } catch (const std::exception &ex) {
            LOG_WARN("StrategyEngine: exception while initializing imbalanceDetector: {}", ex.what());
        } catch (...) {
            LOG_WARN("StrategyEngine: unknown exception while initializing imbalanceDetector");
        }
        liquidityMonitor_ = std::make_shared<LiquidityFlowMonitor>(config_.liquidityFlowConfig);
        // volumeProfileAnalyzer_ = std::make_shared<VolumeProfileAnalyzer>(config_.volumeProfileConfig);
     
        signalGenerator_ = std::make_shared<ImbalanceSignalGenerator>(config_.signalGeneratorConfig);
        // Provide the top-level strategy id to the generator so it can tag signals
        // that were created without an explicit StrategyId.
        try {
            signalGenerator_->setStrategyId(config_.strategy_id);
        } catch (const std::exception& e) {
            LOG_WARN("Failed to set strategy id on signal generator: {}", e.what());
        }
        momentumCorrelator_ = std::make_shared<MomentumCorrelation>(config_.momentumCorrelationConfig);
        thresholdManager_ = std::make_shared<ThresholdManager>(config_.thresholdManagerConfig);
        // signalDecayModel_ = std::make_shared<SignalDecayModel>(config_.signalDecayConfig);
        signalDecayModel_ = std::make_shared<SignalDecayModel>(DecayModelType::EXPONENTIAL);
        exchangeCalibration_ = std::make_shared<ExchangeCalibration>(config_.exchangeCalibrationConfig);
        adaptiveMechanism_ = std::make_shared<AdaptiveMechanism>(config_.adaptiveMechanismConfig);
        
        // Set up component dependencies
        signalGenerator_->setMomentumCorrelation(momentumCorrelator_);
        signalGenerator_->setThresholdManager(thresholdManager_);
        signalGenerator_->setSignalDecayModel(signalDecayModel_);
        // signalGenerator_->setMarketRegimeDetector(regimeDetector_);
        //set up regime detector and performance tracker later when available
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
        // subscribe to MetricsUpdatedEvents for adaptive mechanism
        metricsUpdatedSubscriptionId_ = common::EventBus::getInstance().subscribe<events::MetricsUpdatedEvent>(
            [this](const events::MetricsUpdatedEvent& event) {
                // this->adaptiveMechanism_->processMetrics(event.metrics);
            }
        );
        // Remove signal generated event subscription to avoid circular dependencies
        // We use direct callbacks from the signal generator instead

        // Initialize trading pairs and create a VolumeProfileAnalyzer per pair using factory
        for (const auto& pair : config_.tradingPairs) {
            std::string pairId = pair.second.toString() + ":" + pair.first.toString();
            TradingPairState state;
            state.symbol = pair.first;
            state.exchangeId = pair.second;
            state.state = TradingPairState::ACTIVE;
            state.lastUpdateTime = std::chrono::system_clock::now();
            state.lastPrice = 0.0;
            state.previousPrice = 0.0;
            tradingPairStates_[pairId] = std::move(state);

            // Create VolumeProfileAnalyzer for this trading pair using the factory
            try {
                auto analyzer = createVolumeProfileAnalyzer(pair.second, pair.first, config_.volumeProfileConfig.params);
                volumeProfileAnalyzers_[pairId] = analyzer;
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to create VolumeProfileAnalyzer for {}: {}", pairId, e.what());
            }

            LOG_INFO("Monitoring trading pair: {} on exchange {}", 
                     pair.first.toString(), pair.second.toString());
        }
         
        // Load default parameters
        loadDefaultParameters();
        
        // Create regime subscriber (non-blocking) - will be started in start()
        try {
            regime_subscriber_ = std::make_unique<trading::strategy::core_analysis::RegimeSubscriber>();
            // Connect regime subscriber to imbalance detector
            if (imbalanceDetector_) {
                imbalanceDetector_->setRegimeSubscriber(regime_subscriber_.get());
                LOG_INFO("Connected RegimeSubscriber to ImbalanceDetectionEngine");
            }
        } catch (const std::exception& e) {
            LOG_WARN("Failed to create RegimeSubscriber: {}", e.what());
        }
        
        // Initialize signal filters
        try {
            meta_filter_ = std::make_unique<engine::MetaFilter>();
            shock_detector_ = std::make_unique<engine::ShockDetector>();
            LOG_INFO("Signal filters initialized successfully");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to initialize signal filters: {}", e.what());
            return false;
        }
        
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

    // VOLATILITY FILTER: Check if volatility is sufficient for trading
    // Only apply volatility filter to BUY/entry signals, allow sell/exit signals to pass through
    if (orderBookAnalyzer_ && signal.direction == core::Direction::BUY) {
        try {
            // Request metrics for the specific symbol/exchange for this signal
            auto metrics = orderBookAnalyzer_->getMetrics(signal.symbol, signal.exchangeId);
            double vol_1m = metrics.realize_volatility_1m;
            
            // Get symbol-specific threshold or use default
            double symbolThreshold = getSymbolVolatilityThreshold(signal.symbol.toString());
            
            // Skip trading if volatility is below threshold
            if (vol_1m < symbolThreshold) {
                LOG_INFO("Signal {} rejected: volatility too low ({:.6f} < {:.6f}) for symbol {}", 
                         signal.id, vol_1m, symbolThreshold, signal.symbol.toString());
                return;
            }
            
            // Optional: Log volatility info for debugging
            LOG_DEBUG("Signal {} volatility check passed: vol_1m={:.6f} >= {:.6f} for symbol {}", 
                     signal.id, vol_1m, symbolThreshold, signal.symbol.toString());
                     
        } catch (const std::exception& e) {
            LOG_WARN("Error checking volatility for signal {}: {}", signal.id, e.what());
            // Continue processing signal even if volatility check fails
        }
    }
    // Ensure the signal carries a valid strategy id/version. Some generators
    // create signals without populating strategy information (left as the
    // default StrategyId which is "UNKNOWN:1.0"). Fill it from the
    // engine's configured strategy id when missing to keep downstream
    // components (execution, risk) informed.
    TradingSignal s = signal; // make a mutable copy
    if (!s.strategyId.isValid()) {
        s.strategyId = config_.strategy_id;
        // keep the config_.version string in sync if necessary
        if (s.strategyId.version.empty()) s.strategyId.version = config_.version;
    }

    // Selective significance check for BUY/entry signals.
    // We use an adaptive threshold (may incorporate volume profile context)
    // to decide whether a detected imbalance is strong enough to act on.
    try {
        if (s.direction == core::Direction::BUY) {
            double baseThreshold = getSymbolVolatilityThreshold(s.symbol.toString());
            double adaptiveThreshold = calculateAdaptiveThreshold(s.symbol.toString(), baseThreshold, 0.0);
            double imbalanceStrength = s.strength; // use signal strength as proxy for imbalance

            if (!isImbalanceSignificant(s.symbol.toString(), imbalanceStrength, adaptiveThreshold)) {
                LOG_INFO("Signal {} rejected: imbalance not significant (strength={:.3f} < threshold={:.6f}) for symbol {}", 
                         s.id, imbalanceStrength, adaptiveThreshold, s.symbol.toString());
                return;
            }
        }
    } catch (const std::exception &ex) {
        LOG_WARN("Exception during imbalance significance check for signal {}: {}", s.id, ex.what());
        // On error, continue processing the signal rather than dropping it
    }

    activeSignals_[s.id] = s;
    for (const auto& [id, callback] : signalCallbacks_) {
        try {
            callback(s);
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

bool StrategyEngine::getRegimeInfo(const std::string& symbol, trading::strategy::core_analysis::RegimeInfo& out) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!regime_subscriber_) return false;
    return regime_subscriber_->get(symbol, out);
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

        // Start regime subscriber if available
        try {
            if (regime_subscriber_) regime_subscriber_->start();
        } catch (const std::exception& e) {
            LOG_WARN("Failed to start RegimeSubscriber: {}", e.what());
        }
         
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
    // Stop regime subscriber
    try {
        if (regime_subscriber_) regime_subscriber_->stop();
    } catch (const std::exception& e) {
        LOG_WARN("Error stopping RegimeSubscriber: {}", e.what());
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
            // Process the order book through the analyzer
            try {
                orderBookAnalyzer_->processOrderBook(orderBook);
            } catch (const std::bad_alloc& e) {
                LOG_ERROR("std::bad_alloc during orderBookAnalyzer_->processOrderBook for {}: {}", orderBook.getSymbol(), e.what());
                return false;
            }

             // Get the metrics from the analyzer
             decltype(orderBookAnalyzer_->getMetrics(orderBook.getSymbol(), orderBook.getExchangeId())) metrics;
            try {
                metrics = orderBookAnalyzer_->getMetrics(orderBook.getSymbol(), orderBook.getExchangeId());
            } catch (const std::bad_alloc& e) {
                LOG_ERROR("std::bad_alloc during orderBookAnalyzer_->getMetrics for {}: {}", orderBook.getSymbol(), e.what());
                return false;
            }
         //print bids and asks and volumes       

         // Process imbalance detection

        OrderBookDelta delta = orderBook.getOrderBookDelta();
        if (!delta.isEmpty()) {
            try {
                liquidityMonitor_->processOrderBookDelta(std::move(delta));
            } catch (const std::bad_alloc& e) {
                LOG_ERROR("std::bad_alloc during liquidityMonitor_->processOrderBookDelta for {}: {}", orderBook.getSymbol(), e.what());
                return false;
            }
        }

        //the below function was never been called which mean orderbook delta is always empty
        // liquidityMonitor_->processOrderBookDelta(std::move(delta)); //dont do this
        
        // Update the volume profile analyzer for this trading pair (create on demand)
         std::string vpId = orderBook.getExchangeId().toString() + ":" + orderBook.getSymbol().toString();

         auto vpIt = volumeProfileAnalyzers_.find(vpId);
             if (vpIt == volumeProfileAnalyzers_.end()) {
             // Create analyzer on demand using params from config
             try {
                 auto analyzer = createVolumeProfileAnalyzer(orderBook.getExchangeId(), orderBook.getSymbol(), config_.volumeProfileConfig.params);
                 // protect insertion which may allocate
                 try {
                     volumeProfileAnalyzers_[vpId] = analyzer;
                 } catch (const std::bad_alloc& e) {
                     LOG_ERROR("std::bad_alloc inserting VolumeProfileAnalyzer into map for {}: {}", vpId, e.what());
                     return false;
                 }
                 vpIt = volumeProfileAnalyzers_.find(vpId);
             } catch (const std::bad_alloc& e) {
                 LOG_ERROR("std::bad_alloc creating VolumeProfileAnalyzer for {}: {}", vpId, e.what());
                 return false;
             } catch (const std::exception& e) {
                 LOG_ERROR("Failed to create VolumeProfileAnalyzer on demand for {}: {}", vpId, e.what());
             }
         }

         if (vpIt != volumeProfileAnalyzers_.end() && vpIt->second) {
             // Update volume profile with current order book data
             vpIt->second->updateVolumeProfile(orderBook, {});
             
             // Get volume profile insights for enhanced signal generation
             auto profile = vpIt->second->getCurrentProfile();
             double poc = vpIt->second->calculatePointOfControl();
             auto valueArea = vpIt->second->calculateValueArea(0.7); // 70% value area
             auto anomalies = vpIt->second->detectAnomalies();
             double skewness = vpIt->second->calculateVolumeProfileSkewness();
             
             // Calculate density ratio (volume concentration metric)
             double currentPrice = orderBook.getMidPrice();
             double densityRatio = 1.0;
             if (currentPrice > 0.0 && profile.total_volume > 0.0) {
                 // Estimate local density based on proximity to high-volume areas
                 double pocDistance = poc > 0.0 ? std::abs(currentPrice - poc) / currentPrice : 1.0;
                 bool inValueArea = (currentPrice >= valueArea.second && currentPrice <= valueArea.first);
                 
                 // Higher density when near POC and inside value area
                 densityRatio = 1.0;
                 if (pocDistance < 0.01) densityRatio += 1.0; // Near POC
                 if (inValueArea) densityRatio += 0.5; // In value area
                 if (anomalies.hasAnomalies()) densityRatio += 0.3; // During anomalies
             }
             
             // Store volume profile metrics for later use in signal enhancement
             std::string symbol = orderBook.getSymbol().toString();
             volumeProfileMetrics_[symbol] = {
                 {"poc", poc},
                 {"value_area_high", valueArea.first},
                 {"value_area_low", valueArea.second},
                 {"total_volume", profile.total_volume},
                 {"vwap", profile.vwap},
                 {"has_anomalies", anomalies.hasAnomalies() ? 1.0 : 0.0},
                 {"density_ratio", densityRatio}
             };
            //  update every 30 counts
            static size_t updateCount = 0;
            if (++updateCount % 30 == 0) {
                 LOG_DEBUG("Volume profile updated for {}: POC={:.4f}, VAH={:.4f}, VAL={:.4f}, VWAP={:.4f}", 
                          symbol, poc, valueArea.first, valueArea.second, profile.vwap);
            }
         }
        //  return true; // Return true if imbalance detected
         try {
             imbalanceDetector_->analyzeMetrics(metrics);
         } catch (const std::bad_alloc& e) {
             LOG_ERROR("std::bad_alloc during imbalanceDetector_->analyzeMetrics for {}: {}", orderBook.getSymbol(), e.what());
             return false;
         }
         //print detected imbalances
         auto imbalances = imbalanceDetector_->getDetectedImbalances();
         for (const auto& imb : imbalances) {
             LOG_DEBUG("Detected imbalance: Type={} Strength={} Confidence={}",
                       static_cast<int>(imb.type), imb.strength, static_cast<int>(imb.confidence));
         }
         // Prepare data for locked section
         auto symbol = orderBook.getSymbol();
         ExchangeId exchangeId = orderBook.getExchangeId();
         std::string pairId = orderBook.getExchangeId().toString() + ":" + orderBook.getSymbol().toString();
         auto currentTime = std::chrono::system_clock::now();
         double currentPrice = orderBook.getMidPrice();
         
         // Update shock detector with current price
         updateShockDetectorPrice(symbol.toString(), currentPrice);
         
         // Check for emergency exit conditions on every price update
         //skip this for now
         // checkEmergencyExit(symbol.toString()); // COMMENTED OUT: Testing without regime-based emergency exits
         
         //print exchange and symbol
         // Variables to store data for processing outside the lock
         bool shouldUpdateCorrelation = false;
         PriceMovement movement;
         //print to know if we reach here
        // std::cout << "imbalanceDetector_ reached" << std::endl;
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
         //debug print

         // Update momentum correlation OUTSIDE the lock to avoid blocking other threads
         if (shouldUpdateCorrelation) {
            try {
                momentumCorrelator_->updateCorrelationModel(metrics, movement);
            } catch (const std::bad_alloc& e) {
                LOG_ERROR("std::bad_alloc during momentumCorrelator_->updateCorrelationModel for {}: {}", orderBook.getSymbol(), e.what());
                return false;
            }
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
        //  signals.push_back(signal);
        signals.emplace_back(std::move(signal));
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
        
        // Update volatility threshold
        double oldThreshold = volatilityThreshold_;
        volatilityThreshold_ = parameters_.getDouble("volatilityThreshold", 0.0005);
        if (oldThreshold != volatilityThreshold_) {
            LOG_INFO("Volatility threshold updated: {:.6f} -> {:.6f} ({:.3f}%)", 
                     oldThreshold, volatilityThreshold_, volatilityThreshold_ * 100.0);
        }
        
        // Propagate to sub-components
        orderBookAnalyzer_->updateParameters(params);
        imbalanceDetector_->updateParameters(params);
        liquidityMonitor_->updateParameters(params);
        // Update all volume profile analyzers
        for (auto &entry : volumeProfileAnalyzers_) {
            if (entry.second) entry.second->updateParameters(params);
        }
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
} // Get current strategy parameters
 ParameterSet StrategyEngine::getParameters() const {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     return parameters_;
 }
 
 // Get strategy status as JSON string
 std::string StrategyEngine::getStatus() const {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     
     json status;
     status["strategyId"] = strategyId_.toString();
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
        //get current regime and set to metric
        MarketRegime regime = momentumCorrelator_->detectMarketRegime();

        enhancedSignal.setMetric("marketRegime", static_cast<int>(regime));
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
            enhancedSignal.generationTime = current_time;
        }
        // LOG_DEBUG("Signal generation time: {}", 
        //           std::chrono::duration_cast<std::chrono::milliseconds>(enhancedSignal.generationTime.time_since_epoch()).count());
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
    
    // VOLUME PROFILE ENHANCEMENT: Add volume profile context to signal
    std::string symbol = enhancedSignal.symbol.toString();
    auto vpMetricsIt = volumeProfileMetrics_.find(symbol);
    if (vpMetricsIt != volumeProfileMetrics_.end()) {
        const auto& vpMetrics = vpMetricsIt->second;
        
        // Early exit if no valid volume profile data
        if (vpMetrics.empty()) {
            return enhancedSignal;
        }
        
        // Get current price from trading pair state or supporting metrics
        double currentPrice = 0.0;
        
        // Try to get price from supporting metrics first
        currentPrice = sourceEvent.getSupportingMetric("price", 0.0);
        
        // If not available in metrics, get from trading pair state
        if (currentPrice <= 0.0) {
            std::string pairId = sourceEvent.exchangeId.toString() + ":" + symbol;
            auto pairIt = tradingPairStates_.find(pairId);
            if (pairIt != tradingPairStates_.end()) {
                currentPrice = pairIt->second.lastPrice;
            }
        }
        
        // Early exit if no valid price data
        if (currentPrice <= 0.0) {
            return enhancedSignal;
        }
        
        // Add basic volume profile metrics to signal
        for (const auto& [metricName, value] : vpMetrics) {
            enhancedSignal.setMetric("vp_" + metricName, value);
        }
        
        // Calculate distance metrics and contextual flags
        auto pocIt = vpMetrics.find("poc");
        auto vahIt = vpMetrics.find("value_area_high");
        auto valIt = vpMetrics.find("value_area_low");
        auto vwapIt = vpMetrics.find("vwap");
        auto totalVolumeIt = vpMetrics.find("total_volume");
        auto anomaliesIt = vpMetrics.find("has_anomalies");
        
        // Context analysis variables
        double pocDistance = 1.0;
        double vahDistance = 1.0;
        double valDistance = 1.0;
        double vwapDistance = 1.0;
        bool nearPOC = false;
        bool nearEdge = false;
        bool inValueArea = false;
        bool hasAnomaly = false;
        
        // Calculate POC distance and context
        if (pocIt != vpMetrics.end()) {
            pocDistance = std::abs(currentPrice - pocIt->second) / currentPrice;
            nearPOC = pocDistance < 0.005; // Within 0.5% of POC
            enhancedSignal.setMetric("vp_poc_distance_pct", pocDistance * 100.0);
            enhancedSignal.setMetric("vp_near_poc", nearPOC ? 1.0 : 0.0);
        }
        
        // Calculate value area context
        if (vahIt != vpMetrics.end() && valIt != vpMetrics.end()) {
            inValueArea = (currentPrice >= valIt->second && currentPrice <= vahIt->second);
            vahDistance = std::abs(currentPrice - vahIt->second) / currentPrice;
            valDistance = std::abs(currentPrice - valIt->second) / currentPrice;
            nearEdge = (vahDistance < 0.003 || valDistance < 0.003); // Within 0.3% of edges
            
            enhancedSignal.setMetric("vp_in_value_area", inValueArea ? 1.0 : 0.0);
            enhancedSignal.setMetric("vp_vah_distance_pct", vahDistance * 100.0);
            enhancedSignal.setMetric("vp_val_distance_pct", valDistance * 100.0);
            enhancedSignal.setMetric("vp_near_value_area_edge", nearEdge ? 1.0 : 0.0);
        }
        
        // Calculate VWAP context
        if (vwapIt != vpMetrics.end()) {
            vwapDistance = std::abs(currentPrice - vwapIt->second) / currentPrice;
            enhancedSignal.setMetric("vp_vwap_distance_pct", vwapDistance * 100.0);
            enhancedSignal.setMetric("vp_above_vwap", currentPrice > vwapIt->second ? 1.0 : 0.0);
        }
        
        // Check for volume anomalies
        hasAnomaly = (anomaliesIt != vpMetrics.end() && anomaliesIt->second > 0.0);
        
        // Calculate volume density ratio (volume context strength)
        double densityRatio = 1.0;
        if (totalVolumeIt != vpMetrics.end() && totalVolumeIt->second > 0.0) {
            // Estimate local density - higher near POC and value area
            double pocWeight = nearPOC ? 2.0 : 1.0;
            double valueAreaWeight = inValueArea ? 1.5 : 1.0;
            densityRatio = pocWeight * valueAreaWeight;
        }
        enhancedSignal.setMetric("vp_density_ratio", densityRatio);
        
        // CONTEXT-BASED SIGNAL ADJUSTMENT (unified approach)
        double contextWeight = 1.0;
        
        // Base enhancements for significant levels
        if (nearPOC) contextWeight += 0.2;          // POC is strongest level
        if (nearEdge) contextWeight += 0.15;        // Value area edges are strong S/R
        if (hasAnomaly) contextWeight += 0.1;       // Unusual volume activity
        
        // Get imbalance direction for context-aware filtering
        double imbalanceDirection = sourceEvent.getSupportingMetric("imbalance_direction", 0.0);
        
        // MEAN REVERSION vs BREAKOUT LOGIC
        if (inValueArea && nearPOC) {
            // Inside value area near POC -> mean reversion zone
            contextWeight *= 0.7; // Suppress directional signals (expect reversal)
            enhancedSignal.setMetric("vp_context_suppression", 1.0);
            enhancedSignal.setMetric("vp_context_boost", 0.0);
            
        } else if (!inValueArea && std::abs(imbalanceDirection) > 0.5) {
            // Outside value area with strong directional imbalance -> breakout regime
            contextWeight *= 1.2; // Boost breakout signals
            enhancedSignal.setMetric("vp_context_boost", 1.0);
            enhancedSignal.setMetric("vp_context_suppression", 0.0);
            
        } else if (nearEdge && std::abs(imbalanceDirection) > 0.3) {
            // At value area edge with moderate imbalance -> potential breakout
            contextWeight *= 1.1; // Moderate boost
            enhancedSignal.setMetric("vp_context_boost", 0.5);
            enhancedSignal.setMetric("vp_context_suppression", 0.0);
            
        } else {
            // Neutral context
            enhancedSignal.setMetric("vp_context_boost", 0.0);
            enhancedSignal.setMetric("vp_context_suppression", 0.0);
        }
        
        // Apply density-based adaptive scaling
        contextWeight *= (0.8 + 0.4 * std::min(densityRatio, 2.0)); // Scale by local volume density
        
        // Clamp final context weight to prevent extreme values
        contextWeight = std::clamp(contextWeight, 0.6, 1.5);
        
        // Apply unified context weight to signal strength
        enhancedSignal.strength *= contextWeight;
        enhancedSignal.setMetric("vp_context_weight", contextWeight);
        
        // Enhanced logging with context information
        LOG_DEBUG("Enhanced signal {} with VP context: POC_dist={:.3f}%, in_VA={}, ctx_weight={:.3f}, regime={}", 
                 enhancedSignal.id, 
                 pocDistance * 100.0,
                 inValueArea,
                 contextWeight,
                 inValueArea && nearPOC ? "mean_reversion" : (!inValueArea ? "breakout" : "neutral"));
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
    // Before handing events to the signal generator, enrich them with
    // context that may be available in the engine (for example the
    // detected market regime from the momentum correlator). This ensures
    // the signal generator can read `event.getSupportingMetric("marketRegime")`.
    std::vector<ImbalanceEvent> enrichedEvents = events;
    if (momentumCorrelator_) {
        MarketRegime detected = momentumCorrelator_->detectMarketRegime();
        for (auto &ev : enrichedEvents) {
            ev.supportingMetrics["marketRegime"] = static_cast<double>(detected);
        }
        // LOG_DEBUG("Injected marketRegime={} into {} imbalance event(s)", marketRegimeToString(detected), enrichedEvents.size());
    }

    // Process all events at once for efficiency
    signalGenerator_->processImbalances(enrichedEvents);
    auto allSignals = signalGenerator_->generateSignals();
    
    // Clear the signal generator's state to prevent accumulation
    signalGenerator_->clear();
    // LOG_INFO("Generated {} signals from {} imbalance events", allSignals.size(), events.size());
    for (const auto& signal : allSignals) {
        try {
            // Find the source event for this signal
            auto sourceEvent = findSourceEvent(signal, events);
            if (sourceEvent) {
                auto enhancedSignal = enhanceSignalWithEventContext(signal, *sourceEvent);
                
                // Apply signal filters before processing
                if (applySignalFilters(enhancedSignal)) {
                    activeSignals_[enhancedSignal.id] = enhancedSignal;
                    notifySignalCallbacks(enhancedSignal);
                } else {
                    LOG_INFO("Signal {} blocked by filters", enhancedSignal.id);
                }
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
    
    // // Defensive: call registered callbacks directly while holding the callback mutex.
    // // This ensures any in-process listeners (for example the enqueue callback in main.cpp)
    // // are invoked even if the EventBus has an issue delivering events.
    // {
    //     std::lock_guard<std::recursive_mutex> lk(callbackMutex_);
    //     for (const auto &entry : signalCallbacks_) {
    //         try {
    //             entry.second(signal);
    //         } catch (const std::exception &e) {
    //             LOG_ERROR("Exception invoking local signal callback {}: {}", entry.first, e.what());
    //         } catch (...) {
    //             LOG_ERROR("Unknown exception invoking local signal callback {}", entry.first);
    //         }
    //     }
    // }

    // Still publish to the EventBus for external subscribers
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
    
    // Load volatility threshold from parameters
    volatilityThreshold_ = parameters_.getDouble("volatilityThreshold", 0.0005); // Default 0.05%
    LOG_INFO("Volatility threshold set to: {:.6f} ({:.3f}%)", volatilityThreshold_, volatilityThreshold_ * 100.0);
    
    // Propagate to sub-components
    orderBookAnalyzer_->updateParameters(parameters_);
    imbalanceDetector_->updateParameters(parameters_);
    liquidityMonitor_->updateParameters(parameters_);
    for (auto &entry : volumeProfileAnalyzers_) {
        if (entry.second) entry.second->updateParameters(parameters_);
    }
    signalGenerator_->updateParameters(parameters_);
    momentumCorrelator_->updateParameters(parameters_);
    thresholdManager_->updateParameters(parameters_);
    signalDecayModel_->updateParameters(parameters_);
} void StrategyEngine::performAdaptiveAdjustments() {
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
             for (auto &entry : volumeProfileAnalyzers_) {
                 if (entry.second) entry.second->updateParameters(parameters_);
             }
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
 
bool StrategyEngine::initializeTradingPair(const core::Symbol& symbol, ExchangeId exchangeId) {
    std::string pairId = exchangeId.toString() + ":" + symbol.toString();    
     
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
                    symbol.toString(), exchangeId.toString());
     return true;
 }
 
 void StrategyEngine::updateTradingPairState(const core::Symbol& symbol, TradingPairState state) {
     std::lock_guard<std::recursive_mutex> lock(mutex_);
     for (auto& [pairId, pairState] : tradingPairStates_) {
         if (pairState.symbol == symbol.toString()) {
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

// Signal filtering implementation
bool StrategyEngine::applySignalFilters(const TradingSignal& signal) const {
    // Only filter buy/entry signals, allow sell/exit signals to pass through
    if (signal.direction != core::Direction::BUY) {
        return true;
    }
    
    std::string symbol = signal.symbol.toString();
    
    // Apply MetaFilter (regime-based filter)
    // COMMENTED OUT: Testing without regime-based filtering to capture more opportunities
    // if (meta_filter_) {
    //     auto meta_result = meta_filter_->checkEntryAllowed(symbol, regime_subscriber_.get());
    //     if (!meta_result.allow_entry) {
    //         LOG_WARN("MetaFilter BLOCKED entry signal {} for {}: {} (p_trend_up={:.3f}, regime_confidence={:.3f})",
    //                  signal.id, symbol, meta_result.block_reason, 
    //                  meta_result.p_trend_up_value, meta_result.regime_confidence_value);
    //         return false;
    //     }
    // }
    
    // Apply ShockDetector (volatility-based filter)  
    if (shock_detector_) {
        auto shock_result = shock_detector_->checkTradingSuspended(symbol);
        if (shock_result.suspend_trading) {
            LOG_WARN("ShockDetector BLOCKED entry signal {} for {}: {} (recent_vol={:.6f}, baseline_vol={:.6f}, ratio={:.2f})",
                     signal.id, symbol, shock_result.suspend_reason,
                     shock_result.recent_volatility, shock_result.baseline_volatility, shock_result.volatility_ratio);
            return false;
        }
    }
    
    // All filters passed
    return true;
}

void StrategyEngine::updateShockDetectorPrice(const std::string& symbol, double price) const {
    if (shock_detector_) {
        shock_detector_->updatePrice(symbol, price);
    }
}

// Configuration update methods
void StrategyEngine::updateMetaFilterConfig(const engine::MetaFilterConfig& config) {
    if (meta_filter_) {
        meta_filter_->updateConfig(config);
        LOG_INFO("MetaFilter configuration updated");
    }
}

void StrategyEngine::updateShockDetectorConfig(const engine::ShockDetectorConfig& config) {
    if (shock_detector_) {
        shock_detector_->updateConfig(config);
        LOG_INFO("ShockDetector configuration updated");
    }
}

// Statistics methods  
std::string StrategyEngine::getMetaFilterStatistics() const {
    if (meta_filter_) {
        return meta_filter_->getStatistics();
    }
    return "{}";
}

std::string StrategyEngine::getShockDetectorStatistics() const {
    if (shock_detector_) {
        return shock_detector_->getStatistics();
    }
    return "{}";
}

void StrategyEngine::registerEmergencyExitCallback(const EmergencyExitCallback& callback) {
    std::lock_guard<std::mutex> lock(emergency_callback_mutex_);
    emergency_exit_callback_ = callback;
    LOG_INFO("Emergency exit callback registered");
}

void StrategyEngine::checkEmergencyExit(const std::string& symbol) const {
    if (!meta_filter_) {
        return;
    }
    
    auto exit_result = meta_filter_->checkEmergencyExit(symbol, regime_subscriber_.get());
    if (exit_result.trigger_emergency_exit) {
        std::lock_guard<std::mutex> lock(emergency_callback_mutex_);
        if (emergency_exit_callback_) {
            try {
                bool success = emergency_exit_callback_(symbol, exit_result.emergency_reason);
                if (success) {
                    LOG_CRITICAL("EMERGENCY EXIT EXECUTED: {} - {}", symbol, exit_result.emergency_reason);
                } else {
                    LOG_ERROR("EMERGENCY EXIT FAILED: {} - {}", symbol, exit_result.emergency_reason);
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Exception during emergency exit for {}: {}", symbol, e.what());
            }
        } else {
            LOG_TRACE("EMERGENCY EXIT NEEDED but no callback registered: {} - {}", symbol, exit_result.emergency_reason);
        }
    }
}

// Volatility threshold management methods
void StrategyEngine::setSymbolVolatilityThreshold(const std::string& symbol, double threshold) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    symbolVolatilityThresholds_[symbol] = threshold;
    LOG_INFO("Set volatility threshold for {}: {:.6f} ({:.3f}%)", 
             symbol, threshold, threshold * 100.0);
}

double StrategyEngine::getSymbolVolatilityThreshold(const std::string& symbol) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = symbolVolatilityThresholds_.find(symbol);
    if (it != symbolVolatilityThresholds_.end()) {
        return it->second;
    }
    // Return default threshold if symbol-specific not found
    return volatilityThreshold_;
}

bool StrategyEngine::loadSymbolVolatilityConfig(const std::string& configPath) {
    try {
        std::ifstream file(configPath);
        if (!file.is_open()) {
            LOG_ERROR("Cannot open volatility config file: {}", configPath);
            return false;
        }
        
        nlohmann::json config;
        file >> config;
        
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        
        // Load symbol-specific thresholds
        if (config.contains("symbol_volatility_thresholds") && 
            config["symbol_volatility_thresholds"].contains("symbols")) {
            
            auto symbols = config["symbol_volatility_thresholds"]["symbols"];
            size_t loadedCount = 0;
            
            for (auto& [symbol, symbolConfig] : symbols.items()) {
                if (symbolConfig.contains("threshold")) {
                    double threshold = symbolConfig["threshold"];
                    symbolVolatilityThresholds_[symbol] = threshold;
                    loadedCount++;
                    
                    LOG_DEBUG("Loaded volatility threshold for {}: {:.6f} ({:.3f}%)", 
                             symbol, threshold, threshold * 100.0);
                }
            }
            
            LOG_INFO("Loaded volatility thresholds for {} symbols from {}", 
                     loadedCount, configPath);
        }
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("Error loading symbol volatility config from {}: {}", configPath, e.what());
        return false;
    }
}

// Volume Profile Access Methods Implementation
VolumeProfileAnalyzerPtr StrategyEngine::getVolumeProfileAnalyzer(const core::Symbol& symbol, const core::ExchangeId& exchangeId) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::string pairId = exchangeId.toString() + ":" + symbol.toString();
    auto it = volumeProfileAnalyzers_.find(pairId);
    return (it != volumeProfileAnalyzers_.end()) ? it->second : nullptr;
}

std::map<std::string, double> StrategyEngine::getVolumeProfileMetrics(const std::string& symbol) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = volumeProfileMetrics_.find(symbol);
    return (it != volumeProfileMetrics_.end()) ? it->second : std::map<std::string, double>{};
}

bool StrategyEngine::isNearVolumeProfileLevel(const std::string& symbol, double price, double tolerance) const {
    if (price <= 0.0) return false;
    
    auto metrics = getVolumeProfileMetrics(symbol);
    if (metrics.empty()) return false;
    
    // Check distance from POC
    auto pocIt = metrics.find("poc");
    if (pocIt != metrics.end()) {
        double pocDistance = std::abs(price - pocIt->second) / price;
        if (pocDistance < tolerance) return true;
    }
    
    // Check distance from value area edges
    auto vahIt = metrics.find("value_area_high");
    auto valIt = metrics.find("value_area_low");
    if (vahIt != metrics.end() && valIt != metrics.end()) {
        double vahDistance = std::abs(price - vahIt->second) / price;
        double valDistance = std::abs(price - valIt->second) / price;
        if (vahDistance < tolerance || valDistance < tolerance) return true;
    }
    
    // Check distance from VWAP
    auto vwapIt = metrics.find("vwap");
    if (vwapIt != metrics.end()) {
        double vwapDistance = std::abs(price - vwapIt->second) / price;
        if (vwapDistance < tolerance) return true;
    }
    
    return false;
}

double StrategyEngine::getVolumeProfileSignalFactor(const std::string& symbol, double price) const {
    if (price <= 0.0) return 1.0;
    
    auto metrics = getVolumeProfileMetrics(symbol);
    if (metrics.empty()) return 1.0;
    
    double factor = 1.0;
    
    // Boost near POC (Point of Control - highest volume area)
    auto pocIt = metrics.find("poc");
    if (pocIt != metrics.end()) {
        double pocDistance = std::abs(price - pocIt->second) / price;
        if (pocDistance < 0.005) { // Within 0.5%
            factor *= 1.25; // Strong boost near POC
        } else if (pocDistance < 0.01) { // Within 1%
            factor *= 1.1;  // Moderate boost
        }
    }
    
    // Boost near value area edges (support/resistance)
    auto vahIt = metrics.find("value_area_high");
    auto valIt = metrics.find("value_area_low");
    if (vahIt != metrics.end() && valIt != metrics.end()) {
        double vahDistance = std::abs(price - vahIt->second) / price;
        double valDistance = std::abs(price - valIt->second) / price;
        
        if (vahDistance < 0.003 || valDistance < 0.003) { // Within 0.3%
            factor *= 1.2; // Good boost at value area edges
        }
    }
    
    // Additional boost during volume anomalies
    auto anomaliesIt = metrics.find("has_anomalies");
    if (anomaliesIt != metrics.end() && anomaliesIt->second > 0.0) {
        factor *= 1.15; // Moderate boost during unusual volume activity
    }
    
    // Slight reduction if outside value area (less significant levels)
    auto valAreaIt = metrics.find("vp_in_value_area");
    if (valAreaIt != metrics.end() && valAreaIt->second == 0.0) {
        factor *= 0.95; // Slight reduction outside value area
    }
    
    return std::min(factor, 2.0); // Cap maximum enhancement at 2x
}

double StrategyEngine::calculateAdaptiveThreshold(const std::string& symbol, double baseThreshold, double currentPrice) const {
    auto metrics = getVolumeProfileMetrics(symbol);
    if (metrics.empty() || baseThreshold <= 0.0) {
        return baseThreshold; // No volume profile data, return base threshold
    }

    // Get current price if not provided
    if (currentPrice <= 0.0) {
        // Find any trading pair for this symbol to get current price
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        for (const auto& [pairId, state] : tradingPairStates_) {
            if (pairId.find(":" + symbol) != std::string::npos) {
                currentPrice = state.lastPrice;
                break;
            }
        }
        if (currentPrice <= 0.0) return baseThreshold; // No price data
    }

    // Get volume profile context
    auto pocIt = metrics.find("poc");
    auto vahIt = metrics.find("value_area_high");
    auto valIt = metrics.find("value_area_low");
    auto densityRatioIt = metrics.find("density_ratio");

    // Density adjustment (separate from context) - non-linear but bounded
    double densityAdj = 1.0;
    if (densityRatioIt != metrics.end()) {
        double densityRatio = densityRatioIt->second;
        // Higher density -> require stronger imbalance; cap influence
        densityAdj = 1.0 + 0.5 * std::min(densityRatio, 2.0);
    }

    // Context adjustment based on POC / value area
    double contextAdj = 1.0;
    bool nearPOC = false;
    bool inValueArea = false;

    if (pocIt != metrics.end() && pocIt->second > 0.0) {
        // Normalize distance relative to POC itself
        double pocDistance = std::abs(currentPrice - pocIt->second) / pocIt->second;
        nearPOC = pocDistance < 0.005; // Within 0.5% of POC
        LOG_DEBUG("Current price {:.4f}, POC {:.4f}, pocDistance {:.4f}%, nearPOC={}",
                  currentPrice, pocIt->second, pocDistance * 100.0, nearPOC);
    }

    if (vahIt != metrics.end() && valIt != metrics.end()) {
        inValueArea = (currentPrice >= valIt->second && currentPrice <= vahIt->second);
    }

    if (inValueArea && nearPOC) {
        // Need stronger imbalance in noisy high-volume area
        contextAdj = 1.3;
    } else if (!inValueArea) {
        // Loosen threshold outside value area for breakout detection
        contextAdj = 0.8;
    }

    double adaptiveMultiplier = std::clamp(densityAdj * contextAdj, 0.5, 2.0);
    return baseThreshold * adaptiveMultiplier;
}


bool StrategyEngine::isImbalanceSignificant(const std::string& symbol, double imbalanceStrength, double threshold) const {
    bool isSignificant = imbalanceStrength > threshold;
    LOG_DEBUG("Imbalance check {}: strength={:.4f}, threshold={:.4f}, significant={}",
              symbol, imbalanceStrength, threshold, isSignificant);
    return isSignificant;
}
 
 } // namespace strategy
 } // namespace trading
  // End of file