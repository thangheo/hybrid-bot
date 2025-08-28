/**
 * @file direction.hpp
 * @brief Core direction-related types and utilities
 * 
 * This file defines common direction/side enums used across all components
 * of the trading system.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include <string>
 #include <algorithm>
 
 namespace trading {
 namespace core {
 
 /**
  * Represents the direction of a trade, order, signal, or imbalance
  */
 enum class Direction {
     BUY,      // Buy/long/bid direction
     SELL,     // Sell/short/ask direction
     NEUTRAL   // No clear direction
 };
 
 /**
  * Converts a Direction enum to string
  * 
  * @param direction The direction to convert
  * @return String representation ("buy", "sell", or "neutral")
  */
 inline std::string directionToString(Direction direction) {
     switch (direction) {
         case Direction::BUY:     return "buy";
         case Direction::SELL:    return "sell";
         case Direction::NEUTRAL: return "neutral";
         default:                 return "unknown";
     }
 }
 
 /**
  * Converts a string to Direction enum
  * 
  * @param str String representation ("buy", "sell", or "neutral", case-insensitive)
  * @return Corresponding Direction enum
  */
 inline Direction directionFromString(const std::string& str) {
     // Convert to lowercase for case-insensitive comparison
     auto lower = str;
     std::transform(lower.begin(), lower.end(), lower.begin(), 
         [](unsigned char c) { return std::tolower(c); });
     
     if (lower == "buy" || lower == "bid" || lower == "long") return Direction::BUY;
     if (lower == "sell" || lower == "ask" || lower == "short") return Direction::SELL;
     return Direction::NEUTRAL;
 }
 
 } // namespace core
 } // namespace trading