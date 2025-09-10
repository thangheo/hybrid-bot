/**
 * @file strategy/types/trade.hpp
 * @brief Trade structure definition for strategy component
 * 
 * This file defines the Trade structure used throughout 
 * the strategy component for representing executed trades.
 * 
 * @author Trading System Team
 * @date May 13, 2025
 */

 #pragma once

 #include "core/types.hpp"
 #include "core/market_types.hpp"
 #include <string>
 
 namespace trading {
 namespace strategy {
 
 /**
  * Represents a trade execution
  */
 struct Trade {
     core::ExchangeId exchangeId;     // Exchange identifier
     core::Symbol symbol;              // Trading pair
     double price;                     // Execution price
     double quantity;                  // Execution quantity
     bool is_buy;                      // True if taker bought, false if taker sold
     core::Timestamp timestamp;        // Timestamp of the trade
     std::string trade_id;             // Unique trade identifier
     double fee;                       // Fee amount
     std::string fee_currency;         // Fee currency
     
     // Default constructor
     Trade() 
         : exchangeId(), symbol(), price(0.0), quantity(0.0), is_buy(false),
           timestamp(core::Timestamp()), trade_id(), fee(0.0), fee_currency() {}
     
     // Constructor with essential fields
     Trade(double p, double q, bool buy, const core::Timestamp& ts)
         : exchangeId(), symbol(), price(p), quantity(q), is_buy(buy),
           timestamp(ts), trade_id(), fee(0.0), fee_currency() {}
     
     // Constructor with all fields
     Trade(const core::ExchangeId& exchange, const core::Symbol& sym, double p, double q, 
           bool buy, const core::Timestamp& ts, const std::string& id = "",
           double f = 0.0, const std::string& fc = "")
         : exchangeId(exchange), symbol(sym), price(p), quantity(q), is_buy(buy),
           timestamp(ts), trade_id(id), fee(f), fee_currency(fc) {}
           
     // Get the trading pair as a string
     std::string getTradingPair() const {
         return symbol.toString();
     }
     
     // Set the trading pair from a string
     void setTradingPair(const core::Symbol& tradingPair) {
         symbol = tradingPair;
     }
 };
 
 } // namespace strategy
 } // namespace trading