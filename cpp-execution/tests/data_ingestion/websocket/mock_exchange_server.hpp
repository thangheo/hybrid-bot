// tests/data_ingestion/websocket/mock_exchange_server.hpp
#pragma once

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <string>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <atomic>
#include <random>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace trading {
namespace testing {

/**
 * @brief Mock WebSocket server for testing WebSocket clients
 * 
 * This server implements a simplified version of the Binance WebSocket API for testing
 * purposes. It can handle subscriptions, respond to requests, and publish simulated
 * market data.
 */
class MockExchangeServer {
public:
    /**
     * @brief Construct a new Mock Exchange Server
     * 
     * @param port The port to listen on
     */
    explicit MockExchangeServer(uint16_t port = 9001);
    
    /**
     * @brief Destroy the Mock Exchange Server
     */
    ~MockExchangeServer();
    
    /**
     * @brief Start the server
     * 
     * This starts the WebSocket server and the data publisher threads.
     */
    void start();
    
    /**
     * @brief Stop the server
     * 
     * This stops the WebSocket server and the data publisher threads.
     */
    void stop();
    
    /**
     * @brief Check if the server is running
     * 
     * @return true if the server is running
     * @return false otherwise
     */
    bool isRunning() const;
    
    /**
     * @brief Get the number of connected clients
     * 
     * @return int The number of connected clients
     */
    int getConnectionCount() const;
    
private:
    // WebSocket server types
    using server_type = websocketpp::server<websocketpp::config::asio>;
    using message_ptr = websocketpp::config::asio::message_type::ptr;
    using connection_hdl = websocketpp::connection_hdl;
    
    // Client information
    struct ClientInfo {
        int id;
        std::set<std::string> subscriptions;
    };
    
    // Trading pair data
    struct TradingPairData {
        double bid_price;
        double ask_price;
        double last_price;
        double open_price;
        double high_price;
        double low_price;
        double volume;
        int64_t last_update_id;
        int64_t next_trade_id;
        int64_t next_buyer_order_id;
        int64_t next_seller_order_id;
    };
    
    // WebSocket event handlers
    void onOpen(connection_hdl hdl);
    void onClose(connection_hdl hdl);
    void onMessage(connection_hdl hdl, server_type::message_ptr msg);
    
    // Subscription handlers
    void handleSubscription(connection_hdl hdl, int id, const json& streams);
    void handleUnsubscription(connection_hdl hdl, int id, const json& streams);
    
    // Response methods
    void sendSuccessResponse(connection_hdl hdl, int id);
    void sendErrorResponse(connection_hdl hdl, int id, const std::string& message);
    
    // Data publishing
    void publishData();
    void publishOrderBookUpdate(const std::string& symbol);
    void publishTrade(const std::string& symbol);
    void publishTickerUpdate(const std::string& symbol);
    
    // Message creation
    json createOrderBookUpdate(const std::string& symbol);
    json createTradeUpdate(const std::string& symbol);
    json createTickerUpdate(const std::string& symbol);
    
    // Test data setup
    void initializeTestData();
    void addTradingPair(const std::string& symbol, double base_price);
    
    // Utility methods
    double getRandomQuantity();
    std::string formatPrice(double price);
    std::string formatQuantity(double quantity);
    int64_t getCurrentTimestampMs();
    
    // Server state
    uint16_t port_;
    server_type server_;
    std::atomic<bool> running_;
    std::thread server_thread_;
    std::thread publisher_thread_;
    
    // Client connections
    mutable std::mutex connections_mutex_;
    std::map<connection_hdl, ClientInfo, std::owner_less<connection_hdl>> connections_;
    int next_client_id_;
    
    // Test data
    std::map<std::string, TradingPairData> test_data_;
    
    // Random number generator
    std::mt19937 random_engine_;
};

} // namespace testing
} // namespace trading
