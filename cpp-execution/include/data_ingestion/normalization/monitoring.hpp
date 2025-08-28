// monitoring.h
#include <chrono>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include "prometheus/counter.h"
#include "prometheus/gauge.h"
#include "prometheus/histogram.h"
#include "prometheus/registry.h"
#include "prometheus/exposer.h"

class OrderBookMetrics {
public:
    OrderBookMetrics() : registry(std::make_shared<prometheus::Registry>()) {
        // Set up metrics
        auto& metrics_family = prometheus::BuildCounter()
            .Name("orderbook_updates_total")
            .Help("Total number of order book updates processed")
            .Register(*registry);
        
        updateCounter = &metrics_family.Add({{"type", "all"}});
        
        auto& latency_family = prometheus::BuildHistogram()
            .Name("orderbook_processing_time")
            .Help("Time to process order book updates (ms)")
            .Register(*registry);
        
        processingTimeHistogram = &latency_family.Add(
            {{"stage", "normalize"}}, 
            prometheus::Histogram::BucketBoundaries{0.1, 0.5, 1, 2, 5, 10, 20, 50, 100}
        );
        
        auto& queue_family = prometheus::BuildGauge()
            .Name("orderbook_queue_size")
            .Help("Number of items in processing queues")
            .Register(*registry);
        
        for (const auto& stage : {"adapter", "orderbook", "quality", "timestamp", "publisher"}) {
            queueSizeGauges[stage] = &queue_family.Add({{"stage", stage}});
        }
        
        auto& book_family = prometheus::BuildGauge()
            .Name("orderbook_state_count")
            .Help("Number of order books being tracked")
            .Register(*registry);
        
        orderBookCount = &book_family.Add({});
        
        // Start the exposer
        exposer = std::make_unique<prometheus::Exposer>("0.0.0.0:8081");
        exposer->RegisterCollectable(registry);
    }
    
    void recordUpdate(const core::ExchangeId& exchangeId, const std::string& symbol) {
        updateCounter->Increment();
        
        // Track unique books
        std::string key = exchangeId + ":" + symbol;
        {
            std::lock_guard<std::mutex> lock(booksMutex);
            if (books.insert(key).second) {
                orderBookCount->Set(books.size());
            }
        }
    }
    
    void recordProcessingTime(double milliseconds) {
        processingTimeHistogram->Observe(milliseconds);
    }
    
    void setQueueSize(const std::string& stage, double size) {
        auto it = queueSizeGauges.find(stage);
        if (it != queueSizeGauges.end()) {
            it->second->Set(size);
        }
    }
    
private:
    std::shared_ptr<prometheus::Registry> registry;
    std::unique_ptr<prometheus::Exposer> exposer;
    
    prometheus::Counter* updateCounter;
    prometheus::Histogram* processingTimeHistogram;
    std::unordered_map<std::string, prometheus::Gauge*> queueSizeGauges;
    prometheus::Gauge* orderBookCount;
    
    std::unordered_set<std::string> books;
    std::mutex booksMutex;
};

// Utility for measuring execution time
class ScopedTimer {
public:
    ScopedTimer(std::function<void(double)> callback) : callback(callback) {
        startTime = std::chrono::high_resolution_clock::now();
    }
    
    ~ScopedTimer() {
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            endTime - startTime).count() / 1000.0;  // Convert to milliseconds
        callback(duration);
    }
    
private:
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
    std::function<void(double)> callback;
};