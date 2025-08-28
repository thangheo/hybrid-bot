// pipeline.hpp
#include <thread>
#include <atomic>
#include <vector>
#include <boost/lockfree/spsc_queue.hpp>
#include <tbb/concurrent_queue.h>


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
                if (inputQueue->pop(input)) {
                    try {
                        process(input, output);
                        if (outputQueue && !outputQueue->push(output)) {
                            // Output queue full, handle backpressure
                            handleBackpressure();
                        }
                    } catch (const std::exception& e) {
                        // Log exception
                        std::cerr << "Exception in pipeline stage: " << e.what() << std::endl;
                    }
                } else {
                    // No input available, yield
                    std::this_thread::yield();
                }
            }
        });
    }
    
    void stop() {
        if (!running.load()) return;
        
        running.store(false);
        if (thread.joinable()) {
            thread.join();
        }
    }
    
protected:
    virtual void handleBackpressure() {
        // Default implementation: just wait a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    PipelineQueue<InputT>* inputQueue;
    PipelineQueue<OutputT>* outputQueue;
    std::atomic<bool> running;
    std::thread thread;
};

// Implementation of pipeline stages

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
                  std::shared_ptr<TimestampSynchronizer> synchronizer)
        : PipelineStage(input, output), synchronizer(synchronizer) {}
    
    void process(const OrderBookUpdate& input, OrderBookUpdate& output) override {
        output = input;  // Copy the input
        
        // Correct timestamps
        output.processedTime = TimestampSynchronizer::getCurrentTime();
        output.exchangeTime = synchronizer->correctTimestamp(
            output.exchangeTime, output.receivedTime, output.exchangeId);
    }
    
private:
    std::shared_ptr<TimestampSynchronizer> synchronizer;
};

// 5. Publisher Stage
class PublisherStage : public PipelineStage<OrderBookUpdate, int> {
public:
    PublisherStage(PipelineQueue<OrderBookUpdate>* input, 
                  PipelineQueue<int>* output,  // No output needed
                  std::shared_ptr<NormalizedDataPublisher> publisher,
                  std::shared_ptr<OrderBookManager> manager)
        : PipelineStage(input, output), publisher(publisher), orderBookManager(manager) {}
    
    void process(const OrderBookUpdate& input, int& output) override {
        // Publish the update
        publisher->publishUpdate(input);
        
        // Generate and publish snapshot if needed
        if (shouldPublishSnapshot(input)) {
            auto snapshot = orderBookManager->getSnapshot(
                input.exchangeId, input.symbol);
            publisher->publishSnapshot(snapshot);
        }
        
        output = 1;  // Dummy output
    }
    
private:
    bool shouldPublishSnapshot(const OrderBookUpdate& update) const {
        // This could be based on various heuristics:
        // - Every N updates
        // - Significant changes
        // - Time-based
        
        // Simple implementation: every 100th update or for snapshots
        static std::unordered_map<std::string, int> updateCounts;
        std::string key = update.exchangeId + ":" + update.symbol;
        
        if (update.isSnapshot) return true;
        
        int& count = updateCounts[key];
        count++;
        if (count >= 100) {
            count = 0;
            return true;
        }
        
        return false;
    }
    
    std::shared_ptr<NormalizedDataPublisher> publisher;
    std::shared_ptr<OrderBookManager> orderBookManager;
};

// Pipeline manager
class NormalizationPipeline {
public:
    NormalizationPipeline(
        std::shared_ptr<AdapterFactory> adapterFactory,
        std::shared_ptr<OrderBookManager> orderBookManager,
        std::shared_ptr<DataQualityControl> qualityControl,
        std::shared_ptr<TimestampSynchronizer> synchronizer,
        std::shared_ptr<NormalizedDataPublisher> publisher) 
    {
        // Create queues
        exchangeDataQueue = std::make_unique<PipelineQueue<ExchangeData>>();
        stage1OutputQueue = std::make_unique<PipelineQueue<OrderBookUpdate>>();
        stage2OutputQueue = std::make_unique<PipelineQueue<OrderBookUpdate>>();
        stage3OutputQueue = std::make_unique<PipelineQueue<OrderBookUpdate>>();
        dummyOutputQueue = std::make_unique<PipelineQueue<int>>();
        
        // Create stages
        adapterStage = std::make_unique<ProtocolAdapterStage>(
            exchangeDataQueue.get(), stage1OutputQueue.get(), adapterFactory);
            
        orderBookStage = std::make_unique<OrderBookStage>(
            stage1OutputQueue.get(), stage2OutputQueue.get(), orderBookManager);
            
        qualityStage = std::make_unique<DataQualityStage>(
            stage2OutputQueue.get(), stage3OutputQueue.get(), qualityControl);
            
        timestampStage = std::make_unique<TimestampStage>(
            stage3OutputQueue.get(), stage3OutputQueue.get(), synchronizer);
            
        publisherStage = std::make_unique<PublisherStage>(
            stage3OutputQueue.get(), dummyOutputQueue.get(), publisher, orderBookManager);
    }
    
    void start() {
        adapterStage->start();
        orderBookStage->start();
        qualityStage->start();
        timestampStage->start();
        publisherStage->start();
    }
    
    void stop() {
        // Stop in reverse order
        publisherStage->stop();
        timestampStage->stop();
        qualityStage->stop();
        orderBookStage->stop();
        adapterStage->stop();
    }
    
    bool pushExchangeData(const ExchangeData& data) {
        return exchangeDataQueue->push(data);
    }
    
private:
    // Queues
    std::unique_ptr<PipelineQueue<ExchangeData>> exchangeDataQueue;
    std::unique_ptr<PipelineQueue<OrderBookUpdate>> stage1OutputQueue;
    std::unique_ptr<PipelineQueue<OrderBookUpdate>> stage2OutputQueue;
    std::unique_ptr<PipelineQueue<OrderBookUpdate>> stage3OutputQueue;
    std::unique_ptr<PipelineQueue<int>> dummyOutputQueue;
    
    // Stages
    std::unique_ptr<ProtocolAdapterStage> adapterStage;
    std::unique_ptr<OrderBookStage> orderBookStage;
    std::unique_ptr<DataQualityStage> qualityStage;
    std::unique_ptr<TimestampStage> timestampStage;
    std::unique_ptr<PublisherStage> publisherStage;
};