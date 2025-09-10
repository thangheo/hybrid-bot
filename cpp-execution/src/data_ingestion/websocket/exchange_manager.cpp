// Fixed exchange_manager.cpp - Remove event bus circular dependency
#include "data_ingestion/exchange_manager.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <functional>

namespace trading {
namespace data_ingestion {

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
        // std::cout << "[DEBUG] WebSocket data received - Exchange: " << exchange.toString() 
        //         << ", Symbol: '" << symbol << "', Data size: " << data.size() << std::endl;
        // Forward to the normalizer
        if (normalizer_) {
            normalizer_->processMarketData(update);
        } 
        
    } catch (const std::exception& e) {
        std::cerr << "Error processing WebSocket data: " << e.what() << '\n';
    }
}

// SIMPLIFIED DIRECT SUBSCRIPTION - Remove event bus complexity
bool ExchangeManager::subscribe(const std::vector<std::string>& tradingPairs,
                               const std::vector<DataType>& dataTypes,
                               int depth) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    
    if (!connectionPool_ || !isConnected_.load()) {
        std::cerr << "Not connected. Call connect() first.\n";
        return false;
    }
    
    try {
        std::cout << "=== SUBSCRIBING TO SYMBOLS ===" << std::endl;
        for (const auto& pair : tradingPairs) {
            std::cout << "Will subscribe to: " << pair << std::endl;
        }
        std::cout << "===============================" << std::endl;
        
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
            auto symbol = core::Symbol::fromString(pair);
            std::cout << "Created symbol: " << symbol.base << "/" << symbol.quote << std::endl;
            symbols.emplace_back(symbol);
        }
        
        // Subscribe using the connection
        bool success = connection->subscribe(symbols);
        
        // Release the connection back to the pool
        connectionPool_->releaseConnection(connection);
        
        if (success) {
            // Store the subscription request for reconnection scenarios
            SubscriptionRequest request{};
            request.exchangeId = core::ExchangeId::binance();
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

bool ExchangeManager::isConnected() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return isConnected_.load() && connectionPool_ && connectionPool_->isHealthy();
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

void ExchangeManager::monitorConnections() {
    std::cout << "Starting connection monitor thread\n";
    
    const auto checkInterval = std::chrono::seconds(30);
    
    while (running_.load()) {
        try {
            {
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                
                if (connectionPool_) {
                    bool poolHealthy = connectionPool_->isHealthy();
                    bool currentlyConnected = isConnected_.load();
                    
                    // Update connection status if it changed
                    if (poolHealthy != currentlyConnected) {
                        isConnected_.store(poolHealthy);
                        
                        if (poolHealthy) {
                            std::cout << "Connection to Binance restored\n";
                            
                            // Restore subscriptions after reconnection
                            for (const auto& subscription : activeSubscriptions_) {
                                std::cout << "Attempting to restore subscription for " 
                                          << subscription.tradingPairs.size() << " pairs\n";
                            }
                        } else {
                            std::cout << "Connection to Binance lost\n";
                        }
                    }
                    
                    // Periodic status logging
                    if (poolHealthy) {
                        std::cout << getConnectionStats() << '\n';
                    }
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
    const std::vector<std::string>& tradingPairs,
    const std::vector<DataType>& dataTypes,
    int depth) {
    
    std::vector<std::string> streams;
    streams.reserve(tradingPairs.size() * dataTypes.size());
    
    for (const auto& pair : tradingPairs) {
        // Use ONLY ONE way to format the pair - choose the simpler one:
        std::string formattedPair = toLowercase(pair);
        std::cout << "Adding symbol: " << pair << " to stream as: " << formattedPair << std::endl;

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