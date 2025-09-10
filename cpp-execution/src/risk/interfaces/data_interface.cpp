/**
 * Implementation of the interface for accessing market data
 *
 * File: data_interface.cpp
 * Created: 2025-04-29
 */

#include "risk/interfaces/data_interface.hpp"
#include "risk/interfaces/market_data_interface.hpp"
#include <common/logger.hpp>
#include <iostream>
#include <sstream>
#include <chrono>

namespace trading {
namespace risk {

// Implementation of the DataInterface that wraps MarketDataInterface
class DataInterfaceImpl : public DataInterface {
private:
    // The new implementation we're wrapping
    std::unique_ptr<MarketDataInterface> marketDataInterface_;
    
    // Track initialization state
    bool initialized_ = false;
    // mutex_ for thread safety
    mutable std::mutex mutex_;
    // For compatibility - track callbacks
    std::function<void(const MarketData&)> marketDataCallback_;
    
public:
    DataInterfaceImpl() : DataInterface() {
        try {
            // Create the new market data interface
            marketDataInterface_ = std::make_unique<MarketDataInterface>();
            LOG_INFO("DataInterfaceImpl created with MarketDataInterface wrapper");
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to create MarketDataInterface: " + std::string(e.what()));
            throw;
        }
    }
    
    ~DataInterfaceImpl() override {
        // Cleanup is automatic with unique_ptr
        LOG_INFO("DataInterfaceImpl destroyed");
    }
    
    bool initialize() override {
        if (initialized_) {
            LOG_WARN("DataInterface already initialized");
            return true;
        }
        
        try {
            // Initialize the wrapped interface
            if (!marketDataInterface_) {
                LOG_ERROR("MarketDataInterface is null");
                return false;
            }
            
            bool success = marketDataInterface_->initialize();
            
            if (success) {
                initialized_ = true;
                LOG_INFO("DataInterface initialized successfully via MarketDataInterface");
                
                // If we have a stored callback, register it now
                if (marketDataCallback_) {
                    marketDataInterface_->registerMarketDataCallback(marketDataCallback_);
                }
            } else {
                LOG_ERROR("Failed to initialize MarketDataInterface");
            }
            
            return success;
        } catch (const std::exception& e) {
            LOG_ERROR("Exception during initialization: " + std::string(e.what()));
            return false;
        }
    }
   
    std::optional<MarketData> getMarketData(const core::Symbol& tradingPair, 
                            const std::optional<std::string>& exchangeId) override {
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized");
            return std::nullopt;
        }
        
        try {
            return marketDataInterface_->getMarketData(tradingPair, exchangeId);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get market data: " + std::string(e.what()));
            return std::nullopt;
        }
    }
    // isConnected
    bool isConnected() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_ && marketDataInterface_ && marketDataInterface_->isConnected();
    }

    double getCurrentPrice(const core::Symbol& tradingPair, 
                          const std::optional<std::string>& exchangeId) override {
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized");
            return 0.0;
        }
        
        try {
            return marketDataInterface_->getCurrentPrice(tradingPair, exchangeId);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get current price: " + std::string(e.what()));
            return 0.0;
        }
    }
    
    std::vector<std::pair<Timestamp, double>> getHistoricalPrices(
        const core::Symbol& tradingPair,
        const std::string& interval,
        int count,
        const std::optional<std::string>& exchangeId) override {
        
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized");
            return {};
        }
        
        try {
            return marketDataInterface_->getHistoricalPrices(tradingPair, interval, count, exchangeId);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get historical prices: " + std::string(e.what()));
            return {};
        }
    }
    
    double getVolatility(const core::Symbol& tradingPair, 
                        const std::string& timeframe, 
                        const std::optional<std::string>& exchangeId) override {
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized");
            return 0.0;
        }
        
        try {
            return marketDataInterface_->getVolatility(tradingPair, timeframe, exchangeId);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get volatility: " + std::string(e.what()));
            return 0.0;
        }
    }
    
    std::vector<OrderRequest> getOrderHistory(const core::ExchangeId& exchangeId, 
                                            const core::Symbol& tradingPair, 
                                            int limit) override {
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized");
            return {};
        }
        
        try {
            // The new implementation might not have full order history yet
            auto history = marketDataInterface_->getOrderHistory(exchangeId, tradingPair, limit);
            
            // If no history available, log a warning
            if (history.empty()) {
                LOG_WARN("No order history available for " + exchangeId.toString() + ":" + tradingPair.toString());
            }
            
            return history;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get order history: " + std::string(e.what()));
            return {};
        }
    }
    
    std::map<std::string, double> getExchangeBalances(const core::ExchangeId& exchangeId) override {
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized");
            return {};
        }
        
        try {
            auto balances = marketDataInterface_->getExchangeBalances(exchangeId);
            
            // You can also update balances if you have them from another source
            // For example, if you receive balance updates from execution engine:
            // marketDataInterface_->updateBalance(exchangeId, "BTC", 1.5);
            
            return balances;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get exchange balances: " + std::string(e.what()));
            return {};
        }
    }
    
    void registerMarketDataCallback(const std::function<void(const MarketData&)>& callback) override {
        // Store the callback
        marketDataCallback_ = callback;
        
        // If already initialized, register with the wrapped interface
        if (initialized_ && marketDataInterface_) {
            try {
                marketDataInterface_->registerMarketDataCallback(callback);
                LOG_INFO("Market data callback registered");
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to register callback: " + std::string(e.what()));
            }
        }
    }
    
    bool subscribeMarketData(const core::Symbol& tradingPair, 
                           const std::optional<std::string>& exchangeId) override {
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized");
            return false;
        }
        
        try {
            bool success = marketDataInterface_->subscribeMarketData(tradingPair, exchangeId);
            
            if (success) {
                std::string exchange = exchangeId.value_or("default");
                LOG_INFO("Subscribed to market data for " + exchange + ":" + tradingPair.toString());
            } else {
                LOG_ERROR("Failed to subscribe to market data");
            }
            
            return success;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to subscribe to market data: " + std::string(e.what()));
            return false;
        }
    }
    
    bool unsubscribeMarketData(const core::Symbol& tradingPair, 
                              const std::optional<std::string>& exchangeId) override {
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized");
            return false;
        }
        
        try {
            bool success = marketDataInterface_->unsubscribeMarketData(tradingPair, exchangeId);
            
            if (success) {
                std::string exchange = exchangeId.value_or("default");
                LOG_INFO("Unsubscribed from market data for " + exchange + ":" + tradingPair.toString());
            }
            
            return success;
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to unsubscribe from market data: " + std::string(e.what()));
            return false;
        }
    }
    
    std::map<std::string, double> getTradingFees(const core::ExchangeId& exchangeId, 
                                                const std::optional<std::string>& tradingPair) override {
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized");
            return {};
        }
        
        try {
            return marketDataInterface_->getTradingFees(exchangeId, tradingPair);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get trading fees: " + std::string(e.what()));
            return {};
        }
    }
    
    // Additional helper methods for enhanced functionality
    void updateBalance(const core::ExchangeId& exchangeId, const std::string& asset, double balance) {
        if (initialized_ && marketDataInterface_) {
            try {
                marketDataInterface_->updateBalance(exchangeId, asset, balance);
                LOG_DEBUG("Updated balance for " + exchangeId.toString() + ":" + asset + " = " + std::to_string(balance));
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to update balance: " + std::string(e.what()));
            }
        }
    }
    
    double getBalance(const core::ExchangeId& exchangeId, const std::string& asset) const {
        if (initialized_ && marketDataInterface_) {
            try {
                return marketDataInterface_->getBalance(exchangeId, asset);
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to get balance: " + std::string(e.what()));
                return 0.0;
            }
        }
        return 0.0;
    }
};

// Constructor implementation
DataInterface::DataInterface() {
    // Base constructor
}

 
// Factory function to create DataInterface
std::unique_ptr<DataInterface> createDataInterface() {
    try {
        return std::make_unique<DataInterfaceImpl>();
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create DataInterface: " + std::string(e.what()));
        throw;
    }
}

} // namespace risk
} // namespace trading