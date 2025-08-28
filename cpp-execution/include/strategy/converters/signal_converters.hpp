/**
 * @file signal_converters.hpp
 * @brief Conversion functions for trading signals
 * 
 * This file defines functions to convert between different
 * trading signal representations across system components.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include "strategy/types/trading_signal.hpp"
 #include "execution/types/trading_signal.hpp"
 
 namespace trading {
 namespace strategy {
 
 /**
  * Convert strategy TradingSignal to execution TradingSignal
  * 
  * @param signal The strategy signal to convert
  * @return Equivalent execution trading signal
  */
 execution::TradingSignal toExecutionSignal(const strategy::TradingSignal& signal) {
     execution::TradingSignal exec_signal;
     
     // Transfer basic properties
     exec_signal.signal_id = signal.id;
     exec_signal.tradingPair = signal.getTradingPair();
     exec_signal.direction = signal.direction;
     exec_signal.strength = signal.strength;
     exec_signal.exchange = signal.exchange;
     
     // Set TTL
     exec_signal.signal_ttl = std::chrono::milliseconds(signal.ttl);
     
     // Set target size based on strength if available
     double size_metric = signal.getMetric("target_size", 0.0);
     exec_signal.target_size = (size_metric > 0.0) ? size_metric : signal.strength;
     
     // Set strategy information
     exec_signal.strategy_id = signal.getMetric("strategy_id", "unknown");
     exec_signal.strategy_version = signal.getMetric("strategy_version", "1.0.0");
     
     // Copy relevant metrics
     for (const auto& [key, value] : signal.signal_metrics) {
         // Only copy specific metrics to avoid cluttering
         if (key == "confidence" || key == "expected_profit" || 
             key == "maxDrawdown" || key == "win_probability") {
             exec_signal.signal_metrics[key] = value;
         }
     }
     
     // Add source type as a metric
     exec_signal.signal_metrics["source_type"] = 
         static_cast<double>(static_cast<int>(signal.source_type));
     
     return exec_signal;
 }
 
 } // namespace strategy
 
 namespace execution {
 
 /**
  * Convert execution TradingSignal to strategy TradingSignal
  * 
  * @param signal The execution signal to convert
  * @return Equivalent strategy trading signal
  */
 strategy::TradingSignal toStrategySignal(const execution::TradingSignal& signal) {
     strategy::TradingSignal strat_signal;
     
     // Transfer basic properties
     strat_signal.id = signal.signal_id;
     strat_signal.setTradingPair(signal.tradingPair);
     strat_signal.direction = signal.direction;
     strat_signal.strength = signal.strength;
     strat_signal.exchange = signal.exchange;
     
     // Set TTL
     strat_signal.ttl = signal.signal_ttl.count();
     
     // Set generation time to now since it's not available in execution signal
     strat_signal.generation_time = core::now();
     
     // Set confidence based on strength if not available
     strat_signal.confidence = signal.strength;
     
     // Determine source type if available in metrics
     auto source_type_it = signal.signal_metrics.find("source_type");
     if (source_type_it != signal.signal_metrics.end()) {
         int type_value = static_cast<int>(source_type_it->second);
         strat_signal.source_type = static_cast<strategy::ImbalanceType>(type_value);
     } else {
         strat_signal.source_type = strategy::ImbalanceType::CUSTOM;
     }
     
     // Copy all metrics
     strat_signal.signal_metrics = signal.signal_metrics;
     
     // Add strategy information as metrics
     strat_signal.setMetric("strategy_id", std::stod(signal.strategy_id));
     strat_signal.setMetric("strategy_version", std::stod(signal.strategy_version));
     strat_signal.setMetric("target_size", signal.target_size);
     
     return strat_signal;
 }
 
 } // namespace execution
 } // namespace trading