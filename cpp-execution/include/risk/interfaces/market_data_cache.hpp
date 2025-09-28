#pragma once

#include "risk/risk_types.hpp"
#include "core/market_types.hpp"
#include <string>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <shared_mutex>
#include <mutex>
#include <deque>
#include <vector>
#include <functional>
#include <atomic>

namespace trading {
namespace risk {

/**
 * Volatility calculator with EWMA and simple methods
 */
struct VolatilityCalculator {
    struct EWMAState {
        double volatility = 0.0;
        double lambda = 0.94;  // Decay factor (RiskMetrics standard)
        bool initialized = false;
    };
    
    std::unordered_map<std::string, EWMAState> ewmaStates;
    mutable std::mutex ewmaMutex_; 
    
    double calculateEWMAVolatility(const std::deque<std::pair<Timestamp, double>>& prices, 
                                   const std::string& key);
    double calculateSimpleVolatility(const std::deque<std::pair<Timestamp, double>>& prices,
                                    const std::string& timeframe);
};

/**
 * Price history for volatility calculation and historical data
 */
struct PriceHistory {
    std::deque<std::pair<Timestamp, double>> prices;
    size_t maxSize = 1000;  // Keep last 1000 price points
    
    void addPrice(Timestamp timestamp, double price);
    std::vector<std::pair<Timestamp, double>> getRecentPrices(int count) const;
    void removeOlderThan(const Timestamp& cutoff);
    size_t size() const { return prices.size(); }
    bool empty() const { return prices.empty(); }
};

/**
 * Market data cache entry with validity tracking
 */
struct MarketDataCacheEntry {
    core::MarketData data;
    std::chrono::steady_clock::time_point lastUpdate;
    bool isValid = false;
    
    bool isExpired(std::chrono::milliseconds maxAge) const {
        auto now = std::chrono::steady_clock::now();
        return (now - lastUpdate) > maxAge;
    }
};

/**
 * Configuration for MarketDataCache
 */
struct MarketDataCacheConfig {
    std::chrono::milliseconds cacheValidityDuration{2000};  // 2 seconds
    size_t maxHistorySize{1000};
    size_t maxCacheEntries{10000};
    std::chrono::hours cleanupInterval{1};
    bool enableVolatilityCalculation{true};
};

/**
 * MarketDataCache - Handles heavy data processing, storage, and caching
 * 
 * Responsibilities:
 * - Store and retrieve market data with expiration
 * - Maintain price history for volatility calculations
 * - Calculate volatility using various methods
 * - Periodic cleanup of stale data
 * - Thread-safe access to all data
 */
class MarketDataCache {
public:
    explicit MarketDataCache(const MarketDataCacheConfig& config = MarketDataCacheConfig{});
    ~MarketDataCache();
    
    // Disable copy/move - cache should be singleton-like
    MarketDataCache(const MarketDataCache&) = delete;
    MarketDataCache& operator=(const MarketDataCache&) = delete;
    MarketDataCache(MarketDataCache&&) = delete;
    MarketDataCache& operator=(MarketDataCache&&) = delete;
    
    // Core data operations
    void updateMarketData(const std::string& cacheKey, const core::MarketData& data);
    std::optional<core::MarketData> getMarketData(const std::string& cacheKey) const;
    
    // Price operations
    void updatePrice(const std::string& cacheKey, Timestamp timestamp, double price);
    std::optional<double> getCurrentPrice(const std::string& cacheKey) const;
    
    // Historical data
    std::vector<std::pair<Timestamp, double>> getHistoricalPrices(
        const std::string& cacheKey, int count) const;
    
    // Volatility calculations
    double getVolatility(const std::string& cacheKey, const std::string& timeframe) const;
    
    // Cache management
    void cleanup();
    void pruneExpiredEntries();
    void pruneOldHistory();
    
    // Statistics and health
    struct CacheStats {
        size_t totalEntries{0};
        size_t validEntries{0};
        size_t expiredEntries{0};
        size_t totalHistoryPoints{0};
        std::chrono::steady_clock::time_point lastCleanup;
    };
    CacheStats getStats() const;
    
    // Utility methods
    bool isStale(const std::string& cacheKey) const;
    void clear();
    
    // Cache key creation helper
    static std::string createCacheKey(const core::ExchangeId& exchangeId, 
                                     const core::Symbol& tradingPair);

private:
    // Configuration
    MarketDataCacheConfig config_;
    
    // Market data cache
    mutable std::shared_mutex cacheMutex_;
    std::unordered_map<std::string, MarketDataCacheEntry> marketDataCache_;
    
    // Price history
    mutable std::shared_mutex historyMutex_;
    std::unordered_map<std::string, PriceHistory> priceHistories_;
    
    // Volatility calculator
    VolatilityCalculator volatilityCalculator_;
    
    // Cleanup management
    std::chrono::steady_clock::time_point lastCleanup_;
    
    // Statistics
    mutable std::atomic<uint64_t> totalUpdates_{0};
    mutable std::atomic<uint64_t> cacheHits_{0};
    mutable std::atomic<uint64_t> cacheMisses_{0};
    
    // Internal helpers
    void performPeriodicCleanup();
    bool shouldPerformCleanup() const;
};

} // namespace risk
} // namespace trading
