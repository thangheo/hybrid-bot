#pragma once

#include "data_ingestion/orderbook/order_book_types.hpp"
#include "core/market_types.hpp"
// #include "core/market_types.hpp"
using namespace trading::core;
namespace trading {
namespace data_ingestion {

/**
 * @brief Base class for exchange-specific data adapters
 * Used by OrderBookNormalizer to convert exchange data to normalized format
 */
class ExchangeAdapter {
public:
    virtual ~ExchangeAdapter() = default;
    
    /**
     * @brief Check if adapter can handle this market data update
     */
    virtual bool canHandle(const MarketDataUpdate& update) = 0;
    
    /**
     * @brief Normalize market data update to standard format
     */
    virtual OrderBookUpdate normalize(const MarketDataUpdate& update) = 0;
    
    /**
     * @brief Standardize trading pair format (e.g., "BTCUSDT" -> "BTC/USDT")
     */
    virtual std::string standardizeTradingPair(const core::Symbol& tradingPair) = 0;
    
    /**
     * @brief Get exchange identifier
     */
    virtual ExchangeId getExchangeId() const = 0;
};

} // namespace data_ingestion
} // namespace trading