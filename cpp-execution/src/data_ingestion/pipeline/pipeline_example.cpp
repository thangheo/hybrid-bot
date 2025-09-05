/**
 * @file pipeline_example_fixed.cpp
 * @brief Fixed version with safe JSON parsing and proper pipeline usage
 */

#include "data_ingestion/pipeline/normalization_pipeline.hpp"
// REMOVE THIS LINE - causes conflicts:
// #include "pipeline/pipeline.hpp"

#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/publisher/zeromq_publisher.hpp"
#include "data_ingestion/protocol/binance_adapter.hpp"
#include "data_ingestion/config/config.hpp"
#include "data_ingestion/orderbook/order_book.hpp"
#include "common/exchange_types.hpp"
#include "core/market_types.hpp"
#include <nlohmann/json.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <memory>
#include <atomic>
#include <signal.h>

using namespace trading::data_ingestion;
using namespace trading::common;
using namespace trading::core;
using json = nlohmann::json;

// Global flags
std::atomic<bool> g_running{true};
std::atomic<uint64_t> g_messageCount{0};
std::atomic<uint64_t> g_totalBytes{0};

void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ". Shutting down gracefully..." << std::endl;
    g_running = false;
}

// SAFE JSON helper functions
bool safeJsonContainsString(const json& j, const std::string& key, const std::string& expectedValue) {
    try {
        if (!j.contains(key)) return false;
        if (!j[key].is_string()) return false;
        return j[key].get<std::string>() == expectedValue;
    } catch (...) {
        return false;
    }
}

std::string safeJsonGetString(const json& j, const std::string& key, const std::string& defaultValue = "") {
    try {
        if (!j.contains(key)) return defaultValue;
        if (!j[key].is_string()) return defaultValue;
        return j[key].get<std::string>();
    } catch (...) {
        return defaultValue;
    }
}

json safeJsonGetArray(const json& j, const std::string& key) {
    try {
        if (!j.contains(key)) return json::array();
        if (!j[key].is_array()) return json::array();
        return j[key];
    } catch (...) {
        return json::array();
    }
}

class BinanceWebSocketFeeder {
private:
    std::shared_ptr<trading::data_ingestion::NormalizationPipeline> pipeline_;
    std::unique_ptr<ConnectionPool> connectionPool_;
    std::vector<ClientInterfacePtr> activeConnections_;
    std::atomic<bool> running_{false};
    std::thread metricsThread_;
    
    std::chrono::steady_clock::time_point startTime_;
    std::atomic<uint64_t> messagesReceived_{0};
    std::atomic<uint64_t> bytesReceived_{0};
    std::atomic<uint64_t> pipelineErrors_{0};

public:
    BinanceWebSocketFeeder(std::shared_ptr<trading::data_ingestion::NormalizationPipeline> pipeline)
        : pipeline_(pipeline), startTime_(std::chrono::steady_clock::now()) {
        
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
            2, 4
        );
    }
    
    ~BinanceWebSocketFeeder() {
        stop();
    }
    
    bool start(const std::vector<std::string>& symbols) {
        if (running_.load()) {
            std::cout << "Feeder already running" << std::endl;
            return true;
        }
        
        std::cout << "Starting Binance WebSocket feeder..." << std::endl;
        
        connectionPool_->initialize();
        if (!connectionPool_->connectAll()) {
            std::cerr << "Failed to establish WebSocket connections" << std::endl;
            return false;
        }
        
        std::cout << "WebSocket connections established" << std::endl;
        
        // Acquire connections
        int connectionsNeeded = std::min(static_cast<int>(symbols.size() / 5 + 1), 4);
        
        for (int i = 0; i < connectionsNeeded; ++i) {
            auto conn = connectionPool_->acquireConnection(5000);
            if (conn) {
                activeConnections_.push_back(conn);
                std::cout << "Acquired connection " << (i + 1) << std::endl;
            }
        }
        
        if (activeConnections_.empty()) {
            std::cerr << "No connections available" << std::endl;
            return false;
        }
        
        // Subscribe symbols
        std::cout << "Subscribing to market data streams..." << std::endl;
        
        for (size_t i = 0; i < activeConnections_.size(); ++i) {
            std::vector<Symbol> connectionSymbols;
            
            for (size_t j = i; j < symbols.size(); j += activeConnections_.size()) {
                connectionSymbols.emplace_back(Symbol::fromString(symbols[j]));
            }
            
            if (!connectionSymbols.empty()) {
                if (activeConnections_[i]->subscribe(connectionSymbols)) {
                    std::cout << "Connection " << (i + 1) << " subscribed to " 
                              << connectionSymbols.size() << " symbols" << std::endl;
                }
            }
        }
        
        running_.store(true);
        metricsThread_ = std::thread(&BinanceWebSocketFeeder::metricsLoop, this);
        
        std::cout << "WebSocket feeder started successfully" << std::endl;
        return true;
    }
    
    void stop() {
        if (!running_.load()) return;
        
        std::cout << "Stopping WebSocket feeder..." << std::endl;
        running_.store(false);
        
        if (metricsThread_.joinable()) {
            metricsThread_.join();
        }
        
        for (auto& conn : activeConnections_) {
            connectionPool_->releaseConnection(conn);
        }
        activeConnections_.clear();
        connectionPool_->shutdown();
        
        std::cout << "WebSocket feeder stopped" << std::endl;
    }
    
private:
    // Add this at the very start of your onWebSocketData callback:
    void onWebSocketData(const ExchangeId& exchange, const std::string& symbol, const std::vector<uint8_t>& data) {
        // Update statistics
        messagesReceived_.fetch_add(1);
        bytesReceived_.fetch_add(data.size());
        g_messageCount.fetch_add(1);
        g_totalBytes.fetch_add(data.size());
        
        std::string dataStr(data.begin(), data.end());
        
        // COMPLETELY SAFE JSON parsing - no crashes possible
        static std::atomic<int> printCount{0};
        if (printCount.fetch_add(1) < 10) {
            try {
                json parsedData = json::parse(dataStr);
                
                // Check if it's a depth update using SAFE access
                if (parsedData.is_object() && 
                    parsedData.contains("e") && 
                    parsedData["e"].is_string() && 
                    parsedData["e"] == "depthUpdate") {
                    
                    std::cout << "\nORDER BOOK [" << symbol << "]" << std::endl;
                    
                    // SAFE bid parsing - check every step
                    if (parsedData.contains("b") && parsedData["b"].is_array()) {
                        auto bids = parsedData["b"];
                        if (!bids.empty() && bids[0].is_array() && bids[0].size() >= 2) {
                            try {
                                if (bids[0][0].is_string() && bids[0][1].is_string()) {
                                    std::string bidPrice = bids[0][0];
                                    std::string bidQty = bids[0][1];
                                    std::cout << "Best Bid: " << bidPrice << " @ " << bidQty << std::endl;
                                }
                            } catch (...) {
                                std::cout << "Best Bid: [parsing error]" << std::endl;
                            }
                        }
                    }
                    
                    // SAFE ask parsing - check every step
                    if (parsedData.contains("a") && parsedData["a"].is_array()) {
                        auto asks = parsedData["a"];
                        if (!asks.empty() && asks[0].is_array() && asks[0].size() >= 2) {
                            try {
                                if (asks[0][0].is_string() && asks[0][1].is_string()) {
                                    std::string askPrice = asks[0][0];
                                    std::string askQty = asks[0][1];
                                    std::cout << "Best Ask: " << askPrice << " @ " << askQty << std::endl;
                                }
                            } catch (...) {
                                std::cout << "Best Ask: [parsing error]" << std::endl;
                            }
                        }
                    }
                    
                    std::cout << "----------------------------------------" << std::endl;
                } else {
                    // Not a depth update - just show data type
                    std::string eventType = "unknown";
                    if (parsedData.is_object() && parsedData.contains("e") && parsedData["e"].is_string()) {
                        eventType = parsedData["e"];
                    }
                    std::cout << "DATA [" << symbol << "] type: " << eventType << std::endl;
                }
                
            } catch (const json::parse_error& e) {
                std::cout << "JSON parse error for [" << symbol << "]: " << e.what() << std::endl;
                std::cout << "Raw data (first 100 chars): " << dataStr.substr(0, 100) << std::endl;
            } catch (const std::exception& e) {
                std::cout << "Data processing error for [" << symbol << "]: " << e.what() << std::endl;
            } catch (...) {
                std::cout << "Unknown error processing data for [" << symbol << "]" << std::endl;
            }
        }
        
        // Convert to ExchangeData format for pipeline
        ExchangeData exchangeData;
        exchangeData.exchangeId = exchange;
        exchangeData.symbol = Symbol::fromString(symbol);
        exchangeData.data = dataStr;
        exchangeData.receivedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        exchangeData.dataType = "orderbook";
        exchangeData.isSnapshot = false;
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

void printEnhancedPipelineStatus(const std::shared_ptr<PipelineStatus>& status) {
    std::cout << "\n=== PIPELINE STATUS ===" << std::endl;
    std::cout << "Overall: " << (status->isHealthy() ? "HEALTHY" : "UNHEALTHY") << std::endl;
    std::cout << "Running: " << (status->isRunning() ? "YES" : "NO") << std::endl;
    
    auto stageStates = status->getStageStates();
    std::cout << "Stages:" << std::endl;
    for (const auto& [stageName, state] : stageStates) {
        std::string stateStr;
        
        switch (state) {
            case PipelineStatus::StageState::RUNNING:
                stateStr = "RUNNING";
                break;
            case PipelineStatus::StageState::STOPPED:
                stateStr = "STOPPED";
                break;
            case PipelineStatus::StageState::ERROR:
                stateStr = "ERROR";
                break;
            default:
                stateStr = "UNKNOWN";
                break;
        }
        
        std::cout << "  " << stageName << ": " << stateStr << std::endl;
    }
    
    if (!status->getLastError().empty()) {
        std::cout << "Last Error: " << status->getLastError() << std::endl;
    }
    std::cout << "=======================" << std::endl;
}

int main() {
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    std::cout << "=== CRYPTO TRADING DATA PIPELINE ===" << std::endl;
    std::cout << "Initializing market data processing..." << std::endl;
    
    try {
        // Create pipeline configuration
        PipelineConfig config;
        config.maxQueueSize = 50000;
        config.threadPoolSize = 8;
        config.enableMetrics = true;
        config.metricsInterval = std::chrono::milliseconds(2000);
        
        std::cout << "Creating normalization pipeline..." << std::endl;
        auto uniquePipeline = createDefaultNormalizationPipeline(config);
        auto pipeline = std::shared_ptr<trading::data_ingestion::NormalizationPipeline>(uniquePipeline.release());
        
        std::cout << "Starting pipeline..." << std::endl;
        if (!pipeline->start()) {
            std::cerr << "Failed to start pipeline!" << std::endl;
            return 1;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::cout << "Setting up data feeder..." << std::endl;
        BinanceWebSocketFeeder feeder(pipeline);
        
        // Smaller symbol list for easier debugging
        std::vector<std::string> symbols = {
            "BTCUSDT",
            "ETHUSDT", 
            "BNBUSDT"
        };
        
        std::cout << "Starting data collection from " << symbols.size() << " trading pairs..." << std::endl;
        
        if (!feeder.start(symbols)) {
            std::cerr << "Failed to start WebSocket feeder!" << std::endl;
            return 1;
        }
        
        std::cout << "\nLIVE MARKET DATA PROCESSING ACTIVE!" << std::endl;
        std::cout << "Processing real-time order book updates from Binance" << std::endl;
        std::cout << "Press Ctrl+C to stop gracefully..." << std::endl;
        
        auto startTime = std::chrono::steady_clock::now();
        int statusCounter = 0;
        
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            statusCounter++;
            
            if (statusCounter % 4 == 0) {
                auto status = pipeline->getStatus();
                printEnhancedPipelineStatus(status);
                
                auto metrics = pipeline->getMetrics();
                double throughput = metrics->getThroughputPerSecond();
                
                std::cout << "\nTHROUGHPUT METRICS:" << std::endl;
                std::cout << "Pipeline: " << std::fixed << std::setprecision(1) 
                         << throughput << " updates/sec" << std::endl;
                std::cout << "WebSocket: " << std::fixed << std::setprecision(1)
                         << (statusCounter > 0 ? g_messageCount.load() / (statusCounter * 5.0) : 0) 
                         << " messages/sec" << std::endl;
                std::cout << "Data Volume: " << std::fixed << std::setprecision(2)
                         << g_totalBytes.load() / 1024.0 / 1024.0 << " MB total" << std::endl;
            } else {
                auto elapsed = std::chrono::duration_cast<std::chrono::minutes>(
                    std::chrono::steady_clock::now() - startTime).count();
                std::cout << "Live processing... (" << elapsed << " min, " 
                         << g_messageCount.load() << " messages)" << std::endl;
            }
            
            auto status = pipeline->getStatus();
            if (!status->isHealthy()) {
                std::cerr << "Pipeline became unhealthy: " << status->getLastError() << std::endl;
            }
            
            // Stop after 5 minutes for demo
            auto runTime = std::chrono::duration_cast<std::chrono::minutes>(
                std::chrono::steady_clock::now() - startTime).count();
            if (runTime >= 5) {
                std::cout << "\nDemo time limit reached. Stopping..." << std::endl;
                break;
            }
        }
        
        // Graceful shutdown
        std::cout << "\nINITIATING GRACEFUL SHUTDOWN..." << std::endl;
        
        std::cout << "1/3 Stopping WebSocket data collection..." << std::endl;
        feeder.stop();
        
        std::cout << "2/3 Draining pipeline buffers..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        
        std::cout << "3/3 Stopping processing pipeline..." << std::endl;
        pipeline->stop();
        
        // Final statistics
        std::cout << "\n" << std::string(50, '=') << std::endl;
        std::cout << "FINAL EXECUTION REPORT" << std::endl;
        std::cout << std::string(50, '=') << std::endl;
        
        auto finalMetrics = pipeline->getMetrics();
        auto totalRuntime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - startTime).count();
        
        std::cout << "Runtime: " << totalRuntime << " seconds" << std::endl;
        std::cout << "Messages Processed: " << finalMetrics->getTotalInputCount() << std::endl;
        std::cout << "Processing Errors: " << finalMetrics->getTotalErrorCount() << std::endl;
        std::cout << "Success Rate: " << std::fixed << std::setprecision(2) 
                 << (100.0 * (1.0 - finalMetrics->getErrorRate())) << "%" << std::endl;
        std::cout << "Throughput: " << std::fixed << std::setprecision(1)
                 << finalMetrics->getThroughputPerSecond() << " updates/sec" << std::endl;
        
        std::cout << std::string(50, '=') << std::endl;
        std::cout << "Pipeline execution completed successfully!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Pipeline execution failed: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}