/**
 * @file exchange_gateway.cpp
 * @brief Implementation of ExchangeGateway
 * 
 * @details 
 * Implements the ExchangeGateway by wrapping the existing ApiClientManager
 * and adding caching and coordination features.
 * 
 * Created on: December 2024
 */
#include "exchange_gateway.hpp"
#include <stdexcept>
#include <sstream>

namespace trading {
namespace common {

ExchangeGateway::ExchangeGateway(std::shared_ptr<execution::ApiClientManager> apiClientManager)
    : apiClientManager_(std::move(apiClientManager)) {
    if (!apiClientManager_) {
        throw std::invalid_argument("ApiClientManager cannot be null");
    }
}

ExchangeGateway::~ExchangeGateway() = default;

// === ORDER OPERATIONS (Direct passthrough) ===
// getAccountBalancesAsync
std::future<std::unordered_map<std::string, double>> ExchangeGateway::getAccountBalancesAsync(const core::ExchangeId& exchangeId) {
    return std::async(std::launch::async, [this, exchangeId]() {
        // Get the appropriate exchange client
        auto client = apiClientManager_->getClient(exchangeId);
        
        // Get account balances directly through the existing API
        auto future = client->getAccountBalances();
        return future.get();
    });
}

std::future<execution::models::ExecutionReport> ExchangeGateway::placeOrder(const execution::models::Order& order) {
    return std::async(std::launch::async, [this, order]() {
        // Get the appropriate exchange client
        auto client = apiClientManager_->getClient(order.getExchangeId());
        
        // Place the order directly through the existing API
        auto future = client->placeOrder(order);
        return future.get();
    });
}

std::future<execution::models::ExecutionReport> ExchangeGateway::cancelOrder(
    const std::string& orderId, 
    const core::Symbol& symbol,
    const core::ExchangeId& exchangeId) {
    
    return std::async(std::launch::async, [this, orderId, symbol, exchangeId]() {
        // Get the appropriate exchange client
        auto client = apiClientManager_->getClient(exchangeId);
        
        // Cancel the order directly through the existing API
        auto future = client->cancelOrder(orderId, symbol);
        return future.get();
    });
}

std::future<execution::models::ExecutionReport> ExchangeGateway::getOrderStatus(
    const std::string& orderId, 
    const core::Symbol& symbol,
    const core::ExchangeId& exchangeId) {
    
    return std::async(std::launch::async, [this, orderId, symbol, exchangeId]() {
        // Get the appropriate exchange client
        auto client = apiClientManager_->getClient(exchangeId);
        
        // Get order status directly through the existing API
        auto future = client->getOrderStatus(orderId, symbol);
        return future.get();
    });
}

// === ACCOUNT OPERATIONS (With caching) ===

std::future<std::unordered_map<std::string, double>> ExchangeGateway::getAccountBalances(
    const core::ExchangeId& exchangeId, 
    bool forceRefresh) {
    
    return std::async(std::launch::async, [this, exchangeId, forceRefresh]() {
        // Check cache first (unless force refresh)
        if (!forceRefresh) {
            auto cached = getCachedBalances(exchangeId);
            if (cached.has_value()) {
                return cached.value();
            }
        }
        
        // Fetch fresh data from API
        auto client = apiClientManager_->getClient(exchangeId);
        auto future = client->getAccountBalances();
        auto balances = future.get();
        
        // Convert std::map to std::unordered_map for cache
        std::unordered_map<std::string, double> unorderedBalances(balances.begin(), balances.end());
        
        // Update cache
        updateBalanceCache(exchangeId, unorderedBalances);
        
        return unorderedBalances;
    });
}

// === MARKET DATA OPERATIONS (With caching) ===

std::future<double> ExchangeGateway::getCurrentPrice(
    const core::ExchangeId& exchangeId, 
    const core::Symbol& symbol,
    bool forceRefresh) {
    
    return std::async(std::launch::async, [this, exchangeId, symbol, forceRefresh]() {
        // Check cache first (unless force refresh)
        if (!forceRefresh) {
            auto cached = getCachedPrice(exchangeId, symbol);
            if (cached.has_value()) {
                return cached.value();
            }
        }
        
        // Fetch fresh data from API
        auto client = apiClientManager_->getClient(exchangeId);
        auto future = client->getCurrentPrice(symbol);
        auto price = future.get();
        
        // Update cache
        updatePriceCache(exchangeId, symbol, price);
        
        return price;
    });
}

// === HEALTH AND STATUS ===

bool ExchangeGateway::isHealthy() const {
    try {
        auto connections = apiClientManager_->checkConnections();
        for (const auto& [exchange, isConnected] : connections) {
            if (!isConnected) {
                return false;
            }
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::string ExchangeGateway::getStatus() const {
    std::ostringstream status;
    status << "{\n";
    status << "  \"service\": \"exchange_gateway\",\n";
    status << "  \"healthy\": " << (isHealthy() ? "true" : "false") << ",\n";
    status << "  \"underlying_api_manager\": " << apiClientManager_->getHealthStatus() << ",\n";
    
    // Cache statistics
    {
        std::shared_lock balanceLock(balanceCacheMutex_);
        std::shared_lock priceLock(priceCacheMutex_);
        status << "  \"cache_stats\": {\n";
        status << "    \"balance_entries\": " << balanceCache_.size() << ",\n";
        status << "    \"price_entries\": " << priceCache_.size() << "\n";
        status << "  }\n";
    }
    
    status << "}";
    return status.str();
}

std::shared_ptr<execution::ApiClientManager> ExchangeGateway::getApiClientManager() const {
    return apiClientManager_;
}

// === CACHE MANAGEMENT ===

void ExchangeGateway::clearCache() {
    {
        std::unique_lock lock(balanceCacheMutex_);
        balanceCache_.clear();
    }
    {
        std::unique_lock lock(priceCacheMutex_);
        priceCache_.clear();
    }
}

void ExchangeGateway::setBalanceCacheTtl(std::chrono::seconds ttl) {
    defaultBalanceTtl_ = ttl;
}

void ExchangeGateway::setPriceCacheTtl(std::chrono::seconds ttl) {
    defaultPriceTtl_ = ttl;
}

// === PRIVATE HELPER METHODS ===

std::string ExchangeGateway::makeBalanceKey(const core::ExchangeId& exchangeId) const {
    return "balance_" + exchangeId.toString();
}

std::string ExchangeGateway::makePriceKey(const core::ExchangeId& exchangeId, const core::Symbol& symbol) const {
    return "price_" + exchangeId.toString() + "_" + symbol.toString();
}

std::optional<std::unordered_map<std::string, double>> ExchangeGateway::getCachedBalances(const core::ExchangeId& exchangeId) const {
    std::shared_lock lock(balanceCacheMutex_);
    
    auto key = makeBalanceKey(exchangeId);
    auto it = balanceCache_.find(key);
    
    if (it != balanceCache_.end() && !it->second.isExpired()) {
        return it->second.balances;
    }
    
    return std::nullopt;
}

std::optional<double> ExchangeGateway::getCachedPrice(const core::ExchangeId& exchangeId, const core::Symbol& symbol) const {
    std::shared_lock lock(priceCacheMutex_);
    
    auto key = makePriceKey(exchangeId, symbol);
    auto it = priceCache_.find(key);
    
    if (it != priceCache_.end() && !it->second.isExpired()) {
        return it->second.price;
    }
    
    return std::nullopt;
}

void ExchangeGateway::updateBalanceCache(const core::ExchangeId& exchangeId, const std::unordered_map<std::string, double>& balances) {
    std::unique_lock lock(balanceCacheMutex_);
    
    auto key = makeBalanceKey(exchangeId);
    CachedBalances cached;
    cached.balances = balances;
    cached.timestamp = std::chrono::steady_clock::now();
    cached.ttl = defaultBalanceTtl_;
    
    balanceCache_[key] = std::move(cached);
}

void ExchangeGateway::updatePriceCache(const core::ExchangeId& exchangeId, const core::Symbol& symbol, double price) {
    std::unique_lock lock(priceCacheMutex_);
    
    auto key = makePriceKey(exchangeId, symbol);
    CachedPrice cached;
    cached.price = price;
    cached.timestamp = std::chrono::steady_clock::now();
    cached.ttl = defaultPriceTtl_;
    
    priceCache_[key] = std::move(cached);
}

} // namespace common
} // namespace trading
