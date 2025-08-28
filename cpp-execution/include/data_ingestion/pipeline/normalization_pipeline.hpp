// include/data_ingestion/pipeline/normalization_pipeline.hpp
#pragma once

#include "data_ingestion/orderbook/order_book_types.hpp"
#include "data_ingestion/protocol/adapter_interface.hpp"
// #include "data_ingestion/orderbook/order_book_manager.hpp"
#include "data_ingestion/data_quality/data_quality.hpp"
#include "data_ingestion/time/synchronizer.hpp"
// #include "data_ingestion/publisher/publisher.hpp"
#include "data_ingestion/publisher/zeromq_publisher.hpp"

#include "data_ingestion/monitoring/pipeline_metrics.hpp"

#include <thread>
#include <atomic>
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <exception>

namespace trading {
namespace data_ingestion {

// Thread-safe queue for inter-stage communication
template<typename T>
class ThreadSafeQueue {
public:
    ThreadSafeQueue(size_t max_size = 10000) : max_size_(max_size), shutdown_(false) {}
    
    bool push(const T& item, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (shutdown_) {
            return false;
        }
        
        // Wait until queue has space or timeout occurs
        bool success = !full_condition_.wait_for(lock, timeout, [this]() { 
            return queue_.size() >= max_size_ || shutdown_; 
        });
        
        if (success && !shutdown_) {
            queue_.push(item);
            empty_condition_.notify_one();
            return true;
        }
        
        return false;
    }
    
    bool pop(T& item, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Wait until queue has items or timeout occurs
        bool success = empty_condition_.wait_for(lock, timeout, [this]() { 
            return !queue_.empty() || shutdown_; 
        });
        
        if (success && !queue_.empty()) {
            item = queue_.front();
            queue_.pop();
            full_condition_.notify_one();
            return true;
        }
        
        return false;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        empty_condition_.notify_all();
        full_condition_.notify_all();
    }
    
    bool is_shutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }
    
private:
    std::queue<T> queue_;
    size_t max_size_;
    mutable std::mutex mutex_;
    std::condition_variable empty_condition_;
    std::condition_variable full_condition_;
    bool shutdown_;
};

// Base class for pipeline stages
template<typename InputT, typename OutputT>
class PipelineStage {
public:
    PipelineStage(const std::string& name, 
                 ThreadSafeQueue<InputT>* input_queue, 
                 ThreadSafeQueue<OutputT>* output_queue,
                 std::shared_ptr<PipelineMetrics> metrics = nullptr)
        : name_(name)
        , input_queue_(input_queue)
        , output_queue_(output_queue)
        , metrics_(metrics)
        , running_(false)
        , error_count_(0)
        , processed_count_(0) {}
    
    virtual ~PipelineStage() {
        stop();
    }
    
    // Process a single input item
    virtual bool process(const InputT& input, OutputT& output) = 0;
    
    // Start the processing thread
    void start() {
        if (running_.load()) return;
        
        running_.store(true);
        worker_thread_ = std::thread([this]() {
            try {
                this->run();
            } catch (const std::exception& e) {
                // Log critical error
                std::cerr << "Critical error in stage " << name_ << ": " << e.what() << std::endl;
                
                // Increment error count
                error_count_++;
                
                // Stop the stage
                running_.store(false);
            }
        });
    }
    
    // Stop the processing thread
    void stop() {
        if (!running_.load()) return;
        
        running_.store(false);
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }
    
    // Get stage statistics
    size_t get_processed_count() const { return processed_count_.load(); }
    size_t get_error_count() const { return error_count_.load(); }
    bool is_running() const { return running_.load(); }
    std::string get_name() const { return name_; }
    
protected:
    // Main processing loop
    virtual void run() {
        InputT input;
        OutputT output;
        
        while (running_.load()) {
            // Update queue size metric
            if (metrics_) {
                metrics_->set_queue_size(name_, input_queue_->size());
            }
            
            // Poll for input
            if (input_queue_->pop(input)) {
                auto start_time = std::chrono::high_resolution_clock::now();
                
                try {
                    // Process the input
                    bool success = process(input, output);
                    
                    // Measure processing time
                    auto end_time = std::chrono::high_resolution_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                        end_time - start_time).count() / 1000.0;  // Convert to ms
                    
                    // Update metrics
                    if (metrics_) {
                        metrics_->record_processing_time(name_, duration);
                    }
                    
                    // Push to output queue if processing was successful
                    if (success && output_queue_) {
                        if (!output_queue_->push(output)) {
                            // Handle backpressure
                            handle_backpressure();
                        }
                    }
                    
                    // Update processed count
                    processed_count_++;
                    
                } catch (const std::exception& e) {
                    // Log error
                    std::cerr << "Error in stage " << name_ << ": " << e.what() << std::endl;
                    
                    // Increment error count
                    error_count_++;
                    
                    // Update error metric
                    if (metrics_) {
                        metrics_->increment_error_count(name_);
                    }
                }
            } else {
                // No input available, yield
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    }
    
    virtual void handle_backpressure() {
        // Default implementation: just wait a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Update metric
        if (metrics_) {
            metrics_->increment_backpressure_count(name_);
        }
    }
    
    std::string name_;
    ThreadSafeQueue<InputT>* input_queue_;
    ThreadSafeQueue<OutputT>* output_queue_;
    std::shared_ptr<PipelineMetrics> metrics_;
    std::atomic<bool> running_;
    std::atomic<size_t> error_count_;
    std::atomic<size_t> processed_count_;
    std::thread worker_thread_;
};

// Protocol Adapter Stage
class ProtocolAdapterStage : public PipelineStage<MarketDataUpdate, OrderBookUpdate> {
public:
    ProtocolAdapterStage(
        ThreadSafeQueue<MarketDataUpdate>* input_queue,
        ThreadSafeQueue<OrderBookUpdate>* output_queue,
        std::shared_ptr<AdapterFactory> adapter_factory,
        std::shared_ptr<PipelineMetrics> metrics = nullptr)
        : PipelineStage("protocol_adapter", input_queue, output_queue, metrics)
        , adapter_factory_(adapter_factory) {}
    
    bool process(const MarketDataUpdate& input, OrderBookUpdate& output) override {
        // Get adapter for this exchange
        auto adapter = adapter_factory_->createAdapter(input.exchangeId);
        
        // Check if adapter can handle this update
        if (!adapter->canHandle(input)) {
            std::cerr << "Adapter cannot handle update type for exchange: " 
                      << input.exchangeId << std::endl;
            return false;
        }
        
        // Normalize the update
        output = adapter->normalize(input);
        
        // Standardize trading pair
        output.tradingPair = adapter->standardizeTradingPair(input.tradingPair);
        
        return true;
    }
    
private:
    std::shared_ptr<AdapterFactory> adapter_factory_;
};

// Order Book Reconstruction Stage
class OrderBookStage : public PipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    OrderBookStage(
        ThreadSafeQueue<OrderBookUpdate>* input_queue,
        ThreadSafeQueue<OrderBookUpdate>* output_queue,
        std::shared_ptr<OrderBookManager> order_book_manager,
        std::shared_ptr<PipelineMetrics> metrics = nullptr)
        : PipelineStage("order_book", input_queue, output_queue, metrics)
        , order_book_manager_(order_book_manager) {}
    
    bool process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        // Apply update to order book
        bool success = order_book_manager_->applyUpdate(input);
        
        // Pass through the update for further processing
        output = input;
        
        return success;
    }
    
private:
    std::shared_ptr<OrderBookManager> order_book_manager_;
};

// Data Quality Stage
class DataQualityStage : public PipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    DataQualityStage(
        ThreadSafeQueue<OrderBookUpdate>* input_queue,
        ThreadSafeQueue<OrderBookUpdate>* output_queue,
        std::shared_ptr<DataQualityControl> quality_control,
        std::shared_ptr<PipelineMetrics> metrics = nullptr)
        : PipelineStage("data_quality", input_queue, output_queue, metrics)
        , quality_control_(quality_control) {}
    
    bool process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        // Make a copy for validation
        output = input;
        
        // Apply quality checks
        return quality_control_->validateUpdate(output);
    }
    
private:
    std::shared_ptr<DataQualityControl> quality_control_;
};

// Timestamp Synchronization Stage
class TimestampStage : public PipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    TimestampStage(
        ThreadSafeQueue<OrderBookUpdate>* input_queue,
        ThreadSafeQueue<OrderBookUpdate>* output_queue,
        std::shared_ptr<TimeSync> time_sync,
        std::shared_ptr<PipelineMetrics> metrics = nullptr)
        : PipelineStage("timestamp", input_queue, output_queue, metrics)
        , time_sync_(time_sync) {}
    
    bool process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        // Copy the input
        output = input;
        
        // Add processing timestamp
        output.processed_timestamp = time_sync_->getCurrentTimestamp();
        
        // Adjust exchange timestamp if needed
        if (output.exchange_timestamp > 0) {
            output.adjusted_timestamp = time_sync_->adjustTimestamp(
                output.exchangeId, output.exchange_timestamp);
        } else {
            output.adjusted_timestamp = output.receivedTimestamp;
        }
        
        return true;
    }
    
private:
    std::shared_ptr<TimeSync> time_sync_;
};

// Publisher Stage
class PublisherStage : public PipelineStage<OrderBookUpdate, OrderBookUpdate> {
public:
    PublisherStage(
        ThreadSafeQueue<OrderBookUpdate>* input_queue,
        ThreadSafeQueue<OrderBookUpdate>* output_queue,
        std::shared_ptr<OrderBookPublisher> publisher,
        std::shared_ptr<PipelineMetrics> metrics = nullptr)
        : PipelineStage("publisher", input_queue, output_queue, metrics)
        , publisher_(publisher) {}
    
    bool process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        // Copy the input
        output = input;
        
        // Publish the update
        publisher_->publishUpdate(output);
        
        return true;
    }
    
private:
    std::shared_ptr<OrderBookPublisher> publisher_;
};

// Main normalization pipeline that ties everything together
class NormalizationPipeline {
public:
    NormalizationPipeline(
        std::shared_ptr<AdapterFactory> adapter_factory,
        std::shared_ptr<OrderBookManager> order_book_manager,
        std::shared_ptr<DataQualityControl> quality_control,
        std::shared_ptr<TimeSync> time_sync,
        std::shared_ptr<OrderBookPublisher> publisher,
        size_t queue_size = 10000) 
    {
        // Create metrics
        metrics_ = std::make_shared<PipelineMetrics>();
        
        // Create queues
        input_queue_ = std::make_unique<ThreadSafeQueue<MarketDataUpdate>>(queue_size);
        adapter_queue_ = std::make_unique<ThreadSafeQueue<OrderBookUpdate>>(queue_size);
        order_book_queue_ = std::make_unique<ThreadSafeQueue<OrderBookUpdate>>(queue_size);
        quality_queue_ = std::make_unique<ThreadSafeQueue<OrderBookUpdate>>(queue_size);
        timestamp_queue_ = std::make_unique<ThreadSafeQueue<OrderBookUpdate>>(queue_size);
        output_queue_ = std::make_unique<ThreadSafeQueue<OrderBookUpdate>>(queue_size);
        
        // Create stages
        adapter_stage_ = std::make_unique<ProtocolAdapterStage>(
            input_queue_.get(), adapter_queue_.get(), adapter_factory, metrics_);
            
        order_book_stage_ = std::make_unique<OrderBookStage>(
            adapter_queue_.get(), order_book_queue_.get(), order_book_manager, metrics_);
            
        quality_stage_ = std::make_unique<DataQualityStage>(
            order_book_queue_.get(), quality_queue_.get(), quality_control, metrics_);
            
        timestamp_stage_ = std::make_unique<TimestampStage>(
            quality_queue_.get(), timestamp_queue_.get(), time_sync, metrics_);
            
        publisher_stage_ = std::make_unique<PublisherStage>(
            timestamp_queue_.get(), output_queue_.get(), publisher, metrics_);
    }
    
    ~NormalizationPipeline() {
        stop();
    }
    
    // Start all pipeline stages
    void start() {
        adapter_stage_->start();
        order_book_stage_->start();
        quality_stage_->start();
        timestamp_stage_->start();
        publisher_stage_->start();
        
        is_running_ = true;
    }
    
    // Stop all pipeline stages
    void stop() {
        if (!is_running_) return;
        
        // Stop stages in reverse order
        publisher_stage_->stop();
        timestamp_stage_->stop();
        quality_stage_->stop();
        order_book_stage_->stop();
        adapter_stage_->stop();
        
        // Shutdown queues
        input_queue_->shutdown();
        adapter_queue_->shutdown();
        order_book_queue_->shutdown();
        quality_queue_->shutdown();
        timestamp_queue_->shutdown();
        output_queue_->shutdown();
        
        is_running_ = false;
    }
    
    // Push data into the pipeline
    bool push(const MarketDataUpdate& update) {
        if (!is_running_) return false;
        
        // Update metric
        metrics_->increment_input_count(update.exchangeId, update.tradingPair);
        
        // Push to input queue
        return input_queue_->push(update);
    }
    
    // Get pipeline metrics
    std::shared_ptr<PipelineMetrics> getMetrics() const {
        return metrics_;
    }
    
    // Get pipeline status information
    struct PipelineStatus {
        bool is_running;
        
        struct StageStatus {
            std::string name;
            size_t queue_size;
            size_t processed_count;
            size_t error_count;
            bool is_running;
        };
        
        std::vector<StageStatus> stages;
    };
    
    PipelineStatus getStatus() const {
        PipelineStatus status;
        status.is_running = is_running_;
        
        // Add stage statuses
        status.stages.push_back({
            adapter_stage_->get_name(),
            input_queue_->size(),
            adapter_stage_->get_processed_count(),
            adapter_stage_->get_error_count(),
            adapter_stage_->is_running()
        });
        
        status.stages.push_back({
            order_book_stage_->get_name(),
            adapter_queue_->size(),
            order_book_stage_->get_processed_count(),
            order_book_stage_->get_error_count(),
            order_book_stage_->is_running()
        });
        
        status.stages.push_back({
            quality_stage_->get_name(),
            order_book_queue_->size(),
            quality_stage_->get_processed_count(),
            quality_stage_->get_error_count(),
            quality_stage_->is_running()
        });
        
        status.stages.push_back({
            timestamp_stage_->get_name(),
            quality_queue_->size(),
            timestamp_stage_->get_processed_count(),
            timestamp_stage_->get_error_count(),
            timestamp_stage_->is_running()
        });
        
        status.stages.push_back({
            publisher_stage_->get_name(),
            timestamp_queue_->size(),
            publisher_stage_->get_processed_count(),
            publisher_stage_->get_error_count(),
            publisher_stage_->is_running()
        });
        
        return status;
    }
    
private:
    // Pipeline state
    bool is_running_ = false;
    
    // Metrics
    std::shared_ptr<PipelineMetrics> metrics_;
    
    // Queues
    std::unique_ptr<ThreadSafeQueue<MarketDataUpdate>> input_queue_;
    std::unique_ptr<ThreadSafeQueue<OrderBookUpdate>> adapter_queue_;
    std::unique_ptr<ThreadSafeQueue<OrderBookUpdate>> order_book_queue_;
    std::unique_ptr<ThreadSafeQueue<OrderBookUpdate>> quality_queue_;
    std::unique_ptr<ThreadSafeQueue<OrderBookUpdate>> timestamp_queue_;
    std::unique_ptr<ThreadSafeQueue<OrderBookUpdate>> output_queue_;
    
    // Stages
    std::unique_ptr<ProtocolAdapterStage> adapter_stage_;
    std::unique_ptr<OrderBookStage> order_book_stage_;
    std::unique_ptr<DataQualityStage> quality_stage_;
    std::unique_ptr<TimestampStage> timestamp_stage_;
    std::unique_ptr<PublisherStage> publisher_stage_;
};

} // namespace data_ingestion
} // namespace trading
