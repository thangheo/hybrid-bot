/**
 * @file strategy_engine_example.cpp
 * @brief Example usage of the Strategy Engine with JSON configuration
 * 
 * This example demonstrates how to:
 * 1. Load configuration from JSON file
 * 2. Initialize and start the strategy engine
 * 3. Process order book data
 * 4. Handle trading signals
 * 5. Monitor performance
 * 
 * Created on: May 26, 2025
 */

#include "strategy/engine/strategy_engine.hpp"
#include "strategy/config/strategy_config_loader.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "common/logger.hpp"
#include "common/event_bus.hpp"
#include "common/trading_events.hpp"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

using namespace trading;
using namespace trading::strategy;
using namespace std::chrono_literals;
using namespace data_ingestion;

// Function to load configuration from JSON file (already implemented in strategy_config_loader.cpp)
// extern StrategyConfig loadStrategyConfigFromFile(const std::string& filePath);

// Signal callback function to handle new trading signals
class SignalHandler {
public:
    void onNewSignal(const TradingSignal& signal) {
        LOG_INFO("NEW TRADING SIGNAL RECEIVED!");
        LOG_INFO("  Signal ID: {}", signal.id);
        LOG_INFO("  Symbol: {}", signal.symbol.toString());
        LOG_INFO("  Direction: {}", signal.direction == Direction::BUY ? "BUY" : "SELL");
        LOG_INFO("  Strength: {:.3f}", signal.strength);
        LOG_INFO("  Confidence: {}", static_cast<int>(signal.confidence));
        LOG_INFO("  Expected Price Impact: {:.6f}", signal.expectedPriceImpact);
        
        // Here you would typically send this signal to the execution engine
        // For demonstration, we'll just store it
        receivedSignals_.push_back(signal);
        
        // Simulate signal execution result after some delay
        simulateSignalExecution(signal);
    }
    
    const std::vector<TradingSignal>& getReceivedSignals() const {
        return receivedSignals_;
    }
    
private:
    std::vector<TradingSignal> receivedSignals_;
    
    void simulateSignalExecution(const TradingSignal& signal) {
        // Simulate async execution in a separate thread
        std::thread([this, signal]() {
            // Simulate execution delay
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // Create mock execution result
            SignalExecutionResult result;
            result.signal_id = signal.id;
            result.status = SignalExecutionStatus::COMPLETED;
            result.startTime = std::chrono::system_clock::now() - std::chrono::seconds(5);
            result.completionTime = std::chrono::system_clock::now();
            result.orderQuantity =  1; //signal.targetSize;
            result.orderPrice = 50000.0; // Mock BTC price
            result.pnl = (signal.direction == Direction::BUY ? 1.0 : -1.0) * 
                        signal.strength * 100.0; // Mock P&L based on signal strength
            result.signalStrength = signal.strength;
            
            LOG_INFO("SIGNAL EXECUTION COMPLETED: {} with P&L: {:.2f}", 
                          signal.id, result.pnl);
            
            // You would send this result back to the strategy engine
            // For now, we'll just log it
        }).detach();
    }
};

// Mock order book data generator for testing
data_ingestion::OrderBook createMockOrderBook(const std::string& symbol, 
                                             const core::ExchangeId& exchangeId) {
    
    OrderBook orderBook(exchangeId, core::Symbol::fromString(symbol));
    
    // Create mock bid and ask levels with some imbalance
    std::vector<core::PriceLevel> bids;
    std::vector<core::PriceLevel> asks;
    
    double midPrice = 50000.0; // Mock BTC price
    double spread = 10.0;
    
    // Create imbalanced order book (more volume on bid side)
    for (int i = 0; i < 10; ++i) {
        double bidPrice = midPrice - spread/2.0 - i * 5.0;
        double askPrice = midPrice + spread/2.0 + i * 5.0;
        
        // Create volume imbalance (more on bid side)
        double bidVolume = 1.0 + i * 0.5 + (i < 3 ? 2.0 : 0.0); // Extra volume in top 3 levels
        double askVolume = 0.8 + i * 0.3;
        
        bids.emplace_back(bidPrice, bidVolume);
        asks.emplace_back(askPrice, askVolume);
    }
    
    // Apply snapshot
    uint64_t sequenceNumber = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    orderBook.applySnapshot(bids, asks, sequenceNumber, std::chrono::system_clock::now());
    
    return orderBook;
}

// Main example function
int main() {
    try {
        // common::Logger::initialize("TradingApp", trading::common::LogLevel::INFO);
        common::Logger::initialize("TradingApp", common::LogLevel::DEBUG);  // INFO, WARN, ERROR, CRITICAL
        //event bus
        common::Logger::initialize("TradingApp", common::LogLevel::DEBUG);
    
        // Start event bus before creating any components
        auto& eventBus = common::EventBus::getInstance();
        eventBus.start();
        LOG_INFO("=== Strategy Engine Example Starting ===");
        
        // 1. Load configuration from JSON file
        std::string configPath = "strategy_config.json";
        LOG_INFO("Loading configuration from: {}", configPath);
        
        StrategyConfig config = loadStrategyConfigFromFile(configPath);
        LOG_INFO("Configuration loaded successfully");
        LOG_INFO("  Strategy ID: {}", config.strategy_id);
        LOG_INFO("  Version: {}", config.version);
        LOG_INFO("  Trading Pairs: {}", config.tradingPairs.size());
        
        // 2. Create and initialize strategy engine
        StrategyEngine strategyEngine(config.strategy_id);
        
        if (!strategyEngine.initialize(config)) {
            LOG_ERROR("Failed to initialize strategy engine");
            return -1;
        }
        LOG_INFO("Strategy engine initialized successfully");
        
        // 3. Set up signal handler
        SignalHandler signalHandler;
        size_t callbackId = strategyEngine.registerSignalCallback(
            [&signalHandler](const TradingSignal& signal) {
                signalHandler.onNewSignal(signal);
            }
        );
        LOG_INFO("Signal callback registered with ID: {}", callbackId);
        
        // 4. Start the strategy engine
        if (!strategyEngine.start()) {
            LOG_ERROR("Failed to start strategy engine");
            return -1;
        }
        LOG_INFO("Strategy engine started successfully");
        
        // 5. Simulate order book data processing
        LOG_INFO("Starting order book simulation...");
        
        // Process mock order book data for multiple symbols
        for (int cycle = 0; cycle < 20; ++cycle) {
            LOG_INFO("Processing cycle {}/20", cycle + 1);
            
            // Process BTC/USDT
            auto btcOrderBook = createMockOrderBook("BTC/USDT", core::ExchangeId::binance());
            if (!strategyEngine.processOrderBook(btcOrderBook)) {
                LOG_WARN("Failed to process BTC order book in cycle {}", cycle + 1);
            }
            
            // Process ETH/USDT (if configured)
            auto ethOrderBook = createMockOrderBook("ETH/USDT", core::ExchangeId::binance());
            if (!strategyEngine.processOrderBook(ethOrderBook)) {
                LOG_WARN("Failed to process ETH order book in cycle {}", cycle + 1);
            }
            //
            // Check active signals
            auto activeSignals = strategyEngine.getActiveSignals();
            LOG_INFO("Active signals: {}", activeSignals.size());
            
            // Print strategy status every 5 cycles
            if ((cycle + 1) % 5 == 0) {
                std::string status = strategyEngine.getStatus();
                LOG_INFO("Strategy Status:\n{}", status);
                
                auto performance = strategyEngine.getPerformance();
                LOG_INFO("Performance Metrics:");
                LOG_INFO("  Signals Generated: {}", performance.signalsGenerated);
                LOG_INFO("  Signals Executed: {}", performance.signalsExecuted);
                LOG_INFO("  Successful Signals: {}", performance.successfulSignals);
                LOG_INFO("  Total P&L: {:.2f}", performance.totalPnl);
                LOG_INFO("  Average Signal Strength: {:.3f}", performance.averageSignalStrength);
            }
            
            // Wait between cycles to simulate real-time processing
            std::this_thread::sleep_for(2s);
        }
        
        // 6. Test parameter updates
        LOG_INFO("Testing parameter updates...");
        ParameterSet newParams = strategyEngine.getParameters();
        newParams.numeric_params["global_sensitivity"] = 0.9; // Increase sensitivity
        newParams.numeric_params["signal_strength_threshold"] = 0.5; // Lower threshold
        
        if (strategyEngine.updateParameters(newParams)) {
            LOG_INFO("Parameters updated successfully");
        } else {
            LOG_ERROR("Failed to update parameters");
        }
        
        // 7. Test manual signal evaluation
        LOG_INFO("Testing manual signal evaluation...");
        if (strategyEngine.triggerSignalEvaluation()) {
            LOG_INFO("Manual signal evaluation triggered successfully");
        } else {
            LOG_ERROR("Failed to trigger manual signal evaluation");
        }
        
        // 8. Continue processing for a bit with new parameters
        for (int cycle = 0; cycle < 10; ++cycle) {
            auto orderBook = createMockOrderBook("BTC/USDT", core::ExchangeId::binance());
            strategyEngine.processOrderBook(orderBook);
            std::this_thread::sleep_for(1s);
        }
        
        // 9. Final status and cleanup
        LOG_INFO("Final strategy status:");
        std::string finalStatus = strategyEngine.getStatus();
        LOG_INFO("\n{}", finalStatus);
        
        auto finalPerformance = strategyEngine.getPerformance();
        LOG_INFO("Final Performance Metrics:");
        LOG_INFO("  Signals Generated: {}", finalPerformance.signalsGenerated);
        LOG_INFO("  Signals Executed: {}", finalPerformance.signalsExecuted);
        LOG_INFO("  Success Rate: {:.2f}%", 
                        finalPerformance.signalsExecuted > 0 ? 
                        (double)finalPerformance.successfulSignals / finalPerformance.signalsExecuted * 100.0 : 0.0);
        LOG_INFO("  Total P&L: {:.2f}", finalPerformance.totalPnl);
        
        LOG_INFO("Received {} signals via callback", signalHandler.getReceivedSignals().size());
        
        // Stop the strategy engine
        if (strategyEngine.stop()) {
            LOG_INFO("Strategy engine stopped successfully");
        } else {
            LOG_ERROR("Failed to stop strategy engine");
        }
        
        // Unregister callback
        if (strategyEngine.unregisterSignalCallback(callbackId)) {
            LOG_INFO("Signal callback unregistered successfully");
        }
        
        LOG_INFO("=== Strategy Engine Example Completed Successfully ===");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception in strategy engine example: {}", e.what());
        return -1;
    } catch (...) {
        LOG_ERROR("Unknown exception in strategy engine example");
        return -1;
    }
    
    return 0;
}

// Additional utility function to demonstrate loading exchange-specific parameters
void demonstrateExchangeSpecificConfig() {
    LOG_INFO("=== Demonstrating Exchange-Specific Configuration ===");
    
    // Load exchange-specific parameters
    try {
        auto exchangeParams = loadExchangeSpecificParameters("exchange_specific_params.json");
        
        for (const auto& [exchangeId, params] : exchangeParams) {
            LOG_INFO("Exchange: {}", exchangeId.toString());
            LOG_INFO("  Numeric params: {}", params.numeric_params.size());
            LOG_INFO("  String params: {}", params.string_params.size());
            LOG_INFO("  Boolean params: {}", params.bool_params.size());
            LOG_INFO("  Array params: {}", params.array_params.size());
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to load exchange-specific parameters: {}", e.what());
    }
}
