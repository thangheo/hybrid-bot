/**
 * @file exchange_gateway.hpp
 * @brief Centralized gateway for all exchange operations
 * 
 * @details 
 * Wraps the existing ApiClientManager to provide:
 * - Centralized exchange access
 * - Built-in caching
 * - Global rate limiting coordination
 * - Thread-safe operations
 * 
 * Created on: December 2024
 */
#pragma once

#include <memory>
#include <future>
#include <functional>
#include <unordered_map>
#include <shared_mutex>
#include <chrono>
#include <optional>
#include "execution/models/order.hpp"
#include "execution/models/execution_report.hpp"
#include "execution/api_client_manager.hpp"
#include "core/market_types.hpp"
// Optional dependency on risk market data interface for shared cache reads
#include "risk/interfaces/data_interface.hpp"

namespace trading {
namespace execution { //this is stupid but too late to change

/**
 * Cached account balances with timestamp
 */
struct CachedBalances {
    std::unordered_map<std::string, double> balances;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::seconds ttl{30}; // 30 second default TTL
    
    bool isExpired() const {
        auto now = std::chrono::steady_clock::now();
        return (now - timestamp) > ttl;
    }
};

/**
 * Cached price with timestamp
 */
struct CachedPrice {
    double price;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::seconds ttl{5}; // 5 second default TTL for prices
    
    bool isExpired() const {
        auto now = std::chrono::steady_clock::now();
        return (now - timestamp) > ttl;
    }
};

/**
 * ExchangeGateway provides centralized access to all exchange operations
 * with built-in caching, rate limiting, and thread safety.
 */
class ExchangeGateway {
public:
    // Protected/default construction support for unit tests and mocks
protected:
    ExchangeGateway() = default;

public:
    
    // for future use
    // std::shared_ptr<TradingInterface> getTradingInterface();        // For Execution
    // std::shared_ptr<MonitoringInterface> getMonitoringInterface();  // For Risk/Portfolio
    // std::shared_ptr<DataInterface> getDataInterface(); 
    /**
     * Constructor
     * @param apiClientManager Your existing ApiClientManager
     */
    explicit ExchangeGateway(std::shared_ptr<execution::ApiClientManager> apiClientManager,
                             std::shared_ptr<trading::risk::DataInterface> marketDataInterface = nullptr);
    
    /**
     * Destructor
     */
    virtual ~ExchangeGateway();
    
    // === ORDER OPERATIONS (Direct passthrough to ApiClientManager) ===
    // getAccountBalancesAsync
    virtual std::future<std::unordered_map<std::string, double>> getAccountBalancesAsync(const core::ExchangeId& exchangeId);
    /**
     * Place an order through the gateway
     * @param order The order to place
     * @return Future containing execution report
     */
    virtual std::future<execution::models::ExecutionReport> placeOrder(const execution::models::Order& order);
    
    /**
     * Cancel an order through the gateway
     * @param orderId Order ID to cancel
     * @param symbol Trading symbol
     * @param exchangeId Exchange identifier
     * @return Future containing execution report
     */
    virtual std::future<execution::models::ExecutionReport> cancelOrder(
        const std::string& orderId, 
        const core::Symbol& symbol,
        const core::ExchangeId& exchangeId);
    
    /**
     * Get order status through the gateway
     * @param orderId Order ID to query
     * @param symbol Trading symbol
     * @param exchangeId Exchange identifier
     * @return Future containing execution report
     */
    virtual std::future<execution::models::ExecutionReport> getOrderStatus(
        const std::string& orderId, 
        const core::Symbol& symbol,
        const core::ExchangeId& exchangeId);
    
    // === ACCOUNT OPERATIONS (With caching) ===
    
    /**
     * Get account balances with caching
     * @param exchangeId Exchange to query
     * @param forceRefresh If true, bypass cache and fetch fresh data
     * @return Future containing balance map
     */
    virtual std::future<std::unordered_map<std::string, double>> getAccountBalances(
        const core::ExchangeId& exchangeId, 
        bool forceRefresh = false);
    
    // === MARKET DATA OPERATIONS (With caching) ===
    
    /**
     * Get current price with caching
     * @param exchangeId Exchange to query
     * @param symbol Trading symbol
     * @param forceRefresh If true, bypass cache and fetch fresh data
     * @return Future containing current price
     */
    virtual std::future<double> getCurrentPrice(
        const core::ExchangeId& exchangeId, 
        const core::Symbol& symbol,
        bool forceRefresh = false);
    
    // === HEALTH AND STATUS ===
    
    /**
     * Check if gateway is healthy
     * @return true if all connections are healthy
     */
    bool isHealthy() const;
    
    /**
     * Get detailed status information
     * @return JSON string with status details
     */
    std::string getStatus() const;
    
    /**
     * Get the underlying ApiClientManager (for migration purposes)
     * @return Shared pointer to ApiClientManager
     */
    std::shared_ptr<execution::ApiClientManager> getApiClientManager() const;
    
    // === CACHE MANAGEMENT ===
    
    /**
     * Clear all cached data
     */
    void clearCache();
    
    /**
     * Set TTL for balance cache
     * @param ttl Time to live for balance cache
     */
    void setBalanceCacheTtl(std::chrono::seconds ttl);
    
    /**
     * Set TTL for price cache
     * @param ttl Time to live for price cache
     */
    void setPriceCacheTtl(std::chrono::seconds ttl);
    
private:
    // Underlying API client manager (your existing component)
    std::shared_ptr<execution::ApiClientManager> apiClientManager_;
    // Optional shared market-data interface (cache backed)
    std::shared_ptr<trading::risk::DataInterface> marketDataInterface_;
    
    // Cache storage
    mutable std::shared_mutex balanceCacheMutex_;
    mutable std::shared_mutex priceCacheMutex_;
    std::unordered_map<std::string, CachedBalances> balanceCache_;
    std::unordered_map<std::string, CachedPrice> priceCache_;
    
    // Default TTLs
    std::chrono::seconds defaultBalanceTtl_{30};
    std::chrono::seconds defaultPriceTtl_{5};
    
    // Helper methods
    std::string makeBalanceKey(const core::ExchangeId& exchangeId) const;
    std::string makePriceKey(const core::ExchangeId& exchangeId, const core::Symbol& symbol) const;
    
    // Cache retrieval helpers
    std::optional<std::unordered_map<std::string, double>> getCachedBalances(const core::ExchangeId& exchangeId) const;
    std::optional<double> getCachedPrice(const core::ExchangeId& exchangeId, const core::Symbol& symbol) const;
    
    // Cache update helpers
    void updateBalanceCache(const core::ExchangeId& exchangeId, const std::unordered_map<std::string, double>& balances);
    void updatePriceCache(const core::ExchangeId& exchangeId, const core::Symbol& symbol, double price);
};

} // namespace common
} // namespace trading
