// src/data_ingestion/websocket/connection_pool.cpp
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>
#include "common/logger.hpp"

namespace trading {
namespace data_ingestion {

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

// FIXED: Proper destructor with ordered shutdown
ConnectionPool::~ConnectionPool() {
    // Signal shutdown first
    shutdownRequested_.store(true);
    running_.store(false);
    
    // Wake up any waiting threads
    connectionAvailable_.notify_all();
    
    // Shutdown monitor thread first
    if (monitorThread_ && monitorThread_->joinable()) {
        auto joinFuture = std::async(std::launch::async, [this]() {
            monitorThread_->join();
        });
        
        if (joinFuture.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
            std::cerr << "Monitor thread join timeout, detaching" << std::endl;
            monitorThread_->detach();
        }
    }
    
    // Now shutdown all connections
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& info : connections_) {
            info.shutdown();
        }
        connections_.clear();
    }
    
    std::cout << "Connection pool '" << poolId_ << "' destroyed safely" << std::endl;
}

ClientInterfacePtr ConnectionPool::acquireConnection(int timeoutMs) {
    if (shutdownRequested_.load()) {
        return nullptr;
    }
    
    connectionRequests_++;
    
    auto startTime = std::chrono::steady_clock::now();
    
    {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Check for healthy available connections
        for (auto& info : connections_) {
            if (shutdownRequested_.load()) {
                return nullptr;
            }
            
            if (!info.inUse && info.connection && info.connection->isConnected()) {
                // Additional health check if connection supports it
                bool isHealthy = true;
                
                auto healthyConn = std::dynamic_pointer_cast<BinanceWebSocketClient>(info.connection);
                if (healthyConn) {
                    isHealthy = healthyConn->isConnected() && healthyConn->getStatus() == ConnectionStatus::CONNECTED;
                }
                
                if (isHealthy) {
                    info.inUse = true;
                    info.lastUsed = std::chrono::steady_clock::now();
                    info.failureCount = 0;
                    
                    LOG_DEBUG("Acquired healthy connection from pool '" + poolId_ + "'");
                    return info.connection;
                } else {
                    LOG_WARN("Connection appears unhealthy, skipping");
                    info.failureCount++;
                }
            }
        }
        
        // EMERGENCY RECOVERY: If all connections are unhealthy
        bool allUnhealthy = std::all_of(connections_.begin(), connections_.end(),
            [](const ConnectionInfo& info) {
                return !info.connection || !info.connection->isConnected() || info.failureCount >= 3;
            });
            
        if (allUnhealthy && !connections_.empty() && !shutdownRequested_.load()) {
            LOG_ERROR("ALL CONNECTIONS UNHEALTHY - Starting emergency recovery for pool '" + poolId_ + "'");
            
            auto& firstConn = connections_[0];
            if (firstConn.connection) {
                firstConn.connection->disconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                
                if (firstConn.connection->connect()) {
                    firstConn.failureCount = 0;
                    firstConn.inUse = true;
                    firstConn.lastUsed = std::chrono::steady_clock::now();
                    
                    if (!activeSubscriptions_.empty()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        try {
                            firstConn.connection->subscribe(activeSubscriptions_);
                            LOG_INFO("Emergency recovery successful");
                        } catch (const std::exception& e) {
                            LOG_ERROR("Failed to resubscribe during emergency recovery: " + std::string(e.what()));
                        }
                    }
                    
                    return firstConn.connection;
                }
            }
        }
        
        // Create new connection if under limit
        if (static_cast<int>(connections_.size()) < maxConnections_ && !shutdownRequested_.load()) {
            auto conn = createConnection();
            if (conn) {
                connections_.emplace_back(conn);
                connections_.back().inUse = true;
                
                if (!activeSubscriptions_.empty()) {
                    LOG_INFO("Subscribing new connection to " + std::to_string(activeSubscriptions_.size()) + " active symbols");
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    
                    try {
                        bool subSuccess = conn->subscribe(activeSubscriptions_);
                        if (subSuccess) {
                            LOG_INFO("Successfully subscribed new connection");
                        } else {
                            LOG_WARN("Failed to subscribe new connection");
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
                return shutdownRequested_.load() || 
                       std::any_of(connections_.begin(), connections_.end(),
                           [](const ConnectionInfo& info) {
                               return !info.inUse && info.connection && info.connection->isConnected();
                           });
            });
        
        if (available && !shutdownRequested_.load()) {
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
    
    if (shutdownRequested_.load()) {
        return nullptr;
    }
    
    connectionTimeouts_++;
    LOG_WARN("Timed out waiting for connection from pool '" + poolId_ + "'");
    return nullptr;
}

void ConnectionPool::releaseConnection(ClientInterfacePtr connection) {
    if (!connection || shutdownRequested_.load()) {
        return;
    }
    
    connectionReleases_++;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(connections_.begin(), connections_.end(),
        [&connection](const ConnectionInfo& info) {
            return info.connection.get() == connection.get();
        });
    
    if (it != connections_.end()) {
        it->inUse = false;
        it->lastUsed = std::chrono::steady_clock::now();
        
        if (!connection->isConnected()) {
            it->failureCount++;
            LOG_WARN("Released broken connection to pool '" + poolId_ + 
                       "' (failure count: " + std::to_string(it->failureCount) + ")");
        } else {
            LOG_DEBUG("Released connection to pool '" + poolId_ + "'");
        }
        
        connectionAvailable_.notify_one();
    } else {
        LOG_WARN("Released connection not found in pool '" + poolId_ + "'");
    }
}

ClientInterfacePtr ConnectionPool::createConnection() {
    if (shutdownRequested_.load()) {
        return nullptr;
    }
    
    try {
        auto conn = connectionFactory_();
        
        if (conn && conn->connect()) {
            LOG_DEBUG("Created new connection for pool '" + poolId_ + "'");
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

void ConnectionPool::setupRateLimits(const core::ExchangeId& exchangeId) {
    if (!rateLimitController_) return;
    
    if (exchangeId == core::ExchangeId::binance()) {
        std::unordered_map<execution::ExchangeRequestType, execution::RateLimitRule> rules = {
            {execution::ExchangeRequestType::CONNECTION, {5, std::chrono::seconds(1)}},
            {execution::ExchangeRequestType::SUBSCRIPTION, {10, std::chrono::seconds(1)}},
            {execution::ExchangeRequestType::STREAM_REQUEST, {20, std::chrono::seconds(1)}}
        };
        
        rateLimitController_->setRateLimitRules(rules);
        LOG_INFO("Configured Binance rate limits for connection pool");
    }
}

bool ConnectionPool::subscribeAllConnections(const std::vector<core::Symbol>& symbols) {
    if (shutdownRequested_.load()) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (rateLimitController_) {
        if (!rateLimitController_->waitForPermission(execution::ExchangeRequestType::SUBSCRIPTION)) {
            LOG_WARN("Rate limited: cannot subscribe to symbols");
            return false;
        }
    }

    setActiveSubscriptions(symbols);
    
    bool allSuccess = true;
    int subscribed = 0;
    
    for (auto& info : connections_) {
        if (shutdownRequested_.load()) break;
        
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

void ConnectionPool::setActiveSubscriptions(const std::vector<core::Symbol>& symbols) {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    activeSubscriptions_ = symbols;
    
    LOG_INFO("Connection pool '" + poolId_ + "' tracking " + std::to_string(symbols.size()) + " active subscriptions");
}

std::vector<core::Symbol> ConnectionPool::getActiveSubscriptions() const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    return activeSubscriptions_;
}

bool ConnectionPool::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_.load() || shutdownRequested_.load()) {
        return false;
    }

    std::cout << "Initializing connection pool '" << poolId_ << "'..." << std::endl;

    int created = 0;
    for (int i = 0; i < minConnections_; ++i) {
        if (shutdownRequested_.load()) break;
        
        auto conn = createConnection();
        if (conn) {
            ConnectionInfo info(conn);
            connections_.push_back(std::move(info));
            // Start the run thread after the object is fully constructed
            // and has been published into the connections_ container. This
            // prevents other threads from accessing a partially-constructed
            // ConnectionInfo (TSAN data race).
            if (!connections_.empty()) {
                connections_.back().startRunThread();
            }
            ++created;
        }
    }

    if (created > 0 && !shutdownRequested_.load()) {
        running_.store(true);
        monitorThread_ = std::make_unique<std::thread>(&ConnectionPool::monitorConnections, this);
        std::cout << "Connection pool '" << poolId_ << "' initialized with "
                  << connections_.size() << " connections" << std::endl;
        return true;
    } else {
        std::cerr << "Failed to initialize any connections for pool '" << poolId_ << "'" << std::endl;
        return false;
    }
}

bool ConnectionPool::connectAll() {
    if (shutdownRequested_.load()) return false;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    bool all_connected = true;
    for (auto& info : connections_) {
        if (shutdownRequested_.load()) break;
        
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

// FIXED: Proper shutdown with ordered cleanup
void ConnectionPool::shutdown() {
    // Signal shutdown to all components
    shutdownRequested_.store(true);
    running_.store(false);
    
    std::cout << "Shutting down connection pool '" << poolId_ << "'..." << std::endl;
    
    // Wake up any threads waiting for connections
    connectionAvailable_.notify_all();
    
    // Stop monitoring thread first
    if (monitorThread_ && monitorThread_->joinable()) {
        auto joinFuture = std::async(std::launch::async, [this]() {
            monitorThread_->join();
        });
        
        if (joinFuture.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
            std::cerr << "Monitor thread timeout, detaching" << std::endl;
            monitorThread_->detach();
        }
    }
    
    // Shutdown all connections
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& info : connections_) {
            info.shutdown();
        }
        
        connections_.clear();
    }
    
    std::cout << "Connection pool '" << poolId_ << "' shut down safely" << std::endl;
}

int ConnectionPool::getActiveConnectionCount() const {
    if (shutdownRequested_.load()) return 0;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    int count = 0;
    for (const auto& info : connections_) {
        if (info.inUse.load()) {
            ++count;
        }
    }
    return count;
}

int ConnectionPool::getAvailableConnectionCount() const {
    if (shutdownRequested_.load()) return 0;
    
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

// FIXED: Enhanced monitoring with proper shutdown checks
void ConnectionPool::monitorConnections() {
    LOG_INFO("Starting enhanced connection pool monitor for '" + poolId_ + "'");
    
    while (running_.load() && !shutdownRequested_.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        if (!running_.load() || shutdownRequested_.load()) {
            break;
        }
        
        try {
            checkConnectionHealth();
            pruneConnections();
            ensureMinConnections();
            removeUnhealthyConnections();
            
            auto stats = getStats();
            if (stats.totalConnections > 0) {
                LOG_DEBUG("Pool '" + poolId_ + "' status: " + 
                         std::to_string(stats.totalConnections) + " total, " +
                         std::to_string(stats.availableConnections) + " available, " +
                         std::to_string(stats.activeConnections) + " active");
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Exception in monitor thread: " + std::string(e.what()));
        }
    }
    
    LOG_INFO("Stopped enhanced connection pool monitor for '" + poolId_ + "'");
}

void ConnectionPool::checkConnectionHealth() {
    if (shutdownRequested_.load()) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    int unhealthyCount = 0;
    int reconnected = 0;
    auto now = std::chrono::steady_clock::now();
    
    for (auto& info : connections_) {
        if (shutdownRequested_.load()) break;
        
        if (info.connection) {
            bool wasConnected = info.connection->isConnected();
            
            if (!wasConnected) {
                info.failureCount++;
                unhealthyCount++;
                
                if (info.failureCount <= 3 && !info.inUse) {
                    LOG_WARN("Attempting to reconnect failed connection in pool '" + poolId_ + "'");
                    if (info.connection->connect()) {
                        info.failureCount = 0;
                        reconnected++;
                        LOG_INFO("Successfully reconnected connection in pool '" + poolId_ + "'");
                        
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
            
            // Check connection-specific health
            auto healthyConn = std::dynamic_pointer_cast<BinanceWebSocketClient>(info.connection);
            if (healthyConn) {
                bool isHealthy = healthyConn->isConnected() && healthyConn->getStatus() == ConnectionStatus::CONNECTED;
                if (!isHealthy) {
                    info.failureCount++;
                    unhealthyCount++;
                } else if (info.failureCount > 0) {
                    info.failureCount = std::max(0, info.failureCount - 1);
                }
            }
            
            // Check for stuck connections
            if (info.inUse) {
                auto inUseTime = std::chrono::duration_cast<std::chrono::minutes>(now - info.lastUsed);
                if (inUseTime.count() > 30) {
                    LOG_WARN("Connection stuck for " + std::to_string(inUseTime.count()) + " minutes - forcing release");
                    info.inUse = false;
                    connectionAvailable_.notify_one();
                }
            }
        }
    }
    
    if (unhealthyCount > 0) {
        LOG_INFO("Found " + std::to_string(unhealthyCount) + " unhealthy connections");
    }
    if (reconnected > 0) {
        LOG_INFO("Reconnected " + std::to_string(reconnected) + " connections");
    }
}

bool ConnectionPool::emergencyRecovery() {
    if (shutdownRequested_.load()) return false;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_ERROR("INITIATING EMERGENCY RECOVERY for pool '" + poolId_ + "'");
    
    for (auto& info : connections_) {
        if (info.connection) {
            info.connection->disconnect();
            info.inUse = false;
        }
    }
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    for (auto& info : connections_) {
        if (shutdownRequested_.load()) break;
        
        if (info.connection) {
            LOG_INFO("Attempting emergency reconnect...");
            if (info.connection->connect()) {
                info.failureCount = 0;
                
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
    if (shutdownRequested_.load()) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (connections_.size() <= static_cast<size_t>(minConnections_)) {
        return;
    }
    
    auto now = std::chrono::steady_clock::now();
    int pruned = 0;
    
    auto it = connections_.begin();
    while (it != connections_.end() && 
           static_cast<int>(connections_.size() - pruned) > minConnections_ &&
           !shutdownRequested_.load()) {
        
        if (!it->inUse) {
            auto idle_time = std::chrono::duration_cast<std::chrono::seconds>(now - it->lastUsed);
            
            if (idle_time > idleTimeout_) {
                std::cout << "Pruning idle connection from pool '" << poolId_ 
                          << "' (idle for " << idle_time.count() << "s)" << std::endl;
                
                it->shutdown();
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

void ConnectionPool::forceReconnectAll() {
    if (shutdownRequested_.load()) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    LOG_WARN("Force reconnecting all connections in pool '" + poolId_ + "'");
    
    for (auto& info : connections_) {
        if (shutdownRequested_.load()) break;
        
        if (info.connection) {
            try {
                info.connection->disconnect();
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                
                if (info.connection->connect()) {
                    info.failureCount = 0;
                    
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

bool ConnectionPool::isHealthy() const {
    if (shutdownRequested_.load()) return false;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    int workingConnections = std::count_if(connections_.begin(), connections_.end(),
        [](const ConnectionInfo& info) {
            return info.connection && info.connection->isConnected() && info.failureCount < 3;
        });
    
    bool hasMinWorking = workingConnections >= 1;
    bool hasGoodRatio = connections_.empty() || 
                       (workingConnections * 2 >= static_cast<int>(connections_.size()));
    
    return hasMinWorking && hasGoodRatio;
}

void ConnectionPool::ensureMinConnections() {
    if (shutdownRequested_.load()) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    int healthy_count = std::count_if(connections_.begin(), connections_.end(),
        [](const ConnectionInfo& info) {
            return info.connection && info.connection->isConnected() && info.failureCount < 3;
        });
    
    if (healthy_count < minConnections_) {
        int to_create = minConnections_ - healthy_count;
        LOG_INFO("Creating " + std::to_string(to_create) + " connections to maintain minimum in pool '" + poolId_ + "'");
        
        for (int i = 0; i < to_create; ++i) {
            if (shutdownRequested_.load()) break;
            
            auto conn = createConnection();
            if (conn) {
                ConnectionInfo info(conn);
                connections_.push_back(std::move(info));
                if (!connections_.empty()) {
                    connections_.back().startRunThread();
                }
                
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
    if (shutdownRequested_.load()) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = connections_.begin();
    while (it != connections_.end()) {
        if (it->failureCount >= maxFailures_) {
            std::cout << "Removing unhealthy connection from pool '" << poolId_ 
                      << "' (failures: " << it->failureCount << ")" << std::endl;
            
            it->shutdown();
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