/**
 * @file strategy/types/imbalance_event.hpp
 * @brief Imbalance event structure
 * 
 * This file defines the ImbalanceEvent structure used for representing
 * detected order book imbalances.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include <string>
 #include <map>
 #include "core/types.hpp"
 #include "core/direction.hpp"
 #include "core/confidence.hpp"
 #include "core/market_types.hpp"
 #include "strategy/types/imbalance_type.hpp"
 
 namespace trading {
 namespace strategy {
 
 /**
  * Represents an imbalance event detected in the order book
  */
 struct ImbalanceEvent {
     ImbalanceType type;                                // Type of imbalance detected
     core::Direction direction;                         // Direction of imbalance (buy/sell)
     double strength;                                   // Normalized strength (0.0-1.0)
     core::Timestamp detectionTime;                    // When the imbalance was detected
     double primary_metric;                             // Primary metric value for this imbalance
     std::map<std::string, double> supportingMetrics;  // Additional metrics supporting the imbalance
     uint64_t eventId;                                 // Unique identifier for this event
     core::ConfidenceLevel confidence;                  // Confidence level in this imbalance
     core::ExchangeId exchangeId;                      // Exchange where imbalance was detected
     core::Symbol symbol;                               // Trading pair
 
     // Constructor with required fields
     ImbalanceEvent(ImbalanceType t, core::Direction d, double s, core::Timestamp dt)
         : type(t), direction(d), strength(s), detectionTime(dt),
           primary_metric(0.0), eventId(0), confidence(core::ConfidenceLevel::MEDIUM),
           exchangeId(core::ExchangeId()), symbol(core::Symbol()) {}
     
     // Default constructor
     ImbalanceEvent() 
         : type(ImbalanceType::VOLUME_IMBALANCE), direction(core::Direction::NEUTRAL),
           strength(0.0), detectionTime(core::Timestamp()), primary_metric(0.0),
           eventId(0), confidence(core::ConfidenceLevel::MEDIUM),
           exchangeId(core::ExchangeId()), symbol(core::Symbol()) {}
           
     // Add a supporting metric
     void addSupportingMetric(const std::string& name, double value) {
         supportingMetrics[name] = value;
     }
     
     // Get a supporting metric with default value if not found
     double getSupportingMetric(const std::string& name, double default_value = 0.0) const {
         auto it = supportingMetrics.find(name);
         return (it != supportingMetrics.end()) ? it->second : default_value;
     }
     
     // Get string representation of trading pair
     std::string getTradingPair() const {
         return symbol.toString();  // Assuming Symbol has a toString() method
         // Alternative: return std::string(symbol); // If Symbol has string conversion operator
     }
     
     // Set trading pair
     void setTradingPair(const std::string& tradingPair) {
         symbol = core::Symbol::fromString(tradingPair);
     }
     core::ExchangeId getExchangeFromExchangeId(const std::string& exchange_id_str) const {
            // Assuming exchange_id_str is a string representation of the exchange ID
            // Convert string to lowercase for comparison
            std::string lower = exchange_id_str;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                [](unsigned char c) { return std::tolower(c); });
            if (lower == "binance") return core::ExchangeId::binance();
            if (lower == "coinbase") return core::ExchangeId::coinbase();
            if (lower == "ftx") return core::ExchangeId::ftx();
            // if (lower == "kraken") return core::Exchange::KRAKEN
            // if (lower == "huobi") return core::Exchange::HUOBI;
            return core::ExchangeId::unknown(); // Return unknown if not found
     }
 };
 
 } // namespace strategy
 } // namespace trading