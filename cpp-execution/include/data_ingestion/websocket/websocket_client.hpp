#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>

namespace trading {
namespace data_ingestion {

using MessageCallback = std::function<void(const std::string&)>;
using ErrorCallback = std::function<void(const std::string&)>;

class WebSocketClient {
public:
    WebSocketClient(const std::string& host, const std::string& port, 
                    const std::string& target, MessageCallback msgCallback,
                    ErrorCallback errCallback);
    ~WebSocketClient();

    void connect();
    void disconnect();
    void send(const std::string& message);
    bool isConnected() const;

private:
    void doRead();
    void onRead(boost::system::error_code ec, std::size_t bytes_transferred);
    void handleError(const std::string& errorMsg);

    std::string host_;
    std::string port_;
    std::string target_;
    MessageCallback messageCallback_;
    ErrorCallback errorCallback_;
    
    std::atomic<bool> isConnected_;
    
    boost::asio::io_context ioc_;
    boost::beast::flat_buffer buffer_;
    std::unique_ptr<boost::beast::websocket::stream<boost::beast::tcp_stream>> ws_;
    std::unique_ptr<boost::asio::io_context::work> work_;
    std::unique_ptr<std::thread> ioThread_;
};

} // namespace data_ingestion
} // namespace trading
