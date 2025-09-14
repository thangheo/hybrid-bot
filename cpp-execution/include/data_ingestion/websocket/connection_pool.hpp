// include/data_ingestion/websocket/connection_pool.hpp
#pragma once

#include "data_ingestion/websocket/client_interface.hpp"
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

namespace trading {
namespace data_ingestion {

// Forward declaration
using ClientInterfacePtr = std::shared_ptr<ClientInterface>;

// Connection factory function type - Updated to use ClientInterface
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

private:
    // Connection tracking
    struct ConnectionInfo {
        ClientInterfacePtr connection;
        bool inUse;
        std::chrono::steady_clock::time_point lastUsed;
        int failureCount;
        
        ConnectionInfo(ClientInterfacePtr conn) 
            : connection(std::move(conn))
            , inUse(false)
            , lastUsed(std::chrono::steady_clock::now())
            , failureCount(0) {}
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