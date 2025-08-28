/**
 * @file trading_signal.hpp
 * @brief Execution trading signal type
 * 
 * This file defines the TradingSignal structure used in the execution
 * component of the trading system.
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
 #include <chrono>
 
 namespace trading {
 namespace execution {
//  struct TradingSignal;

 /**
  * Represents a trading signal for the execution engine
  */
 struct TradingSignal {
     std::string signal_id;                        // Unique signal identifier
     std::string tradingPair;                     // Trading pair (e.g., "BTC/USDT")
     core::Direction direction;                    // Buy or sell direction
     double strength;                              // Signal strength (0.0-1.0)
     double target_size;                           // Suggested position size
     std::chrono::milliseconds signal_ttl;         // Time-to-live for the signal
     std::string strategy_id;                      // ID of the strategy that generated the signal
     std::string strategy_version;                 // Version of the strategy
     core::ExchangeId exchange;                      // Exchange to execute on
     std::map<std::string, double> signal_metrics; // Additional metrics for this signal
     
     /**
      * Default constructor
      */
     TradingSignal()
         : signal_id(""),
           tradingPair(""),
           direction(core::Direction::NEUTRAL),
           strength(0.0),
           target_size(0.0),
           signal_ttl(std::chrono::milliseconds(0)),
           strategy_id(""),
           strategy_version(""),
           exchange(core::ExchangeId()) {}
     
     /**
      * Constructor with required fields
      */
     TradingSignal(
         const std::string& id,
         const std::string& pair,
         core::Direction dir,
         double str
     )
         : signal_id(id),
           tradingPair(pair),
           direction(dir),
           strength(str),
           target_size(0.0),
           signal_ttl(std::chrono::milliseconds(5000)), // Default 5 seconds
           strategy_id(""),
           strategy_version(""),
           exchange(core::ExchangeId()) {}
     
     /**
      * Get the signal ID
      */
     const std::string& getSignalId() const { return signal_id; }
     
     /**
      * Get the trading pair
      */
     const std::string& getTradingPair() const { return tradingPair; }
     
     /**
      * Get the direction
      */
     core::Direction getDirection() const { return direction; }
     
     /**
      * Get the strength
      */
     double getStrength() const { return strength; }
     
     /**
      * Get the target size
      */
     double getTargetSize() const { return target_size; }
     
     /**
      * Get the signal TTL
      */
     const std::chrono::milliseconds& getSignalTtl() const { return signal_ttl; }
     
     /**
      * Get the strategy ID
      */
     const std::string& getStrategyId() const { return strategy_id; }
     
     /**
      * Get the strategy version
      */
     const std::string& getStrategyVersion() const { return strategy_version; }
     
     /**
      * Get the signal metrics
      */
     const std::map<std::string, double>& getSignalMetrics() const { return signal_metrics; }
     
     /**
      * Check if this is a buy signal
      */
     bool isBuySignal() const { return direction == core::Direction::BUY; }
     
     /**
      * Check if this is a sell signal
      */
     bool isSellSignal() const { return direction == core::Direction::SELL; }
     
     /**
      * Check if the signal is still valid based on its TTL and a reference time
      */
    //  bool isValid(const core::Timestamp& current_time) const {
    //      // Implementation would need the signal's generation time, which
    //      // is not currently included in this structure. Consider adding it.
    //      return true; // Placeholder
    //  }

    /**
     * Check if the signal is still valid based on its TTL and a reference time
     */
    bool isValid(const core::Timestamp& current_time) const {
        // Suppress unused parameter warning
        (void)current_time;
        
        // TODO: Implement proper TTL validation when generation time is added
        // For now, assume all signals are valid
        return true;
    }
 };
 
 } // namespace execution
 
 namespace strategy {
     // Forward declaration for conversion function
     struct TradingSignal;
 }
 
 namespace execution {
     /**
      * Convert execution TradingSignal to strategy TradingSignal
      */
     strategy::TradingSignal toStrategySignal(const execution::TradingSignal& signal);
 }
 
 } // namespace trading