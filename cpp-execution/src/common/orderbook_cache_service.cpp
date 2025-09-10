// src/common/orderbook_cache_service.cpp
#include "common/orderbook_cache_service.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <chrono>

using namespace trading::common;
namespace trading {
namespace common {

OrderBookCacheService::OrderBookCacheService(const OrderBookCacheConfig& config)
    : config_(config) {
    
    // Create ZeroMQ configuration
    ZeroMQConfig zmqConfig;
    zmqConfig.subscriberEndpoint = "tcp://localhost:5556";
    zmqConfig.publisherEndpoint = "tcp://localhost:5557";
    zmqConfig.reconnectInterval = std::chrono::milliseconds(500);
    zmqConfig.maxReconnectAttempts = 5;
    zmqConfig.sendHighWaterMark = 1000;
    zmqConfig.receiveHighWaterMark = 1000;
    zmqConfig.pollTimeout = std::chrono::milliseconds(100);
    
    // Create and initialize the publisher (as a member variable)
    publisher_ = std::make_unique<ZeroMQClient>(zmqConfig);
    
    // Start the ZeroMQ client
    if (!publisher_->start()) {
        throw std::runtime_error("Failed to start ZeroMQ client");
    }
    
    // Start worker threads
    publishThread_ = std::thread(&OrderBookCacheService::publishWorker, this);
    evictionThread_ = std::thread(&OrderBookCacheService::evictionWorker, this);
    
    LOG_INFO("OrderBookCacheService initialized with max cache size: {}", 
             config_.maxCacheSize);
}

OrderBookCacheService::~OrderBookCacheService() {
    stopPublishing_ = true;
    stopEviction_ = true;
    running_ = false;
    
    if (publishThread_.joinable()) {
        publishThread_.join();
    }
    
    if (evictionThread_.joinable()) {
        evictionThread_.join();
    }
    
    // Stop ZeroMQ client
    if (publisher_) {
        publisher_->stop();
    }

    publishCondition_.notify_all();
    
    if (publishThread_.joinable()) {
        publishThread_.join();
    }
    if (evictionThread_.joinable()) {
        evictionThread_.join();
    }
}

std::shared_ptr<const data_ingestion::OrderBook> 
OrderBookCacheService::getOrderBook(const std::string& exchangeId, 
                                   const core::Symbol& symbol) const {
    totalRequests_++;
    
    std::string key = createKey(exchangeId, symbol);
    
    // Try read lock first (most common case)
    {
        std::shared_lock<std::shared_mutex> lock(cacheMutex_);
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            it->second->updateAccessTime();
            cacheHits_++;
            return it->second->orderBook;
        }
    }
    
    cacheMisses_++;
    return nullptr;
}
void OrderBookCacheService::subscribeToUpdates(
    const std::string& topic, 
    std::function<void(const data_ingestion::OrderBook&)> callback) {
    
    if (!publisher_) {
        LOG_ERROR("Cannot subscribe to updates: publisher not initialized");
        return;
    }
    
    LOG_INFO("Setting up subscription for topic: {}", topic);
    
    // Store callback for this topic (you might want to add a map to store multiple callbacks)
    // For now, we'll use the existing delta callback mechanism
    // This is a simplified implementation - in production you'd want a proper topic-based system
    
    try {
        // Set message handler for ZeroMQ client to receive our own published messages
        publisher_->setMessageHandler([callback](const std::string& receivedTopic, const std::string& message) {
            // Parse the message back to OrderBook (simplified)
            // In production, you'd deserialize properly
            LOG_INFO("Received message on topic: {}", receivedTopic);
            // Note: This is a simplified callback - real implementation would reconstruct OrderBook
        });
        
        LOG_INFO("Successfully set up subscription for topic: {}", topic);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to subscribe to topic {}: {}", topic, e.what());
    }
}
void OrderBookCacheService::clearCache() {
    LOG_INFO("Clearing order book cache...");
    
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    
    size_t previousSize = cache_.size();
    cache_.clear();
    
    // Reset metrics
    totalRequests_.store(0);
    cacheHits_.store(0);
    cacheMisses_.store(0);
    
    LOG_INFO("Cache cleared. Removed {} entries", previousSize);
}
size_t OrderBookCacheService::getCacheSize() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    return cache_.size();
}
double OrderBookCacheService::getCacheHitRatio() const {
    size_t total = totalRequests_.load();
    size_t hits = cacheHits_.load();
    
    if (total == 0) {
        return 0.0;
    }
    
    return static_cast<double>(hits) / static_cast<double>(total);
}
std::vector<std::shared_ptr<const data_ingestion::OrderBook>> 
OrderBookCacheService::getOrderBooks(
    const std::vector<std::pair<std::string, std::string>>& symbols) const {
    
    std::vector<std::shared_ptr<const data_ingestion::OrderBook>> result;
    result.reserve(symbols.size());
    
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    
    for (const auto& [exchangeId, symbol] : symbols) {
        totalRequests_++;
        std::string key = createKey(exchangeId, symbol);
        
        auto it = cache_.find(key);
        if (it != cache_.end()) {
            it->second->updateAccessTime();
            result.push_back(it->second->orderBook);
            cacheHits_++;
        } else {
            result.push_back(nullptr);
            cacheMisses_++;
        }
    }
    
    return result;
}

void OrderBookCacheService::updateOrderBook(
    std::shared_ptr<data_ingestion::OrderBook> orderBook) {
    
    if (!orderBook) return;
    
    // Apply memory optimization if enabled
    if (config_.enableMemoryOptimization) {
        orderBook = optimizeOrderBookMemory(orderBook);
    }
    
    std::string key = createKey(
        orderBook->getExchangeId().toString(), 
        orderBook->getSymbol().toString()
    );
    
    // Update cache
    {
        std::unique_lock<std::shared_mutex> lock(cacheMutex_);
        
        // Check if we need to evict entries
        if (cache_.size() >= config_.maxCacheSize) {
            evictLRUEntries();
        }
        
        cache_[key] = std::make_unique<CacheEntry>(orderBook);
    }
    
    // Async publish update
    asyncPublishUpdate(orderBook);
}

void OrderBookCacheService::asyncPublishUpdate(
    std::shared_ptr<data_ingestion::OrderBook> orderBook) {
    
    std::lock_guard<std::mutex> lock(publishMutex_);
    
    // Drop oldest updates if queue is full
    while (publishQueue_.size() >= config_.publishQueueSize) {
        publishQueue_.pop();
    }
    
    publishQueue_.push(orderBook);
    publishCondition_.notify_one();
}

void OrderBookCacheService::publishWorker() {
    while (!stopPublishing_) {
        std::unique_lock<std::mutex> lock(publishMutex_);
        
        publishCondition_.wait(lock, [this] {
            return !publishQueue_.empty() || stopPublishing_;
        });
        
        while (!publishQueue_.empty()) {
            auto orderBook = publishQueue_.front();
            publishQueue_.pop();
            lock.unlock();
            
            // Publish without holding lock
            publishUpdate(*orderBook);
            
            lock.lock();
        }
    }
}

void OrderBookCacheService::publishUpdate(const data_ingestion::OrderBook& orderBook) {
    if (!publisher_) return;
    
    try {
        // Create topic from exchange and symbol
        std::string topic = "orderbook." + 
                           orderBook.getExchangeId().toString() + "." +
                           orderBook.getSymbol().toString();
        
        // Serialize orderbook (simplified - implement proper serialization)
        std::string message = topic; // Replace with actual serialization
        
        publisher_->publish(topic,message);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to publish orderbook update: {}", e.what());
    }
}

void OrderBookCacheService::evictionWorker() {
    while (!stopEviction_) {
        std::this_thread::sleep_for(std::chrono::minutes(1));
        
        if (!stopEviction_) {
            evictStaleEntries();
        }
    }
}

void OrderBookCacheService::evictStaleEntries() {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    
    auto evictionThreshold = std::chrono::duration_cast<std::chrono::milliseconds>(
        config_.cacheEvictionTime).count();
    
    std::unique_lock<std::shared_mutex> lock(cacheMutex_);
    
    auto it = cache_.begin();
    size_t evicted = 0;
    
    while (it != cache_.end()) {
        if (now - it->second->lastAccessTime > evictionThreshold) {
            it = cache_.erase(it);
            evicted++;
        } else {
            ++it;
        }
    }
    
    if (evicted > 0) {
        LOG_INFO("Evicted {} stale cache entries", evicted);
    }
}

void OrderBookCacheService::evictLRUEntries() {
    // Remove 10% of entries (oldest access time)
    size_t toRemove = cache_.size() / 10;
    if (toRemove == 0) toRemove = 1;
    
    std::vector<std::pair<uint64_t, std::string>> accessTimes;
    accessTimes.reserve(cache_.size());
    
    for (const auto& [key, entry] : cache_) {
        accessTimes.emplace_back(entry->lastAccessTime.load(), key);
    }
    
    // Sort by access time (oldest first)
    std::sort(accessTimes.begin(), accessTimes.end());
    
    // Remove oldest entries
    for (size_t i = 0; i < toRemove && i < accessTimes.size(); ++i) {
        cache_.erase(accessTimes[i].second);
    }
}

std::shared_ptr<data_ingestion::OrderBook> 
OrderBookCacheService::optimizeOrderBookMemory(
    std::shared_ptr<data_ingestion::OrderBook> orderBook) {
    
    // Limit depth to reduce memory usage
    auto snapshot = orderBook->getSnapshot(config_.maxOrderBookDepth);
    
    // Create new orderbook with limited depth
    auto optimized = std::make_shared<data_ingestion::OrderBook>(
        orderBook->getExchangeId(), orderBook->getSymbol());
   
    optimized->applySnapshot(
        snapshot.bids, snapshot.asks,
        snapshot.sequenceNumber, snapshot.receivedTimestamp
    );
    
    return optimized;
}

std::string OrderBookCacheService::createKey(const std::string& exchangeId, 
                                           const core::Symbol& symbol) const {
    return exchangeId + ":" + symbol;
}

OrderBookCacheService::Metrics OrderBookCacheService::getMetrics() const {
    std::shared_lock<std::shared_mutex> lock(cacheMutex_);
    
    size_t total = totalRequests_.load();
    size_t hits = cacheHits_.load();
    
    return {
        .cacheSize = cache_.size(),
        .totalRequests = total,
        .cacheHits = hits,
        .cacheMisses = cacheMisses_.load(),
        .hitRatio = total > 0 ? static_cast<double>(hits) / total : 0.0,
        .memoryUsageMB = estimateMemoryUsage(),
        .publishQueueDepth = [this]() {
            std::lock_guard<std::mutex> lock(publishMutex_);
            return publishQueue_.size();
        }()
    };
}

size_t OrderBookCacheService::estimateMemoryUsage() const {
    // Rough estimation: 100KB per orderbook + overhead
    size_t estimatedBytes = cache_.size() * (100 * 1024 + sizeof(CacheEntry) + 100);
    return estimatedBytes / (1024 * 1024); // Convert to MB
}

std::string OrderBookCacheService::createOrderBookMessage(
    const data_ingestion::OrderBook& orderBook) const {
    
    // Get snapshot with limited depth for efficiency - use int to match OrderBook API
    auto snapshot = orderBook.getSnapshot(config_.maxOrderBookDepth);
    
    // Create a simple JSON-like message (you should use protobuf in production)
    std::string message = "{";
    message += "\"exchange\":\"" + snapshot.exchangeId.toString() + "\",";
    message += "\"tradingPair\":\"" + snapshot.tradingPair.toString() + "\",";
    message += "\"timestamp\":" + std::to_string(core::timestampToMillis(snapshot.exchangeTimestamp)) + ",";
    message += "\"sequence\":" + std::to_string(snapshot.sequenceNumber) + ",";
    // message += "\"isSnapshot\":" + (snapshot.isSnapshot ? "true" : "false") + ",";
    message += std::string("\"isSnapshot\":") 
        + (snapshot.isSnapshot ? "true" : "false") 
        + ",";

    // Add bids
    message += "\"bids\":[";
    for (size_t i = 0; i < snapshot.bids.size(); ++i) {
        if (i > 0) message += ",";
        message += "[" + std::to_string(snapshot.bids[i].price) + "," + 
                   std::to_string(snapshot.bids[i].quantity) + "]";
    }
    message += "],";
    
    // Add asks
    message += "\"asks\":[";
    for (size_t i = 0; i < snapshot.asks.size(); ++i) {
        if (i > 0) message += ",";
        message += "[" + std::to_string(snapshot.asks[i].price) + "," + 
                   std::to_string(snapshot.asks[i].quantity) + "]";
    }
    message += "],";
    
    // Add checksum for validation
    message += "\"checksum\":\"" + snapshot.checksum + "\"";
    message += "}";
    
    return message;
}

bool OrderBookCacheService::isHealthy() const {
    auto metrics = getMetrics();
    
    // Health checks
    bool queueHealthy = metrics.publishQueueDepth < config_.publishQueueSize * 0.9;
    bool memoryHealthy = metrics.memoryUsageMB < 500; // 500MB threshold
    bool publisherHealthy = publisher_ && publisher_->isConnected();
    
    return queueHealthy && memoryHealthy && publisherHealthy;
}

} // namespace common
} // namespace trading