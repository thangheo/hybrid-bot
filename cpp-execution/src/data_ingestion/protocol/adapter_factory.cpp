/**
 * @file adapter_factory.cpp
 * @brief Simple adapter factory implementation - Fixed version
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#include "protocol/adapter_factory.hpp"
#include <stdexcept>
#include <iostream>

namespace trading {
namespace data_ingestion {

AdapterFactory::AdapterFactory() {
    initializeBinanceAdapter();
}

void AdapterFactory::initializeBinanceAdapter() {
    std::lock_guard<std::mutex> lock(mutex_);
    binanceAdapter_ = std::make_shared<BinanceAdapter>();
}

std::shared_ptr<IProtocolAdapter> AdapterFactory::getAdapter(const common::ExchangeId& exchangeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Only support Binance for now
    if (exchangeId == common::ExchangeId::binance()) {
        return binanceAdapter_;
    }
    
    return nullptr;
}

std::shared_ptr<IProtocolAdapter> AdapterFactory::getAdapter(const core::ExchangeData& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Try Binance adapter first (and only option currently)
    if (binanceAdapter_ && binanceAdapter_->canHandle(data)) {
        return binanceAdapter_;
    }
    
    return nullptr;
}

bool AdapterFactory::supportsExchange(const common::ExchangeId& exchangeId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Only support Binance
    return exchangeId == common::ExchangeId::binance();
}

std::vector<common::ExchangeId> AdapterFactory::getSupportedExchanges() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Return only Binance
    return {common::ExchangeId::binance()};
}

bool AdapterFactory::registerAdapter(const common::ExchangeId& exchangeId, 
                                    std::unique_ptr<IProtocolAdapter> adapter) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Only allow Binance registration for now
    if (exchangeId != common::ExchangeId::binance()) {
        std::cerr << "AdapterFactory: Only Binance exchange is currently supported" << std::endl;
        return false;
    }
    
    if (!adapter) {
        std::cerr << "AdapterFactory: Cannot register null adapter" << std::endl;
        return false;
    }
    
    // Replace existing Binance adapter
    binanceAdapter_ = std::shared_ptr<IProtocolAdapter>(adapter.release());
    return true;
}

} // namespace data_ingestion
} // namespace trading