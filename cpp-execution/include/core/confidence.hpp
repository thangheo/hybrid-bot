/**
 * @file confidence.hpp
 * @brief Core confidence-related types
 * 
 * This file defines confidence level enumerations used across 
 * all components of the trading system.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include <string>
 
 namespace trading {
 namespace core {
 
 /**
  * Represents a confidence level for signals, predictions, or detections
  */
 enum class ConfidenceLevel {
     VERY_LOW,
     LOW,
     MEDIUM,
     HIGH,
     VERY_HIGH
 };
 
 /**
  * Converts a ConfidenceLevel enum to string
  */
 inline std::string confidenceLevelToString(ConfidenceLevel level) {
     switch (level) {
         case ConfidenceLevel::VERY_LOW:  return "very_low";
         case ConfidenceLevel::LOW:       return "low";
         case ConfidenceLevel::MEDIUM:    return "medium";
         case ConfidenceLevel::HIGH:      return "high";
         case ConfidenceLevel::VERY_HIGH: return "very_high";
         default:                         return "unknown";
     }
 }
 
 /**
  * Converts a ConfidenceLevel enum to a normalized value between 0 and 1
  */
 inline double confidenceLevelToValue(ConfidenceLevel level) {
     switch (level) {
         case ConfidenceLevel::VERY_LOW:  return 0.1;
         case ConfidenceLevel::LOW:       return 0.3;
         case ConfidenceLevel::MEDIUM:    return 0.5;
         case ConfidenceLevel::HIGH:      return 0.7;
         case ConfidenceLevel::VERY_HIGH: return 0.9;
         default:                         return 0.0;
     }
 }
 
 /**
  * Converts a normalized confidence value to the appropriate ConfidenceLevel
  */
 inline ConfidenceLevel valueToConfidenceLevel(double value) {
     if (value < 0.2)       return ConfidenceLevel::VERY_LOW;
     else if (value < 0.4)  return ConfidenceLevel::LOW;
     else if (value < 0.6)  return ConfidenceLevel::MEDIUM;
     else if (value < 0.8)  return ConfidenceLevel::HIGH;
     else                   return ConfidenceLevel::VERY_HIGH;
 }
 
 } // namespace core
 } // namespace trading