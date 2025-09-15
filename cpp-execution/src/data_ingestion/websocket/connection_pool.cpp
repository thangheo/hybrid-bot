// src/data_ingestion/websocket/connection_pool.cpp
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include "common/logger.hpp"

namespace trading {
namespace data_ingestion {

// 1. Update constructor
ConnectionPool::ConnectionPool(const std::string& poolId,
                              ConnectionFactory factory,
                              int minConnections,
                              int maxConnections)
    : poolId_(poolId),
      connectionFactory_(std::move(factory)),
      minConnections_(minConnections),
      maxConnections_(maxConnections) {
    
    // Validate parameters
    if (minConnections < 1) {
        minConnections_ = 1;
    }
    
    if (maxConnections < minConnections_) {
        maxConnections_ = minConnections_;
    }
    
    LOG_INFO("Created connection pool '" + poolId_ + "' with " +
             std::to_string(minConnections_) + "-" + std::to_string(maxConnections_) + " connections");
}
 
ClientInterfacePtr ConnectionPool::acquireConnection(int timeoutMs) {
    connectionRequests_++;
    
    auto startTime = std::chrono::steady_clock::now();
    
    {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // ✅ First check for healthy available connections
        for (auto& info : connections_) {
            if (!info.inUse && info.connection && info.connection->isConnected()) {
                // ✅ Additional health check if connection supports it
                bool isHealthy = true;
                
                // Check if connection has health monitoring
                auto healthyConn = std::dynamic_pointer_cast<BinanceWebSocketClient>(info.connection);
                if (healthyConn) {
                    isHealthy = healthyConn->isConnectionHealthy();
                }
                
                if (isHealthy) {
                    info.inUse = true;
                    info.lastUsed = std::chrono::steady_clock::now();
                    info.failureCount = 0; // Reset failure count on successful acquisition
                    
                    LOG_DEBUG("Acquired healthy connection from pool '" + poolId_ + "'");
                    return info.connection;
                } else {
                    LOG_WARN("Connection appears unhealthy, skipping");
                    info.failureCount++;
                }
            }
        }
        
        // ✅ EMERGENCY RECOVERY: If all connections are unhealthy, force recreate one
        bool allUnhealthy = std::all_of(connections_.begin(), connections_.end(),
            [](const ConnectionInfo& info) {
                return !info.connection || !info.connection->isConnected() || info.failureCount >= 3;
            });
            
        if (allUnhealthy && !connections_.empty()) {
            LOG_ERROR("ALL CONNECTIONS UNHEALTHY - Starting emergency recovery for pool '" + poolId_ + "'");
            
            // Force recreate the first connection
            auto& firstConn = connections_[0];
            if (firstConn.connection) {
                firstConn.connection->disconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(1000)); // Brief pause
                
                if (firstConn.connection->connect()) {
                    firstConn.failureCount = 0;
                    firstConn.inUse = true;
                    firstConn.lastUsed = std::chrono::steady_clock::now();
                    
                    // Resubscribe to active subscriptions
                    if (!activeSubscriptions_.empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        try {
                            firstConn.connection->subscribe(activeSubscriptions_);
                            LOG_INFO("Emergency recovery successful - reconnected and resubscribed");
                        } catch (const std::exception& e) {
                            LOG_ERROR("Failed to resubscribe during emergency recovery: " + std::string(e.what()));
                        }
                    }
                    
                    return firstConn.connection;
                } else {
                    LOG_ERROR("Emergency recovery failed - could not reconnect");
                }
            }
        }
        
        // No healthy available connections, can we create a new one?
        if (static_cast<int>(connections_.size()) < maxConnections_) {
            auto conn = createConnection();
            if (conn) {
                connections_.emplace_back(conn);
                connections_.back().inUse = true;
                
                // Subscribe new connection to active subscriptions
                if (!activeSubscriptions_.empty()) {
                    LOG_INFO("Subscribing new connection to " + std::to_string(activeSubscriptions_.size()) + " active symbols");
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    
                    try {
                        bool subSuccess = conn->subscribe(activeSubscriptions_);
                        if (subSuccess) {
                            LOG_INFO("Successfully subscribed new connection to active symbols");
                        } else {
                            LOG_WARN("Failed to subscribe new connection to active symbols");
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Exception subscribing new connection: " + std::string(e.what()));
                    }
                }
                
                LOG_DEBUG("Created and subscribed new connection for pool '" + poolId_ + "'");
                return connections_.back().connection;
            }
        }
        
        // Wait for connection to be released
        auto available = connectionAvailable_.wait_for(
            lock, 
            std::chrono::milliseconds(timeoutMs),
            [this]() {
                return std::any_of(connections_.begin(), connections_.end(),
                    [](const ConnectionInfo& info) {
                        return !info.inUse && info.connection && info.connection->isConnected();
                    });
            });
        
        if (available) {
            for (auto& info : connections_) {
                if (!info.inUse && info.connection && info.connection->isConnected()) {
                    info.inUse = true;
                    info.lastUsed = std::chrono::steady_clock::now();
                    
                    LOG_DEBUG("Acquired released connection from pool '" + poolId_ + "'");
                    return info.connection;
                }
            }
        }
    }
    
    // Timeout
    connectionTimeouts_++;
    LOG_WARN("Timed out waiting for connection from pool '" + poolId_ + "'");
    return nullptr;
}


// 3. Update releaseConnection method  
void ConnectionPool::releaseConnection(ClientInterfacePtr connection) {
    if (!connection) {
        return;
    }
    
    connectionReleases_++;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find the connection in our pool
    auto it = std::find_if(connections_.begin(), connections_.end(),
        [&connection](const ConnectionInfo& info) {
            return info.connection.get() == connection.get();
        });
    
    if (it != connections_.end()) {
        it->inUse = false;
        it->lastUsed = std::chrono::steady_clock::now();
        
        // If the connection is broken, increment its failure count
        if (!connection->isConnected()) {
            it->failureCount++;
            LOG_WARN("Released broken connection to pool '" + poolId_ + 
                       "' (failure count: " + std::to_string(it->failureCount) + ")");
        } else {
            LOG_DEBUG("Released connection to pool '" + poolId_ + "'");
        }
        
        // Notify one waiting thread that a connection is available
        connectionAvailable_.notify_one();
    } else {
        LOG_WARN("Released connection not found in pool '" + poolId_ + "'");
    }
}


ClientInterfacePtr ConnectionPool::createConnection() {
    try {
        auto conn = connectionFactory_();
        
        if (conn && conn->connect()) {
            LOG_DEBUG("Created new connection for pool '" + poolId_ + "'");
            
            // ✅ REMOVED: Auto-subscription logic that can cause duplicates
            // Let the calling code handle subscriptions explicitly
            // This prevents multiple connections from subscribing to the same symbols
            
            return conn;
        } else {
            connectionFailures_++;
            LOG_ERROR("Failed to create/connect new connection for pool '" + poolId_ + "'");
        }
    } catch (const std::exception& e) {
        connectionFailures_++;
        LOG_ERROR("Exception creating connection for pool '" + poolId_ + "': " + e.what());
    }
    
    return nullptr;
}    


// ✅ ADD: New method for explicit subscription management
bool ConnectionPool::subscribeAllConnections(const std::vector<core::Symbol>& symbols) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    setActiveSubscriptions(symbols); // Track the subscriptions
    
    bool allSuccess = true;
    int subscribed = 0;
    
    for (auto& info : connections_) {
        if (info.connection && info.connection->isConnected()) {
            try {
                bool success = info.connection->subscribe(symbols);
                if (success) {
                    subscribed++;
                    LOG_DEBUG("Subscribed connection to " + std::to_string(symbols.size()) + " symbols");
                } else {
                    allSuccess = false;
                    LOG_WARN("Failed to subscribe connection to symbols");
                }
            } catch (const std::exception& e) {
                allSuccess = false;
                LOG_ERROR("Exception subscribing connection: " + std::string(e.what()));
            }
        }
    }
    
    LOG_INFO("Subscribed " + std::to_string(subscribed) + "/" + std::to_string(connections_.size()) + 
             " connections to " + std::to_string(symbols.size()) + " symbols");
    
    return allSuccess;
}


// ✅ Subscription management methods
void ConnectionPool::setActiveSubscriptions(const std::vector<core::Symbol>& symbols) {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    activeSubscriptions_ = symbols;
    
    LOG_INFO("Connection pool '" + poolId_ + "' tracking " + std::to_string(symbols.size()) + " active subscriptions");
    for (const auto& symbol : symbols) {
        LOG_DEBUG("  Tracking subscription: " + symbol.toString());
    }
}

std::vector<core::Symbol> ConnectionPool::getActiveSubscriptions() const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    return activeSubscriptions_;
}

ConnectionPool::~ConnectionPool() {
    shutdown();
}
bool ConnectionPool::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        return false;
    }

    std::cout << "Initializing connection pool '" << poolId_ << "'..." << std::endl;

    // Pre-create minimum number of connections
    int created = 0;
    for (int i = 0; i < minConnections_; ++i) {
        auto conn = createConnection();
        if (conn) {
            ConnectionInfo info(conn);
            connections_.push_back(std::move(info));
            ++created;
        }
    }

    // Start the monitoring thread only if at least one connection was created
    if (created > 0) {
        running_ = true;
        monitorThread_ = std::thread(&ConnectionPool::monitorConnections, this);
        std::cout << "Connection pool '" << poolId_ << "' initialized with "
                  << connections_.size() << " connections" << std::endl;
        return true;
    } else {
        std::cerr << "Failed to initialize any connections for pool '" << poolId_ << "'" << std::endl;
        return false;
    }
}


bool ConnectionPool::connectAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    bool all_connected = true;
    for (auto& info : connections_) {
        if (info.connection && !info.connection->isConnected()) {
            if (!info.connection->connect()) {
                all_connected = false;
            }
        }
    }
    return all_connected;
}
void ConnectionPool::disconnectAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& info : connections_) {
        if (info.connection && info.connection->isConnected()) {
            info.connection->disconnect();
        }
    }
}
void ConnectionPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!running_) {
            return;
        }
        
        std::cout << "Shutting down connection pool '" << poolId_ << "'..." << std::endl;
        running_ = false;
    }
    
    // Wake up any threads waiting for connections
    connectionAvailable_.notify_all();
    
    // Wait for the monitor thread to finish
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
    
    // Close all connections
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& info : connections_) {
            if (info.connection) {
                info.connection->disconnect();
                info.connection.reset();
            }
        }
        
        connections_.clear();
    }
    
    std::cout << "Connection pool '" << poolId_ << "' shut down" << std::endl;
}

// bool ConnectionPool::isHealthy() const {
//     std::lock_guard<std::mutex> lock(mutex_);
    
//     // The pool is healthy if at least one connection is working
//     return std::any_of(connections_.begin(), connections_.end(),
//         [](const ConnectionInfo& info) {
//             return info.connection && info.connection->isConnected();
//         });
// }

int ConnectionPool::getActiveConnectionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    return std::count_if(connections_.begin(), connections_.end(),
        [](const ConnectionInfo& info) {
            return info.inUse;
        });
}

int ConnectionPool::getAvailableConnectionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    return std::count_if(connections_.begin(), connections_.end(),
        [](const ConnectionInfo& info) {
            return !info.inUse && info.connection && info.connection->isConnected();
        });
}

int ConnectionPool::getTotalConnectionCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

// ✅ UPDATE: Enhanced monitoring with health checks
void ConnectionPool::monitorConnections() {
    LOG_INFO("Starting enhanced connection pool monitor for '" + poolId_ + "'");
    
    while (running_) {
        // ✅ More frequent monitoring for better stability
        std::this_thread::sleep_for(std::chrono::seconds(30)); // Check every 30 seconds
        
        if (!running_) {
            break;
        }
        
        // Perform maintenance tasks
        checkConnectionHealth();
        pruneConnections();
        ensureMinConnections();
        removeUnhealthyConnections();
        
        // ✅ Log pool status periodically
        auto stats = getStats();
        if (stats.totalConnections > 0) {
            LOG_DEBUG("Pool '" + poolId_ + "' status: " + 
                     std::to_string(stats.totalConnections) + " total, " +
                     std::to_string(stats.availableConnections) + " available, " +
                     std::to_string(stats.activeConnections) + " active");
        }
    }
    
    LOG_INFO("Stopped enhanced connection pool monitor for '" + poolId_ + "'");
}

void ConnectionPool::checkConnectionHealth() {
    std::lock_guard<std::mutex> lock(mutex_);
    int unhealthyCount = 0;
    int reconnected = 0;
    auto now = std::chrono::steady_clock::now();
    
    for (auto& info : connections_) {
        if (info.connection) {
            bool wasConnected = info.connection->isConnected();
            
            // Check basic connection status
            if (!wasConnected) {
                info.failureCount++;
                unhealthyCount++;
                
                // ✅ TRY TO RECONNECT if failure count is not too high
                if (info.failureCount <= 3 && !info.inUse) {
                    LOG_WARN("Attempting to reconnect failed connection in pool '" + poolId_ + "'");
                    if (info.connection->connect()) {
                        info.failureCount = 0; // Reset on successful reconnect
                        reconnected++;
                        LOG_INFO("Successfully reconnected connection in pool '" + poolId_ + "'");
                        
                        // Resubscribe after reconnect
                        if (!activeSubscriptions_.empty()) {
                            try {
                                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                                info.connection->subscribe(activeSubscriptions_);
                            } catch (const std::exception& e) {
                                LOG_ERROR("Failed to resubscribe after reconnect: " + std::string(e.what()));
                            }
                        }
                    }
                }
                continue;
            }
            
            // ✅ Check connection-specific health if available (but be less aggressive)
            auto healthyConn = std::dynamic_pointer_cast<BinanceWebSocketClient>(info.connection);
            if (healthyConn && !healthyConn->isConnectionHealthy()) {
                // ✅ Only increment failure count every other check to reduce false positives
                static int healthCheckCounter = 0;
                healthCheckCounter++;
                
                if (healthCheckCounter % 2 == 0) {  // Only increment every other check
                    LOG_WARN("Connection in pool '" + poolId_ + "' appears inactive - marking as unhealthy");
                    info.failureCount++;
                    unhealthyCount++;
                } else {
                    LOG_DEBUG("Connection appears inactive but giving another chance");
                }
            } else if (healthyConn) {
                // ✅ Connection is healthy - reduce failure count
                if (info.failureCount > 0) {
                    info.failureCount = std::max(0, info.failureCount - 1);
                }
            }
            
            // Check if connection has been idle too long while in use
            if (info.inUse) {
                auto inUseTime = std::chrono::duration_cast<std::chrono::minutes>(now - info.lastUsed);
                if (inUseTime.count() > 30) { // 30 minutes max usage
                    LOG_WARN("Connection has been in use for " + std::to_string(inUseTime.count()) + " minutes - forcing release");
                    info.inUse = false;
                    connectionAvailable_.notify_one();
                }
            }
        }
    }
    
    if (unhealthyCount > 0) {
        LOG_INFO("Found " + std::to_string(unhealthyCount) + " unhealthy connections in pool '" + poolId_ + "'");
    }
    if (reconnected > 0) {
        LOG_INFO("Reconnected " + std::to_string(reconnected) + " connections in pool '" + poolId_ + "'");
    }
}

// ✅ Add this new method for emergency situations
bool ConnectionPool::emergencyRecovery() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_ERROR("INITIATING EMERGENCY RECOVERY for pool '" + poolId_ + "'");
    
    // Force disconnect all connections
    for (auto& info : connections_) {
        if (info.connection) {
            info.connection->disconnect();
            info.inUse = false;
        }
    }
    
    // Brief pause
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Try to reconnect at least one connection
    for (auto& info : connections_) {
        if (info.connection) {
            LOG_INFO("Attempting emergency reconnect...");
            if (info.connection->connect()) {
                info.failureCount = 0;
                
                // Resubscribe
                if (!activeSubscriptions_.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    try {
                        bool subSuccess = info.connection->subscribe(activeSubscriptions_);
                        if (subSuccess) {
                            LOG_INFO("EMERGENCY RECOVERY SUCCESSFUL");
                            connectionAvailable_.notify_all();
                            return true;
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Emergency resubscribe failed: " + std::string(e.what()));
                    }
                }
            }
        }
    }
    
    LOG_ERROR("EMERGENCY RECOVERY FAILED");
    return false;
}


void ConnectionPool::pruneConnections() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (connections_.size() <= static_cast<size_t>(minConnections_)) {
        return;  // Don't prune if we're at or below the minimum
    }
    
    auto now = std::chrono::steady_clock::now();
    int pruned = 0;
    
    // Identify connections to remove (idle and exceeding the minimum)
    auto it = connections_.begin();
    while (it != connections_.end() && static_cast<int>(connections_.size() - pruned) > minConnections_) {
        if (!it->inUse) {
            auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(now - it->lastUsed);
            
            if (idle_time > idleTimeout_) {
                std::cout << "Pruning idle connection from pool '" << poolId_ 
                          << "' (idle for " << idle_time.count() << "s)" << std::endl;
                
                if (it->connection) {
                    it->connection->disconnect();
                    it->connection.reset();
                }
                
                it = connections_.erase(it);
                pruned++;
                continue;
            }
        }
        ++it;
    }
    
    if (pruned > 0) {
        std::cout << "Pruned " << pruned << " idle connections from pool '" << poolId_ << "'" << std::endl;
    }
}

// void ConnectionPool::ensureMinConnections() {
//     std::lock_guard<std::mutex> lock(mutex_);
    
//     // Count current healthy connections
//     int healthy_count = std::count_if(connections_.begin(), connections_.end(),
//         [](const ConnectionInfo& info) {
//             return info.connection && info.connection->isConnected();
//         });
    
//     // Create new connections if we're below the minimum
//     if (healthy_count < minConnections_) {
//         int to_create = minConnections_ - healthy_count;
//         std::cout << "Creating " << to_create << " connections to maintain minimum in pool '" 
//                   << poolId_ << "'" << std::endl;
        
//         for (int i = 0; i < to_create; ++i) {
//             auto conn = createConnection();
//             if (conn) {
//                 // ConnectionInfo info{conn, false, std::chrono::steady_clock::now(), 0};
//                 ConnectionInfo info(conn);
//                 connections_.push_back(std::move(info));
//             } else {
//                 connectionFailures_++;
//             }
//         }
//     }
// }


// ✅ ADD: Force reconnection of all connections (for emergency recovery)
void ConnectionPool::forceReconnectAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_WARN("Force reconnecting all connections in pool '" + poolId_ + "'");
    
    for (auto& info : connections_) {
        if (info.connection) {
            try {
                // Disconnect and reconnect
                info.connection->disconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                if (info.connection->connect()) {
                    info.failureCount = 0;
                    
                    // Resubscribe to active subscriptions
                    if (!activeSubscriptions_.empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        info.connection->subscribe(activeSubscriptions_);
                    }
                    
                    LOG_INFO("Successfully reconnected connection in pool '" + poolId_ + "'");
                } else {
                    info.failureCount++;
                    LOG_ERROR("Failed to reconnect connection in pool '" + poolId_ + "'");
                }
            } catch (const std::exception& e) {
                info.failureCount++;
                LOG_ERROR("Exception during force reconnect: " + std::string(e.what()));
            }
        }
    }
}
// ✅ ADD: Method to check overall pool health
bool ConnectionPool::isHealthy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Pool is healthy if we have at least one working connection and
    // at least 50% of connections are functional
    int workingConnections = std::count_if(connections_.begin(), connections_.end(),
        [](const ConnectionInfo& info) {
            return info.connection && info.connection->isConnected() && info.failureCount < 3;
        });
    
    bool hasMinWorking = workingConnections >= 1;
    bool hasGoodRatio = connections_.empty() || 
                       (workingConnections * 2 >= static_cast<int>(connections_.size()));
    
    return hasMinWorking && hasGoodRatio;
}

// ✅ UPDATE: Improved ensureMinConnections with subscription handling
void ConnectionPool::ensureMinConnections() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Count current healthy connections
    int healthy_count = std::count_if(connections_.begin(), connections_.end(),
        [](const ConnectionInfo& info) {
            return info.connection && info.connection->isConnected() && info.failureCount < 3;
        });
    
    // Create new connections if we're below the minimum
    if (healthy_count < minConnections_) {
        int to_create = minConnections_ - healthy_count;
        LOG_INFO("Creating " + std::to_string(to_create) + " connections to maintain minimum in pool '" + poolId_ + "'");
        
        for (int i = 0; i < to_create; ++i) {
            auto conn = createConnection();
            if (conn) {
                ConnectionInfo info(conn);
                connections_.push_back(std::move(info));
                
                // ✅ Subscribe new connection if we have active subscriptions
                if (!activeSubscriptions_.empty()) {
                    LOG_DEBUG("Subscribing new maintenance connection to active symbols");
                    
                    try {
                        std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        bool subSuccess = conn->subscribe(activeSubscriptions_);
                        if (!subSuccess) {
                            LOG_WARN("Failed to subscribe maintenance connection");
                        }
                    } catch (const std::exception& e) {
                        LOG_ERROR("Exception subscribing maintenance connection: " + std::string(e.what()));
                    }
                }
            } else {
                connectionFailures_++;
            }
        }
    }
}



void ConnectionPool::removeUnhealthyConnections() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Remove connections with too many failures
    auto it = connections_.begin();
    while (it != connections_.end()) {
        if (it->failureCount >= maxFailures_) {
            std::cout << "Removing unhealthy connection from pool '" << poolId_ 
                      << "' (failures: " << it->failureCount << ")" << std::endl;
            
            if (it->connection) {
                it->connection->disconnect();
                it->connection.reset();
            }
            
            it = connections_.erase(it);
        } else {
            ++it;
        }
    }
}


ConnectionPool::PoolStats ConnectionPool::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    PoolStats stats;
    stats.connectionRequests = connectionRequests_.load();
    stats.connectionTimeouts = connectionTimeouts_.load();
    stats.connectionFailures = connectionFailures_.load();
    stats.connectionReleases = connectionReleases_.load();
    stats.totalConnections = static_cast<int>(connections_.size());
    
    // Count active and available connections
    for (const auto& info : connections_) {
        if (info.inUse) {
            stats.activeConnections++;
        } else if (info.connection && info.connection->isConnected()) {
            stats.availableConnections++;
        }
    }
    
    return stats;
}

} // namespace data_ingestion
} // namespace trading
// End of file