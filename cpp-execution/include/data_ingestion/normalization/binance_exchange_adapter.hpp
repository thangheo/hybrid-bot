#pragma once

#include "data_ingestion/protocol/exchange_adapter.hpp"
#include <nlohmann/json.hpp>

namespace trading {
namespace data_ingestion {

/**
 * @brief Binance-specific exchange adapter for order book normalization
 */
class BinanceExchangeAdapter : public ExchangeAdapter {
public:
    BinanceExchangeAdapter() = default;
    ~BinanceExchangeAdapter() override = default;
    
    bool canHandle(const MarketDataUpdate& update) override;
    OrderBookUpdate normalize(const MarketDataUpdate& update) override;
    std::string standardizeTradingPair(const std::string& tradingPair) override;
    ExchangeId getExchangeId() const override;

private:
    OrderBookUpdate parseDepthUpdate(const nlohmann::json& data, const MarketDataUpdate& update);
    OrderBookUpdate parseDepthSnapshot(const nlohmann::json& data, const MarketDataUpdate& update);
    std::vector<PriceLevel> parsePriceLevels(const nlohmann::json& levels);
};

} // namespace data_ingestion
} // namespace trading