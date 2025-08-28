/**
 * @file trading_signal_base.hpp
 * @brief Base trading signal structure
 * 
 * This file defines the common base structure for trading signals
 * used across all system components.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include "core/time.hpp"
 #include "core/direction.hpp"
 #include "core/market_types.hpp"
 #include <string>
 #include <map>
 
 namespace trading {
 namespace core {
 
 /**
  * Base trading signal structure with common properties
  * used across all system components
  */
 struct TradingSignalBase {
     SignalId id;                           // Unique identifier
     Direction direction;                   // Buy or sell direction
     double strength;                       // Signal strength (0.0-1.0)
     Timestamp generation_time;             // When the signal was generated
     uint64_t ttl_ms;                       // Time-to-live in milliseconds
     Symbol symbol;                         // Trading pair symbol
     ExchangeId exchange;                     // Exchange to execute on
     std::map<std::string, double> metrics; // Common metrics
     
     /**
      * Default constructor
      */
     TradingSignalBase()
         : id(""),
           direction(Direction::NEUTRAL),
           strength(0.0),
           generation_time(now()),
           ttl_ms(5000), // Default 5 seconds
           exchange(ExchangeId::UNKNOWN) {}
     
     /**
      * Constructor with required fields
      */
     TradingSignalBase(
         const SignalId& sid,
         Direction dir,
         double str,
         const Timestamp& gt = now()
     )
         : id(sid),
           direction(dir),
           strength(str),
           generation_time(gt),
           ttl_ms(5000), // Default 5 seconds
           exchange(ExchangeId::UNKNOWN) {}
     
     /**
      * Check if signal is still valid based on TTL
      */
     bool isValid(const Timestamp& current_time) const {
         auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
             current_time - generation_time).count();
         return elapsed < ttl_ms;
     }
     
     /**
      * Get the trading pair as a string
      */
     std::string getTradingPair() const {
         return symbol.toString();
     }
     
     /**
      * Set the trading pair from a string
      */
     void setTradingPair(const std::string& tradingPair) {
         symbol = Symbol::fromString(tradingPair);
     }
     
     /**
      * For convenience when checking direction
      */
     bool isBuySignal() const { 
         return direction == Direction::BUY; 
     }
     
     /**
      * For convenience when checking direction
      */
     bool isSellSignal() const { 
         return direction == Direction::SELL; 
     }
     
     /**
      * Get a metric with default if not found
      */
     double getMetric(const std::string& name, double default_value = 0.0) const {
         auto it = metrics.find(name);
         return (it != metrics.end()) ? it->second : default_value;
     }
     
     /**
      * Set a metric value
      */
     void setMetric(const std::string& name, double value) {
         metrics[name] = value;
     }
 };
 
 } // namespace core
 } // namespace trading