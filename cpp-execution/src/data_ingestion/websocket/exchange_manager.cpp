// DEADLOCK-FREE exchange_manager.cpp - ULTIMATE FIX
#include "data_ingestion/exchange_manager.hpp"
#include "execution/rate_limit_controller.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <functional>
#include "common/logger.hpp"

#include <fstream>
#include <sys/types.h>
#include <unistd.h>
#include <atomic>
#include <malloc.h>

// #include "iostream"
// using namespace trading::common;
#define LOG_DEBUG(msg) Logger::getLogger()->debug(msg)
namespace trading {
namespace data_ingestion {

ExchangeManager::ExchangeManager(const core::ExchangeId& exchangeId,
                            std::shared_ptr<execution::RateLimitController> rateLimitController,
                            NormalizedOrderBookCallback normalizedCallback)
    : exchangeId_(exchangeId),
    rateLimitController_(std::move(rateLimitController)),
    normalizedCallback_(std::move(normalizedCallback)),
    minConnections_(3),
    maxConnections_(8) {
    adapter_ = std::make_unique<BinanceAdapter>();
    // Setup Binance rate limits
    if (rateLimitController_ && exchangeId == core::ExchangeId::binance()) {
        std::unordered_map<execution::ExchangeRequestType, execution::RateLimitRule> rules = {
            {execution::ExchangeRequestType::CONNECTION, {3, std::chrono::seconds(1)}},
            {execution::ExchangeRequestType::SUBSCRIPTION, {21, std::chrono::seconds(1)}}
        };
        rateLimitController_->setRateLimitRules(rules);
    }
   // Create the normalizer with the callback
    normalizer_ = std::make_unique<OrderBookNormalizer>(normalizedCallback_);
    
    // Create WebSocket callback that properly forwards to normalizer
    // websocketCallback_ = [this](const core::ExchangeId& exchange,
    //                            const core::Symbol& symbol,
    //                            const std::vector<uint8_t>& data) {
    //     this->onWebSocketData(exchange, symbol, data);
    // };
    //use raw data callback
    websocketCallback_ = [this](const RawMarketData& rawData) {
        this->onWebSocketRawData(rawData);
    };
    
    std::cout << "ExchangeManager initialized with " << minConnections_
              << "-" << maxConnections_ << " connections\n";
}

ExchangeManager::ExchangeManager(NormalizedOrderBookCallback callback,
                                int minConnections,
                                int maxConnections)
    : normalizedCallback_(std::move(callback)),
      minConnections_(minConnections),
      maxConnections_(maxConnections) {
    
    // Create the normalizer with the callback
    normalizer_ = std::make_unique<OrderBookNormalizer>(normalizedCallback_);
    
    // Create WebSocket callback that properly forwards to normalizer
    // websocketCallback_ = [this](const core::ExchangeId& exchange,
    //                            const core::Symbol& symbol,
    //                            const std::vector<uint8_t>& data) {
    //     this->onWebSocketData(exchange, symbol, data);
    // };
    //use raw data callback
    websocketCallback_ = [this](const RawMarketData& rawData) {
        this->onWebSocketRawData(rawData);
    };

    std::cout << "ExchangeManager initialized with " << minConnections_
              << "-" << maxConnections_ << " connections\n";
}

inline std::string ExchangeManager::toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

ExchangeManager::~ExchangeManager() {
    // Stop the monitor thread first
    running_.store(false);
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
     if (running_.exchange(false)) {
        if (processingThread_.joinable()) {
            processingThread_.join();
        }
    }
    // Then disconnect
    disconnect();
    
    std::cout << "ExchangeManager shutdown complete\n";
}

bool ExchangeManager::initialize() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    try {
        // Create the connection pool for Binance
        connectionPool_ = std::make_unique<ConnectionPool>(
            "binance",
            [this]() -> ClientInterfacePtr {
                auto client = std::make_shared<BinanceWebSocketClient>(
                    "stream.binance.com",
                    "9443");
                client->setRawDataCallback(websocketCallback_);
                // client->setDataCallback(
                //     [this](const ExchangeId& exchange,
                //            const Symbol& symbol,
                //            const std::vector<uint8_t>& data) {
                //         // Forward the actual data to onWebSocketData
                //         core::Symbol coreSymbol(symbol);
                //         this->onWebSocketData(exchange, coreSymbol, data);
                //     }
                // );
                // If inheritance is correct, static_pointer_cast is valid
                return std::static_pointer_cast<ClientInterface>(client);
            },
            minConnections_,
            maxConnections_
        );
        
        // Initialize the connection pool
        if (!connectionPool_->initialize()) {
            std::cerr << "Failed to initialize connection pool\n";
            return false;
        }
        
        // Start the connection monitor thread
        running_.store(true);
        monitorThread_ = std::thread(&ExchangeManager::monitorConnections, this);
        
        processingThread_ = std::thread(&ExchangeManager::processingWorker, this);
        
        std::cout << "Binance connection pool initialized successfully\n";
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error initializing connection pool: " << e.what() << '\n';
        return false;
    }
}
 
void ExchangeManager::onWebSocketRawData(const RawMarketData& rawData) {
    // If memory pressure is active, drop data early to avoid OOM
    if (memoryPressure_.load()) {
        std::cerr << "[ExchangeManager] memory pressure active, dropping incoming data\n";
        // Track drops for observability
        static std::atomic<size_t> dropCount{0};
        dropCount.fetch_add(1, std::memory_order_relaxed);
        if ((dropCount.load() % 100) == 0) {
            std::cerr << "[ExchangeManager] dropped_count=" << dropCount.load() << '\n';
        }
        return;
    }

    // Protect enqueue with a mutex so we can safely swap/destroy the queue
    // instance in clearProcessingQueue without races.
    {
        std::lock_guard<std::mutex> lock(enqueueMutex_);
        auto qptr = processingQueuePtr_.load(std::memory_order_acquire);
        if (!qptr) {
            std::cerr << "Processing queue not available, dropping data\n";
            return;
        }
        if (!qptr->enqueue(rawData)) {
            std::cerr << "Processing queue full, dropping data\n";
        } else {
            processingQueueSize_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}


bool ExchangeManager::subscribe(const std::vector<Symbol>& tradingPairs,
                               const std::vector<DataType>& dataTypes,
                               int depth) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (!connectionPool_ || !isConnected_.load()) {
        std::cerr << "Not connected. Call connect() first.\n";
        return false;
    }
    
    try {
        // 🔥 RATE LIMIT SUBSCRIPTION REQUESTS
        if (rateLimitController_) {
            if (!rateLimitController_->waitForPermission(execution::ExchangeRequestType::SUBSCRIPTION)) {
                std::cerr << "Rate limited: subscription request blocked\n";
                return false;
            }
            else {
                std::cout << "Subscription request permitted by rate limiter\n";
            }
        }
        
        // Acquire a connection from the pool  
        auto connection = connectionPool_->acquireConnection();
        if (!connection) {
            std::cerr << "Failed to acquire connection for subscription\n";
            return false;
        }
        
        // Convert trading pairs to Symbol objects
        std::vector<core::Symbol> symbols;
        symbols.reserve(tradingPairs.size());
        for (const auto& pair : tradingPairs) {
            auto symbol = pair;
            symbols.emplace_back(symbol);
        }
        
        // Subscribe using the connection
        bool success = connection->subscribe(symbols);
        
        // Release the connection back to the pool
        connectionPool_->releaseConnection(connection);
        
        if (success) {
            // Tell the connection pool about active subscriptions
            connectionPool_->setActiveSubscriptions(symbols);
            
            // Store the subscription request
            SubscriptionRequest request{};
            request.exchangeId = exchangeId_ == core::ExchangeId() ? core::ExchangeId::binance() : exchangeId_; //good default
            request.tradingPairs = tradingPairs;
            request.dataTypes = dataTypes;
            request.depth = depth;
            activeSubscriptions_.push_back(std::move(request));
            
            std::cout << "Successfully subscribed to " << tradingPairs.size()
                     << " trading pairs\n";
        } else {
            std::cerr << "Failed to subscribe to trading pairs\n";
        }
        
        return success;
    } catch (const std::exception& e) {
        std::cerr << "Error during subscription: " << e.what() << '\n';
        return false;
    }
}

bool ExchangeManager::connect() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
     if (!rateLimitController_->waitForPermission(execution::ExchangeRequestType::CONNECTION)) {
        std::cerr << "Rate limited: connection attempt blocked\n";
        return false;
    }
    if (!connectionPool_) {
        std::cerr << "Connection pool not initialized. Call initialize() first.\n";
        return false;
    }
    
    try {
        bool success = connectionPool_->connectAll();
        isConnected_.store(success);
        
        if (success) {
            std::cout << "Successfully connected to Binance\n";
        } else {
            std::cerr << "Failed to connect to Binance\n";
        }
        
        return success;
        
    } catch (const std::exception& e) {
        std::cerr << "Error connecting: " << e.what() << '\n';
        return false;
    }
}

void ExchangeManager::disconnect() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (connectionPool_) {
        try {
            connectionPool_->shutdown();
            isConnected_.store(false);
            std::cout << "Disconnected from Binance\n";
           
        } catch (const std::exception& e) {
            std::cerr << "Error disconnecting: " << e.what() << '\n';
        }
    }
}

// ✅ ULTIMATE DEADLOCK FIX: Only use atomic flag - NO MUTEX, NO CONNECTION POOL CALLS
bool ExchangeManager::isConnected() const {
    // SIMPLE: Just return the atomic flag - no locks, no external calls
    return isConnected_.load();
}

OrderBookUpdate ExchangeManager::getOrderBook(const core::Symbol& tradingPair,
                                            int32_t depth) const {
    if (!normalizer_) {
        return OrderBookUpdate{}; // Return empty update if normalizer not available
    }
    
    return normalizer_->getOrderBook(core::ExchangeId::binance(), tradingPair, depth);
}

std::string ExchangeManager::getConnectionStats() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    std::ostringstream oss;
    
    if (connectionPool_) {
        oss << "Binance connection pool - Active: " 
            << connectionPool_->getActiveConnectionCount() 
            << ", Available: " << connectionPool_->getAvailableConnectionCount()
            << ", Total: " << connectionPool_->getTotalConnectionCount();
    } else {
        oss << "Connection pool not initialized";
    }
    
    return oss.str();
}

// ✅ DEADLOCK-FREE MONITOR: Update atomic flag based on pool health without mutex during checks
void ExchangeManager::monitorConnections() {
    std::cout << "Starting connection monitor thread\n";
    
    const auto checkInterval = std::chrono::seconds(30);
    
    while (running_.load()) {
        try {
            bool poolHealthy = false;
            
            // Get a safe reference to the pool without holding the main mutex
            std::unique_ptr<ConnectionPool>* poolRef = nullptr;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                if (connectionPool_) {
                    poolRef = &connectionPool_;
                }
            }
            
            // Check pool health without holding the main mutex
            if (poolRef && *poolRef) {
                poolHealthy = (*poolRef)->isHealthy();
            }
            
            // Update the atomic flag
            bool wasConnected = isConnected_.exchange(poolHealthy);
            
            // Log status changes
            if (poolHealthy != wasConnected) {
                if (poolHealthy) {
                    std::cout << "Connection to Binance restored\n";
                } else {
                    std::cout << "Connection to Binance lost\n";
                }
            }
            
            // Periodic status logging (without holding mutex for long)
            if (poolHealthy) {
                std::string stats;
                {
                    std::lock_guard<std::recursive_mutex> lock(mutex_);
                    if (connectionPool_) {
                        std::ostringstream oss;
                        oss << "Pool status - Active: " 
                            << connectionPool_->getActiveConnectionCount() 
                            << ", Available: " << connectionPool_->getAvailableConnectionCount()
                            << ", Total: " << connectionPool_->getTotalConnectionCount();
                        stats = oss.str();
                    }
                }
                if (!stats.empty()) {
                    std::cout << stats << '\n';
                }
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error in connection monitor: " << e.what() << '\n';
        }
        
        std::this_thread::sleep_for(checkInterval);
    }
    
    std::cout << "Connection monitor thread stopped\n";
}

std::vector<std::string> ExchangeManager::createStreamNames(
    const std::vector<Symbol>& tradingPairs,
    const std::vector<DataType>& dataTypes,
    int depth) {
    
    std::vector<std::string> streams;
    streams.reserve(tradingPairs.size() * dataTypes.size());
    
    for (const auto& pair : tradingPairs) {
        // Use ONLY ONE way to format the pair - choose the simpler one:
        std::string formattedPair = toLowercase(pair.toString());
        std::cout << "Adding symbol: " << pair.toString() << " to stream as: " << formattedPair << std::endl;

        // Remove '/' separator if present
        formattedPair.erase(
            std::remove(formattedPair.begin(), formattedPair.end(), '/'),
            formattedPair.end());
        
        for (const auto& type : dataTypes) {
            switch (type) {
                case DataType::ORDER_BOOK:
                    std::cout << "Creating ORDER_BOOK stream for depth: " << depth << std::endl;
                    if (depth <= 10) {
                        streams.push_back(formattedPair + "@depth10@100ms");
                    } else if (depth <= 20) {
                        streams.push_back(formattedPair + "@depth20@100ms");
                    } else {
                        // streams.push_back(formattedPair + "@depth@100ms");
                        streams.push_back(formattedPair + "@depth" + std::to_string(depth) + "@100ms");

                    }
                    streams.push_back(formattedPair + "@bookTicker");
                    break;
                    
                case DataType::TRADES:
                    streams.push_back(formattedPair + "@trade");
                    break;
                    
                case DataType::TICKER:
                    streams.push_back(formattedPair + "@ticker");
                    break;
                    
                default:
                    std::cerr << "Unknown data type in stream creation\n";
                    break;
            }
        }
    }
    
    return streams;
}


size_t ExchangeManager::getRSSKB() {
    std::ifstream f("/proc/self/statm");
    size_t total_pages = 0, resident_pages = 0;
    if (!(f >> total_pages >> resident_pages)) return 0;
    long page = sysconf(_SC_PAGESIZE);
    return (resident_pages * page) / 1024;
}

void ExchangeManager::processingWorker() {
    constexpr size_t batchSize = 100; // Can be made configurable
    size_t processed = 0;
    size_t total_bytes = 0;
    std::vector<RawMarketData> batch;
    batch.reserve(batchSize);
    while (running_.load()) {
        batch.clear();
        // Try to dequeue up to batchSize items
        RawMarketData rawData;
        size_t dequeued = 0;
        // Load the queue instance atomically for dequeuing
    auto qptr = processingQueuePtr_.load(std::memory_order_acquire);
        while (dequeued < batchSize && qptr && qptr->try_dequeue(rawData)) {
            batch.push_back(std::move(rawData));
            ++dequeued;
        }
        if (!batch.empty()) {
            processingQueueSize_.fetch_sub(batch.size(), std::memory_order_relaxed);
            for (const auto& item : batch) {
                try {
                    ++processed;
                    total_bytes += item.rawData.size();
                    // Parse/normalize using adapter (existing behavior)
                    OrderBookUpdate normalized = adapter_->parseOrderBookData(item);
                    // Forward to normalizer
                    normalizer_->processOrderBookUpdate(normalized);
                    if (normalizedCallback_) {
                        normalizedCallback_(normalized);
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Parse error: " << e.what() << '\n';
                }
            }
            // Periodic logging (every 100*batchSize processed)
            if ((processed % (batchSize * 1)) == 0) {
                size_t rss = getRSSKB();
                std::cerr << "[ExchangeManager] processed=" << processed
                          << " total_bytes=" << total_bytes
                          << " RSS_KB=" << rss
                          << " queue_items=" << processingQueueSize_.load() << '\n';
                // If user configured a memory limit, take protective action
                if (memoryLimitKB_ > 0 && rss > memoryLimitKB_) {
                    std::cerr << "[ExchangeManager] RSS_KB=" << rss
                              << " exceeds limit=" << memoryLimitKB_
                              << ", entering memory pressure mode and clearing queue\n";
                    memoryPressure_.store(true);
                    // Best-effort clear of the queue to release memory
                    clearProcessingQueue();
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void ExchangeManager::clearProcessingQueue() {
    // Replace current queue with a fresh instance atomically. This allows the
    // old queue to be destroyed (and its internal buffers freed) once no more
    // threads hold shared_ptr references to it.
    // Acquire enqueue mutex to prevent producers from enqueuing while we swap
    // and destroy the old queue instance.
    std::lock_guard<std::mutex> lock(enqueueMutex_);
    auto newQueue = std::make_shared<ReaderWriterQueue<RawMarketData>>(DEFAULT_PROCESSING_QUEUE_CAP);
    auto oldQueue = processingQueuePtr_.exchange(newQueue, std::memory_order_acq_rel);

    // Drain the old queue to update counters and know how many items were removed
    RawMarketData tmp;
    size_t removed = 0;
    while (oldQueue && oldQueue->try_dequeue(tmp)) {
        ++removed;
        processingQueueSize_.fetch_sub(1, std::memory_order_relaxed);
    }
    std::cerr << "[ExchangeManager] cleared processing queue, removed=" << removed << '\n';
    // After clearing, attempt to release memory back to OS by hinting to allocator.
    // Use malloc_trim when available on glibc systems.
#if defined(__GLIBC__)
    // malloc_trim(0) returns non-zero if memory was actually released.
    int trimmed = malloc_trim(0);
    std::cerr << "[ExchangeManager] malloc_trim returned=" << trimmed << '\n';
#endif

    // Sleep briefly to allow OS to reclaim memory
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // If RSS has fallen below limit, resume normal operation.
    if (memoryLimitKB_ > 0 && getRSSKB() < memoryLimitKB_) {
        memoryPressure_.store(false);
        std::cerr << "[ExchangeManager] memory pressure cleared, resuming normal processing\n";
    } else {
        std::cerr << "[ExchangeManager] memory still high after clearing queue: RSS_KB=" << getRSSKB() << '\n';
    }
}

std::string ExchangeManager::generateRequestId() {
    return "req_" + std::to_string(requestIdCounter_++);
}

} // namespace data_ingestion
} // namespace trading