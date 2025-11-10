#include "risk/interfaces/market_data_cache.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace trading {
namespace risk {

// PriceHistory implementation
void PriceHistory::addPrice(Timestamp timestamp, double price) {
    prices.push_back({timestamp, price});
    while (prices.size() > maxSize) {
        prices.pop_front();
    }
}

std::vector<std::pair<Timestamp, double>> PriceHistory::getRecentPrices(int count) const {
    std::vector<std::pair<Timestamp, double>> result;
    
    int startIdx = std::max(0, static_cast<int>(prices.size()) - count);
    for (int i = startIdx; i < static_cast<int>(prices.size()); ++i) {
        result.push_back(prices[i]);
    }
    
    return result;
}

void PriceHistory::removeOlderThan(const Timestamp& cutoff) {
    while (!prices.empty() && prices.front().first < cutoff) {
        prices.pop_front();
    }
}

// VolatilityCalculator implementation
double VolatilityCalculator::calculateEWMAVolatility(
    const std::deque<std::pair<Timestamp, double>>& prices, 
    const std::string& key) {
    
    if (prices.size() < 2) return 0.0;
    
    // Protect access to ewmaStates map
    std::lock_guard<std::mutex> g(ewmaMutex_);
    auto& state = ewmaStates[key];
    
    // Calculate returns
    std::vector<double> returns;
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i-1].second > 0) {
            double logReturn = std::log(prices[i].second / prices[i-1].second);
            returns.push_back(logReturn);
        }
    }
    
    if (returns.empty()) return 0.0;
    
    // Initialize EWMA if needed
    if (!state.initialized && returns.size() >= 20) {
        // Use first 20 returns to initialize
        double sum = 0.0;
        for (size_t i = 0; i < 20; ++i) {
            sum += returns[i] * returns[i];
        }
        state.volatility = std::sqrt(sum / 20.0);
        state.initialized = true;
    }
    
    // Update EWMA volatility with latest return
    if (state.initialized && !returns.empty()) {
        double latestReturn = returns.back();
        state.volatility = std::sqrt(
            state.lambda * state.volatility * state.volatility + 
            (1 - state.lambda) * latestReturn * latestReturn
        );
    }
    
    // Annualize (assuming daily data)
    return state.volatility * std::sqrt(252.0);
}

double VolatilityCalculator::calculateSimpleVolatility(
    const std::deque<std::pair<Timestamp, double>>& prices,
    const std::string& timeframe) {
    
    if (prices.size() < 2) return 0.0;
    
    // Simple volatility calculation using standard deviation of returns
    std::vector<double> returns;
    for (size_t i = 1; i < prices.size(); ++i) {
        if (prices[i-1].second > 0) {
            double logReturn = std::log(prices[i].second / prices[i-1].second);
            returns.push_back(logReturn);
        }
    }
    
    if (returns.empty()) return 0.0;
    
    // Calculate mean
    double mean = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    
    // Calculate standard deviation
    double sumSquaredDiff = 0.0;
    for (double ret : returns) {
        sumSquaredDiff += std::pow(ret - mean, 2);
    }
    
    double stdDev = std::sqrt(sumSquaredDiff / returns.size());
    
    // Annualize based on timeframe
    double annualizationFactor = 252.0;  // Daily data
    if (timeframe == "1h") {
        annualizationFactor = 252.0 * 24.0;  // Hourly data
    } else if (timeframe == "1m") {
        annualizationFactor = 252.0 * 24.0 * 60.0;  // Minute data
    }
    
    return stdDev * std::sqrt(annualizationFactor);
}

// MarketDataCache implementation
MarketDataCache::MarketDataCache(const MarketDataCacheConfig& config)
    : config_(config), lastCleanup_(std::chrono::steady_clock::now()) {
    LOG_INFO("MarketDataCache initialized (this={}) with {} ms cache validity, {} max history size",
             static_cast<const void*>(this), config_.cacheValidityDuration.count(), config_.maxHistorySize);
}

MarketDataCache::~MarketDataCache() {
    // Avoid logging from destructor as Logger may be partially torn down
    // during static destruction which can lead to crashes. Perform minimal
    // cleanup without invoking other member functions that log.
    try {
        {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_);
            marketDataCache_.clear();
        }

        {
            std::unique_lock<std::shared_mutex> lock(historyMutex_);
            priceHistories_.clear();
        }
    } catch (...) {
        // swallow
    }
}

void MarketDataCache::updateMarketData(const std::string& cacheKey, const core::MarketData& data) {
    try {
        totalUpdates_.fetch_add(1, std::memory_order_relaxed);
        
        // Update main cache
        LOG_INFO("Updating market data for {}: price={}, bid={}, ask={}, volume={}, cacheKey={}",
                 cacheKey, data.price, data.bid, data.ask, data.volume, cacheKey);

        {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_);
            size_t sizeBefore = marketDataCache_.size();
            marketDataCache_[cacheKey] = MarketDataCacheEntry{
                data,
                std::chrono::steady_clock::now(),
                data.price > 0.0  // Valid if we have a price
            };
            size_t sizeAfter = marketDataCache_.size();
            LOG_TRACE("MarketDataCache::updateMarketData this={} key='{}' sizeBefore={} sizeAfter={}",
                      static_cast<const void*>(this), cacheKey, sizeBefore, sizeAfter);
        }
        // Periodic cleanup check
        if (shouldPerformCleanup()) {
            performPeriodicCleanup();
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error updating market data for {}: {}", cacheKey, e.what());
    }
}

// std::optional<core::MarketData> MarketDataCache::getMarketData(const std::string& cacheKey) const {
//     try {
//         std::shared_lock<std::shared_mutex> lock(cacheMutex_);
//         auto it = marketDataCache_.find(cacheKey);
        
//         if (it != marketDataCache_.end()) {
//             const auto& entry = it->second;
            
//             // Check if entry is still valid
//             if (entry.isValid && !entry.isExpired(config_.cacheValidityDuration)) {
//                 cacheHits_.fetch_add(1, std::memory_order_relaxed);
//                 return entry.data;
//             }
//         }
        
//         cacheMisses_.fetch_add(1, std::memory_order_relaxed);
//         return std::nullopt;
        
//     } catch (const std::exception& e) {
//         LOG_ERROR("Error getting market data for {}: {}", cacheKey, e.what());
//         return std::nullopt;
//     }
// }

void MarketDataCache::updatePrice(const std::string& cacheKey, Timestamp timestamp, double price) {
    try {
        std::unique_lock<std::shared_mutex> lock(historyMutex_);
        auto& history = priceHistories_[cacheKey];
        history.maxSize = config_.maxHistorySize;
        history.addPrice(timestamp, price);
        
        // Trim if needed
        if (history.size() > config_.maxHistorySize * 1.1) {  // 10% buffer before trimming
            size_t target = static_cast<size_t>(config_.maxHistorySize * 0.8);
            auto recent = history.getRecentPrices(static_cast<int>(target));
            
            PriceHistory newHistory;
            newHistory.maxSize = config_.maxHistorySize;
            for (const auto& p : recent) {
                newHistory.addPrice(p.first, p.second);
            }
            priceHistories_[cacheKey] = std::move(newHistory);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error updating price for {}: {}", cacheKey, e.what());
    }
}
std::optional<core::MarketData> MarketDataCache::getMarketData(const std::string& cacheKey) const {
    try {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        
    LOG_TRACE("[cache_this={}] Looking for key '{}' in cache with {} entries",
          static_cast<const void*>(this), cacheKey, marketDataCache_.size());
        
        auto it = marketDataCache_.find(cacheKey);
        
        if (it != marketDataCache_.end()) {
            const auto& entry = it->second;
            LOG_TRACE("Found entry for '{}': isValid={}, isExpired={}", 
                     cacheKey, entry.isValid, entry.isExpired(config_.cacheValidityDuration));
            
            if (entry.isValid && !entry.isExpired(config_.cacheValidityDuration)) {
                cacheHits_.fetch_add(1, std::memory_order_relaxed);
                return entry.data;
            }
        } else {
            LOG_TRACE("Key '{}' not found in cache", cacheKey);
            // Log all keys in cache for debugging
            for (const auto& [key, _] : marketDataCache_) {
                LOG_TRACE("  Cache contains key: '{}'", key);
            }
        }
        
        cacheMisses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error getting market data for {}: {}", cacheKey, e.what());
        return std::nullopt;
    }
}

std::optional<double> MarketDataCache::getCurrentPrice(const std::string& cacheKey) const {
    auto marketData = getMarketData(cacheKey);
    if (marketData.has_value()) {
        const auto& data = marketData.value();
        
        // Use mid-price if available
        if (data.bid > 0 && data.ask > 0) {
            return (data.bid + data.ask) / 2.0;
        }
        
        // Otherwise use last trade price or price field
        if (data.lastTradePrice > 0) {
            return data.lastTradePrice;
        }
        
        return data.price > 0 ? data.price : std::optional<double>{};
    }
    return std::nullopt;
}

std::vector<std::pair<Timestamp, double>> MarketDataCache::getHistoricalPrices(
    const std::string& cacheKey, int count) const {
    
    try {
        std::shared_lock<std::shared_mutex> lock(historyMutex_);
        auto it = priceHistories_.find(cacheKey);
        
        if (it != priceHistories_.end()) {
            return it->second.getRecentPrices(count);
        }
        
        return {};
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error getting historical prices for {}: {}", cacheKey, e.what());
        return {};
    }
}

double MarketDataCache::getVolatility(const std::string& cacheKey, const std::string& timeframe) const {
    if (!config_.enableVolatilityCalculation) {
        return 0.0;
    }
    
    try {
        std::shared_lock<std::shared_mutex> lock(historyMutex_);
        auto it = priceHistories_.find(cacheKey);
        
        if (it != priceHistories_.end() && !it->second.empty()) {
            // Use EWMA volatility by default
            return const_cast<VolatilityCalculator&>(volatilityCalculator_).calculateEWMAVolatility(it->second.prices, cacheKey);
        }
        
        return 0.0;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error calculating volatility for {}: {}", cacheKey, e.what());
        return 0.0;
    }
}
// getCurrentSpread
std::optional<double> MarketDataCache::getCurrentSpread(const std::string& cacheKey) const {
    try {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        auto it = marketDataCache_.find(cacheKey);
        if (it != marketDataCache_.end()) {
            const auto& entry = it->second;
            if (entry.isValid && !entry.isExpired(config_.cacheValidityDuration)) {
                const auto& data = entry.data;
                if (data.ask > 0 && data.bid > 0) {
                    return data.ask - data.bid;
                }
            }
        }
        // Diagnostic logging for cache miss - TRACE level to avoid spam in normal runs
        LOG_INFO("MarketDataCache::getCurrentSpread: cache miss or invalid for key='{}' (entries={})", cacheKey, marketDataCache_.size());
        return std::nullopt;
    } catch (const std::exception& e) {
        LOG_ERROR("Error getting current spread for {}: {}", cacheKey, e.what());
        return std::nullopt;
    }
}


void MarketDataCache::cleanup() {
    LOG_DEBUG("Starting MarketDataCache cleanup");
    
    pruneExpiredEntries();
    pruneOldHistory();
    
    lastCleanup_ = std::chrono::steady_clock::now();
    LOG_DEBUG("MarketDataCache cleanup completed");
}

void MarketDataCache::pruneExpiredEntries() {
    try {
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        
        size_t initialSize = marketDataCache_.size();
        auto now = std::chrono::steady_clock::now();
        
        for (auto it = marketDataCache_.begin(); it != marketDataCache_.end();) {
            if (it->second.isExpired(config_.cacheValidityDuration * 10)) {  // Keep 10x longer for cleanup
                it = marketDataCache_.erase(it);
            } else {
                ++it;
            }
        }
        
        size_t removedCount = initialSize - marketDataCache_.size();
        if (removedCount > 0) {
            LOG_DEBUG("Pruned {} expired cache entries", removedCount);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error pruning expired entries: {}", e.what());
    }
}

void MarketDataCache::pruneOldHistory() {
    try {
        std::unique_lock<std::shared_mutex> lock(historyMutex_);
        
        auto cutoff = std::chrono::system_clock::now() - std::chrono::hours(24);
        size_t totalRemoved = 0;
        
        for (auto& [key, history] : priceHistories_) {
            size_t beforeSize = history.size();
            history.removeOlderThan(cutoff);
            totalRemoved += (beforeSize - history.size());
        }
        
        if (totalRemoved > 0) {
            LOG_DEBUG("Pruned {} old price history entries", totalRemoved);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error pruning old history: {}", e.what());
    }
}

MarketDataCache::CacheStats MarketDataCache::getStats() const {
    CacheStats stats;
    
    try {
        {
            std::shared_lock<std::shared_mutex> lock(cacheMutex_);
            stats.totalEntries = marketDataCache_.size();
            
            auto now = std::chrono::steady_clock::now();
            for (const auto& [key, entry] : marketDataCache_) {
                if (entry.isValid && !entry.isExpired(config_.cacheValidityDuration)) {
                    stats.validEntries++;
                } else {
                    stats.expiredEntries++;
                }
            }
        }
        
        {
            std::shared_lock<std::shared_mutex> lock(historyMutex_);
            for (const auto& [key, history] : priceHistories_) {
                stats.totalHistoryPoints += history.size();
            }
        }
        
        stats.lastCleanup = lastCleanup_;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error getting cache stats: {}", e.what());
    }
    
    return stats;
}

bool MarketDataCache::isStale(const std::string& cacheKey) const {
    try {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        auto it = marketDataCache_.find(cacheKey);
        
        if (it != marketDataCache_.end()) {
            return it->second.isExpired(config_.cacheValidityDuration);
        }
        
        return true;  // Missing entries are considered stale
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error checking staleness for {}: {}", cacheKey, e.what());
        return true;
    }
}

void MarketDataCache::clear() {
    try {
        {
            std::unique_lock<std::shared_mutex> lock(cacheMutex_);
            marketDataCache_.clear();
        }
        
        {
            std::unique_lock<std::shared_mutex> lock(historyMutex_);
            priceHistories_.clear();
        }
        
        LOG_INFO("MarketDataCache cleared");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error clearing cache: {}", e.what());
    }
}

std::string MarketDataCache::createCacheKey(const core::ExchangeId& exchangeId, 
                                           const core::Symbol& tradingPair) {
    return exchangeId.toString() + ":" + tradingPair.toString();
}

void MarketDataCache::performPeriodicCleanup() {
    try {
        cleanup();
    } catch (const std::exception& e) {
        LOG_ERROR("Error during periodic cleanup: {}", e.what());
    }
}

bool MarketDataCache::shouldPerformCleanup() const {
    auto now = std::chrono::steady_clock::now();
    return (now - lastCleanup_) > config_.cleanupInterval;
}

} // namespace risk
} // namespace trading
