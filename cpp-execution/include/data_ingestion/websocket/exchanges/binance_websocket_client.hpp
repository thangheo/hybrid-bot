/**
 * @file binance_websocket_client.hpp
 * @brief Binance WebSocket client implementation
 */
#pragma once

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

namespace trading {
namespace data_ingestion {

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

private:
    // Connection details
    std::string host_;
    std::string port_;
    std::string endpoint_;
    
    // Connection state
    std::atomic<ConnectionStatus> status_{ConnectionStatus::DISCONNECTED};
    std::atomic<bool> connected_{false};
    
    // Callbacks
    MessageCallback messageCallback_;
    ConnectionCallback connectionCallback_;
    std::function<void(const core::ExchangeId&, const std::string&, const std::vector<uint8_t>&)> dataCallback_;
    
    // Statistics
    std::atomic<int> messageCount_{0};
    std::atomic<double> totalLatency_{0.0};
    core::Timestamp lastMessageTime_;
    
    // Subscription tracking
    mutable std::mutex subscriptionMutex_;
    std::vector<std::string> subscribedStreams_;
    std::vector<core::Symbol> subscribedSymbols_;
    int subscriptionIdCounter_ = 1;
    
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
};

} // namespace data_ingestion
} // namespace trading