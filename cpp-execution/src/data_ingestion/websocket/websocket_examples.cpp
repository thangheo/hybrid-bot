// Multi-threaded WebSocket example with enhanced performance
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "core/market_types.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <csignal>
#include <iomanip>
#include <sstream>

using namespace trading::data_ingestion;
using namespace trading::core;
using namespace trading;
// Global control
std::atomic<bool> running{true};

// Market data message structure for queue
struct MarketDataMessage {
    ExchangeId exchange;
    core::Symbol symbol;
    std::vector<uint8_t> data;
    std::chrono::steady_clock::time_point receivedTime;

    MarketDataMessage(const ExchangeId& ex, const core::Symbol& sym, const std::vector<uint8_t>& d)
        : exchange(ex), symbol(sym), data(d), receivedTime(std::chrono::steady_clock::now()) {}
};

// Thread-safe message queue
class MessageQueue {
private:
    std::queue<MarketDataMessage> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<size_t> maxSize_{1000}; // Prevent memory overflow

public:
    void push(MarketDataMessage&& msg) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Drop old messages if queue is full
        while (queue_.size() >= maxSize_) {
            queue_.pop();
        }
        
        queue_.emplace(std::move(msg));
        cv_.notify_one();
    }
    
    bool pop(MarketDataMessage& msg, std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        if (cv_.wait_for(lock, timeout, [this] { return !queue_.empty() || !running; })) {
            if (!queue_.empty()) {
                msg = std::move(queue_.front());
                queue_.pop();
                return true;
            }
        }
        return false;
    }
    
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
};

// Statistics tracking
class Statistics {
private:
    std::atomic<uint64_t> totalMessages_{0};
    std::atomic<uint64_t> totalBytes_{0};
    std::atomic<double> totalLatency_{0.0};
    mutable std::mutex symbolStats_;
    std::map<std::string, uint64_t> messagesPerSymbol_;
    std::chrono::steady_clock::time_point startTime_;

public:
    Statistics() : startTime_(std::chrono::steady_clock::now()) {}
    
    void recordMessage(const core::Symbol& symbol, size_t bytes, double latencyMs) {
        totalMessages_.fetch_add(1);
        totalBytes_.fetch_add(bytes);
        
        // Atomically add to double using compare-and-swap
        double expected = totalLatency_.load();
        while (!totalLatency_.compare_exchange_weak(expected, expected + latencyMs));
        
        std::lock_guard<std::mutex> lock(symbolStats_);
        messagesPerSymbol_[symbol.toString()]++;
    }
    
    void printStats() const {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - startTime_).count();
        
        uint64_t messages = totalMessages_.load();
        uint64_t bytes = totalBytes_.load();
        double avgLatency = messages > 0 ? totalLatency_.load() / messages : 0.0;
        
        std::cout << "\n=== PERFORMANCE STATISTICS ===" << std::endl;
        std::cout << "Runtime: " << elapsed << "s" << std::endl;
        std::cout << "Total messages: " << messages << std::endl;
        std::cout << "Total bytes: " << std::fixed << std::setprecision(2) << bytes / 1024.0 << " KB" << std::endl;
        std::cout << "Messages/sec: " << (elapsed > 0 ? messages / elapsed : 0) << std::endl;
        std::cout << "Avg latency: " << std::fixed << std::setprecision(3) << avgLatency << "ms" << std::endl;
        
        std::lock_guard<std::mutex> lock(symbolStats_);
        std::cout << "Messages per symbol:" << std::endl;
        for (const auto& [symbol, count] : messagesPerSymbol_) {
            std::cout << "  " << symbol << ": " << count << std::endl;
        }
        std::cout << "==============================\n" << std::endl;
    }
};

// Global instances
MessageQueue dataQueue;
Statistics stats;

// Fast callback - just enqueue data
void onMarketData(const ExchangeId& exchange, const core::Symbol& symbol, const std::vector<uint8_t>& data) {
    dataQueue.push(MarketDataMessage(exchange, symbol, data));
}

// Data processing thread
void dataProcessingThread() {
    std::cout << "[DATA PROCESSOR] Thread started" << std::endl;

    MarketDataMessage msg(ExchangeId{}, core::Symbol{}, std::vector<uint8_t>{});
    while (running) {
        if (dataQueue.pop(msg)) {
            // Calculate processing latency
            auto now = std::chrono::steady_clock::now();
            auto latency = std::chrono::duration_cast<std::chrono::microseconds>(now - msg.receivedTime).count() / 1000.0;
            
            // Record statistics
            stats.recordMessage(msg.symbol, msg.data.size(), latency);
            
            // Simulate some processing (parsing JSON, updating order book, etc.)
            // In real implementation, you would parse the data here
            if (msg.data.size() > 0) {
                // Light processing - just log major updates
                static uint64_t processCount = 0;
                if (++processCount % 100 == 0) { // Log every 100th message
                    std::cout << "[PROCESSOR] " << msg.symbol << " - " 
                              << msg.data.size() << " bytes, latency: " 
                              << std::fixed << std::setprecision(2) << latency << "ms" << std::endl;
                }
            }
        }
    }
    
    std::cout << "[DATA PROCESSOR] Thread stopping" << std::endl;
}

// Status monitoring thread
void statusMonitorThread() {
    std::cout << "[STATUS MONITOR] Thread started" << std::endl;
    
    while (running) {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        if (running) {
            std::cout << "\n[STATUS] Queue size: " << dataQueue.size() << " messages" << std::endl;
            stats.printStats();
        }
    }
    
    std::cout << "[STATUS MONITOR] Thread stopping" << std::endl;
}

// Signal handler
void signalHandler(int signal) {
    std::cout << "\nReceived signal " << signal << ", shutting down..." << std::endl;
    running = false;
}

int main() {
    // Set up signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    try {
        std::cout << "Starting Multi-threaded WebSocket example..." << std::endl;
        
        // Start background processing threads
        std::thread dataProcessor(dataProcessingThread);
        std::thread statusMonitor(statusMonitorThread);
        
        // Create connection pool
        auto pool = std::make_unique<ConnectionPool>(
            "binance_multithreaded_pool",
            []() -> ClientInterfacePtr {
                return std::make_shared<BinanceWebSocketClient>(
                    "stream.binance.com", 
                    "9443",
                    onMarketData
                );
            },
            2,  // Min connections for better parallelism
            5   // Max connections
        );
        
        std::cout << "Connection pool created with 2-5 connections" << std::endl;
        
        // Initialize pool
        pool->initialize();
        if (!pool->connectAll()) {
            std::cerr << "Failed to connect all connections" << std::endl;
            running = false;
            dataProcessor.join();
            statusMonitor.join();
            return 1;
        }
        
        // Acquire multiple connections
        std::vector<ClientInterfacePtr> connections;
        for (int i = 0; i < 2; ++i) {
            auto conn = pool->acquireConnection(5000);
            if (conn) {
                connections.push_back(conn);
                std::cout << "Acquired connection " << (i + 1) << std::endl;
            }
        }
        
        if (connections.empty()) {
            std::cerr << "Failed to acquire any connections" << std::endl;
            running = false;
            dataProcessor.join();
            statusMonitor.join();
            return 1;
        }
        
        // Extended symbol list for better testing
        std::vector<Symbol> majorSymbols = {
            Symbol("BTC", "USDT"),   // Bitcoin
            // Symbol("ETH", "USDT"),   // Ethereum  
            // Symbol("BNB", "USDT"),   // Binance Coin
            // Symbol("ADA", "USDT"),   // Cardano
            // Symbol("SOL", "USDT"),   // Solana
            // Symbol("XRP", "USDT"),   // Ripple
            // Symbol("DOT", "USDT"),   // Polkadot
            // Symbol("AVAX", "USDT"),  // Avalanche
            // Symbol("MATIC", "USDT"), // Polygon
            Symbol("LINK", "USDT")   // Chainlink
        };
        
        std::vector<Symbol> altSymbols = {
            Symbol("DOGE", "USDT"),  // Dogecoin
            // Symbol("SHIB", "USDT"),  // Shiba Inu
            // Symbol("UNI", "USDT"),   // Uniswap
            // Symbol("ATOM", "USDT"),  // Cosmos
            // Symbol("FTM", "USDT"),   // Fantom
            // Symbol("ALGO", "USDT"),  // Algorand
            // Symbol("VET", "USDT"),   // VeChain
            // Symbol("ICP", "USDT"),   // Internet Computer
            // Symbol("NEAR", "USDT"),  // Near Protocol
            Symbol("SAND", "USDT")   // Sandbox
        };
        
        // Subscribe different symbols to different connections
        if (connections.size() >= 1) {
            if (connections[0]->subscribe(majorSymbols)) {
                std::cout << "Connection 1 subscribed to " << majorSymbols.size() << " major symbols" << std::endl;
            }
        }
        
        if (connections.size() >= 2) {
            if (connections[1]->subscribe(altSymbols)) {
                std::cout << "Connection 2 subscribed to " << altSymbols.size() << " alt symbols" << std::endl;
            }
        }
        
        std::cout << "\nTotal symbols: " << (majorSymbols.size() + altSymbols.size()) << std::endl;
        std::cout << "Multi-threaded processing started..." << std::endl;
        std::cout << "- Data processing thread: Handles incoming messages" << std::endl;
        std::cout << "- Status monitor thread: Reports statistics every 10s" << std::endl;
        std::cout << "Press Ctrl+C to exit\n" << std::endl;
        
        // Main loop - much lighter than before
        int counter = 0;
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500)); // More responsive
            
            // Light status check every 30 seconds
            if (++counter % 60 == 0) {
                if (pool->isHealthy()) {
                    std::cout << "[MAIN] Pool healthy: " 
                              << pool->getActiveConnectionCount() << " active connections" << std::endl;
                } else {
                    std::cout << "[MAIN] Pool unhealthy!" << std::endl;
                }
            }
            
            // Auto-exit after 2 minutes for demo
            if (counter >= 240) { // 240 * 500ms = 2 minutes
                std::cout << "[MAIN] Demo time completed (2 minutes), exiting..." << std::endl;
                break;
            }
        }
        
        // Cleanup
        std::cout << "\n[MAIN] Starting cleanup..." << std::endl;
        running = false;
        
        // Unsubscribe all
        for (size_t i = 0; i < connections.size(); ++i) {
            if (i == 0) {
                connections[i]->unsubscribe(majorSymbols);
            } else if (i == 1) {
                connections[i]->unsubscribe(altSymbols);
            }
            pool->releaseConnection(connections[i]);
        }
        
        // Shutdown pool
        pool->shutdown();
        
        // Join threads
        dataProcessor.join();
        statusMonitor.join();
        
        // Final stats
        std::cout << "\n=== FINAL STATISTICS ===" << std::endl;
        stats.printStats();
        
        std::cout << "Multi-threaded example completed successfully!" << std::endl;
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        running = false;
        return 1;
    }
}