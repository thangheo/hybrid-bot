/**
 * @file strategy_app_with_orderbook.cpp
 * @brief Strategy Engine with real OrderBook data integration
 */

#include "strategy/engine/strategy_engine.hpp"
#include "strategy/config/strategy_config_loader.hpp"
#include "common/logger.hpp"
#include "common/event_bus.hpp"
#include "common/trading_events.hpp"
#include "common/zeromq_client.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

using namespace trading;
using namespace trading::strategy;
using namespace std::chrono_literals;
using namespace data_ingestion;

class OrderBookReceiver {
private:
    std::unique_ptr<common::ZeroMQClient> zmqClient_;
    StrategyEngine* strategyEngine_;
    std::unordered_map<std::string, std::shared_ptr<OrderBook>> orderBooks_;
    std::mutex orderBooksMutex_;
    std::atomic<size_t> messagesReceived_{0};
    
public:
    OrderBookReceiver(StrategyEngine* engine) : strategyEngine_(engine) {
        // Configure ZeroMQ client to receive order book data
        common::ZeroMQConfig config;
        config.subscriberEndpoint = "tcp://localhost:5555"; // Match data ingestion publisher
        config.receiveHighWaterMark = 10000;
        config.publisherEndpoint = "";  // Leave empty - we don't need to publish
        config.pollTimeout = std::chrono::milliseconds(100);
        
        zmqClient_ = std::make_unique<common::ZeroMQClient>(config);
        
        // Set message handler for order book updates
        zmqClient_->setMessageHandler([this](const std::string& topic, const std::string& message) {
            this->handleOrderBookMessage(topic, message);
        });
        
        // Set error handler
        zmqClient_->setErrorHandler([](const std::string& context, const std::string& error) {
            LOG_ERROR("ZMQ Error in {}: {}", context, error);
        });
    }
    
    bool start(const std::vector<std::string>& symbols) {
        if (!zmqClient_->start()) {
            LOG_ERROR("Failed to start ZeroMQ client");
            return false;
        }
        
        // Subscribe to order book topics for each symbol
        for (const auto& symbol : symbols) {
            // Topic format: "orderbook.BINANCE.BTC/USDT" (matches your publisher format)
            std::string topic = "orderbook.BINANCE." + symbol;
            if (!zmqClient_->subscribe(topic)) {
                LOG_ERROR("Failed to subscribe to topic: {}", topic);
                return false;
            }
            LOG_INFO("Subscribed to order book topic: {}", topic);
        }
        
        LOG_INFO("OrderBookReceiver started successfully");
        return true;
    }
    
    void stop() {
        if (zmqClient_) {
            zmqClient_->stop();
        }
        LOG_INFO("OrderBookReceiver stopped");
    }
    
    size_t getMessagesReceived() const {
        return messagesReceived_.load();
    }
    
    // Get current mid price for a symbol
    double getCurrentMidPrice(const std::string& symbol) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(orderBooksMutex_));
        std::string key = "BINANCE:" + symbol; // Match your key format
        auto it = orderBooks_.find(key);
        if (it != orderBooks_.end()) {
            return it->second->getMidPrice();
        }
        return 0.0; // Return 0 if not found
    }
    
private:
    void handleOrderBookMessage(const std::string& topic, const std::string& message) {
        try {
            messagesReceived_.fetch_add(1);
            
            // Parse JSON message from OrderBookReconstructor
            auto json = nlohmann::json::parse(message);
            
            // Extract order book data
            std::string exchangeIdStr = json["exchangeId"];
            std::string tradingPair = json["tradingPair"];
            uint64_t sequenceNumber = json["sequenceNumber"];
            bool isSnapshot = json["isSnapshot"];
                        
            // Convert to your types
            core::ExchangeId exchangeId = core::ExchangeId::fromString(exchangeIdStr);
            core::Symbol symbol = core::Symbol::fromString(tradingPair);
            
            // Parse bids and asks
            std::vector<core::PriceLevel> bids;
            std::vector<core::PriceLevel> asks;
            
            if (json.contains("bids") && json["bids"].is_array()) {
                for (const auto& bid : json["bids"]) {
                    double price = bid["price"];
                    double quantity = bid["quantity"];
                    bids.emplace_back(price, quantity);
                }
            }
            
            if (json.contains("asks") && json["asks"].is_array()) {
                for (const auto& ask : json["asks"]) {
                    double price = ask["price"];
                    double quantity = ask["quantity"];
                    asks.emplace_back(price, quantity);
                }
            }
            
            // Create or update OrderBook instance
            std::shared_ptr<OrderBook> orderBook;
            {
                std::lock_guard<std::mutex> lock(orderBooksMutex_);
                std::string key = exchangeIdStr + ":" + tradingPair;
                
                auto it = orderBooks_.find(key);
                if (it == orderBooks_.end()) {
                    // Create new order book
                    orderBook = std::make_shared<OrderBook>(exchangeId, symbol);
                    orderBooks_[key] = orderBook;
                    LOG_INFO("Created new OrderBook for {}", key);
                } else {
                    orderBook = it->second;
                }
            }
            
            // Apply the update
            auto timestamp = std::chrono::system_clock::now();
            bool success;
            
            if (isSnapshot) {
                success = orderBook->applySnapshot(bids, asks, sequenceNumber, timestamp);
            } else {
                success = orderBook->applyUpdate(bids, asks, sequenceNumber, timestamp);
            }
            
            if (success && strategyEngine_) {
                // Send to strategy engine for processing
                if (!strategyEngine_->processOrderBook(*orderBook)) {
                    LOG_WARN("Strategy engine failed to process order book for {}", tradingPair);
                }
            } else if (!success) {
                LOG_WARN("Failed to apply order book update for {} seq={}", tradingPair, sequenceNumber);
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR("Error processing order book message: {}", e.what());
        }
    }
};

// Updated signal handler (same as before)
class SignalHandler {
public:
    SignalHandler(StrategyEngine* engine = nullptr, OrderBookReceiver* receiver = nullptr) 
        : strategyEngine_(engine), orderBookReceiver_(receiver) {}
    
    void onNewSignal(const TradingSignal& signal) {
        // LOG_INFO("NEW TRADING SIGNAL RECEIVED!");
        // LOG_INFO("  Signal ID: {}", signal.id);
        // LOG_INFO("  Symbol: {}", signal.symbol.toString());
        // LOG_INFO("  Direction: {}", signal.direction == Direction::BUY ? "BUY" : "SELL");
        // LOG_INFO("  Strength: {:.3f}", signal.strength);
        // LOG_INFO("  Confidence: {}", signal.confidence);

        std::lock_guard<std::mutex> lock(processedMutex_);
        //filter duplicate signals
        //filter signals based on custom criteria (e.g., strength threshold)
        if (signal.strength < 0.5) return; // Weak signals are ignored
        if(signal.direction == core::Direction::NEUTRAL) return; // Ignore neutral signals
        if(signal.confidence < 0.5) return; // Ignore low confidence signals
        //print signal details for debugging
        // LOG_INFO("  Signal ID: {}", signal.id);
        LOG_INFO("  Symbol: {}", signal.symbol.toString());
        LOG_INFO("  Direction: {}", signal.direction == Direction::BUY ? "BUY" : "SELL");
        LOG_INFO("  Strength: {:.3f}", signal.strength);
        LOG_INFO("  Confidence: {}", signal.confidence);
        if (processedSignals_.count(signal.id)) return; // Already processed
        processedSignals_.insert(signal.id);

        receivedSignals_.push_back(signal);

        simulateSignalExecution(signal);
    }
    
    const std::vector<TradingSignal>& getReceivedSignals() const {
        return receivedSignals_;
    }
    
private:
    std::vector<TradingSignal> receivedSignals_;
    std::unordered_set<std::string> processedSignals_;
    std::mutex processedMutex_;
    StrategyEngine* strategyEngine_;
    OrderBookReceiver* orderBookReceiver_;

    void simulateSignalExecution(const TradingSignal& signal) {
        std::thread([this, signal]() {
            // Record entry time and get entry price from signal
            auto entryTime = std::chrono::system_clock::now();
            double entryPrice = signal.getMetric("mid_price", 0); // Use signal's mid price at generation time
            if (entryPrice <= 0.0) {
                LOG_WARN("Signal {} has invalid entry price metric, skipping execution", signal.id);
                return;
            }
            // LOG_INFO("EXECUTING SIGNAL: {} | Symbol: {} | Direction: {} | Strength: {:.3f} | Entry Price: {:.2f}", 
            //          signal.id, signal.symbol.toString(), 
            //          signal.direction == Direction::BUY ? "BUY" : "SELL", 
            //          signal.strength, entryPrice);
            
            // Simulate holding period (shorter for testing)
            std::this_thread::sleep_for(std::chrono::seconds(2));
            
            // Get REAL current exit price from live order book data
            auto exitTime = std::chrono::system_clock::now();
            double exitPrice = entryPrice; // Fallback to entry price
            
            if (orderBookReceiver_) {
                double currentMidPrice = orderBookReceiver_->getCurrentMidPrice(signal.symbol.toString());
                if (currentMidPrice > 0.0) {
                    exitPrice = currentMidPrice; // Use real current price
                } else {
                    // Fallback: simulate realistic price movement if no real data available
                    double priceMovementFactor = (signal.strength - 0.5) * 2.0; // Convert 0-1 to -1 to 1
                    double baseMovement = priceMovementFactor * 0.005; // Max 0.5% movement
                    double randomFactor = (rand() % 200 - 100) / 10000.0; // ±1% random
                    double totalMovement = baseMovement + randomFactor;
                    exitPrice = entryPrice * (1.0 + totalMovement);
                }
            }
            
            // Calculate actual P&L based on direction
            double pnl = 0.0;
            double orderQuantity = 1.0; // 1 unit for simplicity
            double feeRate = 0.001;     // 0.1% per trade

            double fees = entryPrice * orderQuantity * feeRate 
                        + exitPrice * orderQuantity * feeRate;

            if (signal.direction == Direction::BUY) {
                pnl = (exitPrice - entryPrice) * orderQuantity;
            } else if (signal.direction == Direction::SELL) {
                pnl = (entryPrice - exitPrice) * orderQuantity;
            }
            pnl -= fees;

            // Create execution result
            SignalExecutionResult result;
            result.signal_id = signal.id;
            result.status = SignalExecutionStatus::COMPLETED;
            result.startTime = entryTime;
            result.completionTime = exitTime;
            result.orderQuantity = orderQuantity;
            result.orderPrice = entryPrice;
            result.pnl = pnl;
            result.signalStrength = signal.strength;
            
            // Log the trade result with detailed info
            LOG_INFO("TRADE COMPLETED: {} | Entry: {:.2f} | Exit: {:.2f} | P&L: {:.2f} | Duration: {}s | Success: {}", 
                     signal.id, entryPrice, exitPrice, pnl,
                     std::chrono::duration_cast<std::chrono::seconds>(exitTime - entryTime).count(),
                     pnl > 0 ? "YES" : "NO");
            
            // Record the result back to strategy engine for performance tracking
            if (strategyEngine_) {
                bool recorded = strategyEngine_->recordSignalResult(signal.id, result);
                // LOG_INFO("Signal result recorded: {} - Success: {}", signal.id, recorded ? "YES" : "NO");
            }
            
        }).detach();
    }
};

int main() {
    try {
        // Initialize logging and event bus
        common::Logger::initialize("TradingApp", common::LogLevel::DEBUG);
        auto& eventBus = common::EventBus::getInstance();
        eventBus.start();
        
        LOG_INFO("=== Strategy Engine with Real OrderBook Data ===");
        
        // 1. Load configuration
        std::string configPath = "strategy_config.json";
        StrategyConfig config = loadStrategyConfigFromFile(configPath);
        LOG_INFO("Configuration loaded: {} trading pairs", config.tradingPairs.size());
        
        // 2. Initialize strategy engine
        StrategyEngine strategyEngine(config.strategy_id);
        if (!strategyEngine.initialize(config)) {
            LOG_ERROR("Failed to initialize strategy engine");
            return -1;
        }
        
        // 5. Initialize and start OrderBook receiver
        OrderBookReceiver orderBookReceiver(&strategyEngine);
        std::vector<std::string> symbols;
        for (const auto& pair : config.tradingPairs) {
            symbols.push_back(pair.first);
        }
        if (!orderBookReceiver.start(symbols)) {
            LOG_ERROR("Failed to start OrderBook receiver");
            return -1;
        }
        
        // 3. Set up signal handler (after OrderBookReceiver is created)
        SignalHandler signalHandler(&strategyEngine, &orderBookReceiver);
        size_t callbackId = strategyEngine.registerSignalCallback(
            [&signalHandler](const TradingSignal& signal) {
                signalHandler.onNewSignal(signal);
            }
        );
        
        // 4. Start strategy engine
        if (!strategyEngine.start()) {
            LOG_ERROR("Failed to start strategy engine");
            return -1;
        }
        
        LOG_INFO("=== System started - waiting for order book data ===");
        LOG_INFO("Make sure your DataIngestionService is running and publishing data!");
        
        // 6. Monitor and report status
        auto startTime = std::chrono::steady_clock::now();
        size_t lastMessageCount = 0;
        
        for (int cycle = 0; cycle < 60; ++cycle) { // Run for 2 minutes
            std::this_thread::sleep_for(1s);
            
            // Report every 10 seconds
            if (cycle % 10 == 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - startTime).count();

                size_t currentMessages = orderBookReceiver.getMessagesReceived();
                size_t newMessages = currentMessages - lastMessageCount;
                lastMessageCount = currentMessages;

                auto performance = strategyEngine.getPerformance();
                std::cout << "=== Status Report (" << elapsed << "s elapsed) ===" << std::endl;
                std::cout << "  Order book messages received: " << currentMessages << std::endl;
                std::cout << "  Messages in last 10s: " << newMessages << std::endl;
                std::cout << "  Active signals: " << strategyEngine.getActiveSignals().size() << std::endl;
                std::cout << "  Signals received via callback: " << signalHandler.getReceivedSignals().size() << std::endl;
                std::cout << "  Strategy performance:" << std::endl;
                std::cout << "    Signals generated: " << performance.signalsGenerated << std::endl;
                std::cout << std::fixed << std::setprecision(1);
                double successRate = performance.signalsExecuted > 0 ?
                    (double)performance.successfulSignals / performance.signalsExecuted * 100.0 : 0.0;
                std::cout << "    Success rate: " << successRate << "%" << std::endl;
                std::cout << std::fixed << std::setprecision(2);
                std::cout << "    Total P&L: " << performance.totalPnl << std::endl;

                if (newMessages == 0 && cycle > 20) {
                    std::cout << "[WARN] No new order book messages received in last 10s - check DataIngestionService" << std::endl;
                }
            }
            
            // Test parameter updates at 30s mark
            // if (cycle == 30) {
            //     LOG_INFO("Testing parameter updates...");
            //     ParameterSet newParams = strategyEngine.getParameters();
            //     newParams.numeric_params["global_sensitivity"] = 0.9;
            //     newParams.numeric_params["signal_strength_threshold"] = 0.4;
                
            //     if (strategyEngine.updateParameters(newParams)) {
            //         LOG_INFO("Parameters updated successfully");
            //     } else {
            //         LOG_ERROR("Failed to update parameters");
            //     }
            // }
        }
        
        // 7. Final cleanup
        LOG_INFO("Shutting down...");
        
        orderBookReceiver.stop();
        
        if (strategyEngine.stop()) {
            LOG_INFO("Strategy engine stopped successfully");
        }
        
        strategyEngine.unregisterSignalCallback(callbackId);
        
        // Final statistics
        auto finalPerformance = strategyEngine.getPerformance();
        LOG_INFO("=== Final Results ===");
        LOG_INFO("  Total order book messages: {}", orderBookReceiver.getMessagesReceived());
        LOG_INFO("  Total signals generated: {}", finalPerformance.signalsGenerated);
        LOG_INFO("  Total signals via callback: {}", signalHandler.getReceivedSignals().size());
        LOG_INFO("  Final P&L: {:.2f}", finalPerformance.totalPnl);
        
        eventBus.stop();
        LOG_INFO("=== Strategy Engine with Real Data Completed ===");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception: {}", e.what());
        return -1;
    }
    
    return 0;
}