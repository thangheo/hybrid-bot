/**
 * @file strategy/constants.hpp
 * @brief Constants used in the strategy component
 * 
 * This file defines constants used throughout the strategy component.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include <string>
 #include <cstdint>
 
 namespace trading {
 namespace strategy {
 namespace constants {
 
 // Default thresholds
 constexpr double DEFAULT_VOLUME_IMBALANCE_THRESHOLD = 0.7;
 constexpr double DEFAULT_PRICE_LEVEL_IMBALANCE_THRESHOLD = 0.6;
 constexpr double DEFAULT_LIQUIDITY_REMOVAL_THRESHOLD = 0.5;
 
 // Default signal parameters
 constexpr uint64_t DEFAULT_SIGNAL_TTL_MS = 5000;  // 5 seconds
 constexpr double DEFAULT_SIGNAL_DECAY_RATE = 0.1;
 
 // Default depth values
 constexpr int DEFAULT_ORDER_BOOK_DEPTH = 10;
 constexpr int MAX_ORDER_BOOK_DEPTH = 50;
 
 // Signal strength thresholds
 constexpr double MIN_SIGNAL_STRENGTH = 0.1;
 constexpr double STRONG_SIGNAL_THRESHOLD = 0.7;
 
 // Strategy version
 const std::string STRATEGY_VERSION = "1.0.0";
 
 // Default detection window
 constexpr int DEFAULT_DETECTION_WINDOW_MS = 1000;
 
 // Default history size
 constexpr int DEFAULT_HISTORY_SIZE = 1000;
 
 // Minimum confidence for signal generation
 constexpr double MIN_CONFIDENCE_THRESHOLD = 0.3;
 
 } // namespace constants
 } // namespace strategy
 } // namespace trading