/**
 * Interface for accessing market data (Fixed version)
 *
 * File: data_interface.hpp
 * Created: 2025-04-29
 */

#pragma once

#include "risk/risk_types.hpp"
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <optional>
#include <functional>
#include "communication/zmq_server_client.hpp"
namespace trading {
namespace risk {

/**
 * Interface for accessing market data
 */
class DataInterface {
public:
    /**
     * Constructor
     */
    DataInterface();
    
    /**
     * Virtual destructor
     */
    virtual ~DataInterface() = default;

    /**
     * Initialize the data interface
     * 
     * @return true if successful, false otherwise
     */
    virtual bool initialize() = 0;
    // isConnected
    virtual bool isConnected() const = 0;
    // isHealthy
    virtual bool isHealthy() const {
        return true; // Default implementation, can be overridden
    }
    /**
     * Get market data for a trading pair
     * 
     * @param tradingPair The trading pair
     * @param exchangeId Optional exchange ID
     * @return MarketData structure wrapped in optional
     */
    virtual std::optional<MarketData> getMarketData(const core::Symbol& tradingPair,
                                               const std::optional<std::string>& exchangeId = std::nullopt) = 0;
    
    /**
     * Get current price for a trading pair
     * 
     * @param tradingPair The trading pair
     * @param exchangeId Optional exchange ID
     * @return Current price
     */
    virtual double getCurrentPrice(const core::Symbol& tradingPair, const std::optional<std::string>& exchangeId = std::nullopt) = 0;
    
    /**
     * Get price for a trading pair (alias for getCurrentPrice for compatibility)
     * 
     * @param tradingPair The trading pair
     * @return Current price
     */
    virtual double getPrice(const core::Symbol& tradingPair) {
        return getCurrentPrice(tradingPair);
    }
    // fetchCurrentPositions
    virtual std::vector<Position> fetchCurrentPositions() {
        return {}; // Default empty implementation
    }
    // setZmqClient
    virtual void setZmqClient(std::shared_ptr<zmq::socket_t> zmqClient) {
        // Default implementation does nothing
    }
    /**
     * Get historical prices for a trading pair
     * 
     * @param tradingPair The trading pair
     * @param interval Time interval (e.g., "1m", "1h", "1d")
     * @param count Number of data points to retrieve
     * @param exchangeId Optional exchange ID
     * @return Vector of timestamp-price pairs
     */
    virtual std::vector<std::pair<Timestamp, double>> getHistoricalPrices(
        const core::Symbol& tradingPair,
        const std::string& interval,
        int count,
        const std::optional<std::string>& exchangeId = std::nullopt) = 0;
    
    /**
     * Get volatility for a trading pair
     * 
     * @param tradingPair The trading pair
     * @param timeframe Timeframe for volatility calculation (e.g., "1h", "1d", "7d")
     * @param exchangeId Optional exchange ID
     * @return Volatility value
     */
    virtual double getVolatility(const core::Symbol& tradingPair, const std::string& timeframe, const std::optional<std::string>& exchangeId = std::nullopt) = 0;
    
    // shutdown
    virtual void shutdown() {
        // Default implementation does nothing
    }
    /**
     * Get order history for a trading pair
     * 
     * @param exchangeId Exchange ID
     * @param tradingPair The trading pair
     * @param limit Maximum number of orders to retrieve
     * @return Vector of order requests
     */
    virtual std::vector<OrderRequest> getOrderHistory(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair, int limit) = 0;
    
    /**
     * Get exchange balances
     * 
     * @param exchangeId Exchange ID
     * @return Map of asset to balance
     */
    virtual std::map<std::string, double> getExchangeBalances(const core::ExchangeId& exchangeId) = 0;
    
    /**
     * Register callback for market data updates
     * 
     * @param callback Function to call when market data is updated
     */
    virtual void registerMarketDataCallback(const std::function<void(const MarketData&)>& callback) = 0;
    
    /**
     * Subscribe to market data updates
     * 
     * @param tradingPair The trading pair
     * @param exchangeId Optional exchange ID
     * @return true if successful, false otherwise
     */
    virtual bool subscribeMarketData(const core::Symbol& tradingPair, const std::optional<std::string>& exchangeId = std::nullopt) = 0;
    
    /**
     * Unsubscribe from market data updates
     * 
     * @param tradingPair The trading pair
     * @param exchangeId Optional exchange ID
     * @return true if successful, false otherwise
     */
    virtual bool unsubscribeMarketData(const core::Symbol& tradingPair, const std::optional<std::string>& exchangeId = std::nullopt) = 0;
    
    /**
     * Get trading fees
     * 
     * @param exchangeId Exchange ID
     * @param tradingPair Optional trading pair
     * @return Map of fee types to fee values
     */
    virtual std::map<std::string, double> getTradingFees(const core::ExchangeId& exchangeId, const std::optional<std::string>& tradingPair = std::nullopt) = 0;
    
        /**
         * REST fallback: ask the underlying REST client for a current price (may be rate-limited)
         * Implementations should use this sparingly and respect rate limits.
         */
        virtual double getPriceFromRest(const core::Symbol& tradingPair, const std::optional<std::string>& exchangeId = std::nullopt) {
            return 0.0;
        }
};

/**
 * Factory function to create DataInterface
 * 
 * @return Unique pointer to DataInterface implementation
 */
std::unique_ptr<DataInterface> createDataInterface();

/**
 * Mock implementation of DataInterface for testing
 */
class MockDataInterface : public DataInterface {
public:
    MockDataInterface() = default;
    
    bool initialize() override {
        return true;
    }
    
    std::optional<MarketData> getMarketData(const core::Symbol& tradingPair,
                                      const std::optional<std::string>& exchangeId = std::nullopt) override {
        MarketData data;
        data.tradingPair = tradingPair;
        data.price = 50000.0; // Mock price
        data.bid = 49990.0;
        data.ask = 50010.0;
        return data;
    }
    
    double getCurrentPrice(const core::Symbol& tradingPair, const std::optional<std::string>& exchangeId = std::nullopt) override {
        // Simple deterministic mock price for tests
        return 50000.0;
    }
    
    std::vector<std::pair<Timestamp, double>> getHistoricalPrices(
        const core::Symbol& tradingPair,
        const std::string& interval,
        int count,
        const std::optional<std::string>& exchangeId = std::nullopt) override {
        std::vector<std::pair<Timestamp, double>> prices;
        double basePrice = getCurrentPrice(tradingPair);
        auto now = std::chrono::system_clock::now();
        
        for (int i = 0; i < count; ++i) {
            auto timestamp = now - std::chrono::minutes(i);
            double price = basePrice * (1.0 + (i % 10 - 5) * 0.001); // Small variations
            prices.emplace_back(timestamp, price);
        }
        return prices;
    }
    // fetchCurrentPositions
    std::vector<Position> fetchCurrentPositions() override {
        return {}; // Empty for mock
    }
    double getVolatility(const core::Symbol& tradingPair, const std::string& timeframe, const std::optional<std::string>& exchangeId = std::nullopt) override {
        return 0.02; // 2% mock volatility
    }
    
    std::vector<OrderRequest> getOrderHistory(const core::ExchangeId& exchangeId, const core::Symbol& tradingPair, int limit) override {
        return {}; // Empty for mock
    }
    
    std::map<std::string, double> getExchangeBalances(const core::ExchangeId& exchangeId) override {
        return {{"BTC", 1.0}, {"ETH", 10.0}, {"USDT", 50000.0}};
    }
    
    void registerMarketDataCallback(const std::function<void(const MarketData&)>& callback) override {
        // Mock implementation - store callback if needed
    }
    
    bool subscribeMarketData(const core::Symbol& tradingPair, const std::optional<std::string>& exchangeId = std::nullopt) override {
        return true;
    }
    
    bool unsubscribeMarketData(const core::Symbol& tradingPair, const std::optional<std::string>& exchangeId = std::nullopt) override {
        return true;
    }
    
    std::map<std::string, double> getTradingFees(const core::ExchangeId& exchangeId, const std::optional<std::string>& tradingPair = std::nullopt) override {
        return {{"maker", 0.001}, {"taker", 0.001}};
    }
};

} // namespace risk
} // namespace trading