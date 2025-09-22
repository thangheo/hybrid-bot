// include/data_ingestion/websocket/connection_pool.hpp
#pragma once

#include "data_ingestion/websocket/client_interface.hpp"
#include "execution/rate_limit_controller.hpp"
#include "core/types.hpp"
#include <string>
#include <memory>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <unordered_map>
#include <condition_variable>
#include <iostream>
#include <future>

namespace trading {
namespace data_ingestion {

using ClientInterfacePtr = std::shared_ptr<ClientInterface>;
using ConnectionFactory = std::function<ClientInterfacePtr()>;

class ConnectionPool {
public:
    ConnectionPool(const std::string& poolId,
                  ConnectionFactory factory,
                  int minConnections = 5,
                  int maxConnections = 10);
    ~ConnectionPool();
    
    // Pool lifecycle management
    bool initialize();
    void shutdown();
    
    // Subscription management
    void setActiveSubscriptions(const std::vector<core::Symbol>& symbols);
    std::vector<core::Symbol> getActiveSubscriptions() const;
    
    // Connection management
    bool connectAll();
    void disconnectAll();
    ClientInterfacePtr acquireConnection(int timeoutMs = 5000);
    void releaseConnection(ClientInterfacePtr connection);
    
    // Health monitoring API
    bool isHealthy() const;
    int getActiveConnectionCount() const;
    int getAvailableConnectionCount() const;
    int getTotalConnectionCount() const;
    
    // Statistics
    struct PoolStats {
        int connectionRequests = 0;
        int connectionTimeouts = 0;
        int connectionFailures = 0;
        int connectionReleases = 0;
        int totalConnections = 0;
        int activeConnections = 0;
        int availableConnections = 0;
    };
    
    PoolStats getStats() const;
    bool subscribeAllConnections(const std::vector<core::Symbol>& symbols);
    void checkConnectionHealth();
    void forceReconnectAll();
    bool emergencyRecovery();

private:
    std::shared_ptr<execution::RateLimitController> rateLimitController_;
    void setupRateLimits(const core::ExchangeId& exchangeId);

    // FIXED: Safer connection tracking with proper lifecycle management
    struct ConnectionInfo {
        ClientInterfacePtr connection;
        std::atomic<bool> inUse{false};
        std::chrono::steady_clock::time_point lastUsed;
        std::atomic<int> failureCount{0};
        
        // FIXED: Thread management with proper RAII
        std::unique_ptr<std::thread> runThread;
        std::atomic<bool> shouldStop{false};
        std::atomic<bool> isShuttingDown{false};
        
        ConnectionInfo(ClientInterfacePtr conn) 
            : connection(std::move(conn))
            , lastUsed(std::chrono::steady_clock::now()) {
            // Note: do NOT start the run thread here. Starting a thread from
            // within the constructor can allow the new thread to observe this
            // object before construction is complete which causes data races
            // (TSAN). The thread should be started after the object is
            // published (for example, immediately after push_back into the
            // connections_ vector while holding the pool mutex).
        }

        // FIXED: Explicit shutdown with timeout and proper error handling
        ~ConnectionInfo() {
            shutdown();
        }

        void shutdown() {
            // Prevent multiple shutdown calls
            bool expected = false;
            if (!isShuttingDown.compare_exchange_strong(expected, true)) {
                return; // Already shutting down
            }
            
            // Signal the thread to stop
            shouldStop.store(true);
            
            // Stop the connection first to interrupt any blocking operations
            if (connection) {
                try {
                    connection->stop();
                } catch (...) {
                    // Ignore exceptions during shutdown
                }
            }
            
            // Wait for thread completion with timeout
            if (runThread && runThread->joinable()) {
                // Use a future to implement timeout for thread join
                auto joinFuture = std::async(std::launch::async, [this]() {
                    if (runThread && runThread->joinable()) {
                        runThread->join();
                    }
                });
                
                // Wait with timeout
                auto status = joinFuture.wait_for(std::chrono::seconds(3));
                if (status == std::future_status::timeout) {
                    std::cerr << "Warning: Connection thread join timeout, detaching" << std::endl;
                    if (runThread && runThread->joinable()) {
                        runThread->detach();
                    }
                }
            }
            
            // Final disconnect after thread cleanup
            if (connection) {
                try {
                    connection->disconnect();
                } catch (...) {
                    // Ignore exceptions during shutdown
                }
                connection.reset();
            }
            
            runThread.reset();
        }

        // FIXED: Safer thread creation with proper weak_ptr usage
        void startRunThread() {
            if (!connection) return;
            
            try {
                // Create weak reference to avoid circular dependency
                std::weak_ptr<ClientInterface> conn_weak = connection;
                
                runThread = std::make_unique<std::thread>([conn_weak, this]() {
                    try {
                        // Check if we should stop before doing anything
                        if (shouldStop.load()) return;
                        
                        auto conn = conn_weak.lock();
                        if (!conn) {
                            return; // Connection was destroyed
                        }
                        
                        // Run the connection's event loop
                        while (!shouldStop.load()) {
                            try {
                                // Run with periodic checks for shutdown
                                conn->run();
                                
                                // If run() returns normally, the connection stopped
                                if (!shouldStop.load()) {
                                    failureCount.fetch_add(1);
                                }
                                break;
                                
                            } catch (const std::exception& e) {
                                if (!shouldStop.load()) {
                                    std::cerr << "Connection run() exception: " << e.what() << std::endl;
                                    failureCount.fetch_add(1);
                                }
                                break;
                            }
                        }
                        
                    } catch (...) {
                        if (!shouldStop.load()) {
                            std::cerr << "Connection thread unknown exception" << std::endl;
                            failureCount.fetch_add(1);
                        }
                    }
                });
                
            } catch (const std::exception& e) {
                std::cerr << "Failed to start connection thread: " << e.what() << std::endl;
                failureCount.fetch_add(1);
            }
        }

        // FIXED: Proper move semantics
        ConnectionInfo(ConnectionInfo&& other) noexcept 
            : connection(std::move(other.connection))
            , lastUsed(other.lastUsed)
            , runThread(std::move(other.runThread))
        {
            inUse.store(other.inUse.load());
            failureCount.store(other.failureCount.load());
            shouldStop.store(other.shouldStop.load());
            isShuttingDown.store(other.isShuttingDown.load());
            
            // Reset the moved-from object
            other.shouldStop.store(true);
            other.isShuttingDown.store(true);
        }
        
        ConnectionInfo& operator=(ConnectionInfo&& other) noexcept {
            if (this != &other) {
                shutdown(); // Clean up current state first
                
                connection = std::move(other.connection);
                inUse.store(other.inUse.load());
                lastUsed = other.lastUsed;
                failureCount.store(other.failureCount.load());
                runThread = std::move(other.runThread);
                shouldStop.store(other.shouldStop.load());
                isShuttingDown.store(other.isShuttingDown.load());
                
                // Reset the moved-from object
                other.shouldStop.store(true);
                other.isShuttingDown.store(true);
            }
            return *this;
        }
        
        // Delete copy operations
        ConnectionInfo(const ConnectionInfo&) = delete;
        ConnectionInfo& operator=(const ConnectionInfo&) = delete;
    };
    
    // Internal methods
    void monitorConnections();
    ClientInterfacePtr createConnection();
    void pruneConnections();
    void ensureMinConnections();
    void removeUnhealthyConnections();
    
    // Pool identification and configuration
    std::string poolId_;
    ConnectionFactory connectionFactory_;
    int minConnections_;
    int maxConnections_;
    std::chrono::seconds idleTimeout_{300};
    int maxFailures_{3};
    std::chrono::seconds pruneInterval_{60};
    
    // FIXED: Connection management with proper shutdown order
    std::vector<ConnectionInfo> connections_;
    mutable std::mutex mutex_;
    std::condition_variable connectionAvailable_;
    
    // FIXED: Monitoring thread management
    std::unique_ptr<std::thread> monitorThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> shutdownRequested_{false};
    
    // Statistics
    std::atomic<int> connectionRequests_{0};
    std::atomic<int> connectionTimeouts_{0};
    std::atomic<int> connectionFailures_{0};
    std::atomic<int> connectionReleases_{0};
    std::atomic<int> totalConnections_{0};
    std::atomic<int> activeConnections_{0};
    
    // Subscription tracking
    std::vector<core::Symbol> activeSubscriptions_;
    mutable std::mutex subscriptionMutex_;
};

} // namespace data_ingestion
} // namespace trading