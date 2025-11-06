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
    VOLUME_IMBALANCE,              // bid vs ask volume
    PRICE_LEVEL_IMBALANCE,         // level count imbalance
    LIQUIDITY_REMOVAL,             // sudden removal
    ORDER_FLOW_IMBALANCE,          // order submissions imbalance
    LARGE_ORDER_APPEARANCE,        // big orders enter
    LARGE_ORDER_DISAPPEARANCE,     // big orders vanish
    PRICE_LEVEL_CONCENTRATION,     // volume concentrated at specific price
    BID_ASK_SPREAD_CHANGE,         // spread change
    MARKET_DEPTH_CHANGE,           // depth change
    LIQUIDITY_FLOW_IMBALANCE,      // liquidity flow imbalance
    ABNORMAL_SPREAD,               // spread abnormal
    LIQUIDITY_IMBALANCE,           // general liquidity imbalance
    TREND_CONTINUATION,            // price trend continuation
    TREND_REVERSAL,                // price trend reversal
    TRADE_IMBALANCE,               // aggressive trades imbalance
    CANCELLATION_IMBALANCE,        // one-sided cancellations
    PRICE_VOLATILITY_IMBALANCE,    // volatility surge
    HIDDEN_LIQUIDITY_IMBALANCE,    // iceberg / hidden liquidity
    REGIME_BASED_IMBALANCE,        // regime-based signals
    CUSTOM                         // strategy-defined
};

 /**
  * String conversion for ImbalanceType enum
  */
 inline std::string imbalanceTypeToString(ImbalanceType type) {
     switch (type) {
         case ImbalanceType::VOLUME_IMBALANCE: return "volumeImbalance";
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