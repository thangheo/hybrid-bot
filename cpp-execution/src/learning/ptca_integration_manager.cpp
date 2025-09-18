/**
 * @file ptca_integration_manager.cpp
 * @brief Implementation of PTCA integration manager
 * 
 * @author Trading System Team
 * @date September 13, 2025
 */

#include "learning/ptca_integration_manager.hpp"
#include "common/logger.hpp"
#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <iostream>
#include <system_error>

namespace trading {
namespace learning {

PTCAIntegrationManager::PTCAIntegrationManager(const PTCAIntegrationConfig& config)
    : config_(config), lastModelSave_(core::now()) {
    
    // Create model save directory if it doesn't exist
    if (!config_.modelSaveDirectory.empty()) {
        std::filesystem::create_directories(config_.modelSaveDirectory);
    }
    
    // Initialize PTCA with trading-optimized configuration
    PTCAConfig ptcaConfig;
    ptcaConfig.lookbackWindow = 100;
    ptcaConfig.temporalDecayFactor = 0.98;
    ptcaConfig.weakThreshold = config_.priceMovementThreshold;
    ptcaConfig.strongThreshold = config_.strongOutcomeThreshold;
    ptcaConfig.baseLearningRate = 0.05;
    ptcaConfig.extinctionLambda = 0.001;
    ptcaConfig.competitiveInhibition = 0.01;
    ptcaConfig.adaptiveLearningFactor = 0.05;
    ptcaConfig.enableAutoSave = false; // We'll handle saving ourselves
    
    ptca_ = std::make_unique<PavlovianCreditAssignment>(ptcaConfig);
    
    // Try to load the most recent model
    if (!config_.modelSaveDirectory.empty()) {
        auto modelPath = config_.modelSaveDirectory + "latest_model.json";
        if (std::filesystem::exists(modelPath)) {
            ptca_->loadModel(modelPath);
        }
    }
}

PTCAIntegrationManager::~PTCAIntegrationManager() {
    stop();
    
    // Save model on shutdown
    if (ptca_) {
        saveModel("latest_model.json");
    }
}

void PTCAIntegrationManager::start() {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    
    if (running_) {
        return;
    }
    
    running_ = true;
    processingThread_ = std::thread(&PTCAIntegrationManager::processingLoop, this);
}

void PTCAIntegrationManager::stop() {
    {
        std::lock_guard<std::recursive_mutex> lock(signalMutex_);
        running_ = false;
    }
    
    if (processingThread_.joinable()) {
        processingThread_.join();
    }
}

void PTCAIntegrationManager::registerSignal(const strategy::TradingSignal& signal) {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    
    // More aggressive memory management to prevent system errors
    const size_t maxSignals = std::min(config_.maxSignalsToTrack, static_cast<size_t>(1000));
    
    if (activeSignals_.size() >= maxSignals) {
        // Remove multiple old signals at once for efficiency
        size_t signalsToRemove = std::max(static_cast<size_t>(1), maxSignals / 4);
        
        // Sort by signal time and remove oldest
        std::vector<std::pair<std::string, core::Timestamp>> signalsByTime;
        for (const auto& [id, tracker] : activeSignals_) {
            signalsByTime.emplace_back(id, tracker.signalTime);
        }
        
        std::sort(signalsByTime.begin(), signalsByTime.end(),
            [](const auto& a, const auto& b) { return a.second < b.second; });
        
        for (size_t i = 0; i < signalsToRemove && i < signalsByTime.size(); ++i) {
            activeSignals_.erase(signalsByTime[i].first);
        }
    }
    
    // Create tracker for this signal
    SignalOutcomeTracker tracker(signal);
    activeSignals_[signal.id] = tracker;
    
    totalSignalsTracked_++;
    
    if (config_.enableDetailedLogging) {
        // Log signal registration with memory usage info
        if (totalSignalsTracked_ % 1000 == 0) {
            std::cout << "PTCA: Tracked " << totalSignalsTracked_ 
                      << " signals, active: " << activeSignals_.size() << std::endl;
        }
    }
}
 

void PTCAIntegrationManager::registerExecution(const std::string& signalId,
                                              const ExecutionReport& executionReport) {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    
    auto it = activeSignals_.find(signalId);
    if (it != activeSignals_.end()) {
        SignalOutcomeTracker& tracker = it->second;
        tracker.wasExecuted = true;
        tracker.executionTime = core::now();
        tracker.executionPrice = executionReport.averageFillPrice();
        executedSignals_++;
        
        if (config_.enableDetailedLogging) {
            // Log execution registration
        }
    }
}

void PTCAIntegrationManager::updateMarketPrice(const core::Symbol& symbol, double price) {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    currentPrices_[symbol.toString()] = price;
    
    // Update current P&L for active signals
    for (auto& [signalId, tracker] : activeSignals_) {
        if (tracker.originalSignal.symbol == symbol && tracker.wasExecuted) {
            tracker.currentPrice = price;
            tracker.currentPnL = calculateSignalPnL(tracker);
        }
    }
}

double PTCAIntegrationManager::getSignalRecommendation(const strategy::TradingSignal& signal) {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    return ptca_->getSignalRecommendation(signal);
}

PavlovianCreditAssignment::TradingDecision 
PTCAIntegrationManager::generateTradingDecision(const core::Symbol& symbol,
                                               const std::vector<strategy::TradingSignal>& currentSignals) {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    
    // Convert signals to learning events
    std::vector<LearningEvent> events;
    for (const auto& signal : currentSignals) {
        events.push_back(createLearningEvent(signal));
    }
    
    return ptca_->generateTradingDecision(symbol, events);
}

PTCAIntegrationManager::IntegrationStats PTCAIntegrationManager::getStats() const {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    
    IntegrationStats stats;
    stats.totalSignalsTracked = totalSignalsTracked_;
    stats.signalsWithOutcomes = signalsWithOutcomes_;
    stats.executedSignals = executedSignals_;
    stats.averageSignalPnL = (executedSignals_ > 0) ? totalPnL_ / executedSignals_ : 0.0;
    stats.currentlyTrackedSignals = activeSignals_.size();
    stats.lastModelSave = lastModelSave_;
    stats.ptcaStats = ptca_->getLearningStats();
    
    // Calculate learning accuracy (simplified)
    if (signalsWithOutcomes_ > 0) {
        stats.learningAccuracy = std::max(0.0, 1.0 - std::abs(stats.averageSignalPnL) / config_.strongOutcomeThreshold);
    }
    
    return stats;
}

bool PTCAIntegrationManager::saveModel(const std::string& filename) {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    
    std::string filepath = filename;
    if (!config_.modelSaveDirectory.empty()) {
        filepath = config_.modelSaveDirectory + (filename.empty() ? generateModelFilename() : filename);
    }
    
    if (ptca_->saveModel(filepath)) {
        lastModelSave_ = core::now();
        
        // Also save as "latest_model.json" for easy loading
        if (!config_.modelSaveDirectory.empty() && filename != "latest_model.json") {
            ptca_->saveModel(config_.modelSaveDirectory + "latest_model.json");
        }
        
        return true;
    }
    
    return false;
}

bool PTCAIntegrationManager::loadModel(const std::string& filename) {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    
    std::string filepath = filename;
    if (!config_.modelSaveDirectory.empty()) {
        filepath = config_.modelSaveDirectory + filename;
    }
    
    return ptca_->loadModel(filepath);
}

void PTCAIntegrationManager::reset() {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    
    ptca_->reset();
    activeSignals_.clear();
    currentPrices_.clear();
    totalSignalsTracked_ = 0;
    signalsWithOutcomes_ = 0;
    executedSignals_ = 0;
    totalPnL_ = 0.0;
}

// Private methods

void PTCAIntegrationManager::processingLoop() {
    while (running_) {
        try {
            evaluateSignalOutcomes();
            cleanupOldSignals();
            checkModelSave();
            
            // Sleep for processing interval
            std::this_thread::sleep_for(config_.priceUpdateInterval);
            
        } catch (const std::system_error& e) {
            // Handle system errors (like invalid argument, resource temporarily unavailable)
            if (config_.enableDetailedLogging) {
                // Log specific system error with error code
                std::cerr << "PTCA Processing System Error: " << e.what() 
                          << " (code: " << e.code() << ")" << std::endl;
            }
            
            // If it's a threading/mutex issue, try to recover
            if (e.code() == std::errc::invalid_argument || 
                e.code() == std::errc::resource_deadlock_would_occur) {
                // Brief sleep before retry
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                // For other system errors, longer pause
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            
        } catch (const std::bad_alloc& e) {
            // Handle memory allocation failures
            if (config_.enableDetailedLogging) {
                std::cerr << "PTCA Processing Memory Error: " << e.what() << std::endl;
            }
            
            // Try to free up memory by cleaning old signals aggressively
            try {
                std::lock_guard<std::recursive_mutex> lock(signalMutex_);
                if (activeSignals_.size() > config_.maxSignalsToTrack / 2) {
                    auto halfSize = activeSignals_.size() / 2;
                    auto it = activeSignals_.begin();
                    std::advance(it, halfSize);
                    activeSignals_.erase(activeSignals_.begin(), it);
                }
            } catch (...) {
                // If we can't even lock, the system is in bad shape
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
            
        } catch (const std::exception& e) {
            // Log any other errors but continue processing
            if (config_.enableDetailedLogging) {
                std::cerr << "PTCA Processing General Error: " << e.what() << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}


void PTCAIntegrationManager::evaluateSignalOutcomes() {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    auto now = core::now();
    std::vector<std::string> signalsToProcess;
    
    // Find signals ready for outcome evaluation
    for (const auto& [signalId, tracker] : activeSignals_) {
        if (!tracker.outcomeReported) {
            auto signalAge = now - tracker.signalTime;
            // Evaluate if enough time has passed
            if (signalAge >= config_.outcomeEvaluationDelay) {
                signalsToProcess.push_back(signalId);
            }
        }
    }
    
    // Process each ready signal
    for (const std::string& signalId : signalsToProcess) {
        auto it = activeSignals_.find(signalId);
        if (it == activeSignals_.end()) continue;
        
        SignalOutcomeTracker& tracker = it->second;
        
        // MODIFIED: Always calculate theoretical P&L regardless of execution
        double theoreticalPnL = calculateSignalPnL(tracker);
        
        if (std::abs(theoreticalPnL) >= config_.minimumPnLThreshold) {
            // Create learning event and outcome
            LearningEvent learningEvent = createLearningEvent(tracker.originalSignal);
            
            // Create outcome based on theoretical P&L
            OutcomeMeasure outcome;
            outcome.timestamp = now;
            outcome.value = theoreticalPnL;
            outcome.outcomeType = tracker.wasExecuted ? "executed_pnl" : "theoretical_pnl";
            outcome.symbol = tracker.originalSignal.symbol;
            
            // Add to PTCA for learning
            std::vector<LearningEvent> events = {learningEvent};
            ptca_->addTimestep(events, outcome);
            
            tracker.outcomeReported = true;
            signalsWithOutcomes_++;
            totalPnL_ += theoreticalPnL;
            
            if (config_.enableDetailedLogging) {
                std::cout << "PTCA Learning: Signal " << signalId 
                         << " (" << outcome.outcomeType << ") P&L: " 
                         << (theoreticalPnL * 100) << "%" << std::endl;
            }
        } else {
            // P&L too small to learn from
            tracker.outcomeReported = true;
            signalsWithOutcomes_++;
            
            if (config_.enableDetailedLogging) {
                std::cout << "Signal " << signalId << " theoretical P&L: " 
                        << (theoreticalPnL * 100) << "% (threshold: " 
                        << (config_.minimumPnLThreshold * 100) << "%)" << std::endl;
            }
        }
    }
}

// double PTCAIntegrationManager::calculateSignalPnL(const SignalOutcomeTracker& tracker) {
//     if (!tracker.wasExecuted || tracker.executionPrice <= 0.0 || tracker.currentPrice <= 0.0) {
//         return 0.0;
//     }
    
//     double pnl = 0.0;
//     if (tracker.originalSignal.direction == core::Direction::BUY) {
//         pnl = (tracker.currentPrice - tracker.executionPrice) / tracker.executionPrice;
//     } else if (tracker.originalSignal.direction == core::Direction::SELL) {
//         pnl = (tracker.executionPrice - tracker.currentPrice) / tracker.executionPrice;
//     }
    
//     return pnl;
// }
double PTCAIntegrationManager::calculateSignalPnL(const SignalOutcomeTracker& tracker) {
    // Get current price for this symbol
    auto priceIt = currentPrices_.find(tracker.originalSignal.symbol.toString());
    if (priceIt == currentPrices_.end()) {
        return 0.0; // No current price
    }
    
    double currentPrice = priceIt->second;
    double entryPrice = 0.0;
    
    if (tracker.wasExecuted && tracker.executionPrice > 0.0) {
        // Use actual execution price
        entryPrice = tracker.executionPrice;
    } else {
        // ✅ Use theoretical entry price from signal targets
        if (!tracker.originalSignal.targets.empty()) {
            entryPrice = tracker.originalSignal.targets[0].price;
        } else {
            return 0.0;
        }
    }
    
    // Calculate theoretical P&L
    double pnl = 0.0;
    if (tracker.originalSignal.direction == core::Direction::BUY) {
        pnl = (currentPrice - entryPrice) / entryPrice;
    } else if (tracker.originalSignal.direction == core::Direction::SELL) {
        pnl = (entryPrice - currentPrice) / entryPrice;
    }
    
    return pnl;
}

LearningEvent PTCAIntegrationManager::createLearningEvent(const strategy::TradingSignal& signal) {
    LearningEvent event;
    
    // Determine event type based on signal source type
    switch (signal.sourceType) {
        case strategy::ImbalanceType::VOLUME_IMBALANCE:
            event.type = EventType::VOLUME_IMBALANCE_SIGNAL;
            break;
        case strategy::ImbalanceType::PRICE_LEVEL_IMBALANCE:
            event.type = EventType::PRICE_LEVEL_SIGNAL;
            break;
        case strategy::ImbalanceType::LIQUIDITY_REMOVAL:
            event.type = EventType::LIQUIDITY_REMOVAL_SIGNAL;
            break;
        case strategy::ImbalanceType::ORDER_FLOW_IMBALANCE:
            event.type = EventType::ORDER_FLOW_SIGNAL;
            break;
        case strategy::ImbalanceType::LARGE_ORDER_APPEARANCE:
        case strategy::ImbalanceType::LARGE_ORDER_DISAPPEARANCE:
            event.type = EventType::LARGE_ORDER_SIGNAL;
            break;
        case strategy::ImbalanceType::BID_ASK_SPREAD_CHANGE:
        case strategy::ImbalanceType::ABNORMAL_SPREAD:
            event.type = EventType::SPREAD_CHANGE_SIGNAL;
            break;
        case strategy::ImbalanceType::MARKET_DEPTH_CHANGE:
            event.type = EventType::MARKET_DEPTH_SIGNAL;
            break;
        default:
            event.type = EventType::CUSTOM_SIGNAL;
            break;
    }
    
    // Create unique event ID based on signal characteristics
    std::ostringstream eventIdStream;
    eventIdStream << static_cast<int>(event.type) << "_"
                  << signal.symbol.toString() << "_"
                  << static_cast<int>(signal.direction);
    event.eventId = eventIdStream.str();
    
    event.timestamp = signal.generationTime;
    event.isActive = true;
    event.rawScore = signal.strength * signal.confidence * signal.quality;
    event.symbol = signal.symbol;
    event.exchange = signal.exchangeId;
    event.direction = signal.direction;
    
    return event;
}

OutcomeMeasure PTCAIntegrationManager::createOutcome(const SignalOutcomeTracker& tracker) {
    OutcomeMeasure outcome;
    outcome.timestamp = core::now();
    outcome.value = tracker.currentPnL;
    outcome.outcomeType = "pnl";
    outcome.symbol = tracker.originalSignal.symbol;
    return outcome;
}

void PTCAIntegrationManager::cleanupOldSignals() {
    std::lock_guard<std::recursive_mutex> lock(signalMutex_);
    // ✅ ADD: Emergency cleanup if too many signals
    if (activeSignals_.size() > 1000) {  // Hard limit
        std::cout << "EMERGENCY: Too many PTCA signals (" << activeSignals_.size() 
                  << "), clearing oldest 50%" << std::endl;
        
        auto halfSize = activeSignals_.size() / 2;
        auto it = activeSignals_.begin();
        std::advance(it, halfSize);
        activeSignals_.erase(activeSignals_.begin(), it);
        
        // Force garbage collection
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    auto now = core::now();
    auto it = activeSignals_.begin();
    
    while (it != activeSignals_.end()) {
        auto signalAge = now - it->second.signalTime;
        
        // Remove signals that are too old or have been processed
        if (signalAge > config_.signalTrackingWindow || 
            (it->second.outcomeReported && signalAge > config_.outcomeEvaluationDelay)) {
            it = activeSignals_.erase(it);
        } else {
            ++it;
        }
    }
} 

void PTCAIntegrationManager::checkModelSave() {
    if (!config_.enableContinuousLearning || config_.modelSaveDirectory.empty()) {
        return;
    }
    
    auto now = core::now();
    if (now - lastModelSave_ >= config_.modelSaveInterval) {
        saveModel();
    }
}

std::string PTCAIntegrationManager::generateModelFilename() const {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    
    std::ostringstream filename;
    filename << "ptca_model_" << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S") << ".json";
    return filename.str();
}

} // namespace learning
} // namespace trading
