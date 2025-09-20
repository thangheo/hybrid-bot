/**
 * @file order_book_manager.hpp
 * @brief OrderBookManager for managing multiple order books across exchanges
 * 
 * @details 
 * High-performance manager for multiple OrderBook instances in the Data Ingestion
 * & Processing Layer. Provides thread-safe access and efficient routing of updates.
 * 
 * @author Trading System Team
 * @date May 12, 2025
 */
#pragma once

#include "data_ingestion/orderbook/order_book.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include "core/market_types.hpp"
#include "common/exchange_types.hpp"
#include <unordered_map>
#include <memory>
#include <shared_mutex>
#include <atomic>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <optional>

namespace trading {
namespace data_ingestion {

/**
 * Hash function for OrderBookKey to enable unordered_map usage
 */
// struct OrderBookKeyHash {
//     std::size_t operator()(const OrderBookKey& key) const {
//         auto h1 = std::hash<std::string>{}(key.exchangeId.toString());
//         auto h2 = std::hash<std::string>{}(key.symbol.toString());
//         return h1 ^ (h2 << 1);
//     }
// };

/**
 * Statistics for order book manager monitoring
 */
struct OrderBookManagerStats {
    uint64_t totalOrderBooks = 0;
    uint64_t totalUpdatesProcessed = 0;
    uint64_t totalErrors = 0;
    uint64_t totalSnapshotsGenerated = 0;
    uint64_t totalOrderBooksCreated = 0;
    uint64_t totalOrderBooksRemoved = 0;
    uint64_t sequenceErrors = 0;
    uint64_t invalidUpdates = 0;
    std::chrono::system_clock::time_point lastUpdateTime;
    
    // Performance metrics
    double averageUpdateLatencyMs = 0.0;
    double maxUpdateLatencyMs = 0.0;
    uint64_t updatesPerSecond = 0;
    
    OrderBookManagerStats() : lastUpdateTime(std::chrono::system_clock::now()) {}
};

/**
 * Configuration for OrderBookManager behavior
 */
struct OrderBookManagerConfig {
    bool autoCreateOrderBooks = true;           // Auto-create missing order books
    bool autoRemoveInactive = false;            // Auto-remove inactive order books
    size_t maxOrderBooks = 1000;                // Maximum number of order books
    std::chrono::minutes inactiveTimeout{60};   // Time before order book considered inactive
    bool enableStatistics = true;               // Enable performance statistics
    bool validateSequenceNumbers = false;       // Validate sequence numbers on updates (disabled for crypto)
    bool enableDeltaTracking = true;            // Enable delta tracking for order books
    int defaultDepth = 20;                      // Default depth for snapshots
    bool logErrors = true;                      // Log errors to system logger
    
    OrderBookManagerConfig() = default;
};

/**
 * Callback type for order book manager events
 */
using OrderBookManagerCallback = std::function<void(const OrderBookKey&, const std::string& eventType)>;

/**
 * High-performance manager for multiple OrderBook instances
 * 
 * The OrderBookManager is responsible for:
 * - Managing multiple OrderBook instances across exchanges and symbols
 * - Routing updates to the correct order books
 * - Providing thread-safe access to order book data
 * - Collecting statistics and monitoring health
 * - Managing order book lifecycle (creation/removal)
 * 
 * Thread Safety:
 * - Multiple threads can safely read order book data concurrently
 * - Updates are processed with minimal locking using shared_mutex
 * - Statistics are updated atomically
 * 
 * Performance Considerations:
 * - Fast O(1) lookup for order books using hash map
 * - Minimal memory copying during updates
 * - Lock-free statistics updates where possible
 * 
 * Example Usage:
 * @code
 * OrderBookManager manager;
 * 
 * // Create order books
 * manager.createOrderBook(ExchangeId::binance(), Symbol::fromString("BTC/USDT"));
 * 
 * // Process updates
 * OrderBookUpdate update = // ... get update from data source
 * manager.processUpdate(update);
 * 
 * // Query data
 * auto snapshot = manager.getSnapshot(ExchangeId::binance(), Symbol::fromString("BTC/USDT"));
 * auto orderBook = manager.getOrderBook(ExchangeId::binance(), Symbol::fromString("BTC/USDT"));
 * @endcode
 */
class OrderBookManager {
private:
    mutable std::mutex orderBookMutex_;
public:
    bool validateSpread(const OrderBook& book);
    /**
     * Constructor with optional configuration
     * @param config Configuration for manager behavior
     * @param callback Optional callback for manager events
     */
    explicit OrderBookManager(
        const OrderBookManagerConfig& config = OrderBookManagerConfig{},
        OrderBookManagerCallback callback = nullptr
    );
    //setConfig
    void setConfig(const OrderBookManagerConfig& config);
    /**
     * Destructor
     */
    ~OrderBookManager();
    
    // Non-copyable but movable
    OrderBookManager(const OrderBookManager&) = delete;
    OrderBookManager& operator=(const OrderBookManager&) = delete;
    OrderBookManager(OrderBookManager&&) = default;
    OrderBookManager& operator=(OrderBookManager&&) = default;
    
    /**
     * Create a new order book for the specified exchange and symbol
     * @param exchangeId Exchange identifier
     * @param symbol Trading pair symbol
     * @param callback Optional callback for order book updates
     * @return true if order book was created successfully, false if already exists
     */
    bool createOrderBook(
        const ExchangeId& exchangeId, 
        const core::Symbol& symbol,
        OrderBookCallback callback = nullptr
    );
    
    /**
     * Remove an order book
     * @param exchangeId Exchange identifier  
     * @param symbol Trading pair symbol
     * @return true if order book was removed, false if not found
     */
    bool removeOrderBook(const ExchangeId& exchangeId, const core::Symbol& symbol);
    
    /**
     * Check if an order book exists
     * @param exchangeId Exchange identifier
     * @param symbol Trading pair symbol
     * @return true if order book exists
     */
    bool hasOrderBook(const ExchangeId& exchangeId, const core::Symbol& symbol) const;
    
    /**
     * Process an order book update and route to the appropriate order book
     * @param update Order book update to process
     * @return true if update was processed successfully
     */
    bool processUpdate(const OrderBookUpdate& update);
    
    /**
     * Get a shared pointer to an order book
     * @param exchangeId Exchange identifier
     * @param symbol Trading pair symbol
     * @return Shared pointer to order book, or nullptr if not found
     */
    std::shared_ptr<OrderBook> getOrderBook(
        const ExchangeId& exchangeId, 
        const core::Symbol& symbol
    ) const;
    
    /**
     * Get a snapshot of an order book
     * @param exchangeId Exchange identifier
     * @param symbol Trading pair symbol
     * @param depth Order book depth (0 = all levels)
     * @return OrderBookUpdate snapshot, or empty if not found
     */
    std::optional<OrderBookUpdate> getSnapshot(
        const ExchangeId& exchangeId, 
        const core::Symbol& symbol, 
        int depth = 0
    ) const;
    
    /**
     * Get all active order book keys
     * @return Vector of exchange/symbol pairs for all active order books
     */
    std::vector<OrderBookKey> getActiveOrderBooks() const;
    
    /**
     * Get order books for a specific exchange
     * @param exchangeId Exchange to filter by
     * @return Vector of symbols for the specified exchange
     */
    std::vector<core::Symbol> getOrderBooksForExchange(const ExchangeId& exchangeId) const;
    
    /**
     * Get order books for a specific symbol across all exchanges
     * @param symbol Symbol to filter by
     * @return Vector of exchange IDs that have this symbol
     */
    std::vector<ExchangeId> getExchangesForSymbol(const core::Symbol& symbol) const;
    
    /**
     * Get the number of active order books
     * @return Number of order books currently managed
     */
    size_t getOrderBookCount() const;
    
    /**
     * Check if the manager is healthy (all order books are valid)
     * @return true if all order books are in a valid state
     */
    bool isHealthy() const;
    
    /**
     * Get comprehensive statistics
     * @return Statistics structure with performance and health metrics
     */
    OrderBookManagerStats getStatistics() const;
    
    /**
     * Get simplified statistics as key-value pairs
     * @return Map of statistic names to values
     */
    std::map<std::string, uint64_t> getSimpleStatistics() const;
    
    /**
     * Reset statistics counters
     */
    void resetStatistics();
    
    /**
     * Update configuration
     * @param config New configuration
     */
    void updateConfig(const OrderBookManagerConfig& config);
    
    /**
     * Get current configuration
     * @return Current configuration
     */
    OrderBookManagerConfig getConfig() const;
    
    /**
     * Remove all inactive order books based on configuration timeout
     * @return Number of order books removed
     */
    size_t removeInactiveOrderBooks();
    
    /**
     * Force cleanup of all order books (useful for shutdown)
     */
    void cleanup();
    
    /**
     * Set global callback for manager events
     * @param callback Callback function for events like creation/removal
     */
    void setManagerCallback(OrderBookManagerCallback callback);
    
    /**
     * Get best bid/ask prices across all exchanges for a symbol
     * @param symbol Symbol to query
     * @return Pair of (bestBid, bestAsk) across all exchanges
     */
    std::pair<double, double> getBestPricesAcrossExchanges(const core::Symbol& symbol) const;
    
    /**
     * Get mid price for a specific order book
     * @param exchangeId Exchange identifier
     * @param symbol Trading pair symbol
     * @return Mid price, or 0.0 if order book not found or invalid
     */
    double getMidPrice(const ExchangeId& exchangeId, const core::Symbol& symbol) const;
    /**
     * Limit the number of order books to a maximum count by removing oldest/inactive
     * @param maxCount Maximum number of order books to retain
     * @return Number of order books removed
     */
    size_t limitOrderBooks(size_t maxCount);
private:
    // Configuration
    OrderBookManagerConfig config_;
    OrderBookManagerCallback managerCallback_;
    
    // Order book storage
    std::unordered_map<OrderBookKey, std::shared_ptr<OrderBook>, OrderBookKeyHash> orderBooks_;
    
    // Thread synchronization
    mutable std::shared_mutex orderBooksMutex_;
    mutable std::mutex statsMutex_;
    
    // Statistics (atomic for lock-free updates)
    mutable std::atomic<uint64_t> totalUpdatesProcessed_{0};
    mutable std::atomic<uint64_t> totalErrors_{0};
    mutable std::atomic<uint64_t> totalSnapshotsGenerated_{0};
    mutable std::atomic<uint64_t> totalOrderBooksCreated_{0};
    mutable std::atomic<uint64_t> totalOrderBooksRemoved_{0};
    mutable std::atomic<uint64_t> sequenceErrors_{0};
    mutable std::atomic<uint64_t> invalidUpdates_{0};
    
    // Performance tracking
    // mutable std::atomic<double> totalUpdateLatencyMs_{0.0};
    // mutable std::atomic<double> maxUpdateLatencyMs_{0.0};
    // mutable std::atomic<uint64_t> updatesInLastSecond_{0};
    // mutable std::chrono::system_clock::time_point lastSecondReset_;
    mutable std::atomic<uint64_t> totalUpdateLatencyMs_{0};
    mutable std::atomic<double> maxUpdateLatencyMs_{0.0};
    mutable std::atomic<uint32_t> updatesInLastSecond_{0};
    mutable std::chrono::system_clock::time_point lastSecondReset_;
    // mutable std::mutex statsMutex_;


    // Helper methods
    OrderBookKey createKey(const ExchangeId& exchangeId, const core::Symbol& symbol) const;
    OrderBookKey createKey(const OrderBookUpdate& update) const;
    void updateStatistics(double latencyMs) const;
    void notifyManagerEvent(const OrderBookKey& key, const std::string& eventType);
    bool validateUpdate(const OrderBookUpdate& update) const;
    void logError(const std::string& message) const;
    
    // Cleanup helpers
    bool isOrderBookInactive(const std::shared_ptr<OrderBook>& orderBook) const;
    void removeOrderBookInternal(const OrderBookKey& key);
};

/**
 * Factory function for creating OrderBookManager with common configurations
 */
namespace OrderBookManagerFactory {
    /**
     * Create manager optimized for high-frequency trading
     * @return OrderBookManager configured for HFT
     */
    std::unique_ptr<OrderBookManager> createHighFrequencyManager();
    
    /**
     * Create manager optimized for research and backtesting
     * @return OrderBookManager configured for research
     */
    std::unique_ptr<OrderBookManager> createResearchManager();
    
    /**
     * Create manager with custom configuration
     * @param config Custom configuration
     * @param callback Optional callback
     * @return OrderBookManager with specified configuration
     */
    std::unique_ptr<OrderBookManager> createCustomManager(
        const OrderBookManagerConfig& config,
        OrderBookManagerCallback callback = nullptr
    );
}

} // namespace data_ingestion
} // namespace trading