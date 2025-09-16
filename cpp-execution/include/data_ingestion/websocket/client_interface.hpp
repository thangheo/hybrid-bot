/**
 * @file client_interface.hpp
 * @brief Abstract interface for WebSocket client implementations
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * Updated to match the BinanceWebSocketClient implementation.
 * 
 * Created on: April 30, 2025
 * Updated on: September 16, 2025
 */
#pragma once

#include <string>
#include <functional>
#include <vector>
#include <chrono>
#include "core/market_types.hpp"

namespace trading {
namespace data_ingestion {

// Forward declarations from core types
using Symbol = core::Symbol;
using ExchangeId = core::ExchangeId;
using ConnectionStatus = core::ConnectionStatus;
using Timestamp = core::Timestamp;

// Callback function types - updated to match BinanceWebSocketClient
using DataCallback = std::function<void(const ExchangeId&, const Symbol&, const std::vector<uint8_t>&)>;
using StatusCallback = std::function<void(ConnectionStatus, const std::string&)>;
using RawDataCallback = std::function<void(const RawMarketData&)>;
/**
 * @class ClientInterface
 * @brief Abstract interface for WebSocket client implementations
 * 
 * This interface defines the contract that all WebSocket clients must implement.
 * Updated to match the BinanceWebSocketClient implementation patterns.
 */
class ClientInterface {
public:
    virtual ~ClientInterface() = default;
    
    // Connection management
    /**
     * @brief Connect to the WebSocket endpoint
     * @return true if connection successful, false otherwise
     */
    virtual bool connect() = 0;
    
    /**
     * @brief Gracefully disconnect from WebSocket
     */
    virtual void disconnect() = 0;
    
    /**
     * @brief Check if currently connected
     * @return true if connected and healthy
     */
    virtual bool isConnected() const = 0;
    
    /**
     * @brief Get current connection state
     * @return Current connection state
     */
    virtual ConnectionStatus getStatus() const = 0;
    
    // Subscription management
    /**
     * @brief Subscribe to market data streams for given symbols
     * @param symbols List of trading symbols to subscribe to
     * @return true if subscription successful
     */
    virtual bool subscribe(const std::vector<Symbol>& symbols) = 0;
    
    /**
     * @brief Unsubscribe from symbols
     * @param symbols List of symbols to unsubscribe from
     * @return true if unsubscription successful
     */
    virtual bool unsubscribe(const std::vector<Symbol>& symbols) = 0;
    
    /**
     * @brief Get list of currently subscribed symbols
     * @return Vector of subscribed symbols
     */
    virtual std::vector<Symbol> getSubscribedSymbols() const = 0;
         
    // Callback registration - keeping the original signature for compatibility
    /**
     * @brief Set callback for incoming market data
     * @param callback Function to call when data arrives
     */
    // virtual void setDataCallback(DataCallback callback) = 0;
    
    /**
     * @brief Set callback for connection status changes
     * @param callback Function to call when connection state changes
     */
    virtual void setStatusCallback(StatusCallback callback) = 0;
    
    // Connection information
    /**
     * @brief Get the exchange identifier
     * @return Exchange ID
     */
    virtual ExchangeId getExchangeId() const = 0;
    
    /**
     * @brief Get human-readable exchange name
     * @return Exchange name string
     */
    virtual std::string getExchangeName() const = 0;
    
    /**
     * @brief Get the WebSocket endpoint URL
     * @return Endpoint URL string
     */
    virtual std::string getEndpoint() const = 0;

    // Event loop management - adding these methods to match BinanceWebSocketClient
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
    virtual void run() = 0;

    /**
     * @brief Stop the client event loop
     * 
     * Thread-safe method to signal the event loop to stop.
     * Call from another thread to gracefully shutdown the client.
     */
    virtual void stop() = 0;

    // Configuration methods - optional for implementations that support them
    /**
     * @brief Configure reconnection behavior
     * @param enabled Whether to enable automatic reconnection
     * @param maxAttempts Maximum number of reconnection attempts (0 = unlimited)
     * @param baseDelay Initial delay between reconnection attempts
     * @param maxDelay Maximum delay between reconnection attempts
     */
    virtual void setReconnectionConfig(
        bool enabled = true,
        int maxAttempts = 0,
        std::chrono::seconds baseDelay = std::chrono::seconds(2),
        std::chrono::seconds maxDelay = std::chrono::seconds(60)
    ) {
        // Default empty implementation - derived classes can override
        (void)enabled; (void)maxAttempts; (void)baseDelay; (void)maxDelay;
    }

    /**
     * @brief Configure heartbeat mechanism
     * @param interval Interval between ping messages
     * @param timeout Maximum time to wait for pong response
     */
    virtual void setHeartbeatConfig(
        std::chrono::seconds interval = std::chrono::seconds(30),
        std::chrono::seconds timeout = std::chrono::seconds(10)
    ) {
        // Default empty implementation - derived classes can override
        (void)interval; (void)timeout;
    }

    // Statistics - basic interface that implementations can extend
    /**
     * @brief Get basic connection statistics
     * @return Number of messages received (basic metric all implementations should track)
     */
    virtual uint64_t getMessageCount() const {
        return 0; // Default implementation
    }

    /**
     * @brief Get last message timestamp
     * @return Timestamp of last received message
     */
    virtual std::chrono::steady_clock::time_point getLastMessageTime() const {
        return std::chrono::steady_clock::time_point{}; // Default implementation
    }
};

} // namespace data_ingestion
} // namespace trading
// End of file