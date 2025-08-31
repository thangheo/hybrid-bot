/**
 * @file example.cpp
 * @brief Real-time data ingestion pipeline with live WebSocket data
 * 
 * Integrates Binance WebSocket client with the normalization pipeline
 * to process real market data through all pipeline stages.
 */

#include "pipeline/pipeline.hpp"
#include "data_ingestion/pipeline/normalization_pipeline.hpp"
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/publisher/zeromq_publisher.hpp"
#include "data_ingestion/normalization/binance_exchange_adapter.hpp"
#include "data_ingestion/config.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "common/exchange_types.hpp"
#include "core/market_types.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <signal.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <iomanip>
#include <vector>

using namespace trading::data_ingestion;
using namespace trading::common;
using namespace trading::core;

// Global flag for graceful shutdown
std::atomic<bool> g_running{true};
std::atomic<uint64_t> g_messageCount{0};
std::atomic<uint64_t> g_totalBytes{0};

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    g_running = false;
}

// Real-time WebSocket data feeder for the pipeline
class BinanceWebSocketFeeder {
private:
    std::shared_ptr<trading::data_ingestion::NormalizationPipeline> pipeline_;
    std::unique_ptr<ConnectionPool> connectionPool_;
    std::vector<ClientInterfacePtr> activeConnections_;
    std::atomic<bool> running_{false};
    std::thread metricsThread_;
    
    // Statistics
    std::chrono::steady_clock::time_point startTime_;
    std::atomic<uint64_t> messagesReceived_{0};
    std::atomic<uint64_t> bytesReceived_{0};
    std::atomic<uint64_t> pipelineErrors_{0};

public:
    BinanceWebSocketFeeder(std::shared_ptr<trading::data_ingestion::NormalizationPipeline> pipeline)
        : pipeline_(pipeline), startTime_(std::chrono::steady_clock::now()) {
        
        // Create connection pool with callback
        connectionPool_ = std::make_unique<ConnectionPool>(
            "binance_realtime_pool",
            [this]() -> ClientInterfacePtr {
                return std::make_shared<BinanceWebSocketClient>(
                    "stream.binance.com", 
                    "9443",
                    [this](const ExchangeId& exchange, const std::string& symbol, const std::vector<uint8_t>& data) {
                        this->onWebSocketData(exchange, symbol, data);
                    }
                );
            },
            2,  // Min connections
            4   // Max connections  
        );
    }
    
    ~BinanceWebSocketFeeder() {
        stop();
    }
    
    bool start(const std::vector<std::string>& symbols) {
        if (running_.load()) {
            std::cout << "âšī¸ Feeder already running" << std::endl;
            return true;
        }
        
        std::cout << "đ Starting Binance WebSocket feeder..." << std::endl;
        
        // Initialize connection pool
        connectionPool_->initialize();
        if (!connectionPool_->connectAll()) {
            std::cerr << "âŒ Failed to establish WebSocket connections" << std::endl;
            return false;
        }
        
        std::cout << "âœ… WebSocket connections established" << std::endl;
        
        // Acquire connections
        std::cout << "đ Acquiring connections for " << symbols.size() << " symbols..." << std::endl;
        
        // Split symbols across connections for load balancing
        int connectionsNeeded = std::min(static_cast<int>(symbols.size() / 5 + 1), 4);
        
        for (int i = 0; i < connectionsNeeded; ++i) {
            auto conn = connectionPool_->acquireConnection(5000);
            if (conn) {
                activeConnections_.push_back(conn);
                std::cout << "âœ… Acquired connection " << (i + 1) << std::endl;
            } else {
                std::cerr << "âš ī¸ Failed to acquire connection " << (i + 1) << std::endl;
            }
        }
        
        if (activeConnections_.empty()) {
            std::cerr << "âŒ No connections available" << std::endl;
            return false;
        }
        
        // Subscribe symbols across connections
        std::cout << "đ Subscribing to market data streams..." << std::endl;
        
        for (size_t i = 0; i < activeConnections_.size(); ++i) {
            // Distribute symbols across connections
            std::vector<Symbol> connectionSymbols;
            
            for (size_t j = i; j < symbols.size(); j += activeConnections_.size()) {
                connectionSymbols.emplace_back(Symbol::fromString(symbols[j]));
            }
            
            if (!connectionSymbols.empty()) {
                if (activeConnections_[i]->subscribe(connectionSymbols)) {
                    std::cout << "âœ… Connection " << (i + 1) << " subscribed to " 
                              << connectionSymbols.size() << " symbols" << std::endl;
                } else {
                    std::cerr << "âŒ Connection " << (i + 1) << " subscription failed" << std::endl;
                }
            }
        }
        
        running_.store(true);
        
        // Start metrics thread
        metricsThread_ = std::thread(&BinanceWebSocketFeeder::metricsLoop, this);
        
        std::cout << "âœ… WebSocket feeder started successfully" << std::endl;
        return true;
    }
    
    void stop() {
        if (!running_.load()) return;
        
        std::cout << "đ Stopping WebSocket feeder..." << std::endl;
        running_.store(false);
        
        // Stop metrics thread
        if (metricsThread_.joinable()) {
            metricsThread_.join();
        }
        
        // Unsubscribe and release connections
        for (auto& conn : activeConnections_) {
            connectionPool_->releaseConnection(conn);
        }
        activeConnections_.clear();
        
        // Shutdown pool
        connectionPool_->shutdown();
        
        std::cout << "âœ… WebSocket feeder stopped" << std::endl;
    }
    
private:
    void onWebSocketData(const ExchangeId& exchange, const std::string& symbol, const std::vector<uint8_t>& data) {
        // Update statistics
        messagesReceived_.fetch_add(1);
        bytesReceived_.fetch_add(data.size());
        g_messageCount.fetch_add(1);
        g_totalBytes.fetch_add(data.size());
        
        // Convert to ExchangeData format for pipeline
        ExchangeData exchangeData;
        exchangeData.exchangeId = exchange;
        exchangeData.symbol = Symbol::fromString(symbol);
        exchangeData.data = std::string(data.begin(), data.end());
        exchangeData.receivedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        exchangeData.dataType = "orderbook";
        exchangeData.is_snapshot = false;
        exchangeData.sourceType = "websocket";
        
        // Push to pipeline
        if (pipeline_ && !pipeline_->push(exchangeData)) {
            pipelineErrors_.fetch_add(1);
            // Pipeline queue is full - this is expected under high load
        }
    }
    
    void metricsLoop() {
        while (running_.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            
            if (running_.load()) {
                printDetailedMetrics();
            }
        }
    }
    
    void printDetailedMetrics() {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
        
        uint64_t messages = messagesReceived_.load();
        uint64_t bytes = bytesReceived_.load();
        uint64_t errors = pipelineErrors_.load();
        
        std::cout << "\n=== LIVE DATA METRICS ===" << std::endl;
        std::cout << "Runtime: " << elapsed << "s" << std::endl;
        std::cout << "Messages received: " << messages << std::endl;
        std::cout << "Data volume: " << std::fixed << std::setprecision(2) 
                  << bytes / 1024.0 << " KB" << std::endl;
        std::cout << "Rate: " << (elapsed > 0 ? messages / elapsed : 0) << " msg/s" << std::endl;
        std::cout << "Pipeline errors: " << errors << std::endl;
        std::cout << "Active connections: " << activeConnections_.size() << std::endl;
        std::cout << "Pool health: " << (connectionPool_->isHealthy() ? "GOOD" : "POOR") << std::endl;
        std::cout << "=========================" << std::endl;
    }
};

// Enhanced pipeline status printer
void printEnhancedPipelineStatus(const std::shared_ptr<PipelineStatus>& status) {
    std::cout << "\n=== PIPELINE STATUS ===" << std::endl;
    std::cout << "Overall: " << (status->isHealthy() ? "đĸ HEALTHY" : "đ´ UNHEALTHY") << std::endl;
    std::cout << "Running: " << (status->isRunning() ? "âļī¸ YES" : "âšī¸ NO") << std::endl;
    
    auto stageStates = status->getStageStates();
    std::cout << "Stages:" << std::endl;
    for (const auto& [stageName, state] : stageStates) {
        std::string stateStr;
        std::string emoji;
        
        switch (state) {
            case PipelineStatus::StageState::RUNNING:
                stateStr = "RUNNING";
                emoji = "âļī¸";
                break;
            case PipelineStatus::StageState::STOPPED:
                stateStr = "STOPPED";
                emoji = "âšī¸";
                break;
            case PipelineStatus::StageState::ERROR:
                stateStr = "ERROR";
                emoji = "âŧī¸";
                break;
            default:
                stateStr = "UNKNOWN";
                emoji = "âī¸";
                break;
        }
        
        std::cout << "  " << emoji << " " << stageName << ": " << stateStr << std::endl;
    }
    
    if (!status->getLastError().empty()) {
        std::cout << "âŧī¸ Last Error: " << status->getLastError() << std::endl;
    }
    std::cout << "=======================" << std::endl;
}

// Helper function to configure the time synchronizer properly
void configureTimeSynchronizer(std::shared_ptr<TimeSynchronizer> synchronizer) {
    TimeSynchronizer::Config config;
    
    // Reasonable defaults for crypto trading
    config.maxAllowedOffset = std::chrono::milliseconds(100);  // 100ms max offset
    config.enableDriftCorrection = true;
    config.enableExchangeCalibration = true;
    config.syncInterval = std::chrono::minutes(30);  // Sync every 30 minutes
    
    // Add some NTP servers for time sync
    config.ntpServers = {
        "pool.ntp.org",
        "time.google.com"
    };
    
    synchronizer->configure(config);
}

int main() {
    // Setup signal handler for graceful shutdown
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "=== REAL-TIME CRYPTO TRADING DATA PIPELINE ===" << std::endl;
    std::cout << "đ Initializing live market data processing..." << std::endl;
    
    try {
        // Create pipeline with optimized configuration for live data
        PipelineConfig config;
        config.maxQueueSize = 50000;    // Large queue for high-frequency data
        config.threadPoolSize = 8;      // More threads for real-time processing
        config.enableMetrics = true;
        config.metricsInterval = std::chrono::milliseconds(2000);  // Faster metrics updates
        
        std::cout << "đ§ Creating enhanced normalization pipeline..." << std::endl;
        auto uniquePipeline = createDefaultNormalizationPipeline(config);
        auto pipeline = std::shared_ptr<trading::data_ingestion::NormalizationPipeline>(uniquePipeline.release());
        
        // Start the pipeline first
        std::cout << "âļī¸ Starting pipeline..." << std::endl;
        if (!pipeline->start()) {
            std::cerr << "âŒ Failed to start pipeline!" << std::endl;
            return 1;
        }
        
        // Wait for pipeline to stabilize
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Create and start WebSocket data feeder
        std::cout << "đ Setting up live data feeder..." << std::endl;
        BinanceWebSocketFeeder feeder(pipeline);
        
        // Subscribe to a comprehensive list of crypto pairs
        std::vector<std::string> symbols = {
            // Major cryptocurrencies
            "BTCUSDT",   // Bitcoin
            "ETHUSDT",   // Ethereum  
            "BNBUSDT",   // Binance Coin
            "ADAUSDT",   // Cardano
            "SOLUSDT",   // Solana
            "XRPUSDT",   // Ripple
            "DOTUSDT",   // Polkadot
            "AVAXUSDT",  // Avalanche
            "MATICUSDT", // Polygon
            "LINKUSDT",  // Chainlink
            
            // Popular altcoins for more data volume
            "DOGEUSDT",  // Dogecoin
            "SHIBUSDT",  // Shiba Inu
            "UNIUSDT",   // Uniswap
            "ATOMUSDT",  // Cosmos
            "FTMUSDT"    // Fantom
        };
        
        std::cout << "đ Starting live data collection from " << symbols.size() << " trading pairs..." << std::endl;
        
        if (!feeder.start(symbols)) {
            std::cerr << "âŒ Failed to start WebSocket feeder!" << std::endl;
            return 1;
        }
        
        // Monitor pipeline with live data
        std::cout << "\nđ LIVE MARKET DATA PROCESSING ACTIVE!" << std::endl;
        std::cout << "âšī¸ Processing real-time order book updates from Binance" << std::endl;
        std::cout << "âšī¸ Press Ctrl+C to stop gracefully..." << std::endl;
        
        auto startTime = std::chrono::steady_clock::now();
        int statusCounter = 0;
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));  // Check every 5 seconds
            statusCounter++;
            
            // Print comprehensive status every 20 seconds (every 4th iteration)
            if (statusCounter % 4 == 0) {
                auto status = pipeline->getStatus();
                printEnhancedPipelineStatus(status);
                
                // Print pipeline throughput
                auto metrics = pipeline->getMetrics();
                double throughput = metrics->getThroughputPerSecond();
                
                std::cout << "\nđ THROUGHPUT METRICS:" << std::endl;
                std::cout << "Pipeline: " << std::fixed << std::setprecision(1) 
                         << throughput << " updates/sec" << std::endl;
                std::cout << "WebSocket: " << std::fixed << std::setprecision(1)
                         << (statusCounter > 0 ? g_messageCount.load() / (statusCounter * 5.0) : 0) 
                         << " messages/sec" << std::endl;
                std::cout << "Data Volume: " << std::fixed << std::setprecision(2)
                         << g_totalBytes.load() / 1024.0 / 1024.0 << " MB total" << std::endl;
            } else {
                // Show activity indicator
                auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
                    std::chrono::steady_clock::now() - startTime).count();
                std::cout << "đ Live processing... (" << elapsed << " min, " 
                         << g_messageCount.load() << " messages)" << std::endl;
            }
            
            // Check pipeline health
            auto status = pipeline->getStatus();
            if (!status->isHealthy()) {
                std::cerr << "âŒ Pipeline became unhealthy: " << status->getLastError() << std::endl;
                std::cout << "đ ī¸ Attempting to continue with degraded performance..." << std::endl;
                // Continue running but log the issue
            }
            
            // Auto-stop after 5 minutes for demo (remove this for production)
            auto runTime = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::steady_clock::now() - startTime).count();
            if (runTime >= 5) {
                std::cout << "\nâąī¸ Demo time limit (5 minutes) reached. Stopping..." << std::endl;
                break;
            }
        }
        
        // Graceful shutdown sequence
        std::cout << "\nđ INITIATING GRACEFUL SHUTDOWN..." << std::endl;
        
        // 1. Stop WebSocket data collection
        std::cout << "1/3 đ Stopping WebSocket data collection..." << std::endl;
        feeder.stop();
        
        // 2. Allow pipeline to process remaining data
        std::cout << "2/3 â Draining pipeline buffers..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        // 3. Stop pipeline
        std::cout << "3/3 đ Stopping processing pipeline..." << std::endl;
        pipeline->stop();
        
        // Print comprehensive final statistics
        std::cout << "\n" << std::string(50, '=') << std::endl;
        std::cout << "đ FINAL EXECUTION REPORT" << std::endl;
        std::cout << std::string(50, '=') << std::endl;
        
        auto finalMetrics = pipeline->getMetrics();
        auto totalRuntime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count();
        
        std::cout << "đ Runtime Statistics:" << std::endl;
        std::cout << "  Total Runtime: " << totalRuntime << " seconds" << std::endl;
        std::cout << "  Messages Processed: " << finalMetrics->getTotalInputCount() << std::endl;
        std::cout << "  Processing Errors: " << finalMetrics->getTotalErrorCount() << std::endl;
        std::cout << "  Success Rate: " << std::fixed << std::setprecision(2) 
                 << (100.0 * (1.0 - finalMetrics->getErrorRate())) << "%" << std::endl;
        
        std::cout << "\nđĄ Performance Metrics:" << std::endl;
        std::cout << "  Current Throughput: " << std::fixed << std::setprecision(1)
                 << finalMetrics->getThroughputPerSecond() << " updates/sec" << std::endl;
        std::cout << "  Average Throughput: " << std::fixed << std::setprecision(1)
                 << finalMetrics->getThroughputPerSecond() << " updates/sec" << std::endl;
        std::cout << "  Total Data Volume: " << std::fixed << std::setprecision(2)
                 << g_totalBytes.load() / 1024.0 / 1024.0 << " MB" << std::endl;
        
        std::cout << "\nđŊ Data Quality:" << std::endl;
        std::cout << "  WebSocket Messages: " << g_messageCount.load() << std::endl;
        std::cout << "  Pipeline Input Rate: " << std::fixed << std::setprecision(1)
                 << (totalRuntime > 0 ? finalMetrics->getTotalInputCount() / static_cast<double>(totalRuntime) : 0) 
                 << " updates/sec" << std::endl;
        
        if (finalMetrics->getTotalErrorCount() > 0) {
            std::cout << "\nâšī¸ Error Analysis:" << std::endl;
            std::cout << "  Error Rate: " << std::fixed << std::setprecision(3)
                     << (finalMetrics->getErrorRate() * 100.0) << "%" << std::endl;
        }
        
        std::cout << std::string(50, '=') << std::endl;
        std::cout << "âœ… Real-time data pipeline execution completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "âŒ Pipeline execution failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}