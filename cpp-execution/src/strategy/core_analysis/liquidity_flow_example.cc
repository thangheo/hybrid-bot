/**
 * @file strategy_integration_example.cpp
 * @brief Example of integrating LiquidityFlowMonitor with other strategy components
 * 
 * This example demonstrates how to initialize and connect the LiquidityFlowMonitor
 * with other components of the Order Imbalance Strategy Engine.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #include "strategy/core_analysis/liquidity_flow_monitor.hpp"
 #include "strategy/pattern/imbalance_detection_engine.hpp"
 #include "strategy/signal/imbalance_signal_generator.hpp"
 #include "strategy/engine/strategy_engine.hpp"
 #include "data_ingestion/orderbook/order_book.hpp"
 #include "data_ingestion/websocket/client_interface.hpp"
 
 #include <iostream>
 #include <memory>
 #include <thread>
 #include <atomic>
 
 using namespace trading::strategy;
 using namespace trading::data_ingestion;
 
 // Simple callback to log liquidity events
 void logLiquidityEvent(const LiquidityEvent& event) {
     std::cout << "Liquidity Event Detected: " 
               << "Type: ";
     
     switch (event.type) {
         case LiquidityEvent::Type::LARGE_ORDER_ADDED:
             std::cout << "LARGE_ORDER_ADDED"; break;
         case LiquidityEvent::Type::LARGE_ORDER_REMOVED:
             std::cout << "LARGE_ORDER_REMOVED"; break;
         case LiquidityEvent::Type::RAPID_LIQUIDITY_ADDITION:
             std::cout << "RAPID_LIQUIDITY_ADDITION"; break;
         case LiquidityEvent::Type::RAPID_LIQUIDITY_REMOVAL:
             std::cout << "RAPID_LIQUIDITY_REMOVAL"; break;
         case LiquidityEvent::Type::SPREAD_COLLAPSE:
             std::cout << "SPREAD_COLLAPSE"; break;
         case LiquidityEvent::Type::SPREAD_EXPANSION:
             std::cout << "SPREAD_EXPANSION"; break;
         case LiquidityEvent::Type::LIQUIDITY_IMBALANCE:
             std::cout << "LIQUIDITY_IMBALANCE"; break;
         default:
             std::cout << "UNKNOWN"; break;
     }
     
     std::cout << ", Direction: " << directionToString(event.direction)
               << ", Significance: " << event.significance
               << ", Price Level: " << event.price_level
               << ", Quantity: " << event.quantity << std::endl;
 }
 
 // Simple callback to log trading signals
 void logTradingSignal(const TradingSignal& signal) {
     std::cout << "Trading Signal Generated: " 
               << "ID: " << signal.id
               << ", Direction: " << directionToString(signal.direction)
               << ", Strength: " << signal.strength
               << ", Confidence: " << signal.confidence << std::endl;
 }
 
 /**
  * Example demonstrating the integration of LiquidityFlowMonitor with 
  * ImbalanceDetectionEngine and SignalGenerator
  */
 class OrderImbalanceStrategyExample {
 public:
     OrderImbalanceStrategyExample(const core::ExchangeId& exchangeId, const std::string& tradingPair)
         : exchangeId_(exchangeId), 
           trading_pair_(tradingPair),
           running_(false) {
         
         // Load configuration parameters
         ParameterSet flow_monitor_params;
         flow_monitor_params.setParameter("large_order_threshold", 5.0);
         flow_monitor_params.setParameter("history_window_seconds", 60);
         
         // Initialize components
         liquidity_monitor_ = std::make_shared<LiquidityFlowMonitor>(
             exchangeId, tradingPair, flow_monitor_params);
         
         // Initialize other components and connect them
         // (In a real implementation, these would be properly instantiated)
         // imbalance_detector_ = std::make_shared<ImbalanceDetectionEngine>(...);
         // signal_generator_ = std::make_shared<ImbalanceSignalGenerator>(...);
         
         // Setup current and previous order books
         current_book_ = std::make_shared<OrderBook>(exchangeId, tradingPair);
         previous_book_ = std::make_shared<OrderBook>(exchangeId, tradingPair);
         
         std::cout << "Order Imbalance Strategy initialized for " 
                   << exchangeId << " " << tradingPair << std::endl;
     }
     
     // Method to simulate receiving order book updates
     void processOrderBookUpdate(const OrderBookUpdate& update) {
         // Store previous book state
         *previous_book_ = *current_book_;
         
         // Update current book
         if (update.is_snapshot) {
             current_book_->applySnapshot(
                 update.bids, update.asks, update.sequence_number, update.timestamp);
         } else {
             current_book_->applyUpdate(
                 update.bids, update.asks, update.sequence_number, update.timestamp);
         }
         
         // Process through liquidity monitor
         liquidity_monitor_->processOrderBookSnapshots(*previous_book_, *current_book_);
         
         // Check for significant liquidity events
         if (liquidity_monitor_->hasSignificantEvents()) {
             auto events = liquidity_monitor_->detectLiquidityEvents();
             for (const auto& event : events.events) {
                 logLiquidityEvent(event);
                 
                 // In a real implementation, forward events to the imbalance detector
                 // imbalance_detector_->processLiquidityEvent(event);
             }
         }
         
         // In a real implementation, get signals from the signal generator
         // auto signals = signal_generator_->generateSignals();
         // for (const auto& signal : signals) {
         //     logTradingSignal(signal);
         // }
     }
     
     // Method to start the strategy
     void start() {
         running_ = true;
         std::cout << "Strategy started" << std::endl;
         
         // In a real implementation, connect to the exchange and start receiving updates
     }
     
     // Method to stop the strategy
     void stop() {
         running_ = false;
         std::cout << "Strategy stopped" << std::endl;
     }
     
 private:
     std::string exchangeId_;
     std::string trading_pair_;
     std::atomic<bool> running_;
     
     std::shared_ptr<LiquidityFlowMonitor> liquidity_monitor_;
     // std::shared_ptr<ImbalanceDetectionEngine> imbalance_detector_;
     // std::shared_ptr<ImbalanceSignalGenerator> signal_generator_;
     
     std::shared_ptr<OrderBook> current_book_;
     std::shared_ptr<OrderBook> previous_book_;
 };
 
 /**
  * Main function to demonstrate the integration
  */
 int main() {
     // Create and start the strategy
     OrderImbalanceStrategyExample strategy("binance", "BTC/USDT");
     strategy.start();
     
     // Simulate some order book updates
     // In a real implementation, these would come from the exchange
     
     // Create a balanced order book
     OrderBookUpdate update1;
     update1.exchangeId = "binance";
     update1.tradingPair = "BTC/USDT";
     update1.timestamp = std::chrono::system_clock::now();
     update1.sequence_number = 1;
     update1.is_snapshot = true;
     
     // Add some bids and asks
     update1.bids = {
         PriceLevel(50000.0, 1.0),
         PriceLevel(49900.0, 2.0),
         PriceLevel(49800.0, 3.0)
     };
     
     update1.asks = {
         PriceLevel(50100.0, 1.0),
         PriceLevel(50200.0, 2.0),
         PriceLevel(50300.0, 3.0)
     };
     
     // Process the initial update
     strategy.processOrderBookUpdate(update1);
     
     // Wait a moment
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     // Create an update with a large order imbalance
     OrderBookUpdate update2;
     update2.exchangeId = "binance";
     update2.tradingPair = "BTC/USDT";
     update2.timestamp = std::chrono::system_clock::now();
     update2.sequence_number = 2;
     update2.is_snapshot = true;
     
     // Add some bids and asks with imbalance
     update2.bids = {
         PriceLevel(50000.0, 10.0),  // Large increase
         PriceLevel(49900.0, 2.0),
         PriceLevel(49800.0, 3.0)
     };
     
     update2.asks = {
         PriceLevel(50100.0, 0.5),   // Decrease
         PriceLevel(50200.0, 2.0),
         PriceLevel(50300.0, 3.0)
     };
     
     // Process the imbalanced update
     strategy.processOrderBookUpdate(update2);
     
     // Wait a moment
     std::this_thread::sleep_for(std::chrono::milliseconds(100));
     
     // Create an update with a spread collapse
     OrderBookUpdate update3;
     update3.exchangeId = "binance";
     update3.tradingPair = "BTC/USDT";
     update3.timestamp = std::chrono::system_clock::now();
     update3.sequence_number = 3;
     update3.is_snapshot = true;
     
     // Add some bids and asks with tight spread
     update3.bids = {
         PriceLevel(50000.0, 10.0),
         PriceLevel(49900.0, 2.0),
         PriceLevel(49800.0, 3.0)
     };
     
     update3.asks = {
         PriceLevel(50010.0, 0.5),   // Much tighter spread
         PriceLevel(50200.0, 2.0),
         PriceLevel(50300.0, 3.0)
     };
     
     // Process the spread collapse update
     strategy.processOrderBookUpdate(update3);
     
     // Stop the strategy
     strategy.stop();
     
     return 0;
 }