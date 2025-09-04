/**
 * @file strategy/utils/type_adapters.hpp
 * @brief Type adapters for converting between different domain types
 * 
 * This file defines adapter functions for converting between different
 * domain models across the trading system.
 * 
 * @author Trading System Team
 * @date May 13, 2025
 */

 #pragma once

 #include <vector>
 #include "core/market_types.hpp"
 #include "data_ingestion/orderbook/price_level.hpp"
 #include "data_ingestion/orderbook/order_book.hpp"
 
 namespace trading {
 namespace strategy {
 namespace utils {
 
 /**
  * Convert data_ingestion::PriceLevel to core::PriceLevel
  */
 inline core::PriceLevel convertPriceLevel(const data_ingestion::PriceLevel& level) {
     core::PriceLevel result;
     result.price = level.price;
     result.quantity = level.quantity;
     // Copy other fields as needed
     return result;
 }
 
 /**
  * Convert a vector of data_ingestion::PriceLevel to a vector of core::PriceLevel
  */
 inline std::vector<core::PriceLevel> convertPriceLevels(
     const std::vector<data_ingestion::PriceLevel>& levels) {
     
     std::vector<core::PriceLevel> result;
     result.reserve(levels.size());
     
     for (const auto& level : levels) {
         result.push_back(convertPriceLevel(level));
     }
     
     return result;
 }
 
 } // namespace utils
 } // namespace strategy
 } // namespace trading