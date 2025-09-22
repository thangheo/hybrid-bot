/**
 * @file binance_websocket_client.hpp
 * @brief Production-ready Binance WebSocket client with reliable connection management
 * 
 * Features:
 * - Single-threaded design per client for simplicity and reliability
 * - Robust heartbeat mechanism to prevent connection drops
 * - Exponential backoff reconnection strategy
 * - Thread-safe callback mechanism
 * - Proper SSL/TLS handling for Binance streams
 * - Clean resource management and lifecycle
 * 
 * @author Trading System Team
 * @date September 2025
 */

#pragma once

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/steady_timer.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>
#include <unordered_set>

#include "core/market_types.hpp"
#include "data_ingestion/websocket/client_interface.hpp"

namespace trading {
namespace data_ingestion {

// Boost.Beast type aliases
namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;
using ssl_stream = beast::ssl_stream<beast::tcp_stream>;
using ws_stream = websocket::stream<ssl_stream>;

/**
 * @class BinanceWebSocketClient
 * @brief Reliable WebSocket client for Binance market data streams
 * 
 * This implementation focuses on:
 * 1. Connection stability with proper heartbeat mechanism
 * 2. Single-threaded design to avoid race conditions
 * 3. Robust error handling and reconnection logic
 * 4. Clean resource management
 */
class BinanceWebSocketClient : public ClientInterface, public std::enable_shared_from_this<BinanceWebSocketClient> {
public:
    // Additional callback type for enhanced data handling (beyond the interface)
    using EnhancedDataCallback = std::function<void(const ExchangeId&, const std::string&, const std::vector<uint8_t>&)>;
    using json = nlohmann::json;
    
public:
    /**
     * @brief Constructor
     * @param host Binance WebSocket host (e.g., "stream.binance.com")
     * @param port Port number (typically "9443" for secure connections)
     */
    explicit BinanceWebSocketClient(
        std::string host,
        std::string port
    );
    //new constructor with data callback
    explicit BinanceWebSocketClient(
        std::string host,
        std::string port,
        RawDataCallback data_callback
    );
    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~BinanceWebSocketClient() override;

    // Non-copyable, non-movable for resource safety
    BinanceWebSocketClient(const BinanceWebSocketClient&) = delete;
    BinanceWebSocketClient& operator=(const BinanceWebSocketClient&) = delete;
    BinanceWebSocketClient(BinanceWebSocketClient&&) = delete;
    BinanceWebSocketClient& operator=(BinanceWebSocketClient&&) = delete;

    // === ClientInterface Implementation ===

    /**
     * @brief Connect to Binance WebSocket
     * @return true if connection successful, false otherwise
     */
    bool connect() override;

    /**
     * @brief Gracefully disconnect from WebSocket
     */
    void disconnect() override;

    /**
     * @brief Check if currently connected
     * @return true if connected and healthy
     */
    bool isConnected() const override;

    /**
     * @brief Get current connection state
     * @return Current connection state
     */
    ConnectionStatus getStatus() const override;

    /**
     * @brief Subscribe to order book depth streams for given symbols
     * @param symbols List of trading symbols to subscribe to
     * @return true if subscription successful
     */
    bool subscribe(const std::vector<Symbol>& symbols) override;

    /**
     * @brief Unsubscribe from symbols
     * @param symbols List of symbols to unsubscribe from
     * @return true if unsubscription successful
     */
    bool unsubscribe(const std::vector<Symbol>& symbols) override;

    /**
     * @brief Get list of currently subscribed symbols
     * @return Vector of subscribed symbols
     */
    std::vector<Symbol> getSubscribedSymbols() const override;

    /**
     * @brief Set callback for incoming market data (interface version)
     * @param callback Function to call when data arrives
     */
    // void setDataCallback(DataCallback callback) override;

    /**
     * @brief Set callback for connection status changes
     * @param callback Function to call when connection state changes
     */
    void setStatusCallback(StatusCallback callback) override;

    /**
     * @brief Get the exchange identifier
     * @return Exchange ID for Binance
     */
    ExchangeId getExchangeId() const override {
        return ExchangeId::binance();
    }

    /**
     * @brief Get human-readable exchange name
     * @return "Binance"
     */
    std::string getExchangeName() const override { 
        return "Binance"; 
    }

    /**
     * @brief Get the WebSocket endpoint URL
     * @return Binance WebSocket endpoint
     */
    std::string getEndpoint() const override { 
        return "wss://stream.binance.com:9443/ws"; 
    }

    /**
     * @brief Run the client event loop (blocking call)
     * 
     * This method runs the WebSocket client in a loop, handling:
     * - Incoming messages
     * - Heartbeat/ping mechanism
     * - Reconnection logic
     * - Event processing
     * 
     * Call this in a dedicated thread or from your main event loop.
     */
    void run() override;

    /**
     * @brief Stop the client event loop
     * 
     * Thread-safe method to signal the event loop to stop.
     * Call from another thread to gracefully shutdown the client.
     */
    void stop() override;

    /**
     * @brief Configure reconnection behavior
     * @param enabled Whether to enable automatic reconnection
     * @param maxAttempts Maximum number of reconnection attempts (0 = unlimited)
     * @param baseDelay Initial delay between reconnection attempts
     * @param maxDelay Maximum delay between reconnection attempts
     */
    void setReconnectionConfig(
        bool enabled = true,
        int maxAttempts = 0,
        std::chrono::seconds baseDelay = std::chrono::seconds(2),
        std::chrono::seconds maxDelay = std::chrono::seconds(60)
    ) override;

    /**
     * @brief Configure heartbeat mechanism
     * @param interval Interval between ping messages
     * @param timeout Maximum time to wait for pong response
     */
    void setHeartbeatConfig(
        std::chrono::seconds interval = std::chrono::seconds(30),
        std::chrono::seconds timeout = std::chrono::seconds(10)
    ) override;

    /**
     * @brief Get number of messages received
     * @return Message count
     */
    uint64_t getMessageCount() const override;

    /**
     * @brief Get last message timestamp
     * @return Timestamp of last received message
     */
    std::chrono::steady_clock::time_point getLastMessageTime() const override;

    // === Additional Binance-specific methods ===

    /**
     * @brief Set enhanced callback for incoming market data (with more details)
     * @param callback Function to call when data arrives with full details
     */
    void setEnhancedDataCallback(EnhancedDataCallback callback);

    // Statistics and monitoring
    struct Statistics {
        std::chrono::steady_clock::time_point connectedAt;
        uint64_t messagesReceived = 0;
        uint64_t reconnectionAttempts = 0;
        std::chrono::steady_clock::time_point lastMessageAt;
        std::chrono::steady_clock::time_point lastPingAt;
        std::chrono::steady_clock::time_point lastPongAt;
    };

    /**
     * @brief Get detailed connection statistics
     * @return Current statistics
     */
    Statistics getStatistics() const;
    //new method for raws data

    void setRawDataCallback(RawDataCallback callback);
    std::string extractStreamType(const std::string& stream_name) const;
    void invokeRawDataCallback(const RawMarketData& rawData);
private:
    // Connection configuration
    std::string host_;
    std::string port_;
    std::string endpoint_;
    // Boost.Asio components
    std::unique_ptr<net::io_context> ioc_;
    std::unique_ptr<ssl::context> ssl_ctx_;
    std::unique_ptr<ws_stream> ws_;
    std::unique_ptr<beast::flat_buffer> buffer_;
    std::unique_ptr<net::steady_timer> heartbeat_timer_;
    std::unique_ptr<net::steady_timer> reconnect_timer_;

    // ===== Fixes: safer io_context lifecycle and shutdown coordination =====
    std::atomic<bool> shutdown_requested_{false};
    std::atomic<bool> io_context_valid_{false};
    mutable std::mutex io_context_mutex_;

    // Helper to safely run async operations that touch io_context or ws_
    void safeAsyncOp(std::function<void()> op);

    // Connection state
    std::atomic<ConnectionStatus> state_{ConnectionStatus::DISCONNECTED};
    std::atomic<bool> should_stop_{false};
    std::atomic<bool> waiting_for_pong_{false};

    // Callbacks (thread-safe access)
    mutable std::mutex callback_mutex_;
    // DataCallback data_callback_;                    // Interface callback
    // EnhancedDataCallback enhanced_data_callback_;   // Enhanced callback
    RawDataCallback raw_data_callback_;
    StatusCallback status_callback_;

    // Subscription management
    mutable std::mutex subscription_mutex_;
    std::unordered_set<std::string> subscribed_streams_;
    std::vector<Symbol> subscribed_symbols_;
    std::atomic<int> subscription_id_counter_{1};

    // Reconnection configuration
    bool reconnect_enabled_ = true;
    int max_reconnect_attempts_ = 0; // 0 = unlimited
    std::chrono::seconds reconnect_base_delay_{2};
    std::chrono::seconds reconnect_max_delay_{60};
    std::atomic<int> reconnect_attempts_{0};

    // Heartbeat configuration
    std::chrono::seconds heartbeat_interval_{30};
    std::chrono::seconds heartbeat_timeout_{10};

    // Per-instance read flag to prevent concurrent async_read calls for the same client
    std::atomic<bool> reading_{false};

    // Statistics
    mutable std::mutex stats_mutex_;
    Statistics stats_;

    // Private methods

    /**
     * @brief Initialize SSL context with proper settings for Binance
     */
    void initializeSslContext();

    /**
     * @brief Perform the actual WebSocket connection
     * @return true if successful
     */
    bool performConnection();

    /**
     * @brief Start reading messages from WebSocket
     */
    void startRead();

    /**
     * @brief Handle incoming WebSocket message
     * @param message Raw message content
     */
    void handleMessage(const std::string& message);

    /**
     * @brief Process subscription response from Binance
     * @param data Parsed JSON response
     */
    void handleSubscriptionResponse(const json& data);

    /**
     * @brief Process market data stream message
     * @param data Parsed JSON message
     */
    void handleStreamData(const json& data);

    /**
     * @brief Send subscription request to Binance
     * @param streams List of stream names to subscribe/unsubscribe
     * @param subscribe true to subscribe, false to unsubscribe
     * @return true if request sent successfully
     */
    bool sendSubscriptionRequest(const std::vector<std::string>& streams, bool subscribe);

    /**
     * @brief Convert Symbol to Binance stream name
     * @param symbol Trading symbol
     * @param stream_type Type of stream (e.g., "depth20@100ms")
     * @return Stream name string
     */
    std::string symbolToStream(const Symbol& symbol, const std::string& stream_type = "depth20@100ms") const;

    /**
     * @brief Extract symbol from stream name
     * @param stream_name Binance stream name
     * @return Extracted symbol
     */
    Symbol extractSymbolFromStream(const std::string& stream_name) const;

    /**
     * @brief Start heartbeat mechanism
     */
    void startHeartbeat();

    /**
     * @brief Handle heartbeat timer expiration
     */
    void handleHeartbeatTimer(const boost::system::error_code& ec);

    /**
     * @brief Send ping message to keep connection alive
     */
    void sendPing();

    /**
     * @brief Handle pong response from server
     */
    void handlePong();

    /**
     * @brief Handle WebSocket close event
     * @param close_code WebSocket close code
     * @param close_reason Close reason string
     */
    void handleClose(websocket::close_code close_code, const std::string& close_reason);

    /**
     * @brief Handle connection error
     * @param error Error that occurred
     */
    void handleError(const std::string& error);

    /**
     * @brief Schedule reconnection attempt
     */
    void scheduleReconnect();

    /**
     * @brief Handle reconnection timer expiration
     */
    void handleReconnectTimer(const boost::system::error_code& ec);

    /**
     * @brief Calculate next reconnection delay using exponential backoff
     * @return Delay duration
     */
    std::chrono::seconds calculateReconnectDelay() const;

    /**
     * @brief Update connection state and notify callback
     * @param new_state New connection state
     * @param message Optional status message
     */
    void updateConnectionState(ConnectionStatus new_state, const std::string& message = "");

    /**
     * @brief Thread-safe callback invocation for interface data callback
     */
    void invokeDataCallback(const ExchangeId& exchange, const Symbol& symbol, const std::vector<uint8_t>& data);

    /**
     * @brief Thread-safe callback invocation for enhanced data callback
     */
    void invokeEnhancedDataCallback(const ExchangeId& exchange, const Symbol& symbol, const std::vector<uint8_t>& data);

    /**
     * @brief Thread-safe callback invocation for status
     */
    void invokeStatusCallback(ConnectionStatus state, const std::string& message);

    /**
     * @brief Update statistics
     */
    void updateStatistics();

    /**
     * @brief Clean up resources and reset state
     */
    void cleanup();
};

} // namespace data_ingestion
} // namespace trading