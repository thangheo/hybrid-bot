/**
 * @file exchange_manager.hpp
 * @brief Simplified ExchangeManager without event bus circular dependencies
 */

#pragma once
#include <data_ingestion/websocket/exchanges/exchange_connector.hpp>
#include "data_ingestion/types.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/normalization/order_book_normalizer.hpp"
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/protocol/binance_adapter.hpp"
#include "readerwriterqueue.h"
#include <iostream>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <future>
#include <unordered_map>

using RawDataCallback = std::function<void(const RawMarketData&)>;

using namespace moodycamel;
namespace trading {
namespace data_ingestion {

class ExchangeManager {
public:
    using NormalizedOrderBookCallback = std::function<void(const OrderBookUpdate&)>;
    using WebSocketDataCallback = std::function<void(const core::ExchangeId&, const std::string&, const std::vector<uint8_t>&)>;
    
     ExchangeManager(const core::ExchangeId& exchangeId,
                    std::shared_ptr<execution::RateLimitController> rateLimitController,
                    NormalizedOrderBookCallback normalizedCallback);

    ExchangeManager(NormalizedOrderBookCallback callback,
                   int minConnections = 3,
                   int maxConnections = 8);
    ~ExchangeManager();
   
    // Core functionality
    bool initialize();
    bool connect();
    void disconnect();
    bool isConnected() const;
    
    // SIMPLIFIED: Direct subscription without event bus
    bool subscribe(const std::vector<Symbol>& tradingPairs,
                  const std::vector<DataType>& dataTypes,
                  int depth = 20);
    
    // Query methods
    OrderBookUpdate getOrderBook(const core::Symbol& tradingPair, int32_t depth = 20) const;
    std::string getConnectionStats() const;

    // Configure memory limit in KB (0 disables the safeguard)
    void setMemoryLimitKB(size_t kb) { memoryLimitKB_ = kb; }

    // Thêm methods
    void onWebSocketRawData(const RawMarketData& rawData);
    void processingWorker();    
    //create function to set rate limit controller in case we dont use constructor but using factory method
    void setRateLimitController(std::shared_ptr<execution::RateLimitController> rateLimitController) {
        rateLimitController_ = rateLimitController;
    }
private:
    static size_t getRSSKB() ;

    std::unique_ptr<BinanceAdapter> adapter_;
    // Bounded concurrent queue for incoming raw market data. Default capacity
    // tuned to a large value to avoid unbounded growth; adjust as needed.
    static constexpr size_t DEFAULT_PROCESSING_QUEUE_CAP = 1000;
    // Use shared_ptr so we can atomically swap the queue instance to release
    // internal buffers when clearing. Producers/consumers should use
    // std::atomic_load/store on this shared_ptr to get a safe reference.
    // Use atomic shared_ptr to allow safe atomic load/exchange without deprecated
    // shared_ptr atomic helpers.
    std::atomic<std::shared_ptr<ReaderWriterQueue<RawMarketData>>> processingQueuePtr_
        { std::make_shared<ReaderWriterQueue<RawMarketData>>(DEFAULT_PROCESSING_QUEUE_CAP) };

    // Approximate size of the processing queue (maintained atomically by enqueue/dequeue sites)
    std::atomic<size_t> processingQueueSize_{0};

    // Memory-pressure protection: when RSS (KB) exceeds memoryLimitKB_, ExchangeManager
    // will start shedding load (drop incoming messages and clear queue). 0 == disabled.
    std::atomic<bool> memoryPressure_{false};
    size_t memoryLimitKB_{0};

    std::thread processingThread_;
    RawDataCallback websocketCallback_;
 
    std::mutex enqueueMutex_;  
    // rateLimitController_
    std::shared_ptr<execution::RateLimitController> rateLimitController_;
    core::ExchangeId exchangeId_;
    // Configuration
    int minConnections_;
    int maxConnections_;
    
    
    // Core components
    std::unique_ptr<ConnectionPool> connectionPool_;
    std::unique_ptr<OrderBookNormalizer> normalizer_;
    
    // Callbacks
    NormalizedOrderBookCallback normalizedCallback_;
    // WebSocketDataCallback websocketCallback_;
    // Connection management
    std::atomic<bool> isConnected_{false};
    std::atomic<bool> running_{false};
    std::thread monitorThread_;
    // mutable std::mutex mutex_;
    mutable std::recursive_mutex mutex_;
    // Subscription tracking
    struct SubscriptionRequest {
        core::ExchangeId exchangeId;
        std::vector<Symbol> tradingPairs;
        std::vector<DataType> dataTypes;
        int depth;
    };
    std::vector<SubscriptionRequest> activeSubscriptions_;
    
    // Request ID generation
    std::atomic<uint64_t> requestIdCounter_{1};
    
    // Internal methods
    // void onWebSocketData(const core::ExchangeId& exchange,
    //                     const core::Symbol& symbol,
    //                     const std::vector<uint8_t>& data);
    
    void monitorConnections();
    // Clear any queued items and update counters (best-effort, non-blocking)
    void clearProcessingQueue();
    
    std::vector<std::string> createStreamNames(
        const std::vector<Symbol>& tradingPairs,
        const std::vector<DataType>& dataTypes,
        int depth);
    
    std::string generateRequestId();
    std::string toLowercase(const std::string& str);
    //moodyCamel concurrence queue
//     void processingWorker() {
//         MarketDataUpdate update;
//         while (running_) {
//             if (processingQueue_.try_dequeue(update)) {
//                 std::cout << "[ExchangeManager] processingWorker dequeued symbol=" << update.tradingPair.toString()
//                           << " bytes=" << update.data.size() << std::endl;
//                 if (normalizer_) {
//                     normalizer_->processMarketData(update);
//                 }
//             } else {
//                 std::this_thread::sleep_for(std::chrono::milliseconds(100));
//             }
//         }

//         // Process remaining items when shutting down
//         while (processingQueue_.try_dequeue(update)) {
//             if (normalizer_) {
//                 normalizer_->processMarketData(update);
//             }
//         }
//     }
};

} // namespace data_ingestion
} // namespace trading