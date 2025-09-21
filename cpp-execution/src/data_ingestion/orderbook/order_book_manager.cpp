#include "data_ingestion/orderbook/order_book.hpp"
/**
 * @file order_book_manager.cpp
 * @brief Implementation of OrderBookManager
 * 
 * @details 
 * High-performance implementation with focus on low latency and thread safety.
 * Optimized for semi-HFT requirements with minimal locking overhead.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */

#include "data_ingestion/orderbook/order_book_manager.hpp"
#include "common/logger.hpp"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace trading {
namespace data_ingestion {

OrderBookManager::OrderBookManager(
    const OrderBookManagerConfig& config,
    OrderBookManagerCallback callback
) : config_(config), 
    managerCallback_(std::move(callback)),
    lastSecondReset_(std::chrono::system_clock::now()) {
    
    if (config_.enableStatistics) {
        // Reserve space for expected number of order books
        orderBooks_.reserve(config_.maxOrderBooks);
    }
    
    if (config_.logErrors) {
        // Log manager initialization
        // Logger::info("OrderBookManager initialized with maxOrderBooks=" + 
        //              std::to_string(config_.maxOrderBooks));
    }
}

OrderBookManager::~OrderBookManager() {
    cleanup();
}
// setConfig
void OrderBookManager::setConfig(const OrderBookManagerConfig& config) {
    std::unique_lock<std::shared_mutex> writeLock(orderBooksMutex_);
    config_ = config;
}
bool OrderBookManager::createOrderBook(
    const ExchangeId& exchangeId, 
    const core::Symbol& symbol,
    OrderBookCallback callback
) {
    auto key = createKey(exchangeId, symbol);
    
    // Check if order book already exists (read lock first for performance)
    {
        std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
        if (orderBooks_.find(key) != orderBooks_.end()) {
            return false; // Already exists
        }
    }
    
    // Check capacity limits
    if (orderBooks_.size() >= config_.maxOrderBooks) {
        logError("Cannot create order book: maximum capacity reached (" + 
                 std::to_string(config_.maxOrderBooks) + ")");
        return false;
    }
    
    // Create new order book with exclusive lock
    std::unique_lock<std::shared_mutex> writeLock(orderBooksMutex_);
    
    // Double-check after acquiring write lock
    if (orderBooks_.find(key) != orderBooks_.end()) {
        return false;
    }
    
    try {
        // Configure for VPS constraints
        OrderBookConfig config;
        config.max_depth = 500;           // Giảm xuống 500 cho VPS 2GB
        config.max_delta_history = 30;    // Giảm xuống 30
        config.cleanup_threshold = 600;   // Cleanup sớm hơn
        config.enable_auto_cleanup = true;
        config.cleanup_interval = std::chrono::minutes(3); // Cleanup thường xuyên hơn
        config.track_orders = false;      // Không theo dõi đơn hàng cá nhân
        auto orderBook = std::make_shared<OrderBook>(exchangeId, symbol, callback, config);
        orderBooks_[key] = orderBook;
        
        // Update statistics
        ++totalOrderBooksCreated_;
        
        // Notify callback
        notifyManagerEvent(key, "created");
        
        return true;
        
    } catch (const std::exception& e) {
        logError("Failed to create order book for " + key.toString() + ": " + e.what());
        ++totalErrors_;
        return false;
    }
}

bool OrderBookManager::removeOrderBook(const ExchangeId& exchangeId, const core::Symbol& symbol) {
    auto key = createKey(exchangeId, symbol);
    
    std::unique_lock<std::shared_mutex> writeLock(orderBooksMutex_);
    auto it = orderBooks_.find(key);
    if (it == orderBooks_.end()) {
        return false;
    }
    
    orderBooks_.erase(it);
    ++totalOrderBooksRemoved_;
    
    notifyManagerEvent(key, "removed");
    return true;
}

bool OrderBookManager::hasOrderBook(const ExchangeId& exchangeId, const core::Symbol& symbol) const {
    auto key = createKey(exchangeId, symbol);
    std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
    return orderBooks_.find(key) != orderBooks_.end();
}

bool OrderBookManager::processUpdate(const OrderBookUpdate& update) {
    auto startTime = std::chrono::high_resolution_clock::now();
    
    // Validate update
    if (!validateUpdate(update)) {
        ++invalidUpdates_;
        ++totalErrors_;
        return false;
    }
    
    auto key = createKey(update);
    std::shared_ptr<OrderBook> orderBook;
    // std::cout << "Processing update for key: " << key.toString() << std::endl;
    // Fast path: try to get existing order book with read lock
    {
        std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
        auto it = orderBooks_.find(key);
        if (it != orderBooks_.end()) {
            orderBook = it->second;
        }
    }
    
    // If order book doesn't exist and auto-creation is enabled
    if (!orderBook && config_.autoCreateOrderBooks) {
        if (createOrderBook(update.exchangeId, update.tradingPair)) {
            // Try again to get the newly created order book
            std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
            auto it = orderBooks_.find(key);
            if (it != orderBooks_.end()) {
                orderBook = it->second;
            }
        }
    }
    
    if (!orderBook) {
        logError("Order book not found for " + key.toString() + " and auto-creation disabled");
        ++totalErrors_;
        return false;
    }
    
    // Apply update to order book
    bool success = false;
    try {
        auto timestamp = update.receivedTimestamp;
        if (update.isSnapshot) {
            success = orderBook->applySnapshot(
                update.bids, 
                update.asks, 
                update.sequenceNumber, 
                timestamp
            );
        } else {
            success = orderBook->applyUpdate(
                update.bids, 
                update.asks, 
                update.sequenceNumber, 
                timestamp
            );
        }
        
        if (!success && config_.validateSequenceNumbers) {
            ++sequenceErrors_;
            logError("Sequence validation failed for " + key.toString());
        } else if (!success) {
            return false;
            // Non-sequence related failure
            // logError("Update processing failed for " + key.toString());
        }
        
    } catch (const std::exception& e) {
        logError("Exception processing update for " + key.toString() + ": " + e.what());
        ++totalErrors_;
        return false;
    }
    
    // Update performance statistics
    auto endTime = std::chrono::high_resolution_clock::now();
    auto latencyMs = std::chrono::duration<double, std::milli>(endTime - startTime).count();
    updateStatistics(latencyMs);
    
    ++totalUpdatesProcessed_;
    return success;
}

std::shared_ptr<OrderBook> OrderBookManager::getOrderBook(
    const ExchangeId& exchangeId, 
    const core::Symbol& symbol
) const {
    auto key = createKey(exchangeId, symbol);
    std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
    
    auto it = orderBooks_.find(key);
    return (it != orderBooks_.end()) ? it->second : nullptr;
}

std::optional<OrderBookUpdate> OrderBookManager::getSnapshot(
    const ExchangeId& exchangeId, 
    const core::Symbol& symbol, 
    int depth
) const {
    auto orderBook = getOrderBook(exchangeId, symbol);
    if (!orderBook || !orderBook->isValid()) {
        return std::nullopt;
    }
    
    ++totalSnapshotsGenerated_;
    return orderBook->getSnapshot(depth);
}

std::vector<OrderBookKey> OrderBookManager::getActiveOrderBooks() const {
    std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
    
    std::vector<OrderBookKey> keys;
    keys.reserve(orderBooks_.size());
    
    for (const auto& [key, orderBook] : orderBooks_) {
        if (orderBook && orderBook->isValid()) {
            keys.push_back(key);
        }
    }
    
    return keys;
}

std::vector<core::Symbol> OrderBookManager::getOrderBooksForExchange(const ExchangeId& exchangeId) const {
    std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
    
    std::vector<core::Symbol> symbols;
    for (const auto& [key, orderBook] : orderBooks_) {
        if (key.exchangeId == exchangeId && orderBook && orderBook->isValid()) {
            symbols.push_back(key.symbol);
        }
    }
    
    return symbols;
}

std::vector<ExchangeId> OrderBookManager::getExchangesForSymbol(const core::Symbol& symbol) const {
    std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
    
    std::vector<ExchangeId> exchanges;
    for (const auto& [key, orderBook] : orderBooks_) {
        if (key.symbol == symbol && orderBook && orderBook->isValid()) {
            exchanges.push_back(key.exchangeId);
        }
    }
    
    return exchanges;
}

size_t OrderBookManager::getOrderBookCount() const {
    std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
    return orderBooks_.size();
}

bool OrderBookManager::isHealthy() const {
    std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
    
    for (const auto& [key, orderBook] : orderBooks_) {
        if (!orderBook || !orderBook->isValid()) {
            return false;
        }
    }
    
    return true;
}

OrderBookManagerStats OrderBookManager::getStatistics() const {
    OrderBookManagerStats stats;
    
    // Atomic statistics (lock-free)
    stats.totalOrderBooks = orderBooks_.size();
    stats.totalUpdatesProcessed = totalUpdatesProcessed_.load();
    stats.totalErrors = totalErrors_.load();
    stats.totalSnapshotsGenerated = totalSnapshotsGenerated_.load();
    stats.totalOrderBooksCreated = totalOrderBooksCreated_.load();
    stats.totalOrderBooksRemoved = totalOrderBooksRemoved_.load();
    stats.sequenceErrors = sequenceErrors_.load();
    stats.invalidUpdates = invalidUpdates_.load();
    stats.lastUpdateTime = std::chrono::system_clock::now();
    
    // Performance metrics (requires brief lock)
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        stats.averageUpdateLatencyMs = (totalUpdatesProcessed_ > 0) ? 
            totalUpdateLatencyMs_.load() / totalUpdatesProcessed_.load() : 0.0;
        stats.maxUpdateLatencyMs = maxUpdateLatencyMs_.load();
        stats.updatesPerSecond = updatesInLastSecond_.load();
    }
    
    return stats;
}

std::map<std::string, uint64_t> OrderBookManager::getSimpleStatistics() const {
    std::map<std::string, uint64_t> stats;
    stats["total_order_books"] = orderBooks_.size();
    stats["total_updates_processed"] = totalUpdatesProcessed_.load();
    stats["total_errors"] = totalErrors_.load();
    stats["total_snapshots_generated"] = totalSnapshotsGenerated_.load();
    stats["sequence_errors"] = sequenceErrors_.load();
    stats["invalid_updates"] = invalidUpdates_.load();
    stats["updates_per_second"] = updatesInLastSecond_.load();
    return stats;
}

void OrderBookManager::resetStatistics() {
    totalUpdatesProcessed_ = 0;
    totalErrors_ = 0;
    totalSnapshotsGenerated_ = 0;
    totalOrderBooksCreated_ = 0;
    totalOrderBooksRemoved_ = 0;
    sequenceErrors_ = 0;
    invalidUpdates_ = 0;
    
    std::lock_guard<std::mutex> lock(statsMutex_);
    totalUpdateLatencyMs_ = 0.0;
    maxUpdateLatencyMs_ = 0.0;
    updatesInLastSecond_ = 0;
    lastSecondReset_ = std::chrono::system_clock::now();
}

void OrderBookManager::updateConfig(const OrderBookManagerConfig& config) {
    std::unique_lock<std::shared_mutex> writeLock(orderBooksMutex_);
    config_ = config;
}

OrderBookManagerConfig OrderBookManager::getConfig() const {
    std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
    return config_;
}

size_t OrderBookManager::removeInactiveOrderBooks() {
    if (!config_.autoRemoveInactive) {
        return 0;
    }
    
    std::vector<OrderBookKey> toRemove;
    
    // Identify inactive order books
    {
        std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
        for (const auto& [key, orderBook] : orderBooks_) {
            if (isOrderBookInactive(orderBook)) {
                toRemove.push_back(key);
            }
        }
    }
    
    // Remove identified order books
    if (!toRemove.empty()) {
        std::unique_lock<std::shared_mutex> writeLock(orderBooksMutex_);
        for (const auto& key : toRemove) {
            removeOrderBookInternal(key);
        }
    }
    
    return toRemove.size();
}

void OrderBookManager::cleanup() {
    std::unique_lock<std::shared_mutex> writeLock(orderBooksMutex_);

    // Break any external strong references the manager might hold in callbacks
    // This helps avoid retention cycles where callbacks capture the manager.
    managerCallback_ = {};

    // Clear order books to release shared_ptr references to OrderBook instances
    orderBooks_.clear();

    // Note: intentionally not swapping with a temporary unordered_map here because
    // constructing a temporary may require default-constructible hashing/equality
    // for OrderBookKey in some build configurations. If you need to force-free
    // buckets, consider providing a dedicated clearAndShrink() helper that uses
    // an appropriately constructed empty map or a custom allocator.
    orderBooks_.rehash(0);

    if (config_.logErrors) {
        // Logger::info("OrderBookManager cleanup completed");
    }
}

void OrderBookManager::setManagerCallback(OrderBookManagerCallback callback) {
    managerCallback_ = std::move(callback);
}

std::pair<double, double> OrderBookManager::getBestPricesAcrossExchanges(const core::Symbol& symbol) const {
    std::shared_lock<std::shared_mutex> readLock(orderBooksMutex_);
    
    double bestBid = 0.0;
    double bestAsk = std::numeric_limits<double>::max();
    bool foundAny = false;
    
    for (const auto& [key, orderBook] : orderBooks_) {
        if (key.symbol == symbol && orderBook && orderBook->isValid()) {
            auto bid = orderBook->getBestBid();
            auto ask = orderBook->getBestAsk();
            
            if (bid.price > bestBid) {
                bestBid = bid.price;
            }
            if (ask.price < bestAsk) {
                bestAsk = ask.price;
            }
            foundAny = true;
        }
    }
    
    if (!foundAny) {
        return {0.0, 0.0};
    }
    
    return {bestBid, (bestAsk == std::numeric_limits<double>::max()) ? 0.0 : bestAsk};
}

double OrderBookManager::getMidPrice(const ExchangeId& exchangeId, const core::Symbol& symbol) const {
    auto orderBook = getOrderBook(exchangeId, symbol);
    return (orderBook && orderBook->isValid()) ? orderBook->getMidPrice() : 0.0;
}

// Private helper methods

OrderBookKey OrderBookManager::createKey(const ExchangeId& exchangeId, const core::Symbol& symbol) const {
    return OrderBookKey{exchangeId, symbol};
}

OrderBookKey OrderBookManager::createKey(const OrderBookUpdate& update) const {
    return OrderBookKey{update.exchangeId, update.tradingPair};
}

void OrderBookManager::updateStatistics(double latencyMs) const {
    if (!config_.enableStatistics) {
        return;
    }
    
    // Update latency statistics (atomic operations)
    totalUpdateLatencyMs_ += latencyMs;
    
    // Update max latency if needed  
    double currentMax = maxUpdateLatencyMs_.load();
    while (latencyMs > currentMax &&
           !maxUpdateLatencyMs_.compare_exchange_weak(currentMax, latencyMs)) {
        // Keep trying until we successfully update or latencyMs is no longer the max
    }
    
    // 🔧 FIX: TẤT CẢ LOGIC COUNTER PHẢI TRONG LOCK
    auto now = std::chrono::system_clock::now();
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        
        auto timeSinceReset = std::chrono::duration_cast<std::chrono::seconds>(now - lastSecondReset_).count();
        
        if (timeSinceReset >= 1) {
            // Reset counter if more than 1 second has passed
            updatesInLastSecond_ = 1;
            lastSecondReset_ = now;
        } else {
            ++updatesInLastSecond_;
        }
    }
}

void OrderBookManager::notifyManagerEvent(const OrderBookKey& key, const std::string& eventType) {
    if (managerCallback_) {
        try {
            managerCallback_(key, eventType);
        } catch (const std::exception& e) {
            logError("Manager callback exception for " + key.toString() + ": " + e.what());
        }
    }
}

bool OrderBookManager::validateUpdate(const OrderBookUpdate& update) const {
    // Basic validation checks
    if (update.tradingPair.toString().empty()) {
        logError("Invalid update: empty trading pair");
        return false;
    }
    
    if (!update.exchangeId.isValid()) {
        logError("Invalid update: invalid exchange ID");
        return false;
    }
    
    // Note: timestamp validation removed as OrderBookUpdate doesn't have timestamp member
    // Using current time for timestamp in applySnapshot/applyUpdate methods
    
    // Validate price levels
    for (const auto& bid : update.bids) {
        if (bid.price <= 0.0 || bid.quantity < 0.0) {
            logError("Invalid bid price level in update for " + update.tradingPair.toString());
            return false;
        }
    }
    
    for (const auto& ask : update.asks) {
        if (ask.price <= 0.0 || ask.quantity < 0.0) {
            logError("Invalid ask price level in update for " + update.tradingPair.toString());
            return false;
        }
    }
    
    return true;
}

void OrderBookManager::logError(const std::string& message) const {
    if (config_.logErrors) {
        // Logger::error("[OrderBookManager] " + message);
        std::cerr << "[OrderBookManager ERROR] " << message << std::endl;
    }
}

bool OrderBookManager::isOrderBookInactive(const std::shared_ptr<OrderBook>& orderBook) const {
    if (!orderBook) {
        return true;
    }
    
    auto now = std::chrono::system_clock::now();
    auto lastUpdate = orderBook->getTimestamp();
    auto timeSinceUpdate = std::chrono::duration_cast<std::chrono::minutes>(now - lastUpdate);
    
    return timeSinceUpdate > config_.inactiveTimeout;
}

size_t OrderBookManager::limitOrderBooks(size_t maxCount) {
    if (orderBooks_.size() <= maxCount) return 0;

    std::unique_lock<std::shared_mutex> writeLock(orderBooksMutex_);
    
    // Sort by last update time, keep newest
    std::vector<std::pair<OrderBookKey, std::chrono::time_point<std::chrono::system_clock>>> items;
    for (const auto& [key, orderBook] : orderBooks_) {
        items.emplace_back(key, orderBook->getTimestamp());
    }
    
    std::sort(items.begin(), items.end(), 
              [](const auto& a, const auto& b) {
                  return a.second > b.second; // Newest first
              });
    std::cout << "Order books before limiting: " << orderBooks_.size() << ", after limiting to " << maxCount << std::endl;

    size_t removedCount = 0;
    // Keep only maxCount newest
    for (size_t i = maxCount; i < items.size(); ++i) {
        orderBooks_.erase(items[i].first);
        ++removedCount;
        ++totalOrderBooksRemoved_;
        notifyManagerEvent(items[i].first, "removed_oldest");
    }
    std::cout << "Order books after limiting: " << orderBooks_.size() << std::endl;
    return removedCount;
}

// // Hoặc limit theo số lượng (keep most recent)
// bool OrderBookManager::limitOrderBooks(size_t maxCount) {
//     if (orderBooks_.size() <= maxCount) return false;

//     std::unique_lock<std::shared_mutex> writeLock(orderBooksMutex_);
    
//     // Sort by last update time, keep newest
//     std::vector<std::pair<OrderBookKey, std::chrono::time_point<std::chrono::system_clock>>> items;
//     for (const auto& [key, orderBook] : orderBooks_) {
//         items.emplace_back(key, orderBook->getTimestamp());
//     }
    
//     std::sort(items.begin(), items.end(), 
//               [](const auto& a, const auto& b) {
//                   return a.second > b.second; // Newest first
//               });
    
//     // Keep only maxCount newest
//     for (size_t i = maxCount; i < items.size(); ++i) {
//         orderBooks_.erase(items[i].first);
//     }
// }

void OrderBookManager::removeOrderBookInternal(const OrderBookKey& key) {
    auto it = orderBooks_.find(key);
    if (it != orderBooks_.end()) {
        orderBooks_.erase(it);
        ++totalOrderBooksRemoved_;
        notifyManagerEvent(key, "removed_inactive");
    }
}
// Allow sequence gaps up to 1000 for high-frequency crypto data
const int64_t MAX_ALLOWED_SEQUENCE_GAP = 1000;
const int64_t CRYPTO_MAX_SEQUENCE_GAP = 1000;

bool OrderBookManager::validateSpread(const OrderBook& book) {
    auto bestBid = book.getBestBid();
    auto bestAsk = book.getBestAsk();
    
    if (bestBid.price > 0.0 && bestAsk.price > 0.0) {
        if (bestBid.price >= bestAsk.price) {
            // This should trigger a full resync, not just validation failure
            std::cerr << "CRITICAL: Bid-ask spread inversion detected. Triggering resync." << std::endl;
            return false;
        }
    }
    return true;
}
void OrderBookManager::createLiquidityFlowMonitor(
        const ExchangeId& exchangeId,
        const core::Symbol& symbol,
        std::shared_ptr<strategy::LiquidityFlowMonitor> monitor
    ) {
    // Create liquidity flow monitor and register with order book
    if (orderBooks_.count({exchangeId, symbol}) > 0) {
        orderBooks_[{exchangeId, symbol}]->setLiquidityFlowMonitor(monitor);
    }
}
// Factory implementations

namespace OrderBookManagerFactory {

std::unique_ptr<OrderBookManager> createHighFrequencyManager() {
    OrderBookManagerConfig config;
    config.autoCreateOrderBooks = true;
    config.autoRemoveInactive = false;  // Keep all order books for HFT
    config.maxOrderBooks = 2000;        // Higher capacity for HFT
    config.inactiveTimeout = std::chrono::hours{24}; // Longer timeout
    config.enableStatistics = true;
    config.validateSequenceNumbers = true;
    config.enableDeltaTracking = true;
    config.defaultDepth = 10;           // Shallower depth for speed
    config.logErrors = true;
    
    return std::make_unique<OrderBookManager>(config);
}

std::unique_ptr<OrderBookManager> createResearchManager() {
    OrderBookManagerConfig config;
    config.autoCreateOrderBooks = true;
    config.autoRemoveInactive = true;   // Cleanup for research
    config.maxOrderBooks = 500;         // Lower capacity for research
    config.inactiveTimeout = std::chrono::minutes{30}; // Shorter timeout
    config.enableStatistics = true;
    config.validateSequenceNumbers = false; // Flexible for research data
    config.enableDeltaTracking = true;
    config.defaultDepth = 50;           // Deeper depth for research
    config.logErrors = false;           // Less logging for research
    
    return std::make_unique<OrderBookManager>(config);
}

std::unique_ptr<OrderBookManager> createCustomManager(
    const OrderBookManagerConfig& config,
    OrderBookManagerCallback callback
) {
    return std::make_unique<OrderBookManager>(config, std::move(callback));
}

} // namespace OrderBookManagerFactory

} // namespace data_ingestion
} // namespace trading
