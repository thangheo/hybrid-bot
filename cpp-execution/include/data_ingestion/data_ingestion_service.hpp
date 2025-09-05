// include/data_ingestion/data_ingestion_service.hpp
#pragma once

#include "data_ingestion/exchange_manager.hpp"
#include "data_ingestion/orderbook/reconstructor.hpp"
#include "data_ingestion/orderbook/order_book_manager.hpp"
#include "data_ingestion/publisher/zeromq_publisher.hpp"
#include "data_ingestion/config.hpp"
#include "data_ingestion/normalization/order_book_normalizer.hpp"
#include "common/event_bus.hpp"
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>
#include <unordered_map>
#include <functional>

namespace trading {
namespace data_ingestion {

/**
 * Centralized service that integrates all data ingestion components
 * This replaces your IntegratedTradingServer for better architecture
 */
class DataIngestionService {
public:
    using StatusCallback = std::function<void(const std::string&, const std::string&)>;
    using OrderBookUpdateCallback = std::function<void(const OrderBookUpdate&)>;
    
    explicit DataIngestionService(const std::string& configPath = "config/data_ingestion.json");
    ~DataIngestionService();
    
    // Service lifecycle
    bool initialize();
    bool start(const std::vector<std::string>& symbols);
    void stop();
    bool isRunning() const { return running_.load(); }
    
    // Configuration
    void setStatusCallback(StatusCallback callback) { statusCallback_ = std::move(callback); }
    void setOrderBookCallback(OrderBookUpdateCallback callback) { orderBookCallback_ = std::move(callback); }
    
    // Query interface
    std::shared_ptr<OrderBook> getOrderBook(const ExchangeId& exchange, const core::Symbol& symbol) const;
    OrderBookManagerStats getStatistics() const;
    std::vector<OrderBookKey> getActiveOrderBooks() const;
    
    // Health monitoring
    bool isHealthy() const;
    std::string getStatusReport() const;

private:
    // Core components (proper order for RAII)
    std::string configPath_;
    Configuration* config_;
    
    // Event system
    std::unique_ptr<common::EventBus> eventBus_;
    
    // Publishing layer
    std::shared_ptr<ZeroMQPublisher> publisher_;
    
    // Order book management
    OrderBookReconstructor* reconstructor_;
    std::unique_ptr<OrderBookManager> orderBookManager_;
    
    // Data processing
    std::unique_ptr<OrderBookNormalizer> normalizer_;
    std::unique_ptr<ExchangeManager> exchangeManager_;
    
    // Service state
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    
    // Monitoring
    std::thread monitoringThread_;
    std::atomic<uint64_t> messagesProcessed_{0};
    std::atomic<uint64_t> errorsEncountered_{0};
    std::chrono::steady_clock::time_point startTime_;
    
    // Callbacks
    StatusCallback statusCallback_;
    OrderBookUpdateCallback orderBookCallback_;
    
    // Private methods
    bool loadConfiguration();
    bool setupPublisher();
    bool setupOrderBookComponents();
    bool setupExchangeManager();
    void setupEventHandling();
    
    // Event handlers
    void onNormalizedOrderBook(const OrderBookUpdate& update);
    void onOrderBookChanged(const OrderBook& orderBook);
    void onConnectionStatus(const std::string& exchange, const std::string& status);
    
    // Monitoring
    void runMonitoring();
    void publishStatus(const std::string& component, const std::string& status);
    
    // Cleanup
    void cleanup();
};

} // namespace data_ingestion
} // namespace trading