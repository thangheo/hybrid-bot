/**
 * @file binance_adapter.hpp  
 * @brief Binance-specific protocol adapter - Fixed version
 * 
 * Implements the IProtocolAdapter interface for Binance exchange
 * data format conversion.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#pragma once

#include "adapter_interface.hpp"
#include "common/exchange_types.hpp"
#include "core/market_types.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include <nlohmann/json.hpp>
#include <mutex>
#include <unordered_map>

namespace trading {
namespace data_ingestion {

/**
 * Binance-specific protocol adapter
 * 
 * Converts Binance WebSocket and REST API data formats
 * to the standardized OrderBookUpdate format used internally.
 */
class BinanceAdapter : public IProtocolAdapter {
public:
    BinanceAdapter() = default;
    ~BinanceAdapter() override = default;
    
    // IProtocolAdapter interface implementation
    bool canHandle(const core::ExchangeData& data) override;
    OrderBookUpdate convertToStandardFormat(const core::ExchangeData& data) override;
    common::ExchangeId getSupportedExchange() const override;
    std::string getAdapterName() const override;
    bool isConfigured() const override;
    
    // Binance-specific methods
    
    /**
     * Convert Binance depth update message
     */
    OrderBookUpdate convertDepthUpdate(const std::string& jsonData);
    
    /**
     * Convert Binance depth snapshot message  
     */
    OrderBookUpdate convertDepthSnapshot(const std::string& jsonData);
    
    /**
     * Validate Binance message format
     */
    bool validateBinanceMessage(const std::string& jsonData) const;
    
private:
    /**
     * Parse price level from Binance format
     * Binance sends price levels as ["price", "quantity"] arrays
     */
    PriceLevel parsePriceLevel(const nlohmann::json& levelArray);
    
    /**
     * Extract trading pair from Binance symbol format
     * Converts from Binance format (e.g., "BTCUSDT") to internal format
     */
    std::string extractTradingPair(const std::string& binanceSymbol);
    
    /**
     * Validate sequence number for gap detection
     */
    bool validateSequenceNumber(uint64_t sequenceNumber, const std::string& symbol);
    
    // Internal state for sequence tracking (per symbol)
    mutable std::mutex sequenceTrackingMutex_;
    std::unordered_map<std::string, uint64_t> lastSequenceNumbers_;
};

} // namespace data_ingestion
} // namespace trading