/**
 * @file trading_signal.hpp
 * @brief Strategy trading signal type
 * 
 * This file defines the TradingSignal structure used in the strategy
 * component of the trading system.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include "core/time.hpp"
 #include "core/direction.hpp"
 #include "core/market_types.hpp"
 #include "core/confidence.hpp"
 #include "strategy/types/imbalance_type.hpp"
 #include <execution/trading_signal.hpp>
 #include <string>
 #include <vector>
 #include <map>
 #include <cmath>
 
 namespace trading {
 namespace strategy {
 
 /**
  * Represents a price target for a trading signal
  */
 struct PriceTarget {
     double price;                  // Target price level
     double probability;            // Probability of reaching this target
     core::Duration expected_time;  // Expected time to reach target
     
     PriceTarget() : price(0.0), probability(0.0), expected_time(core::Duration::zero()) {}
     
     PriceTarget(double p, double prob, const core::Duration& time) 
         : price(p), probability(prob), expected_time(time) {}
 };
 
 /**
  * Represents a trading signal generated from imbalance detection
  */
 struct TradingSignal {
     core::SignalId id;                                // Unique identifier
     uint64_t sourceEventId;                            // Source event ID
     uint64_t correlationId;                            // Correlation ID
     core::Direction direction;                        // Buy or sell
     //signal quality
     double quality;                                   // Signal quality (0.0-1.0)
     double strength;                                  // Signal strength (0.0-1.0)
     double confidence;                                // Confidence level (0.0-1.0)
     core::Timestamp generation_time;                  // When the signal was generated
        core::Duration ttl;                               // Time to live (TTL) for the signal
     std::vector<PriceTarget> targets;                 // Price targets
     ImbalanceType source_type;                        // Type of imbalance that generated this signal
     std::map<std::string, double> signal_metrics;     // Metrics associated with this signal
     core::Symbol symbol;                              // Trading pair symbol
     core::ExchangeId exchangeId;                        // Exchange identifier (if needed)
    double decayRate;                            // Decay rate for the signal (if applicable)
    double expectedPriceImpact;                     // Expected price impact of the signal (if applicable)     
    // expirationTime
     core::Timestamp expirationTime;                // Expiration time for the signal in milliseconds
     /**
      * Default constructor
      */
     TradingSignal()
         : id(""), 
           direction(core::Direction::NEUTRAL), 
           strength(0.0), 
           confidence(0.0),
           generation_time(core::Timestamp()), 
           ttl(0), 
           source_type(ImbalanceType::VOLUME_IMBALANCE),
           exchangeId(core::ExchangeId::unknown()),
           expirationTime(core::Timestamp()) {}
     
     /**
      * Constructor with required fields
      */
     TradingSignal(
         const core::SignalId& sid, 
         core::Direction d, 
         double s, 
         const core::Timestamp& gt
     )
         : id(sid), 
           direction(d), 
           strength(s), 
           confidence(0.5),
           generation_time(gt), 
           ttl(0), 
           source_type(ImbalanceType::VOLUME_IMBALANCE),
           exchangeId(core::ExchangeId::unknown()),
           expirationTime(core::Timestamp()) {}
     
     /**
      * Check if signal is still valid based on TTL
      */
     bool isValid(const core::Timestamp& current_time) const {
         auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
             current_time - generation_time).count();
         return elapsed < ttl.count();
     }
 
     /**
      * Calculate remaining strength based on signal decay
      */
     double getRemainingStrength(const core::Timestamp& current_time, double decay_rate = 0.0) const {
         if (decay_rate <= 0.0) {
             return isValid(current_time) ? strength : 0.0;
         }
         
         auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
             current_time - generation_time).count();
         
         // Exponential decay formula: strength * e^(-decay_rate * elapsed_time)
         double decay_factor = std::exp(-decay_rate * elapsed_ms / 1000.0);
         return strength * decay_factor;
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
         symbol = core::Symbol::fromString(tradingPair);
     }
     
     /**
      * Get a metric value with a default if not found
      */
     double getMetric(const std::string& name, double default_value = 0.0) const {
         auto it = signal_metrics.find(name);
         return (it != signal_metrics.end()) ? it->second : default_value;
     }
     
     /**
      * Add or update a metric
      */
     void setMetric(const std::string& name, double value) {
         signal_metrics[name] = value;
     }
 };
 
 } // namespace strategy
 
 namespace execution {
     // Forward declaration for conversion function
     struct TradingSignal;
 }
 
 namespace strategy {
     /**
      * Convert strategy TradingSignal to execution TradingSignal
      */
     execution::TradingSignal toExecutionSignal(const strategy::TradingSignal& signal);
 }
 
 } // namespace trading