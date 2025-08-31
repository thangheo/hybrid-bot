/**
 * @file adapter_interface.hpp
 * @brief Base interface for protocol adapters
 * 
 * Defines the common interface that all exchange-specific
 * protocol adapters must implement.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#pragma once

#include "common/exchange_types.hpp"
#include "core/market_types.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"

namespace trading {
namespace data_ingestion {

/**
 * Base interface for all protocol adapters
 * 
 * Each exchange-specific adapter must implement this interface
 * to provide standardized data conversion capabilities.
 */
class IProtocolAdapter {
public:
    virtual ~IProtocolAdapter() = default;
    
    /**
     * Check if this adapter can handle the given exchange data
     * 
     * @param data Raw exchange data to check
     * @return true if this adapter can process the data, false otherwise
     */
    virtual bool canHandle(const core::ExchangeData& data) = 0;
    
    /**
     * Convert exchange-specific data to standardized OrderBookUpdate format
     * 
     * @param data Raw exchange data to convert
     * @return Standardized OrderBookUpdate structure
     * @throws std::runtime_error if conversion fails
     */
    virtual OrderBookUpdate convertToStandardFormat(const core::ExchangeData& data) = 0;
    
    /**
     * Get the exchange ID that this adapter supports
     * 
     * @return ExchangeId that this adapter is designed for
     */
    virtual common::ExchangeId getSupportedExchange() const = 0;
    
    /**
     * Get adapter name for logging and debugging
     * 
     * @return Human-readable adapter name
     */
    virtual std::string getAdapterName() const {
        return getSupportedExchange().toString() + "Adapter";
    }
    
    /**
     * Validate that the adapter is properly configured
     * 
     * @return true if adapter is ready to process data
     */
    virtual bool isConfigured() const {
        return true; // Default implementation
    }
};

} // namespace data_ingestion
} // namespace trading