/**
 * @file adapter_factory.hpp
 * @brief Simple adapter factory supporting only Binance
 * 
 * Simplified implementation for quick integration with the pipeline.
 * Hard-coded to support only Binance exchange.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#pragma once

#include "adapter_interface.hpp"
#include "binance_adapter.hpp"
#include "common/exchange_types.hpp"
#include "core/market_types.hpp"
#include <memory>
#include <mutex>

namespace trading {
namespace data_ingestion {

/**
 * Simple adapter factory that only supports Binance
 * Hard-coded implementation for quick deployment
 */
class AdapterFactory {
public:
    AdapterFactory();
    ~AdapterFactory() = default;
    
    /**
     * Get adapter for the specified exchange
     * Returns nullptr if exchange not supported
     */
    std::shared_ptr<IProtocolAdapter> getAdapter(const common::ExchangeId& exchangeId);
    
    /**
     * Get adapter that can handle the specific data
     * Returns nullptr if no suitable adapter found
     */
    std::shared_ptr<IProtocolAdapter> getAdapter(const core::ExchangeData& data);
    
    /**
     * Check if factory supports the given exchange
     */
    bool supportsExchange(const common::ExchangeId& exchangeId) const;
    
    /**
     * Get list of supported exchanges
     */
    std::vector<common::ExchangeId> getSupportedExchanges() const;
    
    /**
     * Register an adapter (for future extensibility)
     * Currently only accepts Binance adapters
     */
    bool registerAdapter(const common::ExchangeId& exchangeId, 
                        std::unique_ptr<IProtocolAdapter> adapter);

private:
    std::shared_ptr<IProtocolAdapter> binanceAdapter_;
    mutable std::mutex mutex_;
    
    void initializeBinanceAdapter();
};

} // namespace data_ingestion
} // namespace trading