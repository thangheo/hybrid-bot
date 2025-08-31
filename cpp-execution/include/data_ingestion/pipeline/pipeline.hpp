#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <boost/lockfree/spsc_queue.hpp>
// #include "core/market_types.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include "data_ingestion/data_quality/data_quality.hpp"
#include "data_ingestion/time/synchronizer.hpp"
// normalizer
#include "data_ingestion/normalization/normalizer.hpp"
#include "data_ingestion/normalization/order_book_normalizer.hpp"
// #include "pipeline/normalization_pipeline.hpp"
#include "protocol/adapter_factory.hpp"
#include "data_ingestion/orderbook/order_book_manager.hpp"

using namespace trading::data_ingestion;
using namespace trading::core;
using namespace trading::data_ingestion::time;

// Forward declarations
class ProtocolAdapterStage;
class OrderBookStage;
class DataQualityStage;
class TimestampStage;
    
// Lock-free queue between pipeline stages
template<typename T>
class PipelineQueue {
public:
    PipelineQueue(size_t capacity = 10000) : queue(capacity) {}
    
    bool push(const T& item) {
        return queue.push(item);
    }
    
    bool pop(T& item) {
        return queue.pop(item);
    }
    
    bool empty() const {
        return queue.empty();
    }
    
private:
    boost::lockfree::spsc_queue<T> queue;
};

// Pipeline stage base class
template<typename InputT, typename OutputT>
class PipelineStage {
public:
    PipelineStage(PipelineQueue<InputT>* input, PipelineQueue<OutputT>* output)
        : inputQueue(input), outputQueue(output), running(false) {}
    
    virtual ~PipelineStage() {
        stop();
    }
    
    virtual void process(const InputT& input, OutputT& output) = 0;
    
    void start() {
        if (running.load()) return;
        
        running.store(true);
        thread = std::thread([this]() {
            InputT input;
            OutputT output;
            
            while (running.load()) {
                if (inputQueue && inputQueue->pop(input)) {
                    process(input, output);
                    
                    if (outputQueue) {
                        // Retry if queue is full
                        while (running.load() && !outputQueue->push(output)) {
                            std::this_thread::yield();
                        }
                    }
                } else {
                    // Brief pause when no input available
                    std::this_thread::sleep_for(std::chrono::microseconds(10));
                }
            }
        });
    }
    
    void stop() {
        if (running.load()) {
            running.store(false);
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
    
private:
    PipelineQueue<InputT>* inputQueue;
    PipelineQueue<OutputT>* outputQueue;
    std::atomic<bool> running;
    std::thread thread;
};
 
// 1. Protocol Adapter Stage
class ProtocolAdapterStage : public PipelineStage<ExchangeData, OrderBookUpdate> {
public:
    ProtocolAdapterStage(PipelineQueue<ExchangeData>* input, 
                        PipelineQueue<OrderBookUpdate>* output,
                        std::shared_ptr<AdapterFactory> factory)
        : PipelineStage(input, output), adapterFactory(factory) {}
    
    void process(const ExchangeData& input, OrderBookUpdate& output) override {
        auto adapter = adapterFactory->getAdapter(input);
        if (adapter) {
            output = adapter->convertToStandardFormat(input);
        } else {
            // Log error: no suitable adapter found
            throw std::runtime_error("No adapter found for exchange data");
        }
    }
    
private:
    std::shared_ptr<AdapterFactory> adapterFactory;
};

// 2. Order Book Reconstruction Stage
class OrderBookStage : public PipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    OrderBookStage(PipelineQueue<OrderBookUpdate>* input, 
                  PipelineQueue<OrderBookUpdate>* output,
                  std::shared_ptr<OrderBookManager> manager)
        : PipelineStage(input, output), orderBookManager(manager) {}
    
    void process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        // Apply update to order book
        orderBookManager->processUpdate(input);
        
        // Pass through the update
        output = input;
    }
    
private:
    std::shared_ptr<OrderBookManager> orderBookManager;
};

// 3. Data Quality Stage
class DataQualityStage : public PipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    DataQualityStage(PipelineQueue<OrderBookUpdate>* input, 
                    PipelineQueue<OrderBookUpdate>* output,
                    std::shared_ptr<DataQualityControl> qualityControl)
        : PipelineStage(input, output), qualityControl(qualityControl) {}
    
    void process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        output = input;  // Copy the input
        
        // Apply quality checks
        if (!qualityControl->validateUpdate(output)) {
            // Log rejection
            throw std::runtime_error("Update rejected by quality control");
        }
    }
    
private:
    std::shared_ptr<DataQualityControl> qualityControl;
};

// 4. Timestamp Synchronization Stage
class TimestampStage : public PipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    TimestampStage(PipelineQueue<OrderBookUpdate>* input,
                   PipelineQueue<OrderBookUpdate>* output,
                   std::shared_ptr<TimeSynchronizer> synchronizer)
        : PipelineStage(input, output), synchronizer_(synchronizer) {}
 
    void process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        output = input; // Copy the input
        
        // DON'T modify receivedTimestamp - keep original receive time
        auto receivedTimePoint = TimePoint(std::chrono::milliseconds(output.receivedTimestamp));
        
        // Handle exchange timestamp formats
        uint64_t exchangeTimestampMs;
        if (output.exchange_timestamp > 1000000000000ULL) {
            exchangeTimestampMs = output.exchange_timestamp;
        } else if (output.exchange_timestamp > 1000000000ULL) {
            exchangeTimestampMs = output.exchange_timestamp * 1000;
        } else {
            exchangeTimestampMs = output.receivedTimestamp;
        }
        
        // Normalize exchange timestamp
        auto normalizedTimePoint = synchronizer_->normalizeExchangeTimeMs(
            output.exchangeId.toString(),
            exchangeTimestampMs,
            receivedTimePoint);
        
        output.exchange_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            normalizedTimePoint.time_since_epoch()).count();
        
        // Add calibration sample
        synchronizer_->addExchangeTimeSample(
            output.exchangeId.toString(),
            exchangeTimestampMs,
            receivedTimePoint);
    }
private:
    std::shared_ptr<TimeSynchronizer> synchronizer_;
};

// Alternative implementation if OrderBookUpdate uses TimePoint directly
class TimestampStageTimePoint : public PipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    TimestampStageTimePoint(PipelineQueue<OrderBookUpdate>* input,
                           PipelineQueue<OrderBookUpdate>* output,
                           std::shared_ptr<TimeSynchronizer> synchronizer)
        : PipelineStage(input, output), synchronizer_(synchronizer) {}

    void process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        output = input; // Copy the input
        
        // Add calibration sample
        if (output.exchange_timestamp > 0) { // Assuming uint64_t milliseconds
            synchronizer_->addExchangeTimeSample(
                output.exchangeId.toString(),
                output.exchange_timestamp,
                TimePoint(std::chrono::milliseconds(output.receivedTimestamp)));
        }
    }
    
private:
    std::shared_ptr<TimeSynchronizer> synchronizer_;
};


class NormalizationPipeline {
public:
    NormalizationPipeline(
        std::shared_ptr<AdapterFactory> adapterFactory,
        std::shared_ptr<OrderBookManager> orderBookManager,
        std::shared_ptr<DataQualityControl> qualityControl,
        std::shared_ptr<TimeSynchronizer> synchronizer)
    {
        // Create queues (one less queue needed)
        exchangeDataQueue = std::make_unique<PipelineQueue<ExchangeData>>();
        stage1OutputQueue = std::make_unique<PipelineQueue<OrderBookUpdate>>();
        stage2OutputQueue = std::make_unique<PipelineQueue<OrderBookUpdate>>();
        stage3OutputQueue = std::make_unique<PipelineQueue<OrderBookUpdate>>();
        
        // Create stages (no publisher stage)
        adapterStage = std::make_unique<ProtocolAdapterStage>(
            static_cast<PipelineQueue<ExchangeData>*>(exchangeDataQueue.get()), 
            static_cast<PipelineQueue<OrderBookUpdate>*>(stage1OutputQueue.get()), 
            adapterFactory);
            
        orderBookStage = std::make_unique<OrderBookStage>(
            static_cast<PipelineQueue<OrderBookUpdate>*>(stage1OutputQueue.get()), 
            static_cast<PipelineQueue<OrderBookUpdate>*>(stage2OutputQueue.get()), 
            orderBookManager);
            
        qualityStage = std::make_unique<DataQualityStage>(
            static_cast<PipelineQueue<OrderBookUpdate>*>(stage2OutputQueue.get()), 
            static_cast<PipelineQueue<OrderBookUpdate>*>(stage3OutputQueue.get()), 
            qualityControl);
            
        timestampStage = std::make_unique<TimestampStage>(
            static_cast<PipelineQueue<OrderBookUpdate>*>(stage3OutputQueue.get()), 
            nullptr, 
            synchronizer);  // No output queue needed
    }
    
    void start() {
        adapterStage->start();
        orderBookStage->start();
        qualityStage->start();
        timestampStage->start();
        // No publisher stage to start
    }
    
    void stop() {
        // Stop in reverse order (no publisher)
        timestampStage->stop();
        qualityStage->stop();
        orderBookStage->stop();
        adapterStage->stop();
    }
    
    bool pushExchangeData(const ExchangeData& data) {
        return exchangeDataQueue->push(data);
    }
    
    // Add method to get processed data
    PipelineQueue<OrderBookUpdate>* getOutputQueue() {
        return stage3OutputQueue.get();
    }
    
    // Optional: Get processed updates manually
    bool getProcessedUpdate(OrderBookUpdate& update) {
        return stage3OutputQueue->pop(update);
    }

private:
    // Queues (removed dummyOutputQueue)
    std::unique_ptr<PipelineQueue<ExchangeData>> exchangeDataQueue;
    std::unique_ptr<PipelineQueue<OrderBookUpdate>> stage1OutputQueue;
    std::unique_ptr<PipelineQueue<OrderBookUpdate>> stage2OutputQueue;
    std::unique_ptr<PipelineQueue<OrderBookUpdate>> stage3OutputQueue;
    
    // Stages (removed publisherStage)
    std::unique_ptr<ProtocolAdapterStage> adapterStage;
    std::unique_ptr<OrderBookStage> orderBookStage;
    std::unique_ptr<DataQualityStage> qualityStage;
    std::unique_ptr<TimestampStage> timestampStage;
};
