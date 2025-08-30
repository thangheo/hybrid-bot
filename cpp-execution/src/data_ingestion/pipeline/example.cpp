/**
 * @file example.cc
 * @brief Example usage of the void printPipelineStatus(const NormalizationPipeline::PipelineStatus& status) {
    std::cout << "\n=== Pipeline Status ===" << std::endl;
    std::cout << "Overall State: " << (status.is_healthy() ? "HEALTHY" : "UNHEALTHY") << std::endl;
    std::cout << "Running:       " << (status.is_running() ? "YES" : "NO") << std::endl;

    std::cout << "Stages:" << std::endl;
    for (const auto& stage : status.stages) {
        std::cout << "  " << stage.name << ": " 
                 << (stage.is_running ? "RUNNING" : "STOPPED") << std::endl;
    }peline
 * 
 * This example demonstrates a multi-stage pipeline for processing market data:
 * 1. Protocol Adapter Stage - Convert raw data to standard format
 * 2. Order Book Stage - Update in-memory order books
 * 3. Data Quality Stage - Apply quality checks
 * 4. Timestamp Stage - Synchronize timestamps
 * 5. Publisher Stage - Distribute processed data
 */

#include "pipeline/normalization_pipeline.hpp"
// #include "pipeline/protocol_adapters/adapter_factory.hpp"
// #include "orderbook/order_book_manager.hpp"
// #include "pipeline/quality/data_quality_control.hpp"
// #include "pipeline/synchronization/time_sync.hpp"
// #include "pipeline/publishers/order_book_publisher.hpp"
#include "websocket/exchanges/binance_websocket_client.hpp"
#include "common/exchange_types.hpp"
#include "core/market_types.hpp"

#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <signal.h>

using namespace trading::data_ingestion;
using namespace trading::common;
using namespace trading::core;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    g_running = false;
}

void printPipelineMetrics(const PipelineMetrics& metrics) {
    std::cout << "\n=== Pipeline Metrics ===" << std::endl;
    std::cout << "Total Input:          " << metrics.get_total_input_count() << std::endl;
    std::cout << "Total Output:         " << metrics.get_total_output_count() << std::endl;
    
    auto stages = metrics.get_stage_names();
    std::cout << "Stage Errors:" << std::endl;
    for (const auto& stage : stages) {
        std::cout << "  " << stage << ": " << metrics.get_error_count(stage) << std::endl;
    }
    
    std::cout << "Error Count:          " << metrics.get_total_error_count() << std::endl;
}

void printPipelineStatus(const PipelineStatus& status) {
    std::cout << "\n=== Pipeline Status ===" << std::endl;
    std::cout << "Overall State: " << (status.isHealthy() ? "HEALTHY" : "UNHEALTHY") << std::endl;
    std::cout << "Running:       " << (status.isRunning() ? "YES" : "NO") << std::endl;
    
    auto stageStates = status.getStageStates();
    std::cout << "Stage States:" << std::endl;
    for (const auto& [stageName, state] : stageStates) {
        std::cout << "  " << stageName << ": " << 
            (state == PipelineStatus::StageState::RUNNING ? "RUNNING" :
             state == PipelineStatus::StageState::STOPPED ? "STOPPED" :
             state == PipelineStatus::StageState::ERROR ? "ERROR" : "UNKNOWN") << std::endl;
    }
    
    if (!status.getLastError().empty()) {
        std::cout << "Last Error: " << status.getLastError() << std::endl;
    }
}

// Mock market data generator for demonstration
class MockMarketDataGenerator {
public:
    MockMarketDataGenerator(std::shared_ptr<NormalizationPipeline> pipeline)
        : pipeline_(pipeline), running_(false) {}
    
    void start() {
        running_ = true;
        generator_thread_ = std::thread(&MockMarketDataGenerator::generateData, this);
    }
    
    void stop() {
        running_ = false;
        if (generator_thread_.joinable()) {
            generator_thread_.join();
        }
    }

private:
    void generateData() {
        int counter = 0;
        while (running_ && g_running) {
            // Generate mock Binance market data
            MarketDataUpdate update;
            update.exchangeId = ExchangeId::binance();
            update.symbol = Symbol::fromString("BTC/USDT");
            update.timestamp = std::chrono::system_clock::now();
            update.sequence = ++counter;
            
            // Mock order book data
            update.bids = {
                {50000.00 - (counter % 10), 1.5 + (counter % 5) * 0.1},
                {49999.50 - (counter % 10), 2.0 + (counter % 3) * 0.2},
                {49999.00 - (counter % 10), 0.8 + (counter % 7) * 0.05}
            };
            
            update.asks = {
                {50000.50 + (counter % 10), 1.2 + (counter % 4) * 0.1},
                {50001.00 + (counter % 10), 1.8 + (counter % 6) * 0.15},
                {50001.50 + (counter % 10), 0.9 + (counter % 8) * 0.08}
            };
            
            // Push to pipeline
            if (pipeline_) {
                pipeline_->push(update);
            }
            
            // Generate data every 100ms
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    std::shared_ptr<NormalizationPipeline> pipeline_;
    std::atomic<bool> running_;
    std::thread generator_thread_;
};

int main() {
    // Setup signal handler for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "=== Data Ingestion Pipeline Example ===" << std::endl;
    std::cout << "Starting multi-stage market data processing pipeline..." << std::endl;
    
    try {
        // Create pipeline components
        auto adapter_factory = std::make_shared<AdapterFactory>();
        auto order_book_manager = std::make_shared<OrderBookManager>();
        auto quality_control = std::make_shared<DataQualityControl>();
        auto time_sync = std::make_shared<TimeSync>();
        auto publisher = std::make_shared<OrderBookPublisher>();
        
        // Configure components
        std::cout << "\nConfiguring pipeline components..." << std::endl;
        
        // Configure adapter factory for supported exchanges
        adapter_factory->registerAdapter(ExchangeId::binance(), 
            std::make_unique<BinanceProtocolAdapter>());
        
        // Configure quality control thresholds
        DataQualityConfig qc_config;
        qc_config.max_spread_percentage = 5.0;  // 5% max spread
        qc_config.min_volume_threshold = 0.001;  // Minimum volume
        qc_config.max_price_deviation = 10.0;   // 10% max price change
        qc_config.enable_sequence_validation = true;
        quality_control->configure(qc_config);
        
        // Configure time synchronization
        TimeSyncConfig ts_config;
        ts_config.max_time_skew_ms = 1000;  // 1 second max skew
        ts_config.enable_ntp_sync = false;  // Disable for demo
        time_sync->configure(ts_config);
        
        // Configure publisher endpoints
        PublisherConfig pub_config;
        pub_config.zmq_endpoints = {
            "tcp://*:5555",  // Market data feed
            "tcp://*:5556"   // Order book updates
        };
        pub_config.enable_compression = true;
        pub_config.max_queue_size = 10000;
        publisher->configure(pub_config);
        
        // Create the normalization pipeline
        std::cout << "Creating normalization pipeline with 5 stages..." << std::endl;
        auto pipeline = std::make_shared<NormalizationPipeline>(
            adapter_factory,
            order_book_manager,
            quality_control,
            time_sync,
            publisher
        );
        
        // Configure pipeline parameters
        PipelineConfig config;
        config.max_queue_size = 1000;
        config.thread_pool_size = 4;
        config.enable_metrics = true;
        config.metrics_interval_ms = 5000;  // Report every 5 seconds
        pipeline->configure(config);
        
        // Start the pipeline
        std::cout << "Starting pipeline..." << std::endl;
        if (!pipeline->start()) {
            std::cerr << "Failed to start pipeline!" << std::endl;
            return 1;
        }
        
        // Wait for pipeline to initialize
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Start mock data generator
        std::cout << "Starting mock market data generator..." << std::endl;
        MockMarketDataGenerator generator(pipeline);
        generator.start();
        
        // Monitor pipeline for 30 seconds
        std::cout << "Pipeline running. Press Ctrl+C to stop..." << std::endl;
        
        auto start_time = std::chrono::steady_clock::now();
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            // Print metrics every 5 seconds
            auto metrics = pipeline->getMetrics();
            printPipelineMetrics(metrics);
            
            // Print status
            auto status = pipeline->getStatus();
            printPipelineStatus(status);
            
            // Check for errors
            if (!status.isHealthy()) {
                std::cerr << "Pipeline unhealthy: " << status.getLastError() << std::endl;
                break;
            }
            
            // Run for maximum 60 seconds in demo mode
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed > std::chrono::seconds(60)) {
                std::cout << "Demo time limit reached. Stopping..." << std::endl;
                break;
            }
        }
        
        // Graceful shutdown
        std::cout << "\nShutting down pipeline..." << std::endl;
        
        // Stop data generation
        generator.stop();
        
        // Stop pipeline
        pipeline->stop();
        
        // Print final metrics
        std::cout << "\n=== Final Pipeline Statistics ===" << std::endl;
        auto final_metrics = pipeline->getMetrics();
        printPipelineMetrics(final_metrics);
        
        std::cout << "\nPipeline stopped successfully." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Pipeline error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Data Ingestion Pipeline Example completed." << std::endl;
    return 0;
}
