// src/data_ingestion/websocket/connection_pool.cpp
#include "data_ingestion/websocket/connection_pool.hpp"
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

// 2. Update acquireConnection method
ClientInterfacePtr ConnectionPool::acquireConnection(int timeoutMs) {
    connectionRequests_++;
    
    auto startTime = std::chrono::steady_clock::now();
    
    {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // First check for any available connections
        for (auto& info : connections_) {
            if (!info.inUse && info.connection && info.connection->isConnected()) {
                info.inUse = true;
                info.lastUsed = std::chrono::steady_clock::now();
                
                LOG_DEBUG("Acquired existing connection from pool '" + poolId_ + "'");
                return info.connection;
            }
        }
        
        // No available connections, can we create a new one?
        if (static_cast<int>(connections_.size()) < maxConnections_) {
            auto conn = createConnection();
            if (conn) {
                connections_.emplace_back(conn);
                connections_.back().inUse = true;
                
                LOG_DEBUG("Created new connection for pool '" + poolId_ + "'");
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

// 4. Update createConnection method
ClientInterfacePtr ConnectionPool::createConnection() {
    try {
        auto conn = connectionFactory_();
        
        if (conn && conn->connect()) {
            LOG_DEBUG("Created new connection for pool '" + poolId_ + "'");
            
            // ✅ Auto-subscribe to active subscriptions if any exist
            {
                std::lock_guard<std::mutex> lock(subscriptionMutex_);
                if (!activeSubscriptions_.empty()) {
                    LOG_INFO("Auto-subscribing new connection to " + std::to_string(activeSubscriptions_.size()) + " symbols");
                    
                    // Give connection time to stabilize
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    
                    bool subSuccess = conn->subscribe(activeSubscriptions_);
                    if (subSuccess) {
                        LOG_INFO("✅ Successfully auto-subscribed new connection to all symbols");
                    } else {
                        LOG_ERROR("❌ Failed to auto-subscribe new connection to symbols");
                    }
                } else {
                    LOG_DEBUG("No active subscriptions to restore");
                }
            }
            
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

bool ConnectionPool::isHealthy() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // The pool is healthy if at least one connection is working
    return std::any_of(connections_.begin(), connections_.end(),
        [](const ConnectionInfo& info) {
            return info.connection && info.connection->isConnected();
        });
}

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

void ConnectionPool::monitorConnections() {
    std::cout << "Starting connection pool monitor for '" << poolId_ << "'" << std::endl;
    
    while (running_) {
        // Sleep for the prune interval
        std::this_thread::sleep_for(pruneInterval_);
        
        if (!running_) {
            break;
        }
        
        // Perform maintenance tasks
        pruneConnections();
        ensureMinConnections();
        removeUnhealthyConnections();
    }
    
    std::cout << "Stopped connection pool monitor for '" << poolId_ << "'" << std::endl;
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

void ConnectionPool::ensureMinConnections() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Count current healthy connections
    int healthy_count = std::count_if(connections_.begin(), connections_.end(),
        [](const ConnectionInfo& info) {
            return info.connection && info.connection->isConnected();
        });
    
    // Create new connections if we're below the minimum
    if (healthy_count < minConnections_) {
        int to_create = minConnections_ - healthy_count;
        std::cout << "Creating " << to_create << " connections to maintain minimum in pool '" 
                  << poolId_ << "'" << std::endl;
        
        for (int i = 0; i < to_create; ++i) {
            auto conn = createConnection();
            if (conn) {
                // ConnectionInfo info{conn, false, std::chrono::steady_clock::now(), 0};
                ConnectionInfo info(conn);
                connections_.push_back(std::move(info));
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