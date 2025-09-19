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

// using namespace exec
namespace trading {
namespace data_ingestion {

// Forward declaration
using ClientInterfacePtr = std::shared_ptr<ClientInterface>;

// Connection factory function type - Updated to use ClientInterface
using ConnectionFactory = std::function<ClientInterfacePtr()>;

class ConnectionPool {
public:
    // ConnectionPool(const std::string& poolId,
    //               ConnectionFactory factory,
    //               std::shared_ptr<execution::RateLimitController> rateLimitController,
    //               int minConnections = 5,
    //               int maxConnections = 10);
                  
    ConnectionPool(const std::string& poolId,
                  ConnectionFactory factory,
                  int minConnections = 5,
                  int maxConnections = 10);
    ~ConnectionPool();
    
    // Pool lifecycle management
    bool initialize();
    void shutdown();
    
    // ✅ Subscription management
    void setActiveSubscriptions(const std::vector<core::Symbol>& symbols);
    std::vector<core::Symbol> getActiveSubscriptions() const;
    
    // Connect all connections in the pool
    bool connectAll();
    
    // Disconnect all connections in the pool
    void disconnectAll();
    
    // Get a connection from the pool (will block if none available)
    ClientInterfacePtr acquireConnection(int timeoutMs = 5000);
    
    // Release a connection back to the pool
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
    // emergencyRecovery
    bool emergencyRecovery();

private:
    // Add rate limiting support
    std::shared_ptr<execution::RateLimitController> rateLimitController_;
    
    // Rate limit configuration for different exchanges
    void setupRateLimits(const core::ExchangeId& exchangeId);

    // Connection tracking
    struct ConnectionInfo {
        ClientInterfacePtr connection;
        bool inUse;
        std::chrono::steady_clock::time_point lastUsed;
        int failureCount;
    // Background thread that runs the client's event loop (movable via unique_ptr)
    std::unique_ptr<std::thread> runThread;
        
        ConnectionInfo(ClientInterfacePtr conn) 
            : connection(std::move(conn))
            , inUse(false)
            , lastUsed(std::chrono::steady_clock::now())
            , failureCount(0) {
            // If the connection implements a run loop, start it in a background thread
            if (connection) {
                try {
                    // Capture shared_ptr by value to keep object alive while thread runs
                    auto conn_copy = connection;
                    runThread = std::make_unique<std::thread>([conn_copy]() {
                        try {
                            conn_copy->run();
                        } catch (const std::exception& e) {
                            std::cerr << "Connection run() exception: " << e.what() << std::endl;
                        } catch (...) {
                            std::cerr << "Connection run() unknown exception" << std::endl;
                        }
                    });
                } catch (const std::exception& e) {
                    std::cerr << "Failed to start connection run thread: " << e.what() << std::endl;
                }
            }
        }

        ~ConnectionInfo() {
            // Attempt graceful stop of the connection and join the thread
            try {
                if (connection) {
                    connection->stop();
                    // Do NOT call disconnect() here; cleanup will be handled by the run() thread
                }
            } catch (...) {
                // ignore
            }
            if (runThread && runThread->joinable()) {
                try { runThread->join(); } catch (...) { }
            }
        }

        // Make movable but not copyable so this can be stored in containers like std::vector
        ConnectionInfo(ConnectionInfo&&) = default;
        ConnectionInfo& operator=(ConnectionInfo&&) = default;
        ConnectionInfo(const ConnectionInfo&) = delete;
        ConnectionInfo& operator=(const ConnectionInfo&) = delete;
    };
    
    // Internal methods
    void monitorConnections();
    ClientInterfacePtr createConnection();
    void pruneConnections();
    void ensureMinConnections();
    void removeUnhealthyConnections();
    
    // Pool identification
    std::string poolId_;
    
    // Connection factory
    ConnectionFactory connectionFactory_;
    
    // Pool configuration
    int minConnections_;
    int maxConnections_;
    std::chrono::seconds idleTimeout_{300}; // 5 minutes
    int maxFailures_{3};
    std::chrono::seconds pruneInterval_{60}; // 1 minute
    
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
    
    // ✅ Subscription tracking
    std::vector<core::Symbol> activeSubscriptions_;
    mutable std::mutex subscriptionMutex_;

    std::atomic<int> activeConnections_{0};

};

} // namespace data_ingestion
} // namespace trading