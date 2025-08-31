/**
 * @file fixed_simple_collector.cpp
 * @brief Fixed data collector using correct BinanceWebSocketClient interface
 */

#include "pipeline/normalization_pipeline.hpp"
#include "websocket/exchanges/binance_websocket_client.hpp"
#include <iostream>
#include "pipeline/pipeline_all.hpp"
using namespace trading::data_ingestion;
using namespace trading::common;
using namespace trading::core;

class SimplifiedDataCollector {
public:
    SimplifiedDataCollector(std::shared_ptr<NormalizationPipeline> pipeline)
        : pipeline_(pipeline) {}
    
    bool start() {
        // Create Binance WebSocket client
        binanceClient_ = std::make_shared<BinanceWebSocketClient>(
            "stream.binance.com",  // host
            "9443",                // port
            nullptr                // callback (we'll set it below)
        );
        
        // Set callback using the correct method name
        binanceClient_->setMessageCallback([this](
            const ExchangeId& exchangeId,
            const std::string& message, 
            const std::vector<uint8_t>& data) {
            
            // Convert message to ExchangeData
            ExchangeData exchangeData;
            exchangeData.exchangeId = exchangeId;
            exchangeData.data = data;  // Use the data vector directly
            exchangeData.receivedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            exchangeData.sourceType = "websocket";
            
            // Push to pipeline
            if (!pipeline_->push(exchangeData)) {
                std::cerr << "Failed to push data to pipeline (queue full?)" << std::endl;
            }
        });
        
        // Connect and subscribe to BTC/USDT depth stream
        if (!binanceClient_->connect()) {
            std::cerr << "Failed to connect to Binance WebSocket" << std::endl;
            return false;
        }
        
        // Subscribe to order book stream
        std::vector<std::string> streams = {"btcusdt@depth"};
        if (!binanceClient_->subscribeToStreams(streams)) {
            std::cerr << "Failed to subscribe to Binance streams" << std::endl;
            return false;
        }
        
        std::cout << "Successfully connected to Binance WebSocket and subscribed to BTC/USDT depth stream" << std::endl;
        return true;
    }
    
    void stop() {
        if (binanceClient_) {
            std::cout << "Disconnecting from Binance WebSocket..." << std::endl;
            binanceClient_->disconnect();
        }
    }
    
    // Get connection status
    bool isConnected() const {
        return binanceClient_ && binanceClient_->isConnected();
    }
    
    // Get statistics
    void printStats() const {
        if (binanceClient_) {
            std::cout << "WebSocket Stats:" << std::endl;
            std::cout << "  Messages received: " << binanceClient_->getMessageCount() << std::endl;
            std::cout << "  Average latency: " << binanceClient_->getAverageLatency() << " ms" << std::endl;
            std::cout << "  Connection status: " << (isConnected() ? "CONNECTED" : "DISCONNECTED") << std::endl;
        }
    }
    
private:
    std::shared_ptr<NormalizationPipeline> pipeline_;
    std::shared_ptr<BinanceWebSocketClient> binanceClient_;
};

// Complete main function for testing
int main() {
    std::cout << "=== Simplified Trading System with Pipeline ===" << std::endl;
    
    try {
        // Create pipeline with development config
        auto config = PipelinePresets::development();
        std::cout << "Creating pipeline with config:" << std::endl;
        std::cout << "  Max Queue Size: " << config.maxQueueSize << std::endl;
        std::cout << "  Thread Pool: " << config.threadPoolSize << std::endl;
        
        auto pipeline = createDefaultNormalizationPipeline(config);
        
        // Create data collector
        SimplifiedDataCollector collector(pipeline);
        
        // Start pipeline
        std::cout << "\nStarting pipeline..." << std::endl;
        if (!pipeline->start()) {
            std::cerr << "Failed to start pipeline!" << std::endl;
            return 1;
        }
        
        // Start data collection
        std::cout << "Starting Binance data collection..." << std::endl;
        if (!collector.start()) {
            std::cerr << "Failed to start data collector!" << std::endl;
            return 1;
        }
        
        // Monitor for 30 seconds
        std::cout << "\nSystem running. Monitoring for 30 seconds..." << std::endl;
        
        for (int i = 0; i < 6; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            std::cout << "\n--- Status Report " << (i+1) << " ---" << std::endl;
            
            // Print pipeline metrics
            auto metrics = pipeline->getMetrics();
            std::cout << "Pipeline processed " << metrics->getTotalOutputCount() 
                      << " messages (errors: " << metrics->getTotalErrorCount() << ")" << std::endl;
            
            // Print WebSocket stats
            collector.printStats();
            
            // Check health
            auto status = pipeline->getStatus();
            if (!status->isHealthy()) {
                std::cout << "WARNING: Pipeline unhealthy - " << status->getLastError() << std::endl;
            }
        }
        
        // Graceful shutdown
        std::cout << "\nShutting down..." << std::endl;
        collector.stop();
        pipeline->stop();
        
        // Final stats
        std::cout << "\n=== Final Statistics ===" << std::endl;
        auto final_metrics = pipeline->getMetrics();
        std::cout << "Total messages processed: " << final_metrics->getTotalOutputCount() << std::endl;
        std::cout << "Total errors: " << final_metrics->getTotalErrorCount() << std::endl;
        std::cout << "Success rate: " << ((1.0 - final_metrics->getErrorRate()) * 100) << "%" << std::endl;
        
        collector.printStats();
        
        std::cout << "System shutdown complete." << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "System error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}