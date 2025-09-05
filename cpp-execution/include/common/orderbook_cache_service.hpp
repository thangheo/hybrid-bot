// include/common/orderbook_cache_service.hpp
#pragma once
#include "data_ingestion/orderbook/order_book.hpp"
#include "common/zeromq_client.hpp"
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>

namespace trading {
namespace common {
     

// Define Config struct first, completely
struct OrderBookCacheConfig {
    size_t maxCacheSize = 1000;           // Maximum cached orderbooks
    size_t publishQueueSize = 500;        // Maximum publish queue size
    size_t maxOrderBookDepth = 20;        // Maximum depth for cached orderbooks
    std::chrono::minutes cacheEvictionTime{30}; // Cache eviction time
    bool enableMemoryOptimization = true; // Enable memory optimization
    
    // Default constructor
    OrderBookCacheConfig() = default;
};

struct CacheEntry {
    std::shared_ptr<data_ingestion::OrderBook> orderBook;
    std::atomic<uint64_t> lastAccessTime{0};
    std::atomic<uint32_t> accessCount{0};
    
    CacheEntry(std::shared_ptr<data_ingestion::OrderBook> ob) 
        : orderBook(std::move(ob)) {
        updateAccessTime();
    }
    
    void updateAccessTime() {
        lastAccessTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        accessCount++;
    }
};

class OrderBookCacheService {
public:
    // struct Config {
    //     size_t maxCacheSize = 1000;           // Maximum cached orderbooks
    //     size_t publishQueueSize = 10000;      // ZeroMQ publish queue size
    //     std::chrono::minutes cacheEvictionTime{30}; // Evict after 30min of no access
    //     bool enableMemoryOptimization = true;  // Use memory-optimized storage
    //     int maxOrderBookDepth = 20;        // Limit depth to save memory
    // };
    
    explicit OrderBookCacheService(const OrderBookCacheConfig& config = OrderBookCacheConfig{});
    ~OrderBookCacheService();
    std::string createOrderBookMessage(
    const data_ingestion::OrderBook& orderBook) const ;

    // Direct access methods (synchronous, low-latency)
    std::shared_ptr<const data_ingestion::OrderBook> 
    getOrderBook(const std::string& exchangeId, const std::string& symbol) const;
    
    // Bulk access for efficiency
    std::vector<std::shared_ptr<const data_ingestion::OrderBook>> 
    getOrderBooks(const std::vector<std::pair<std::string, std::string>>& symbols) const;
    
    // Subscribe to updates (asynchronous, event-driven)
    void subscribeToUpdates(const std::string& topic, 
                           std::function<void(const data_ingestion::OrderBook&)> callback);
    
    // Update orderbook from data ingestion
    void updateOrderBook(std::shared_ptr<data_ingestion::OrderBook> orderBook);
    
    // Cache management
    void evictStaleEntries();
    void clearCache();
    size_t getCacheSize() const;
    double getCacheHitRatio() const;
    
    // Health check and metrics
    bool isHealthy() const;
    struct Metrics {
        size_t cacheSize;
        size_t totalRequests;
        size_t cacheHits;
        size_t cacheMisses;
        double hitRatio;
        size_t memoryUsageMB;
        size_t publishQueueDepth;
    };
    Metrics getMetrics() const;
    
private:
    OrderBookCacheConfig config_;
    bool running_;
    // Cache storage with LRU eviction support
    mutable std::shared_mutex cacheMutex_;
    std::unordered_map<std::string, std::unique_ptr<CacheEntry>> cache_;
    
    // Metrics
    mutable std::atomic<size_t> totalRequests_{0};
    mutable std::atomic<size_t> cacheHits_{0};
    mutable std::atomic<size_t> cacheMisses_{0};
    
    // Asynchronous publishing
    std::unique_ptr<ZeroMQClient> publisher_;
    std::queue<std::shared_ptr<data_ingestion::OrderBook>> publishQueue_;
    mutable std::mutex publishMutex_;  // Make mutable for const methods
    std::condition_variable publishCondition_;
    std::thread publishThread_;
    std::atomic<bool> stopPublishing_{false};
    
    // Cache eviction thread
    std::thread evictionThread_;
    std::atomic<bool> stopEviction_{false};
    
    // Helper methods
    std::string createKey(const std::string& exchangeId, const std::string& symbol) const;
    void publishUpdate(const data_ingestion::OrderBook& orderBook);
    void asyncPublishUpdate(std::shared_ptr<data_ingestion::OrderBook> orderBook);
    void publishWorker();
    void evictionWorker();
    void evictLRUEntries();
    size_t estimateMemoryUsage() const;
    
    // Memory optimization
    std::shared_ptr<data_ingestion::OrderBook> 
    optimizeOrderBookMemory(std::shared_ptr<data_ingestion::OrderBook> orderBook);
};

} // namespace common
} // namespace trading