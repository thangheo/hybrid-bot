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

    // ✅ FIXED: Safer connection tracking with proper lifecycle management
    struct ConnectionInfo {
        ClientInterfacePtr connection;
        std::atomic<bool> inUse{false};
        std::chrono::steady_clock::time_point lastUsed;
        std::atomic<int> failureCount{0};
        
        // ✅ FIXED: Use shared_ptr for thread to enable proper coordination
        std::shared_ptr<std::thread> runThread;
        std::atomic<bool> shouldStop{false};
        
        ConnectionInfo(ClientInterfacePtr conn) 
            : connection(std::move(conn))
            , lastUsed(std::chrono::steady_clock::now()) {
            
            if (connection) {
                startRunThread();
            }
        }

        // ✅ FIXED: Proper shutdown sequence
        ~ConnectionInfo() {
            shutdown();
        }

        // ✅ FIXED: Explicit shutdown method for controlled teardown
        void shutdown() {
            // Signal the thread to stop
            shouldStop.store(true);
            
            // Stop the connection first (this should interrupt the io_context)
            if (connection) {
                try {
                    connection->stop();
                } catch (...) {
                    // Ignore exceptions during shutdown
                }
            }
            
            // Wait for thread completion with timeout
            if (runThread && runThread->joinable()) {
                auto threadPtr = runThread; // Keep reference alive
                
                // Try to join with timeout
                auto future = std::async(std::launch::async, [threadPtr]() {
                    if (threadPtr->joinable()) {
                        threadPtr->join();
                    }
                });
                
                if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                    std::cerr << "Warning: Connection thread did not shutdown gracefully" << std::endl;
                    // Thread will be detached when shared_ptr is destroyed
                }
            }
            
            // Final disconnect after thread cleanup
            if (connection) {
                try {
                    connection->disconnect();
                } catch (...) {
                    // Ignore exceptions during shutdown
                }
            }
        }

        // ✅ FIXED: Safer thread creation with proper error handling
        void startRunThread() {
            try {
                auto conn_weak = std::weak_ptr<ClientInterface>(connection);
                
                runThread = std::make_shared<std::thread>([conn_weak, this]() {
                    try {
                        auto conn = conn_weak.lock();
                        if (!conn) {
                            return; // Connection was destroyed
                        }
                        
                        // Run with periodic shutdown checks
                        while (!shouldStop.load()) {
                            try {
                                // Run for short periods to enable responsive shutdown
                                conn->run();
                                
                                // If run() returns, the connection likely stopped
                                break;
                            } catch (const std::exception& e) {
                                if (!shouldStop.load()) {
                                    std::cerr << "Connection run() exception: " << e.what() << std::endl;
                                }
                                break;
                            }
                        }
                        
                    } catch (...) {
                        if (!shouldStop.load()) {
                            std::cerr << "Connection run() unknown exception" << std::endl;
                        }
                    }
                });
                
            } catch (const std::exception& e) {
                std::cerr << "Failed to start connection run thread: " << e.what() << std::endl;
            }
        }

        // Make movable but not copyable
        ConnectionInfo(ConnectionInfo&& other) noexcept 
            : connection(std::move(other.connection))
            , lastUsed(other.lastUsed)
            , runThread(std::move(other.runThread))
        {
            inUse.store(other.inUse.load());
            failureCount.store(other.failureCount.load());
            shouldStop.store(other.shouldStop.load());
        }
        
        ConnectionInfo& operator=(ConnectionInfo&& other) noexcept {
            if (this != &other) {
                shutdown(); // Clean up current state
                
                connection = std::move(other.connection);
                inUse.store(other.inUse.load());
                lastUsed = other.lastUsed;
                failureCount.store(other.failureCount.load());
                runThread = std::move(other.runThread);
                shouldStop.store(other.shouldStop.load());
            }
            return *this;
        }
        
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
    
    // Connection management
    std::vector<ConnectionInfo> connections_;
    mutable std::mutex mutex_;
    std::condition_variable connectionAvailable_;
    
    // Monitoring thread
    std::thread monitorThread_;
    std::atomic<bool> running_{false};
    
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