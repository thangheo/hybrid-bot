// include/data_ingestion/websocket/client_base.hpp
#pragma once

#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include "data_ingestion/websocket/client_interface.hpp"

namespace trading {
namespace data_ingestion {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

class WebSocketClientBase : public ClientInterface {
public:
    WebSocketClientBase(const std::string& host, 
                       const std::string& port,
                       bool use_ssl = true);
    virtual ~WebSocketClientBase();
    
    // Connection management
    virtual bool connect() override;
    virtual void disconnect() override;
    virtual bool isConnected() const override;
    
    // Message sending
    virtual void send(const std::string& message) ;
    
    // Configuration
    void setPingInterval(int interval_ms);
    void setReconnectionParameters(int attempt_limit, int delay_ms, float backoff_factor = 1.5f);
    
protected:
    // Event handlers to be implemented by derived classes
    virtual void onOpen() = 0;
    virtual void onMessage(const std::string& message) = 0;
    virtual void onClose(int code, const std::string& reason) = 0;
    virtual void onError(const std::string& error) = 0;
    virtual void onReconnect() = 0;
    
    // Connection properties
    std::string host_;
    std::string port_;
    bool use_ssl_;
    std::atomic<bool> connected_;
    
private:
    // Internal methods
    void runIoService();
    void pingTask();
    void handlePong();
    void handleError(const std::string& error);
    void handleReconnection();
    
    // Boost.Asio components - Fixed types
    net::io_context io_context_;
    std::unique_ptr<ssl::context> ssl_context_;
    
    // Fixed: Use proper stream types without reference
    std::unique_ptr<beast::ssl_stream<tcp::socket>> ssl_stream_;
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws_ssl_stream_;
    std::unique_ptr<websocket::stream<tcp::socket>> ws_stream_;
    
    // Threading and synchronization
    std::thread io_thread_;
    std::thread ping_timer_;
    std::atomic<bool> keep_running_;
    mutable std::mutex mutex_;
    std::mutex write_mutex_;
    
    // Reconnection parameters
    int reconnect_attempt_limit_;
    int current_reconnect_attempts_;
    int reconnect_delay_ms_;
    float reconnect_backoff_factor_;
    
    // Ping/pong handling
    std::chrono::steady_clock::time_point last_pong_time_;
    int ping_interval_ms_;
};

} // namespace data_ingestion
} // namespace trading