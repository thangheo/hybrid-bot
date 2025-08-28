// src/data_ingestion/websocket/exchanges/binance_client_factory.cpp
#include "data_ingestion/websocket/exchanges/binance_client_factory.hpp"

namespace trading {
namespace data_ingestion {

std::shared_ptr<BinanceWebSocketClient> BinanceClientFactory::createClient(
    MarketDataCallback callback,
    bool useTestnet) {
    
    const std::string& host = useTestnet ? TESTNET_HOST : MAINNET_HOST;
    
    // Convert MarketDataCallback to the expected signature
    auto dataCallback = [callback](const core::ExchangeId& exchange, const std::string& symbol, const std::vector<uint8_t>& data) {
        if (callback) {
            // Convert raw data back to MarketDataUpdate if needed
            // For now, we'll create a basic update - you may need to parse JSON here
            MarketDataUpdate update;
            update.exchangeId = exchange;
            update.tradingPair = symbol;
            update.timestamp = std::chrono::system_clock::now();
            update.updateType = "orderbook"; // or parse from data
            update.data = data;
            
            callback(update);
        }
    };
    
    return std::make_shared<BinanceWebSocketClient>(host, PORT, dataCallback);
}

std::unique_ptr<ConnectionPool> BinanceClientFactory::createConnectionPool(
    MarketDataCallback callback,
    int minConnections,
    int maxConnections,
    bool useTestnet) {
    
    const std::string& host = useTestnet ? TESTNET_HOST : MAINNET_HOST;
    
    // Convert callback for connection factory
    auto dataCallback = [callback](const core::ExchangeId& exchange, const std::string& symbol, const std::vector<uint8_t>& data) {
        if (callback) {
            MarketDataUpdate update;
            update.exchangeId = exchange;
            update.tradingPair = symbol;
            update.timestamp = std::chrono::system_clock::now();
            update.updateType = "orderbook";
            update.data = data;
            
            callback(update);
        }
    };
    
    ConnectionFactory factory = [host, dataCallback]() {
        return std::make_shared<BinanceWebSocketClient>(host, PORT, dataCallback);
    };
    
    // Create the connection pool
    auto pool = std::make_unique<ConnectionPool>(
        "binance" + std::string(useTestnet ? "-testnet" : ""),
        factory,
        minConnections,
        maxConnections
    );
    
    // Initialize the pool
    pool->initialize();
    
    return pool;
}

} // namespace data_ingestion
} // namespace trading