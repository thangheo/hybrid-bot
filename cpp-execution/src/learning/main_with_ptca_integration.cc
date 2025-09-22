/**
 * @file main_with_ptca_integration.cpp
 * @brief Example of integrating PTCA learning into your main trading system
 * 
 * This shows how to modify your existing main.cpp to include PTCA learning.
 * The changes are marked with "// PTCA:" comments.
 * 
 * @author Trading System Team
 * @date September 13, 2025
 */

#include "your_existing_headers.hpp"
// PTCA: Add PTCA integration headers
#include "learning/ptca_integration_manager.hpp"
#include "learning/pavlovian_credit_assignment.hpp"

// Your existing includes...

class TradingSystem {
    // Your existing members...
    
    // PTCA: Add PTCA-related members
    std::unique_ptr<trading::learning::PTCAIntegrationManager> ptcaManager_;
    std::chrono::time_point<std::chrono::steady_clock> lastPriceUpdate_;
    
public:
    bool initialize() {
        // Your existing initialization code...
        
        // PTCA: Initialize PTCA learning system
        trading::learning::PTCAIntegrationConfig ptcaConfig;
        
        // Configure PTCA for your trading environment
        ptcaConfig.outcomeEvaluationDelay = std::chrono::minutes(2);  // Evaluate outcomes after 2 minutes
        ptcaConfig.signalTrackingWindow = std::chrono::minutes(15);   // Track signals for 15 minutes
        ptcaConfig.minimumPnLThreshold = 0.0005;                     // 0.05% minimum P&L to learn from
        ptcaConfig.priceMovementThreshold = 0.003;                   // 0.3% price movement significance
        ptcaConfig.pnlSuccessThreshold = 0.008;                      // 0.8% P&L success threshold
        ptcaConfig.strongOutcomeThreshold = 0.02;                    // 2% strong outcome threshold
        
        // Set up model persistence
        ptcaConfig.modelSaveDirectory = "models/ptca/";
        ptcaConfig.modelSaveInterval = std::chrono::minutes(20);
        ptcaConfig.enableContinuousLearning = true;
        ptcaConfig.enableDetailedLogging = true;
        
        // Performance limits
        ptcaConfig.maxSignalsToTrack = 500;  // Limit memory usage
        
        ptcaManager_ = std::make_unique<trading::learning::PTCAIntegrationManager>(ptcaConfig);
        ptcaManager_->start();
        
        lastPriceUpdate_ = std::chrono::steady_clock::now();
        
        // Your existing initialization continues...
        return true;
    }
    
    void run() {
        // Your existing startup code...
        
        // PTCA: Set up connections and callbacks
        setupPTCAConnections();
        
        // Your existing run loop...
    }
    
private:
    // PTCA: Setup PTCA connections to your existing system
    void setupPTCAConnections() {
        // 1. Connect to your market data updates for price tracking
        marketDataProcessor_->registerPriceCallback([this](const core::Symbol& symbol, double price) {
            onMarketPriceUpdate(symbol, price);
        });
        
        // 2. Connect to execution reports for P&L calculation
        executionEngine_->registerExecutionCallback([this](const execution::ExecutionReport& report) {
            onExecutionReport(report);
        });
    }
    
    // PTCA: Your existing signal callback, but enhanced with PTCA learning
    void onTradingSignal(const strategy::TradingSignal& signal) {
        signalsGenerated_++;
        
    // PTCA: Get recommendation from learned patterns (use signal while valid)
    double ptcaRecommendation = ptcaManager_->getSignalRecommendation(signal);
        
    // PTCA: Register signal for learning by moving it to avoid a copy
    ptcaManager_->registerSignal(std::move(const_cast<strategy::TradingSignal&>(signal)));
        
        // PTCA: Apply PTCA as a filter - you can adjust these thresholds
        if (ptcaRecommendation < -0.3) {
            // PTCA strongly recommends against this signal type
            std::cout << "PTCA Filter: Rejecting signal " << signal.id 
                     << " (recommendation: " << ptcaRecommendation << ")" << std::endl;
            return;
        }
        
        // PTCA: Optionally, adjust signal strength based on PTCA confidence
        double adjustedStrength = signal.strength;
        if (ptcaRecommendation > 0.2) {
            // Boost signals that PTCA likes
            adjustedStrength = std::min(1.0, signal.strength * (1.0 + ptcaRecommendation * 0.3));
            std::cout << "PTCA Boost: Signal " << signal.id 
                     << " strength boosted from " << signal.strength 
                     << " to " << adjustedStrength << std::endl;
        } else if (ptcaRecommendation < -0.1) {
            // Reduce strength for signals PTCA is uncertain about
            adjustedStrength = signal.strength * (1.0 + ptcaRecommendation * 0.2);
            std::cout << "PTCA Caution: Signal " << signal.id 
                     << " strength reduced from " << signal.strength 
                     << " to " << adjustedStrength << std::endl;
        }
        
        // Convert strategy signal to execution signal with PTCA adjustment
        execution::TradingSignal execSignal;
        execSignal.id = signal.id;
        execSignal.symbol = signal.symbol;
        execSignal.direction = signal.direction;
        execSignal.strength = adjustedStrength;  // Use PTCA-adjusted strength
        execSignal.confidence = signal.confidence;
        execSignal.tradingPair = signal.symbol.toString();
        execSignal.targetSize = signal.targetSize;
        execSignal.strategy_id = signal.strategyId;
        execSignal.signal_ttl = signal.ttl;
        
        // Continue with your existing risk validation and execution...
        auto validationResult = riskManager_->validateSignal(execSignal);
        if (validationResult.approved) {
            executionEngine_->executeSignal(execSignal);
        }
    }
    
    // PTCA: Market price update handler
    void onMarketPriceUpdate(const core::Symbol& symbol, double price) {
        // Throttle price updates to avoid overwhelming PTCA
        auto now = std::chrono::steady_clock::now();
        if (now - lastPriceUpdate_ >= std::chrono::seconds(5)) {
            ptcaManager_->updateMarketPrice(symbol, price);
            lastPriceUpdate_ = now;
        }
        
        // Your existing price update logic...
    }
    
    // PTCA: Execution report handler
    void onExecutionReport(const execution::ExecutionReport& report) {
        // Register execution with PTCA for learning
        ptcaManager_->registerExecution(report.orderId(), report);
        
        // Your existing execution handling logic...
    }
    
    // PTCA: Add a method to get PTCA trading decisions
    void evaluatePTCADecision(const core::Symbol& symbol) {
        // Get current signals for the symbol
        std::vector<strategy::TradingSignal> currentSignals = getCurrentSignalsForSymbol(symbol);
        
        if (!currentSignals.empty()) {
            auto decision = ptcaManager_->generateTradingDecision(symbol, currentSignals);
            
            std::cout << "PTCA Decision for " << symbol.toString() << ":" << std::endl;
            std::cout << "  Direction: " << core::directionToString(decision.recommendedDirection) << std::endl;
            std::cout << "  Confidence: " << decision.confidence << std::endl;
            std::cout << "  Expected Outcome: " << decision.expectedOutcome << std::endl;
            std::cout << "  Reasoning: " << decision.reasoning << std::endl;
            
            // You could use this decision to:
            // 1. Override or modify existing signals
            // 2. Generate new signals
            // 3. Adjust position sizing
            // 4. Trigger rebalancing
        }
    }
    
    // PTCA: Add periodic PTCA statistics reporting
    void reportPTCAStats() {
        auto stats = ptcaManager_->getStats();
        
        std::cout << "=== PTCA Learning Statistics ===" << std::endl;
        std::cout << "Total signals tracked: " << stats.totalSignalsTracked << std::endl;
        std::cout << "Signals with outcomes: " << stats.signalsWithOutcomes << std::endl;
        std::cout << "Executed signals: " << stats.executedSignals << std::endl;
        std::cout << "Average signal P&L: " << (stats.averageSignalPnL * 100) << "%" << std::endl;
        std::cout << "Learning accuracy: " << (stats.learningAccuracy * 100) << "%" << std::endl;
        std::cout << "Currently tracked: " << stats.currentlyTrackedSignals << std::endl;
        std::cout << "PTCA events learned: " << stats.ptcaStats.totalEvents << std::endl;
        std::cout << "PTCA timesteps: " << stats.ptcaStats.totalTimesteps << std::endl;
        std::cout << "=================================" << std::endl;
    }
    
    // Helper method to get current signals for a symbol (you'll need to implement this)
    std::vector<strategy::TradingSignal> getCurrentSignalsForSymbol(const core::Symbol& symbol) {
        // This should return current active signals for the given symbol
        // You'll need to implement this based on your signal storage
        std::vector<strategy::TradingSignal> signals;
        // Your implementation here...
        return signals;
    }
    
    void shutdown() {
        // PTCA: Stop PTCA learning and save final model
        if (ptcaManager_) {
            std::cout << "Saving final PTCA model..." << std::endl;
            ptcaManager_->saveModel("final_model.json");
            
            reportPTCAStats();
            
            ptcaManager_->stop();
            ptcaManager_.reset();
        }
        
        // Your existing shutdown code...
    }
};

int main(int argc, char* argv[]) {
    try {
        TradingSystem system;
        
        if (!system.initialize()) {
            std::cerr << "Failed to initialize trading system" << std::endl;
            return 1;
        }
        
        // PTCA: Set up periodic statistics reporting
        std::thread statsThread([&system]() {
            while (true) {
                std::this_thread::sleep_for(std::chrono::minutes(10));
                system.reportPTCAStats();
            }
        });
        
        std::cout << "Trading system started with PTCA learning enabled" << std::endl;
        
        system.run();
        
        system.shutdown();
        
        if (statsThread.joinable()) {
            statsThread.detach(); // Let it finish naturally
        }
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Trading system error: " << e.what() << std::endl;
        return 1;
    }
}

/*
 * INTEGRATION NOTES:
 * 
 * 1. PTCA Learning Process:
 *    - Registers every signal generated by your strategy
 *    - Tracks market prices to calculate P&L
 *    - Evaluates outcomes after a configurable delay
 *    - Learns which signal types/patterns lead to positive outcomes
 * 
 * 2. PTCA as a Filter:
 *    - Provides recommendations (-1.0 to 1.0) for each signal
 *    - Can reject signals with strongly negative recommendations
 *    - Can boost confidence for signals with positive recommendations
 * 
 * 3. PTCA Decision Support:
 *    - Can generate trading decisions based on learned patterns
 *    - Provides reasoning for decisions
 *    - Shows individual event contributions
 * 
 * 4. Model Persistence:
 *    - Automatically saves learned model periodically
 *    - Loads previous model on startup
 *    - Creates timestamped backups
 * 
 * 5. Performance Monitoring:
 *    - Tracks learning statistics
 *    - Reports signal success rates
 *    - Shows model performance metrics
 * 
 * 6. Memory Management:
 *    - Limits number of signals tracked simultaneously
 *    - Cleans up old signals automatically
 *    - Throttles price updates to avoid overhead
 */
