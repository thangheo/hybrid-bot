// test_cache_app.cpp
#include "common/orderbook_cache_service.hpp"
#include "common/logger.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "core/market_types.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <random>

using namespace trading;
using namespace trading::common;
using namespace trading::data_ingestion;

class MockOrderBookGenerator {
public:
    MockOrderBookGenerator() : rng_(std::random_device{}()) {}
    
    std::shared_ptr<OrderBook> createMockOrderBook(
        const core::ExchangeId& exchangeId, 
        const std::string& symbol) {
        
        auto orderBook = std::make_shared<OrderBook>(exchangeId, symbol);
        
        // Generate mock price levels
        std::vector<PriceLevel> bids, asks;
        
        double basePrice = generateRandomPrice();
        
        // Generate 10 bid levels (descending prices)
        for (int i = 0; i < 10; ++i) {
            double price = basePrice - (i * 0.01);
            double quantity = generateRandomQuantity();
            bids.emplace_back(price, quantity);
        }
        
        // Generate 10 ask levels (ascending prices)  
        for (int i = 0; i < 10; ++i) {
            double price = basePrice + 0.01 + (i * 0.01);
            double quantity = generateRandomQuantity();
            asks.emplace_back(price, quantity);
        }
        
        // Apply snapshot
        auto now = std::chrono::system_clock::now();
        orderBook->applySnapshot(bids, asks, sequence_++, now);
        
        return orderBook;
    }
    
    void updateOrderBook(std::shared_ptr<OrderBook> orderBook) {
        // Generate small updates
        std::vector<PriceLevel> bidUpdates, askUpdates;
        
        if (uniform_dist_(rng_) < 0.3) { // 30% chance to update
            double basePrice = generateRandomPrice();
            bidUpdates.emplace_back(basePrice - 0.005, generateRandomQuantity());
            askUpdates.emplace_back(basePrice + 0.005, generateRandomQuantity());
            
            auto now = std::chrono::system_clock::now();
            orderBook->applyUpdate(bidUpdates, askUpdates, sequence_++, now);
        }
    }

private:
    std::mt19937 rng_;
    std::uniform_real_distribution<double> uniform_dist_{0.0, 1.0};
    std::uniform_real_distribution<double> price_dist_{100.0, 200.0};
    std::uniform_real_distribution<double> quantity_dist_{0.1, 10.0};
    uint64_t sequence_ = 1;
    
    double generateRandomPrice() {
        return price_dist_(rng_);
    }
    
    double generateRandomQuantity() {
        return quantity_dist_(rng_);
    }
};

int main() {
    try {
        LOG_INFO("=== OrderBook Cache Service Test App Started ===");
        
        // Initialize cache service with custom config
        OrderBookCacheConfig config;
        config.maxCacheSize = 50;
        config.maxOrderBookDepth = 10;
        config.enableMemoryOptimization = true;
        config.publishQueueSize = 1000;
        
        LOG_INFO("Creating OrderBook Cache Service with config:");
        LOG_INFO("  - Max cache size: {}", config.maxCacheSize);
        LOG_INFO("  - Max depth: {}", config.maxOrderBookDepth);
        LOG_INFO("  - Memory optimization: {}", config.enableMemoryOptimization);
        
        auto cacheService = std::make_unique<OrderBookCacheService>(config);
        
        // Create mock data generator
        MockOrderBookGenerator generator;
        
        // Create some test trading pairs
        std::vector<std::pair<core::ExchangeId, std::string>> testPairs = {
            {core::ExchangeId::binance(), "BTCUSDT"},
            {core::ExchangeId::binance(), "ETHUSDT"},
            {core::ExchangeId::coinbase(), "BTCUSD"},
            {core::ExchangeId::coinbase(), "ETHUSD"},
            {core::ExchangeId::kraken(), "XBTUSD"}
        };
        
        LOG_INFO("Creating mock order books for {} trading pairs", testPairs.size());
        
        // Create initial order books and add to cache
        std::vector<std::shared_ptr<OrderBook>> orderBooks;
        for (const auto& [exchange, symbol] : testPairs) {
            auto orderBook = generator.createMockOrderBook(exchange, symbol);
            orderBooks.push_back(orderBook);
            
            cacheService->updateOrderBook(orderBook);
            LOG_INFO("Added order book: {} - {}", exchange.toString(), symbol);
        }
        
        // Test cache retrieval
        LOG_INFO("\n=== Testing Cache Retrieval ===");
        // In test_cache_app.cpp, sửa phần test retrieval:
        LOG_INFO("\n=== Testing Cache Retrieval ===");
        for (const auto& [exchange, symbol] : testPairs) {
            // Use the same key format as updateOrderBook uses
            std::string exchangeStr = exchange.toString();
            auto cachedOrderBook = cacheService->getOrderBook(exchangeStr, symbol);
            if (cachedOrderBook) {
                auto snapshot = cachedOrderBook->getSnapshot(5);
                LOG_INFO("Retrieved {}-{}: {} bids, {} asks, mid price: {:.2f}", 
                    exchangeStr, symbol,
                    snapshot.bids.size(), snapshot.asks.size(),
                    (snapshot.bids.empty() || snapshot.asks.empty()) ? 0.0 : 
                    (snapshot.bids[0].price + snapshot.asks[0].price) / 2.0);
            } else {
                LOG_ERROR("Failed to retrieve order book: {} - {}", exchangeStr, symbol);
                // Debug: print what key we're looking for
                // LOG_ERROR("Cache key used: {}", cacheService->createKey(exchangeStr, symbol));
            }
        }
        
        // Test bulk retrieval
        LOG_INFO("\n=== Testing Bulk Retrieval ===");
        std::vector<std::pair<std::string, std::string>> bulkRequest;
        for (const auto& [exchange, symbol] : testPairs) {
            bulkRequest.emplace_back(exchange.toString(), symbol);
        }
        
        auto bulkResult = cacheService->getOrderBooks(bulkRequest);
        LOG_INFO("Bulk retrieval: {}/{} order books found", 
            std::count_if(bulkResult.begin(), bulkResult.end(), 
                [](const auto& ob) { return ob != nullptr; }),
            bulkResult.size());
        
        // Simulate real-time updates
        LOG_INFO("\n=== Simulating Real-time Updates ===");
        for (int i = 0; i < 20; ++i) {
            for (auto& orderBook : orderBooks) {
                generator.updateOrderBook(orderBook);
                cacheService->updateOrderBook(orderBook);
            }
            
            // Print metrics every 5 updates
            // In test_cache_app.cpp, sửa phần log metrics:
            if ((i + 1) % 5 == 0) {
                auto metrics = cacheService->getMetrics();
                LOG_INFO("Update {}: Cache size: {}, Hit ratio: {:.2f}%, Memory: {}MB, Queue depth: {}", 
                    i + 1, metrics.cacheSize, metrics.hitRatio * 100.0,  // Convert to percentage
                    metrics.memoryUsageMB, metrics.publishQueueDepth);
            }

            // Và phần final metrics:
            // std::cout <<"  - Hit ratio: {:.2f}%"<< finalMetrics.hitRatio * 100.0 << std::endl;
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Final health check
        LOG_INFO("\n=== Final Health Check ===");
        bool isHealthy = cacheService->isHealthy();
        auto finalMetrics = cacheService->getMetrics();
        
        LOG_INFO("Cache Service Health: {}", isHealthy ? "HEALTHY" : "UNHEALTHY");
        LOG_INFO("Final Metrics:");
        LOG_INFO("  - Total requests: {}", finalMetrics.totalRequests);
        LOG_INFO("  - Cache hits: {}", finalMetrics.cacheHits);
        LOG_INFO("  - Cache misses: {}", finalMetrics.cacheMisses);
        LOG_INFO("  - Hit ratio: {:.2f}%", finalMetrics.hitRatio * 100);
        LOG_INFO("  - Memory usage: {}MB", finalMetrics.memoryUsageMB);
        LOG_INFO("  - Cache size: {}", finalMetrics.cacheSize);
        
        // Test cache eviction
        LOG_INFO("\n=== Testing Cache Eviction ===");
        cacheService->evictStaleEntries();
        LOG_INFO("Cache size after eviction: {}", cacheService->getCacheSize());
        
        LOG_INFO("\n=== Test Completed Successfully ===");
        
        // Keep running for a bit to see async publishing
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
    } catch (const std::exception& e) {
        LOG_ERROR("Test failed with exception: {}", e.what());
        return 1;
    }
    
    LOG_INFO("OrderBook Cache Service Test App finished");
    return 0;
}