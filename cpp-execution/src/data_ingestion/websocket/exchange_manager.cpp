// DEADLOCK-FREE exchange_manager.cpp - ULTIMATE FIX
#include "data_ingestion/exchange_manager.hpp"
#include "execution/rate_limit_controller.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <functional>

namespace trading {
namespace data_ingestion {

ExchangeManager::ExchangeManager(const core::ExchangeId& exchangeId,
                            std::shared_ptr<execution::RateLimitController> rateLimitController,
                            NormalizedOrderBookCallback normalizedCallback)
    : exchangeId_(exchangeId),
    rateLimitController_(std::move(rateLimitController)),
    normalizedCallback_(std::move(normalizedCallback)),
    minConnections_(3),
    maxConnections_(8) {
    // Setup Binance rate limits
    if (rateLimitController_ && exchangeId == core::ExchangeId::binance()) {
        std::unordered_map<execution::ExchangeRequestType, execution::RateLimitRule> rules = {
            {execution::ExchangeRequestType::CONNECTION, {5, std::chrono::seconds(1)}},
            {execution::ExchangeRequestType::SUBSCRIPTION, {10, std::chrono::seconds(1)}}
        };
        rateLimitController_->setRateLimitRules(rules);
    }
   // Create the normalizer with the callback
    normalizer_ = std::make_unique<OrderBookNormalizer>(normalizedCallback_);
    
    // Create WebSocket callback that properly forwards to normalizer
    websocketCallback_ = [this](const core::ExchangeId& exchange,
                               const core::Symbol& symbol,
                               const std::vector<uint8_t>& data) {
        this->onWebSocketData(exchange, symbol, data);
    };
    
    std::cout << "ExchangeManager initialized with " << minConnections_
              << "-" << maxConnections_ << " connections\n";
}

ExchangeManager::ExchangeManager(NormalizedOrderBookCallback callback,
                                int minConnections,
                                int maxConnections)
    : normalizedCallback_(std::move(callback)),
      minConnections_(minConnections),
      maxConnections_(maxConnections) {
    
    // Create the normalizer with the callback
    normalizer_ = std::make_unique<OrderBookNormalizer>(normalizedCallback_);
    
    // Create WebSocket callback that properly forwards to normalizer
    websocketCallback_ = [this](const core::ExchangeId& exchange,
                               const core::Symbol& symbol,
                               const std::vector<uint8_t>& data) {
        this->onWebSocketData(exchange, symbol, data);
    };
    
    std::cout << "ExchangeManager initialized with " << minConnections_
              << "-" << maxConnections_ << " connections\n";
}

inline std::string ExchangeManager::toLowercase(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return result;
}

ExchangeManager::~ExchangeManager() {
    // Stop the monitor thread first
    running_.store(false);
    if (monitorThread_.joinable()) {
        monitorThread_.join();
    }
    
    // Then disconnect
    disconnect();
    
    std::cout << "ExchangeManager shutdown complete\n";
}

bool ExchangeManager::initialize() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    try {
        // Create the connection pool for Binance
        connectionPool_ = std::make_unique<ConnectionPool>(
            "binance",
            [this]() -> ClientInterfacePtr {
                return std::make_shared<BinanceWebSocketClient>(
                    "stream.binance.com",
                    "9443",
                    this->websocketCallback_
                );
            },
            minConnections_,
            maxConnections_
        );
        
        // Initialize the connection pool
        if (!connectionPool_->initialize()) {
            std::cerr << "Failed to initialize connection pool\n";
            return false;
        }
        
        // Start the connection monitor thread
        running_.store(true);
        monitorThread_ = std::thread(&ExchangeManager::monitorConnections, this);
        
        std::cout << "Binance connection pool initialized successfully\n";
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Error initializing connection pool: " << e.what() << '\n';
        return false;
    }
}

void ExchangeManager::onWebSocketData(const core::ExchangeId& exchange,
                                    const core::Symbol& symbol, 
                                    const std::vector<uint8_t>& data) {
    try {
        // Convert WebSocket data to MarketDataUpdate format
        MarketDataUpdate update{};
        update.exchangeId = exchange;
        update.tradingPair = symbol;
        update.receivedTimestamp = std::chrono::system_clock::now();
        update.data = data;
        update.updateType = "orderbook";
        // Forward to the normalizer
        if (normalizer_) {
            normalizer_->processMarketData(update);
        } 
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing WebSocket data: " << e.what() << '\n';
    }
}

// SIMPLIFIED DIRECT SUBSCRIPTION - Remove event bus complexity
// bool ExchangeManager::subscribe(const std::vector<Symbol>& tradingPairs,
//                                const std::vector<DataType>& dataTypes,
//                                int depth) {
//     std::lock_guard<std::recursive_mutex> lock(mutex_);
    
//     if (!connectionPool_ || !isConnected_.load()) {
//         std::cerr << "Not connected. Call connect() first.\n";
//         return false;
//     }
    
//     try {
//         // Acquire a connection from the pool
//         auto connection = connectionPool_->acquireConnection();
//         if (!connection) {
//             std::cerr << "Failed to acquire connection for subscription\n";
//             return false;
//         }
        
//         // Convert trading pairs to Symbol objects
//         std::vector<core::Symbol> symbols;
//         symbols.reserve(tradingPairs.size());
        
//         for (const auto& pair : tradingPairs) {
//             auto symbol = pair;
//             symbols.emplace_back(symbol);
//         }
        
//         // Subscribe using the connection
//         bool success = connection->subscribe(symbols);
        
//         // Release the connection back to the pool
//         connectionPool_->releaseConnection(connection);
        
//         if (success) {
//             // Tell the connection pool about active subscriptions for auto-resubscription
//             connectionPool_->setActiveSubscriptions(symbols);
            
//             // Store the subscription request for reconnection scenarios
//             SubscriptionRequest request{};
//             request.exchangeId = core::ExchangeId::binance();
//             request.tradingPairs = tradingPairs ;
//             request.dataTypes = dataTypes;
//             request.depth = depth;
            
//             activeSubscriptions_.push_back(std::move(request));
            
//             std::cout << "Successfully subscribed to " << tradingPairs.size() 
//                       << " trading pairs\n";
//         } else {
//             std::cerr << "Failed to subscribe to trading pairs\n";
//         }
        
//         return success;
        
//     } catch (const std::exception& e) {
//         std::cerr << "Error during subscription: " << e.what() << '\n';
//         return false;
//     }
// }

bool ExchangeManager::subscribe(const std::vector<Symbol>& tradingPairs,
                               const std::vector<DataType>& dataTypes,
                               int depth) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (!connectionPool_ || !isConnected_.load()) {
        std::cerr << "Not connected. Call connect() first.\n";
        return false;
    }
    
    try {
        // 🔥 RATE LIMIT SUBSCRIPTION REQUESTS
        if (rateLimitController_) {
            if (!rateLimitController_->waitForPermission(execution::ExchangeRequestType::SUBSCRIPTION)) {
                std::cerr << "Rate limited: subscription request blocked\n";
                return false;
            }
        }
        
        // Acquire a connection from the pool  
        auto connection = connectionPool_->acquireConnection();
        if (!connection) {
            std::cerr << "Failed to acquire connection for subscription\n";
            return false;
        }
        
        // Convert trading pairs to Symbol objects
        std::vector<core::Symbol> symbols;
        symbols.reserve(tradingPairs.size());
        for (const auto& pair : tradingPairs) {
            auto symbol = pair;
            symbols.emplace_back(symbol);
        }
        
        // Subscribe using the connection
        bool success = connection->subscribe(symbols);
        
        // Release the connection back to the pool
        connectionPool_->releaseConnection(connection);
        
        if (success) {
            // Tell the connection pool about active subscriptions
            connectionPool_->setActiveSubscriptions(symbols);
            
            // Store the subscription request
            SubscriptionRequest request{};
            request.exchangeId = exchangeId_ == core::ExchangeId() ? core::ExchangeId::binance() : exchangeId_; //good default
            request.tradingPairs = tradingPairs;
            request.dataTypes = dataTypes;
            request.depth = depth;
            activeSubscriptions_.push_back(std::move(request));
            
            std::cout << "Successfully subscribed to " << tradingPairs.size()
                     << " trading pairs\n";
        } else {
            std::cerr << "Failed to subscribe to trading pairs\n";
        }
        
        return success;
    } catch (const std::exception& e) {
        std::cerr << "Error during subscription: " << e.what() << '\n';
        return false;
    }
}

bool ExchangeManager::connect() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
     if (!rateLimitController_->waitForPermission(execution::ExchangeRequestType::CONNECTION)) {
        std::cerr << "Rate limited: connection attempt blocked\n";
        return false;
    }
    if (!connectionPool_) {
        std::cerr << "Connection pool not initialized. Call initialize() first.\n";
        return false;
    }
    
    try {
        bool success = connectionPool_->connectAll();
        isConnected_.store(success);
        
        if (success) {
            std::cout << "Successfully connected to Binance\n";
        } else {
            std::cerr << "Failed to connect to Binance\n";
        }
        
        return success;
        
    } catch (const std::exception& e) {
        std::cerr << "Error connecting: " << e.what() << '\n';
        return false;
    }
}

void ExchangeManager::disconnect() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (connectionPool_) {
        try {
            connectionPool_->shutdown();
            isConnected_.store(false);
            std::cout << "Disconnected from Binance\n";
        } catch (const std::exception& e) {
            std::cerr << "Error disconnecting: " << e.what() << '\n';
        }
    }
}

// ✅ ULTIMATE DEADLOCK FIX: Only use atomic flag - NO MUTEX, NO CONNECTION POOL CALLS
bool ExchangeManager::isConnected() const {
    // SIMPLE: Just return the atomic flag - no locks, no external calls
    return isConnected_.load();
}

OrderBookUpdate ExchangeManager::getOrderBook(const core::Symbol& tradingPair,
                                            int32_t depth) const {
    if (!normalizer_) {
        return OrderBookUpdate{}; // Return empty update if normalizer not available
    }
    
    return normalizer_->getOrderBook(core::ExchangeId::binance(), tradingPair, depth);
}

std::string ExchangeManager::getConnectionStats() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    std::ostringstream oss;
    
    if (connectionPool_) {
        oss << "Binance connection pool - Active: " 
            << connectionPool_->getActiveConnectionCount() 
            << ", Available: " << connectionPool_->getAvailableConnectionCount()
            << ", Total: " << connectionPool_->getTotalConnectionCount();
    } else {
        oss << "Connection pool not initialized";
    }
    
    return oss.str();
}

// ✅ DEADLOCK-FREE MONITOR: Update atomic flag based on pool health without mutex during checks
void ExchangeManager::monitorConnections() {
    std::cout << "Starting connection monitor thread\n";
    
    const auto checkInterval = std::chrono::seconds(30);
    
    while (running_.load()) {
        try {
            bool poolHealthy = false;
            
            // Get a safe reference to the pool without holding the main mutex
            std::unique_ptr<ConnectionPool>* poolRef = nullptr;
            {
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                if (connectionPool_) {
                    poolRef = &connectionPool_;
                }
            }
            
            // Check pool health without holding the main mutex
            if (poolRef && *poolRef) {
                poolHealthy = (*poolRef)->isHealthy();
            }
            
            // Update the atomic flag
            bool wasConnected = isConnected_.exchange(poolHealthy);
            
            // Log status changes
            if (poolHealthy != wasConnected) {
                if (poolHealthy) {
                    std::cout << "Connection to Binance restored\n";
                } else {
                    std::cout << "Connection to Binance lost\n";
                }
            }
            
            // Periodic status logging (without holding mutex for long)
            if (poolHealthy) {
                std::string stats;
                {
                    std::lock_guard<std::recursive_mutex> lock(mutex_);
                    if (connectionPool_) {
                        std::ostringstream oss;
                        oss << "Pool status - Active: " 
                            << connectionPool_->getActiveConnectionCount() 
                            << ", Available: " << connectionPool_->getAvailableConnectionCount()
                            << ", Total: " << connectionPool_->getTotalConnectionCount();
                        stats = oss.str();
                    }
                }
                if (!stats.empty()) {
                    std::cout << stats << '\n';
                }
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error in connection monitor: " << e.what() << '\n';
        }
        
        std::this_thread::sleep_for(checkInterval);
    }
    
    std::cout << "Connection monitor thread stopped\n";
}

std::vector<std::string> ExchangeManager::createStreamNames(
    const std::vector<Symbol>& tradingPairs,
    const std::vector<DataType>& dataTypes,
    int depth) {
    
    std::vector<std::string> streams;
    streams.reserve(tradingPairs.size() * dataTypes.size());
    
    for (const auto& pair : tradingPairs) {
        // Use ONLY ONE way to format the pair - choose the simpler one:
        std::string formattedPair = toLowercase(pair.toString());
        std::cout << "Adding symbol: " << pair.toString() << " to stream as: " << formattedPair << std::endl;

        // Remove '/' separator if present
        formattedPair.erase(
            std::remove(formattedPair.begin(), formattedPair.end(), '/'),
            formattedPair.end());
        
        for (const auto& type : dataTypes) {
            switch (type) {
                case DataType::ORDER_BOOK:
                    if (depth <= 10) {
                        streams.push_back(formattedPair + "@depth10@100ms");
                    } else if (depth <= 20) {
                        streams.push_back(formattedPair + "@depth20@100ms");
                    } else {
                        streams.push_back(formattedPair + "@depth@100ms");
                    }
                    streams.push_back(formattedPair + "@bookTicker");
                    break;
                    
                case DataType::TRADES:
                    streams.push_back(formattedPair + "@trade");
                    break;
                    
                case DataType::TICKER:
                    streams.push_back(formattedPair + "@ticker");
                    break;
                    
                default:
                    std::cerr << "Unknown data type in stream creation\n";
                    break;
            }
        }
    }
    
    return streams;
}

std::string ExchangeManager::generateRequestId() {
    return "req_" + std::to_string(requestIdCounter_++);
}

} // namespace data_ingestion
} // namespace trading