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
    std::shared_ptr<DataInterface> marketDataInterface_owner_; // hold the shared MarketDataInterface as DataInterface
    MarketDataInterface* marketDataInterfaceRaw_ = nullptr; // non-owning raw pointer for calls
    
    // Track initialization state
    bool initialized_ = false;
    // mutex_ for thread safety
    mutable std::mutex mutex_;
    // For compatibility - track callbacks
    std::function<void(const MarketData&)> marketDataCallback_;
    
public:
    // Allow injection of an existing MarketDataInterface singleton
    DataInterfaceImpl(std::shared_ptr<DataInterface> injected = nullptr) : DataInterface() {
        try {
            if (injected) {
                marketDataInterface_owner_ = injected;
                // try to get raw pointer to MarketDataInterface if possible
                marketDataInterfaceRaw_ = dynamic_cast<MarketDataInterface*>(marketDataInterface_owner_.get());
                LOG_INFO("DataInterfaceImpl created (this={}) using injected MarketDataInterface (this={})",
                         static_cast<const void*>(this), static_cast<const void*>(marketDataInterfaceRaw_));
            } else {
                // Create the new market data interface (singleton via factory)
                marketDataInterface_owner_ = MarketDataInterface::createDataInterface(RiskConfig{});
                marketDataInterfaceRaw_ = dynamic_cast<MarketDataInterface*>(marketDataInterface_owner_.get());
                LOG_INFO("DataInterfaceImpl created (this={}) with internal MarketDataInterface (this={})",
                         static_cast<const void*>(this), static_cast<const void*>(marketDataInterfaceRaw_));
            }
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
            if (!marketDataInterface_owner_) {
                LOG_ERROR("MarketDataInterface is null");
                return false;
            }

            bool success = marketDataInterface_owner_->initialize();

            if (success) {
                initialized_ = true;
                LOG_INFO("DataInterface initialized successfully via MarketDataInterface");

                // If we have a stored callback, register it now
                if (marketDataCallback_) {
                    marketDataInterface_owner_->registerMarketDataCallback(marketDataCallback_);
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
            return marketDataInterface_owner_->getMarketData(tradingPair, exchangeId);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get market data: " + std::string(e.what()));
            return std::nullopt;
        }
    }
    // isConnected
    bool isConnected() const override {
        std::lock_guard<std::mutex> lock(mutex_);
    return initialized_ && marketDataInterface_owner_ && marketDataInterface_owner_->isConnected();
    }

    double getCurrentPrice(const core::Symbol& tradingPair, 
                          const std::optional<std::string>& exchangeId) override {
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized");
            return 0.0;
        }
        
        try {
            return marketDataInterface_owner_->getCurrentPrice(tradingPair, exchangeId);
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
            return marketDataInterface_owner_->getHistoricalPrices(tradingPair, interval, count, exchangeId);
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
            return marketDataInterface_owner_->getVolatility(tradingPair, timeframe, exchangeId);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get volatility: " + std::string(e.what()));
            return 0.0;
        }
    }

    double getPriceFromRest(const core::Symbol& tradingPair, 
                          const std::optional<std::string>& exchangeId) override {
        if (!initialized_) {
            LOG_ERROR("DataInterface not initialized (REST price lookup)");
            return 0.0;
        }
        try {
            return marketDataInterface_owner_->getPriceFromRest(tradingPair, exchangeId);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get REST price: {}", e.what());
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
            auto history = marketDataInterface_owner_->getOrderHistory(exchangeId, tradingPair, limit);
            
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
            auto balances = marketDataInterface_owner_->getExchangeBalances(exchangeId);
            
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
        if (initialized_ && marketDataInterface_owner_) {
            try {
                marketDataInterface_owner_->registerMarketDataCallback(callback);
                // LOG_INFO("Market data callback registered");
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
            bool success = marketDataInterface_owner_->subscribeMarketData(tradingPair, exchangeId);
            
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
            bool success = marketDataInterface_owner_->unsubscribeMarketData(tradingPair, exchangeId);
            
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
            return marketDataInterface_owner_->getTradingFees(exchangeId, tradingPair);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to get trading fees: " + std::string(e.what()));
            return {};
        }
    }

    // Fetch current positions by querying exchange balances and mapping them to Position objects
    std::vector<Position> fetchCurrentPositions() override {
        std::vector<Position> positions;

        if (!initialized_ || !marketDataInterface_owner_) {
            LOG_WARN("DataInterface", "fetchCurrentPositions called before initialization or without underlying interface");
            return positions;
        }

        try {
            // Iterate over supported exchanges and collect balances
            auto exchanges = core::ExchangeId::getAllSupported();
            for (const auto& ex : exchanges) {
                try {
                    auto balances = marketDataInterface_owner_->getExchangeBalances(ex);
                    for (const auto& [asset, balance] : balances) {
                        if (std::abs(balance) < 1e-12) continue;

                        // Map asset balance to a TradingPair like "ASSET/USDT" as a reasonable default
                        core::Symbol tradingPairSym(asset + "/USDT");
                        TradingPair tradingPair = TradingPair(tradingPairSym);

                        core::StrategyId strategyId;
                        strategyId.id = "reconciliation";
                        strategyId.version = "1.0";

                        // Try to fetch a current market price for the implied pair
                        double avgPrice = 0.0;
                        try {
                            avgPrice = marketDataInterface_owner_->getCurrentPrice(tradingPairSym, std::optional<std::string>(ex.toString()));
                        } catch (...) {
                            // ignore - leave avgPrice as 0.0
                        }

                        Position pos(ex, tradingPair, strategyId, balance, avgPrice);
                        if (avgPrice > 0.0) {
                            pos.setCurrentPrice(avgPrice);
                        }
                        pos.setUnrealizedPnl(0.0);
                        pos.setRealizedPnl(0.0);
                        positions.push_back(std::move(pos));
                    }
                } catch (const std::exception& e) {
                    LOG_WARN("DataInterface", std::string("Failed to fetch balances for ") + ex.toString() + ": " + e.what());
                    continue;
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR(std::string("DataInterface::fetchCurrentPositions exception: ") + e.what());
        }

        return positions;
    }
    
    // Additional helper methods for enhanced functionality
    void updateBalance(const core::ExchangeId& exchangeId, const std::string& asset, double balance) {
        if (initialized_ && marketDataInterface_owner_) {
            try {
                if (marketDataInterfaceRaw_) {
                    marketDataInterfaceRaw_->updateBalance(exchangeId, asset, balance);
                } else {
                    LOG_ERROR("Underlying MarketDataInterface does not support updateBalance");
                }
                LOG_DEBUG("Updated balance for " + exchangeId.toString() + ":" + asset + " = " + std::to_string(balance));
            } catch (const std::exception& e) {
                LOG_ERROR("Failed to update balance: " + std::string(e.what()));
            }
        }
    }
    
    double getBalance(const core::ExchangeId& exchangeId, const std::string& asset) const {
        if (initialized_ && marketDataInterface_owner_) {
            try {
                if (marketDataInterfaceRaw_) {
                    return marketDataInterfaceRaw_->getBalance(exchangeId, asset);
                } else {
                    LOG_ERROR("Underlying MarketDataInterface does not support getBalance");
                    return 0.0;
                }
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

// Backwards-compatible constructor that accepts a RiskConfig
DataInterface::DataInterface(const trading::risk::RiskConfig& /*cfg*/) {
    // Historically some callers passed a RiskConfig to construct a DataInterface.
    // For compatibility we keep this constructor and delegate to the default
    // initialization path. Any config-specific wiring should be done in
    // initialize() if required.
}

 
// Factory function to create DataInterface
std::unique_ptr<DataInterface> createDataInterface() {
    try {
        // Use the shared MarketDataInterface singleton to avoid multiple instances
        auto sharedMdIface = MarketDataInterface::createDataInterface(RiskConfig{});
        return std::make_unique<DataInterfaceImpl>(sharedMdIface);
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to create DataInterface: " + std::string(e.what()));
        throw;
    }
}

} // namespace risk
} // namespace trading