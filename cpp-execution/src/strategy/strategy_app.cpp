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
#include <atomic>

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
    double getCurrentMidPrice(const core::Symbol& symbol) const {
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(orderBooksMutex_));
        std::string key = "BINANCE:" + symbol.toString(); // Match your key format
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
            core::Symbol tradingPair = core::Symbol::fromString(json["tradingPair"]);
            uint64_t sequenceNumber = json["sequenceNumber"];
            bool isSnapshot = json["isSnapshot"];
                        
            // Convert to your types
            core::ExchangeId exchangeId = core::ExchangeId::fromString(exchangeIdStr);
            core::Symbol symbol = tradingPair;
            
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
                std::string key = exchangeIdStr + ":" + tradingPair.toString(); // Match your key format
                
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
            double midPrice = orderBookReceiver_ ? orderBookReceiver_->getCurrentMidPrice(signal.symbol.toString()) : 0.0;

        std::lock_guard<std::mutex> lock(processedMutex_);
        LOG_DEBUG("Received new signal: ID={}, Symbol={}, Price={}, Direction={}, Strength={:.3f}, Confidence={:.2f}, Quality={:.2f}, GeneratedAt={}, TTL={}ms, Expiration={}",
                  signal.id,
                  signal.symbol.toString(),
                  midPrice,
                  (signal.direction == core::Direction::BUY ? "BUY" : (signal.direction == core::Direction::SELL ? "SELL" : "NEUTRAL")),
                  signal.strength,
                  signal.confidence,
                  signal.quality,
                  std::chrono::duration_cast<std::chrono::milliseconds>(signal.generationTime.time_since_epoch()).count(),
                  signal.ttl.count()/1000000,
                  std::chrono::duration_cast<std::chrono::milliseconds>(signal.expirationTime.time_since_epoch()).count());
        // //source of signal event
        // LOG_DEBUG("  Source Type: {}", static_cast<int>(signal.sourceType));
        //  enum class ImbalanceType {
        //     VOLUME_IMBALANCE,              // Significant difference between bid and ask volume
        //     PRICE_LEVEL_IMBALANCE,         // Imbalance across multiple price levels
        //     LIQUIDITY_REMOVAL,             // Sudden removal of liquidity from one side
        //     ORDER_FLOW_IMBALANCE,          // Directional bias in order submissions
        //     LARGE_ORDER_APPEARANCE,        // Large orders appearing in the book
        //     LARGE_ORDER_DISAPPEARANCE,     // Large orders being removed from the book
        //     PRICE_LEVEL_CONCENTRATION,     // Concentration of volume at specific price levels
        //     BID_ASK_SPREAD_CHANGE,         // Significant change in the bid-ask spread
        //     MARKET_DEPTH_CHANGE,           // Significant change in market depth
        //     LIQUIDITY_FLOW_IMBALANCE,      // Imbalance in liquidity flow
        //     ABNORMAL_SPREAD,               // Abnormal spread between bid and ask prices
        //     LIQUIDITY_IMBALANCE,           // General liquidity imbalance
        //     TREND_CONTINUATION,            // Continuation of a price trend
        //     TREND_REVERSAL,                // Reversal of a price trend
        //     CUSTOM                         // Custom imbalance type defined by strategy
        // };


        //filter duplicate signals
        //filter signals based on custom criteria (e.g., strength threshold)
        if (signal.strength < 0.5) return; // Weak signals are ignored
        if(signal.direction == core::Direction::NEUTRAL) return; // Ignore neutral signals
        if(signal.confidence < 0.5) return; // Ignore low confidence signals
        if(signal.quality < 0.5) return; // Ignore low quality signals

        //if target prices are too close to mid price, ignore
        double percentage_change_threshold = 0.0018; // 0.25%
        if(midPrice > 0.0) {
            bool allTargetsTooClose = true;
            for (const auto& target : signal.targets) {
                double priceDiffPercent = std::abs(target.price - midPrice) / midPrice;
                if (priceDiffPercent >= percentage_change_threshold) {
                    allTargetsTooClose = false;
                    break;
                }
            }
            if (allTargetsTooClose) return; // Ignore signals with all targets too close
        }
 
       
        for (const auto& target : signal.targets) {
            double midPrice = orderBookReceiver_ ? orderBookReceiver_->getCurrentMidPrice(signal.symbol.toString()) : 0.0;
            if (midPrice > 0.0) {
                double priceDiffPercent = std::abs(target.price - midPrice) / midPrice * 100.0;
                // LOG_INFO("    Target Price: {:.8f}, Change: {:.2f}%", target.price, priceDiffPercent);
            }
        }
        // }
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

   
    double calculatePositionSize(const TradingSignal& signal, double qualityScore) {

        double basePositionSize = 5000; // Base size in USD
        
        // Scale position size by quality (0.3x to 2.0x multiplier)
        double qualityMultiplier = 0.3 + (qualityScore * 1.7); // 0.6 quality = 1.32x, 1.0 quality = 2.0x
        
        // Additional safety for very high quality signals
        if (qualityScore > 0.9) {
            qualityMultiplier = std::min(qualityMultiplier, 1.8); // Cap at 1.8x for safety
        }
        
        double adjustedSize = basePositionSize * qualityMultiplier;
        if (adjustedSize > 5000) adjustedSize = 5000; // Max cap
        LOG_INFO("Position sizing - Base: ${}, Quality: {:.3f}, Multiplier: {:.2f}x, Final: ${}", 
                basePositionSize, qualityScore, qualityMultiplier, adjustedSize);
        
        return adjustedSize;
    }
    double calculateDecayFactor(const TradingSignal& signal, int64_t elapsedMs) {
        // Base decay rate depends on signal strength
        double baseDecayRate = 1.0 - signal.strength; // Stronger signals decay slower
        
        // Time-based decay (signals lose value over time)
        double timeDecayFactor = 1.0 - (static_cast<double>(elapsedMs) / signal.ttl.count());
        
        // Quality affects decay rate
        double qualityMultiplier = 0.5 + (signal.quality * 0.5); // 0.5 to 1.0
        
        double decayFactor = timeDecayFactor * qualityMultiplier * (1.0 - baseDecayRate);
        return std::max(0.1, std::min(1.0, decayFactor)); // Clamp between 0.1 and 1.0
    }
    void simulateSignalExecution(const TradingSignal& signal) {
        std::thread([this, signal]() {
            auto entryTime = std::chrono::system_clock::now();
            
            // Get entry price from current order book
            double entryPrice = 0.0;
            if (orderBookReceiver_) {
                entryPrice = orderBookReceiver_->getCurrentMidPrice(signal.symbol.toString());
            }
            
            if (entryPrice <= 0.0) {
                return; // Skip if no valid price
            }
            
            // Check if signal expired
            if (entryTime >= signal.expirationTime) {
                return;
            }
            
            // Calculate position size
            double positionValueUSD = calculatePositionSize(signal, signal.quality);
            double orderQuantity = positionValueUSD / entryPrice;
            
            // Calculate hold duration
            auto remainingTTL = std::chrono::duration_cast<std::chrono::milliseconds>(
                signal.expirationTime - entryTime);
            // auto holdDuration = std::min(
            //     std::chrono::duration_cast<std::chrono::milliseconds>(signal.ttl), 
            //     remainingTTL);
            auto holdDuration = std::min(
                std::chrono::duration_cast<std::chrono::milliseconds>(signal.ttl),std::chrono::milliseconds(15000));
            //min = 1s
            if(holdDuration < 1s) holdDuration = 1s;
                LOG_INFO("TRADE EXECUTED: {} | {} | {} | Entry: {:.8f} | Duration: {}ms",
                    signal.id, signal.symbol.toString(),
                    signal.direction == Direction::BUY ? "BUY" : "SELL", 
                    entryPrice, holdDuration.count());
            
            // Hold position
            std::this_thread::sleep_for(holdDuration);
            
            // Get exit price
            auto exitTime = std::chrono::system_clock::now();
            double exitPrice = 0.0;
            if (orderBookReceiver_) {
                exitPrice = orderBookReceiver_->getCurrentMidPrice(signal.symbol.toString());
            }
            
            if (exitPrice <= 0.0) {
                exitPrice = entryPrice; // Fallback to entry price
            }
            
            // Price sanity check
            double priceChangePercent = std::abs((exitPrice / entryPrice) - 1.0);
            if (priceChangePercent > 0.20) { // 20% change seems suspicious for short hold
                exitPrice = entryPrice; // Use entry price if exit looks wrong
            }
            
            // Calculate P&L
            double entryValue = entryPrice * orderQuantity;
            double exitValue = exitPrice * orderQuantity;
            double feeRate = 0.001;
            double fees = (entryValue + exitValue) * feeRate;
            
            double grossPnl = 0.0;
            if (signal.direction == Direction::BUY) {
                grossPnl = exitValue - entryValue;
            } else {
                grossPnl = entryValue - exitValue;
            }
            
            double netPnl = grossPnl - fees;
            
            LOG_INFO("TRADE COMPLETED: {} | Entry: {:.8f} | Exit: {:.8f} | P&L: {:.8f} | {}",
                    signal.id, entryPrice, exitPrice, netPnl, 
                    netPnl > 0 ? "WIN" : "LOSS");
            
            // Record result
            if (strategyEngine_) {
                SignalExecutionResult result;
                result.signal_id = signal.id;
                result.status = SignalExecutionStatus::COMPLETED;
                result.startTime = entryTime;
                result.completionTime = exitTime;
                result.orderQuantity = orderQuantity;
                result.orderPrice = entryPrice;
                result.pnl = netPnl;
                result.signalStrength = signal.strength;
                
                strategyEngine_->recordSignalResult(signal.id, result);
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

        for (int cycle = 0; cycle < 60000; ++cycle) { // Run for 1hr cycles max
            std::this_thread::sleep_for(1ms);
            // Report every 10 
            if (cycle % 10 == 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - startTime).count();

                size_t currentMessages = orderBookReceiver.getMessagesReceived();
                size_t newMessages = currentMessages - lastMessageCount;
                lastMessageCount = currentMessages;

                auto performance = strategyEngine.getPerformance();

            }
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
        // percentage profit per trade
                // Replace lines 500-502 with this corrected calculation:
        double profitPerTradePercent = finalPerformance.signalsExecuted > 0 ?
            (finalPerformance.totalPnl / (finalPerformance.signalsExecuted * 5000)) * 100.0 : 0.0;

        LOG_INFO("  Profit per trade: {:.3f}%", profitPerTradePercent);
        
        // Also add success rate for better analysis:
        double finalSuccessRate = finalPerformance.signalsExecuted > 0 ?
            (double)finalPerformance.successfulSignals / finalPerformance.signalsExecuted * 100.0 : 0.0;
        LOG_INFO("  Success rate: {:.2f}%", finalSuccessRate);
        eventBus.stop();
        LOG_INFO("=== Strategy Engine with Real Data Completed ===");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Exception: {}", e.what());
        return -1;
    }
    
    return 0;
}