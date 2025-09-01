/**
 * @file normalization_pipeline.hpp
 * @brief Complete normalization pipeline with monitoring
 * 
 * Implements a multi-stage pipeline for processing market data with
 * comprehensive monitoring and error handling capabilities.
 * 
 * @author Trading System Team
 * @date May 25, 2025
 */

#pragma once

#include "pipeline_monitoring.hpp"
#include "core/market_types.hpp"
#include "data_ingestion/orderbook/order_book_types.hpp"
#include "data_ingestion/data_quality/data_quality.hpp"
#include "data_ingestion/time/synchronizer.hpp"
#include "normalization/normalizer.hpp"
#include "protocol/adapter_factory.hpp"
#include "orderbook/order_book_manager.hpp"
#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <boost/lockfree/spsc_queue.hpp>

namespace trading {
namespace data_ingestion {

// Use the TimeSynchronizer from the time namespace
using TimeSynchronizer = time::TimeSynchronizer;

// Forward declarations
// class AdapterFactory;
// class OrderBookManager;
// class DataQualityControl;
// class NormalizedDataPublisher;

/**
 * Lock-free queue for pipeline stages
 */
template<typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t capacity = 10000) : queue_(capacity) {}
    
    bool push(const T& item) {
        return queue_.push(item);
    }
    
    bool pop(T& item) {
        return queue_.pop(item);
    }
    
    bool empty() const {
        return queue_.empty();
    }
    
    size_t size() const {
        return queue_.read_available();
    }
    
private:
    boost::lockfree::spsc_queue<T> queue_;
};

/**
 * Base class for pipeline stages with monitoring
 */
template<typename InputT, typename OutputT>
class MonitoredPipelineStage {
public:
    MonitoredPipelineStage(const std::string& stageName,
                          ThreadSafeQueue<InputT>* inputQueue,
                          ThreadSafeQueue<OutputT>* outputQueue,
                          std::shared_ptr<PipelineMetrics> metrics,
                          std::shared_ptr<PipelineStatus> status)
        : stageName_(stageName), 
          inputQueue_(inputQueue), 
          outputQueue_(outputQueue),
          metrics_(metrics),
          status_(status),
          running_(false) {}
    
    virtual ~MonitoredPipelineStage() {
        stop();
    }
    
    virtual void process(const InputT& input, OutputT& output) = 0;
    
    void start() {
        if (running_.load()) return;
        
        running_.store(true);
        status_->setStageState(stageName_, PipelineStatus::StageState::STARTING);
        
        thread_ = std::thread([this]() {
            try {
                status_->setStageState(stageName_, PipelineStatus::StageState::RUNNING);
                runProcessingLoop();
                status_->setStageState(stageName_, PipelineStatus::StageState::STOPPED);
            } catch (const std::exception& e) {
                status_->addError(stageName_, e.what());
            }
        });
    }
    
    void stop() {
        if (running_.load()) {
            running_.store(false);
            status_->setStageState(stageName_, PipelineStatus::StageState::STOPPING);
            
            if (thread_.joinable()) {
                thread_.join();
            }
        }
    }
    
    const std::string& getStageName() const { return stageName_; }
    
protected:
    void runProcessingLoop() {
        InputT input;
        OutputT output;
        
        while (running_.load()) {
            if (inputQueue_ && inputQueue_->pop(input)) {
                auto start = std::chrono::high_resolution_clock::now();
                
                try {
                    metrics_->incrementInputCount(stageName_);
                    
                    // Process the input
                    process(input, output);
                    
                    // Record latency
                    auto end = std::chrono::high_resolution_clock::now();
                    auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
                    metrics_->recordLatency(stageName_, latency);
                    
                    // Forward to next stage
                    if (outputQueue_) {
                        int retryCount = 0;
                        while (running_.load() && !outputQueue_->push(output)) {
                            if (++retryCount > 1000) { // Prevent infinite retry
                                metrics_->incrementDroppedCount(stageName_);
                                metrics_->recordQueueFullEvent(stageName_ + "_output");
                                status_->addWarning(stageName_, "Output queue consistently full, dropping message");
                                break;
                            }
                            std::this_thread::yield();
                        }
                        
                        if (retryCount <= 1000) {
                            metrics_->incrementOutputCount(stageName_);
                        }
                    } else {
                        metrics_->incrementOutputCount(stageName_);
                    }
                    
                } catch (const std::exception& e) {
                    metrics_->incrementErrorCount(stageName_);
                    status_->addError(stageName_, e.what());
                }
                
            } else {
                // Brief pause when no input available
                std::this_thread::sleep_for(std::chrono::microseconds(100));
            }
            
            // Update queue size metrics
            if (inputQueue_) {
                metrics_->updateQueueSize(stageName_ + "_input", inputQueue_->size());
            }
            if (outputQueue_) {
                metrics_->updateQueueSize(stageName_ + "_output", outputQueue_->size());
            }
        }
    }
    
    std::string stageName_;
    ThreadSafeQueue<InputT>* inputQueue_;
    ThreadSafeQueue<OutputT>* outputQueue_;
    std::shared_ptr<PipelineMetrics> metrics_;
    std::shared_ptr<PipelineStatus> status_;
    std::atomic<bool> running_;
    std::thread thread_;
};

/**
 * Protocol Adapter Stage
 */
class ProtocolAdapterStage : public MonitoredPipelineStage<core::ExchangeData, OrderBookUpdate> {
public:
    ProtocolAdapterStage(ThreadSafeQueue<core::ExchangeData>* input,
                        ThreadSafeQueue<OrderBookUpdate>* output,
                        std::shared_ptr<AdapterFactory> factory,
                        std::shared_ptr<PipelineMetrics> metrics,
                        std::shared_ptr<PipelineStatus> status)
        : MonitoredPipelineStage("ProtocolAdapter", input, output, metrics, status),
          adapterFactory_(factory) {}
    
    void process(const core::ExchangeData& input, OrderBookUpdate& output) override {
        auto adapter = adapterFactory_->getAdapter(input.exchangeId);
        if (!adapter) {
            throw std::runtime_error("No adapter found for exchange: " + input.exchangeId.toString());
        }
        
        output = adapter->convertToStandardFormat(input);
    }
    
private:
    std::shared_ptr<AdapterFactory> adapterFactory_;
};

/**
 * Order Book Reconstruction Stage
 */
class OrderBookStage : public MonitoredPipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    OrderBookStage(ThreadSafeQueue<OrderBookUpdate>* input,
                  ThreadSafeQueue<OrderBookUpdate>* output,
                  std::shared_ptr<OrderBookManager> manager,
                  std::shared_ptr<PipelineMetrics> metrics,
                  std::shared_ptr<PipelineStatus> status)
        : MonitoredPipelineStage("OrderBookReconstruction", input, output, metrics, status),
          orderBookManager_(manager) {}
    
    void process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        // Apply update to order book
        if (!orderBookManager_->processUpdate(input)) {
            throw std::runtime_error("Failed to process order book update");
        }
        
        // Pass through the update
        output = input;
    }
    
private:
    std::shared_ptr<OrderBookManager> orderBookManager_;
};

/**
 * Data Quality Control Stage
 */
class DataQualityStage : public MonitoredPipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    DataQualityStage(ThreadSafeQueue<OrderBookUpdate>* input,
                    ThreadSafeQueue<OrderBookUpdate>* output,
                    std::shared_ptr<DataQualityControl> qualityControl,
                    std::shared_ptr<PipelineMetrics> metrics,
                    std::shared_ptr<PipelineStatus> status)
        : MonitoredPipelineStage("DataQuality", input, output, metrics, status),
          qualityControl_(qualityControl) {}
    
    void process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        output = input; // Copy input
        
        if (!qualityControl_->validateUpdate(output)) {
            throw std::runtime_error("Update rejected by quality control");
        }
    }
    
private:
    std::shared_ptr<DataQualityControl> qualityControl_;
};

/**
 * Timestamp Synchronization Stage
 */
class TimestampStage : public MonitoredPipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    TimestampStage(ThreadSafeQueue<OrderBookUpdate>* input,
                  ThreadSafeQueue<OrderBookUpdate>* output,
                  std::shared_ptr<TimeSynchronizer> synchronizer,
                  std::shared_ptr<PipelineMetrics> metrics,
                  std::shared_ptr<PipelineStatus> status)
        : MonitoredPipelineStage("TimestampSync", input, output, metrics, status),
          synchronizer_(synchronizer) {}
    
    void process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        output = input; // Copy input
        
        // Normalize timestamps
        if (synchronizer_) {
            auto receivedTime = time::TimePoint(std::chrono::milliseconds(output.receivedTimestamp));
            
            uint64_t exchangeTimestampMs = output.exchange_timestamp;
            if (exchangeTimestampMs < 1000000000000ULL) { // Convert seconds to milliseconds
                exchangeTimestampMs *= 1000;
            }
            
            auto normalizedTime = synchronizer_->normalizeExchangeTimeMs(
                output.exchangeId.toString(),
                exchangeTimestampMs,
                receivedTime);
            
            output.exchange_timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                normalizedTime.time_since_epoch()).count();
                
            // Add calibration sample
            synchronizer_->addExchangeTimeSample(
                output.exchangeId.toString(),
                exchangeTimestampMs,
                receivedTime);
        }
    }
    
private:
    std::shared_ptr<TimeSynchronizer> synchronizer_;
};

/**
 * Publisher Stage
 */
class PublisherStage : public MonitoredPipelineStage<OrderBookUpdate, int> {
public:
    PublisherStage(ThreadSafeQueue<OrderBookUpdate>* input,
                  ThreadSafeQueue<int>* output,
                //   std::shared_ptr<Normalizer> publisher,
                  std::shared_ptr<OrderBookManager> manager,
                  std::shared_ptr<PipelineMetrics> metrics,
                  std::shared_ptr<PipelineStatus> status)
        : MonitoredPipelineStage("Publisher", input, output, metrics, status),
        //   publisher_(publisher),
          orderBookManager_(manager) {}
    
    void process(const OrderBookUpdate& input, int& output) override {
        // Publish the update
        // if (!publisher_->publishUpdate(input)) {
        //     throw std::runtime_error("Failed to publish order book update");
        // }
        
        // // Generate and publish snapshot if needed
        // if (shouldPublishSnapshot(input)) {
        //     auto snapshot = orderBookManager_->getSnapshot(input.exchangeId, input.symbol);
        //     if (!publisher_->publishSnapshot(snapshot)) {
        //         status_->addWarning(stageName_, "Failed to publish snapshot");
        //     }
        // }
        
        output = 1; // Dummy output
    }
    
private:
    bool shouldPublishSnapshot(const OrderBookUpdate& update) {
        // Simple heuristic: every 100 updates
        static std::unordered_map<std::string, int> updateCounts_;
        std::string key = update.exchangeId.toString() + ":" + update.symbol.toString();
        
        auto& count = updateCounts_[key];
        if (++count >= 100) {
            count = 0;
            return true;
        }
        
        return false;
    }
    
    // std::shared_ptr<Normalizer> publisher_;
    std::shared_ptr<OrderBookManager> orderBookManager_;
};

/**
 * Complete Normalization Pipeline with monitoring
 */
class NormalizationPipeline : public MonitoredPipeline {
public:
    NormalizationPipeline(std::shared_ptr<AdapterFactory> adapterFactory,
                         std::shared_ptr<OrderBookManager> orderBookManager,
                         std::shared_ptr<DataQualityControl> qualityControl,
                         std::shared_ptr<TimeSynchronizer> synchronizer
                        //  std::shared_ptr<Normalizer> publisher
                        );
    
    ~NormalizationPipeline() override;
    
    // Input interface
    bool push(const core::ExchangeData& data);
    bool push(const core::MarketDataUpdate& update);
    
    // Pipeline control
    bool configure(const PipelineConfig& config) override;
    
protected:
    bool doStart() override;
    void doStop() override;
    bool doHealthCheck() override;
    
private:
    // Pipeline components
    std::shared_ptr<AdapterFactory> adapterFactory_;
    std::shared_ptr<OrderBookManager> orderBookManager_;
    std::shared_ptr<DataQualityControl> qualityControl_;
    std::shared_ptr<TimeSynchronizer> synchronizer_;
    // std::shared_ptr<Normalizer> publisher_;
    
    // Pipeline queues
    std::unique_ptr<ThreadSafeQueue<core::ExchangeData>> inputQueue_;
    std::unique_ptr<ThreadSafeQueue<OrderBookUpdate>> stage1Queue_;
    std::unique_ptr<ThreadSafeQueue<OrderBookUpdate>> stage2Queue_;
    std::unique_ptr<ThreadSafeQueue<OrderBookUpdate>> stage3Queue_;
    std::unique_ptr<ThreadSafeQueue<int>> outputQueue_;
    
    // Pipeline stages
    std::unique_ptr<ProtocolAdapterStage> protocolStage_;
    std::unique_ptr<OrderBookStage> orderBookStage_;
    std::unique_ptr<DataQualityStage> qualityStage_;
    std::unique_ptr<TimestampStage> timestampStage_;
    // std::unique_ptr<PublisherStage> publisherStage_;
    
    // Helper methods
    core::ExchangeData convertMarketDataToExchangeData(const core::MarketDataUpdate& update);
    void initializeQueues();
    void initializeStages();
    bool validateComponents();
};

// =============================================================================
// Inline implementations
// =============================================================================

inline bool NormalizationPipeline::push(const core::ExchangeData& data) {
    if (!inputQueue_) return false;
    
    bool success = inputQueue_->push(data);
    if (!success) {
        metrics_->recordQueueFullEvent("input");
        metrics_->incrementDroppedCount("input");
    }
    
    return success;
}

inline bool NormalizationPipeline::push(const core::MarketDataUpdate& update) {
    auto exchangeData = convertMarketDataToExchangeData(update);
    return push(exchangeData);
}

// =============================================================================
// Factory function for easy pipeline creation
// =============================================================================

/**
 * Factory function to create a fully configured normalization pipeline
 */
std::unique_ptr<NormalizationPipeline> createNormalizationPipeline(
    const PipelineConfig& config,
    std::shared_ptr<AdapterFactory> adapterFactory,
    std::shared_ptr<OrderBookManager> orderBookManager,
    std::shared_ptr<DataQualityControl> qualityControl,
    std::shared_ptr<TimeSynchronizer> synchronizer
    // std::shared_ptr<Normalizer> publisher
);

/**
 * Factory function with default components (for testing/demo)
 */
std::unique_ptr<NormalizationPipeline> createDefaultNormalizationPipeline(
    const PipelineConfig& config = PipelineConfig{});

} // namespace data_ingestion
} // namespace trading