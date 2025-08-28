/**
 * @file client_interface.hpp
 * @brief Abstract interface for WebSocket client implementations
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */
#pragma once

#include <string>
#include <functional>
#include <vector>
#include "core/market_types.hpp"

namespace trading {
namespace data_ingestion {
// namespace websocket {

// Callback function types
using MessageCallback = std::function<void(const std::string&, const Timestamp&)>;
using ConnectionCallback = std::function<void(ConnectionStatus, const std::string&)>;

// WebSocket client interface
class ClientInterface {
public:
    virtual ~ClientInterface() = default;
    
    // Connection management
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual ConnectionStatus getStatus() const = 0;
    
    // Subscription management
    virtual bool subscribe(const std::vector<Symbol>& symbols) = 0;
    virtual bool unsubscribe(const std::vector<Symbol>& symbols) = 0;
    virtual std::vector<Symbol> getSubscribedSymbols() const = 0;
    
    // Message sending
    virtual bool sendMessage(const std::string& message) = 0;
    
    // Callback registration
    virtual void setMessageCallback(MessageCallback callback) = 0;
    virtual void setConnectionCallback(ConnectionCallback callback) = 0;
    
    // Connection information
    virtual ExchangeId getExchangeId() const = 0;
    virtual std::string getExchangeName() const = 0;
    virtual std::string getEndpoint() const = 0;
    
    // Statistics
    virtual int getMessageCount() const = 0;
    virtual double getAverageLatency() const = 0;
    virtual Timestamp getLastMessageTime() const = 0;
};

// } // namespace websocket
} // namespace data_ingestion
} // namespace trading
// End of file