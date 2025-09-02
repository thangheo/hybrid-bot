/**
 * @file binance_websocket_client.hpp
 * @brief Binance WebSocket client implementation
 */
#pragma once
#include <boost/beast/ssl.hpp>
#include <boost/asio/ssl/stream.hpp>
#include "data_ingestion/websocket/client_interface.hpp"
#include "core/market_types.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <atomic>
#include <memory>
#include <thread>
#include <random>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace trading {
namespace data_ingestion {

namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

class BinanceWebSocketClient : public ClientInterface {
public:
    // Constructor that matches DataSourceManager expectations
    BinanceWebSocketClient(const std::string& host,
                          const std::string& port,
                          std::function<void(const core::ExchangeId&, const std::string&, const std::vector<uint8_t>&)> callback = nullptr);
    
    ~BinanceWebSocketClient() override;
    
    // Implementation of ClientInterface pure virtual methods
    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    ConnectionStatus getStatus() const override;
    
    // Subscription management (ClientInterface)
    bool subscribe(const std::vector<core::Symbol>& symbols) override;
    bool unsubscribe(const std::vector<core::Symbol>& symbols) override;
    std::vector<core::Symbol> getSubscribedSymbols() const override;
    
    // Message handling (ClientInterface)
    bool sendMessage(const std::string& message) override;
    void setMessageCallback(MessageCallback callback) override;
    void setConnectionCallback(ConnectionCallback callback) override;
    
    // Information methods (ClientInterface)
    core::ExchangeId getExchangeId() const override;
    std::string getExchangeName() const override;
    std::string getEndpoint() const override;
    
    // Statistics (ClientInterface)
    int getMessageCount() const override;
    double getAverageLatency() const override;
    core::Timestamp getLastMessageTime() const override;
    
    // Binance-specific subscription management (for backward compatibility)
    bool subscribeToStreams(const std::vector<std::string>& streams);
    bool unsubscribeFromStreams(const std::vector<std::string>& streams);
    std::vector<std::string> getSubscribedStreams() const;
    // Reconnection control
    void enableAutoReconnect(bool enable) { autoReconnect_ = enable; }
    bool isAutoReconnectEnabled() const { return autoReconnect_; }
    void setMaxReconnectAttempts(int attempts) { maxReconnectAttempts_ = attempts; }
    int getReconnectAttempts() const { return reconnectAttempts_; }
    
    // Connection statistics
    void resetStatistics() {
        messageCount_.store(0);
        totalLatency_.store(0.0);
        reconnectAttempts_ = 0;
    }

private:
    // std::unique_ptr<websocket::stream<ssl::stream<tcp::socket>>> wsStream_;
    // Replace this line:
    // With this:
    // std::unique_ptr<websocket::stream<ssl::stream<tcp::socket>>> wsStream_;
    std::unique_ptr<boost::beast::websocket::stream<
        boost::beast::ssl_stream<boost::beast::tcp_stream>>> wsStream_;

    std::mutex sendMutex_;  // Protect WebSocket send operations    
    // Subscription management
    std::atomic<int> subscriptionIdCounter_{1};
    // Reconnection logic
    bool autoReconnect_ = true;
    int reconnectAttempts_ = 0;
    int maxReconnectAttempts_ = 5;
    std::chrono::seconds reconnectBaseDelay_{2};  // Start with 2 seconds
    std::chrono::seconds maxReconnectDelay_{60};  // Max 60 seconds between attempts

    // Connection state
    std::atomic<bool> keep_running_{false};
    
    // Statistics tracking
    std::atomic<double> totalLatency_{0.0};
    
    // Connection details
    std::string host_;
    std::string port_;
    std::string endpoint_;
    // Add these members
    net::io_context io_context_;
    std::unique_ptr<ssl::context> ssl_context_;
    std::unique_ptr<websocket::stream<ssl::stream<tcp::socket>>> ws_stream_;
    std::thread io_thread_;
    mutable std::mutex write_mutex_;
    
    void runIoLoop();
    // Connection state
    std::atomic<ConnectionStatus> status_{ConnectionStatus::DISCONNECTED};
    std::atomic<bool> connected_{false};
    
    // Callbacks
    MessageCallback messageCallback_;
    ConnectionCallback connectionCallback_;
    std::function<void(const core::ExchangeId&, const std::string&, const std::vector<uint8_t>&)> dataCallback_;
    
    // Statistics
    std::atomic<int> messageCount_{0};
    // std::atomic<double> totalLatency_{0.0};
    core::Timestamp lastMessageTime_;
    
    // Subscription tracking
    mutable std::mutex subscriptionMutex_;
    std::vector<std::string> subscribedStreams_;
    std::vector<core::Symbol> subscribedSymbols_;
    // int subscriptionIdCounter_ = 1;
     void scheduleReconnection();
    
    // Message processing with error handling
    void processMessageSafely(const std::string& message);
    
    // Ping/pong handling for connection keepalive
    void handlePingPong();
    // WebSocket connection (you'll need to implement these with your preferred WebSocket library)
    // These are placeholder - implement with boost::beast or your chosen library
    void initializeConnection();
    void closeConnection();
    bool sendWebSocketMessage(const std::string& message);
    
    // Message processing
    void onWebSocketOpen();
    void onWebSocketMessage(const std::string& message);
    void onWebSocketClose(int code, const std::string& reason);
    void onWebSocketError(const std::string& error);
    
    // Binance-specific message handlers
    void handleStreamMessage(const json& data);
    void handleResponse(const json& data);
    void processOrderBookData(const std::string& symbol, const std::string& eventType, const json& data);
    void processTrade(const std::string& symbol, const json& data);
    void processTicker(const std::string& symbol, const json& data);
    void processBookTicker(const std::string& symbol, const json& data);
    
    // Subscription helpers
    bool sendSubscriptionRequest(const std::vector<std::string>& streams, bool subscribe);
    std::string symbolToStream(const core::Symbol& symbol, const std::string& streamType = "depth") const;
    core::Symbol streamToSymbol(const std::string& stream) const;
    
    // Utility methods
    std::string extractSymbolFromStream(const std::string& stream) const;
    std::string extractEventTypeFromStream(const std::string& stream) const;
    void updateStatistics();
    
    // Thread for handling WebSocket events (if needed)
    std::unique_ptr<std::thread> workerThread_;
    std::atomic<bool> shouldStop_{false};
    void workerFunction();
    
    // Market data simulation methods (temporary until real WebSocket is ready)
    void simulateMarketData();
    void generateOrderBookUpdate(const std::string& symbol, double basePrice, std::mt19937& gen);
    void generateTradeUpdate(const std::string& symbol, double basePrice, std::mt19937& gen);
    void generateTickerUpdate(const std::string& symbol, double basePrice, std::mt19937& gen);
};

} // namespace data_ingestion
} // namespace trading