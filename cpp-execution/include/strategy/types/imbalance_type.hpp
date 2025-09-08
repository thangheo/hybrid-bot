/**
 * @file imbalance_type.hpp
 * @brief Imbalance type enumerations
 * 
 * This file defines imbalance types used by the strategy component
 * for detecting order book imbalances.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

 #pragma once

 #include <string>
 
 namespace trading {
 namespace strategy {
 
 /**
  * Enum for different types of order book imbalances
  */
 enum class ImbalanceType {
     VOLUME_IMBALANCE,              // Significant difference between bid and ask volume
     PRICE_LEVEL_IMBALANCE,         // Imbalance across multiple price levels
     LIQUIDITY_REMOVAL,             // Sudden removal of liquidity from one side
     ORDER_FLOW_IMBALANCE,          // Directional bias in order submissions
     LARGE_ORDER_APPEARANCE,        // Large orders appearing in the book
     LARGE_ORDER_DISAPPEARANCE,     // Large orders being removed from the book
     PRICE_LEVEL_CONCENTRATION,     // Concentration of volume at specific price levels
     BID_ASK_SPREAD_CHANGE,         // Significant change in the bid-ask spread
     MARKET_DEPTH_CHANGE,           // Significant change in market depth
     LIQUIDITY_FLOW_IMBALANCE,      // Imbalance in liquidity flow
     ABNORMAL_SPREAD,               // Abnormal spread between bid and ask prices
     LIQUIDITY_IMBALANCE,           // General liquidity imbalance
     TREND_CONTINUATION,            // Continuation of a price trend
     TREND_REVERSAL,                // Reversal of a price trend
     CUSTOM                         // Custom imbalance type defined by strategy
 };
 
 /**
  * String conversion for ImbalanceType enum
  */
 inline std::string imbalanceTypeToString(ImbalanceType type) {
     switch (type) {
         case ImbalanceType::VOLUME_IMBALANCE: return "volumeImbalance ";
         case ImbalanceType::PRICE_LEVEL_IMBALANCE: return "priceLevelImbalance";
         case ImbalanceType::LIQUIDITY_REMOVAL: return "liquidity_removal";
         case ImbalanceType::ORDER_FLOW_IMBALANCE: return "order_flow_imbalance";
         case ImbalanceType::LARGE_ORDER_APPEARANCE: return "large_order_appearance";
         case ImbalanceType::LARGE_ORDER_DISAPPEARANCE: return "large_order_disappearance";
         case ImbalanceType::PRICE_LEVEL_CONCENTRATION: return "price_level_concentration";
         case ImbalanceType::BID_ASK_SPREAD_CHANGE: return "bid_ask_spread_change";
         case ImbalanceType::MARKET_DEPTH_CHANGE: return "market_depth_change";
         case ImbalanceType::LIQUIDITY_FLOW_IMBALANCE: return "liquidity_flow_imbalance";
         case ImbalanceType::ABNORMAL_SPREAD: return "abnormal_spread";
         case ImbalanceType::LIQUIDITY_IMBALANCE: return "liquidity_imbalance";
         case ImbalanceType::TREND_CONTINUATION: return "trend_continuation";
         case ImbalanceType::TREND_REVERSAL: return "trend_reversal";
         case ImbalanceType::CUSTOM: return "custom";
         default: return "unknown";
     }
 }
 
 /**
  * Convert string to ImbalanceType enum
  */
 inline ImbalanceType imbalanceTypeFromString(const std::string& str) {
     if (str == "volumeImbalance ") return ImbalanceType::VOLUME_IMBALANCE;
     if (str == "priceLevelImbalance") return ImbalanceType::PRICE_LEVEL_IMBALANCE;
     if (str == "liquidity_removal") return ImbalanceType::LIQUIDITY_REMOVAL;
     if (str == "order_flow_imbalance") return ImbalanceType::ORDER_FLOW_IMBALANCE;
     if (str == "large_order_appearance") return ImbalanceType::LARGE_ORDER_APPEARANCE;
     if (str == "large_order_disappearance") return ImbalanceType::LARGE_ORDER_DISAPPEARANCE;
     if (str == "price_level_concentration") return ImbalanceType::PRICE_LEVEL_CONCENTRATION;
     if (str == "bid_ask_spread_change") return ImbalanceType::BID_ASK_SPREAD_CHANGE;
     if (str == "market_depth_change") return ImbalanceType::MARKET_DEPTH_CHANGE;
     if (str == "liquidity_flow_imbalance") return ImbalanceType::LIQUIDITY_FLOW_IMBALANCE;
     if (str == "abnormal_spread") return ImbalanceType::ABNORMAL_SPREAD;
     if (str == "liquidity_imbalance") return ImbalanceType::LIQUIDITY_IMBALANCE;
     if (str == "trend_continuation") return ImbalanceType::TREND_CONTINUATION;
     if (str == "trend_reversal") return ImbalanceType::TREND_REVERSAL;
     if (str == "custom") return ImbalanceType::CUSTOM;
     return ImbalanceType::CUSTOM; // Default to CUSTOM for unknown types
 }
 
 } // namespace strategy
 } // namespace trading