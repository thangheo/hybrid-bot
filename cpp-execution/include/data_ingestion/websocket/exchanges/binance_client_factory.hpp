// include/data_ingestion/websocket/exchanges/binance_client_factory.hpp
#pragma once

#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "data_ingestion/websocket/connection_pool.hpp"
#include "data_ingestion/websocket/exchanges/exchange_connector.hpp"
#include <memory>
#include <functional>
#include <string>

namespace trading {
namespace data_ingestion {

/**
 * @brief Factory functions for creating Binance WebSocket clients and connection pools
 */
class BinanceClientFactory {
public:
    /**
     * @brief Create a new Binance WebSocket client
     * 
     * @param callback The callback function for market data updates
     * @param useTestnet Whether to connect to the testnet API
     * @return std::shared_ptr<BinanceWebSocketClient> The newly created client
     */
    static std::shared_ptr<BinanceWebSocketClient> createClient(
        MarketDataCallback callback,
        bool useTestnet = false);
    
    /**
     * @brief Create a new Binance WebSocket connection pool
     * 
     * @param callback The callback function for market data updates
     * @param minConnections Minimum number of connections to maintain
     * @param maxConnections Maximum number of connections to allow
     * @param useTestnet Whether to connect to the testnet API
     * @return std::unique_ptr<ConnectionPool> The newly created connection pool
     */
    static std::unique_ptr<ConnectionPool> createConnectionPool(
        MarketDataCallback callback,
        int minConnections = 1,
        int maxConnections = 5,
        bool useTestnet = false);
    
private:
    // Connection settings
    static constexpr const char* MAINNET_HOST = "stream.binance.com";
    static constexpr const char* TESTNET_HOST = "testnet.binance.vision";
    static constexpr const char* PORT = "443";
};

} // namespace data_ingestion
} // namespace trading