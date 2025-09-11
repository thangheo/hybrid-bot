// Complete BinanceWebSocketClient implementation with all missing methods
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <random>
#include <unordered_map>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>
#include "common/logger.hpp"
#include <future>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace trading {
namespace data_ingestion {

BinanceWebSocketClient::BinanceWebSocketClient(const std::string& host,
                                             const std::string& port,
                                             std::function<void(const core::ExchangeId&, const std::string&, const std::vector<uint8_t>&)> callback)
    : host_(host)
    , port_(port)
    , endpoint_("wss://" + host + ":" + port + "/stream")
    , dataCallback_(callback)
    , lastMessageTime_(std::chrono::system_clock::now())
    , keep_running_(false)
    , reconnectAttempts_(0)
    , subscriptionIdCounter_(1) {
    
    LOG_INFO("Created BinanceWebSocketClient for " + endpoint_);
}

BinanceWebSocketClient::~BinanceWebSocketClient() {
    try {
        LOG_INFO("Starting BinanceWebSocketClient destruction");
        
        keep_running_ = false;
        connected_.store(false);
        
        if (wsStream_) {
            try {
                std::thread closeThread([this]() {
                    try {
                        if (wsStream_->is_open()) {
                            wsStream_->close(websocket::close_code::normal);
                        }
                    } catch (...) {
                        // Ignore errors during emergency close
                    }
                });
                
                if (closeThread.joinable()) {
                    auto future = std::async(std::launch::async, [&closeThread]() {
                        closeThread.join();
                    });
                    
                    if (future.wait_for(std::chrono::seconds(2)) == std::future_status::timeout) {
                        closeThread.detach();
                    }
                }
            } catch (...) {
                // Ignore close errors during destruction
            }
        }
        
        if (io_thread_.joinable()) {
            LOG_INFO("Waiting for IO thread to finish...");
            
            auto future = std::async(std::launch::async, [this]() {
                io_thread_.join();
            });
            
            if (future.wait_for(std::chrono::seconds(3)) == std::future_status::timeout) {
                LOG_ERROR("IO thread did not finish within timeout - detaching");
                io_thread_.detach();
            } else {
                LOG_INFO("IO thread finished successfully");
            }
        }
        
        if (wsStream_) {
            LOG_INFO("Cleaning up WebSocket stream");
            wsStream_.reset();
        }
        
        LOG_INFO("BinanceWebSocketClient destruction completed successfully");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error in BinanceWebSocketClient destructor: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Unknown error in BinanceWebSocketClient destructor");
    }
}

bool BinanceWebSocketClient::connect() {
    if (connected_.load()) {
        LOG_WARN("Already connected to Binance WebSocket");
        return true;
    }
    
    try {
        status_.store(ConnectionStatus::CONNECTING);
        
        if (io_thread_.joinable()) {
            LOG_WARN("Previous IO thread still running, cleaning up...");
            keep_running_ = false;
            io_thread_.join();
        }
        
        keep_running_ = true;
        connected_.store(false);
        
        io_thread_ = std::thread([this]() {
            try {
                this->runIoLoop();
            } catch (const std::exception& e) {
                LOG_ERROR("IO thread exception: " + std::string(e.what()));
                connected_.store(false);
                status_.store(ConnectionStatus::ERROR);
            } catch (...) {
                LOG_ERROR("Unknown IO thread exception");
                connected_.store(false);
                status_.store(ConnectionStatus::ERROR);
            }
        });
        
        int attempts = 0;
        while (!connected_.load() && keep_running_ && attempts < 50) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ++attempts;
        }
        
        if (connected_.load()) {
            if (connectionCallback_) {
                connectionCallback_(ConnectionStatus::CONNECTED, "Connected to Binance WebSocket");
            }
            LOG_INFO("Connected to Binance WebSocket: " + endpoint_);
            return true;
        } else {
            LOG_ERROR("Connection timeout after 5 seconds");
            status_.store(ConnectionStatus::ERROR);
            return false;
        }
        
    } catch (const std::exception& e) {
        status_.store(ConnectionStatus::ERROR);
        connected_.store(false);
        
        if (connectionCallback_) {
            connectionCallback_(ConnectionStatus::ERROR, e.what());
        }
        
        LOG_ERROR("Failed to connect to Binance WebSocket: " + std::string(e.what()));
        return false;
    }
}

void BinanceWebSocketClient::disconnect() {
    if (!connected_.load()) {
        LOG_INFO("Already disconnected from Binance WebSocket");
        return;
    }
    
    LOG_INFO("Disconnecting from Binance WebSocket...");
    
    keep_running_ = false;
    connected_.store(false);
    status_.store(ConnectionStatus::DISCONNECTING);
    
    if (wsStream_) {
        try {
            if (wsStream_->is_open()) {
                wsStream_->close(websocket::close_code::normal);
            }
        } catch (const std::exception& e) {
            LOG_WARN("Error closing WebSocket during disconnect: " + std::string(e.what()));
        }
    }
    
    if (io_thread_.joinable()) {
        try {
            io_thread_.join();
            LOG_INFO("IO thread joined successfully");
        } catch (const std::exception& e) {
            LOG_ERROR("Error joining IO thread: " + std::string(e.what()));
        }
    }
    
    status_.store(ConnectionStatus::DISCONNECTED);
    
    if (connectionCallback_) {
        connectionCallback_(ConnectionStatus::DISCONNECTED, "Disconnected from Binance WebSocket");
    }
    
    LOG_INFO("Disconnected from Binance WebSocket successfully");
}

bool BinanceWebSocketClient::isConnected() const {
    return connected_.load();
}

ConnectionStatus BinanceWebSocketClient::getStatus() const {
    return status_.load();
}

bool BinanceWebSocketClient::subscribe(const std::vector<core::Symbol>& symbols) {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);

    if (!connected_.load()) {
        LOG_ERROR("Cannot subscribe - not connected to Binance WebSocket");
        return false;
    }

    LOG_INFO("Subscribing to " + std::to_string(symbols.size()) + " symbols");

    std::vector<std::string> streams;
    for (const auto& symbol : symbols) {
        std::string depthStream = symbolToStream(symbol, "depth");
        streams.push_back(depthStream);

        if (std::find(subscribedStreams_.begin(), subscribedStreams_.end(), depthStream) == subscribedStreams_.end()) {
            subscribedStreams_.push_back(depthStream);
        }

        if (std::find(subscribedSymbols_.begin(), subscribedSymbols_.end(), symbol) == subscribedSymbols_.end()) {
            subscribedSymbols_.push_back(symbol);
            LOG_INFO("Added symbol to subscription: " + symbol.base + symbol.quote);
        }
    }

    bool success = subscribeToStreams(streams);

    if (success) {
        LOG_INFO("Successfully subscribed to " + std::to_string(symbols.size()) + " symbols on Binance");
    } else {
        LOG_ERROR("Failed to subscribe to symbols on Binance");
    }

    return success;
}

bool BinanceWebSocketClient::unsubscribe(const std::vector<core::Symbol>& symbols) {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    
    if (!connected_.load()) {
        LOG_ERROR("Cannot unsubscribe - not connected to Binance WebSocket");
        return false;
    }
    
    LOG_INFO("Unsubscribing from " + std::to_string(symbols.size()) + " symbols");
    
    std::vector<std::string> streams;
    for (const auto& symbol : symbols) {
        std::string depthStream = symbolToStream(symbol, "depth");
        streams.push_back(depthStream);
        
        // Remove from subscribed streams
        auto streamIt = std::find(subscribedStreams_.begin(), subscribedStreams_.end(), depthStream);
        if (streamIt != subscribedStreams_.end()) {
            subscribedStreams_.erase(streamIt);
        }
        
        // Remove from subscribed symbols
        auto symbolIt = std::find(subscribedSymbols_.begin(), subscribedSymbols_.end(), symbol);
        if (symbolIt != subscribedSymbols_.end()) {
            subscribedSymbols_.erase(symbolIt);
            LOG_INFO("Removed symbol from subscription: " + symbol.base + symbol.quote);
        }
    }
    
    bool success = unsubscribeFromStreams(streams);
    
    if (success) {
        LOG_INFO("Successfully unsubscribed from " + std::to_string(symbols.size()) + " symbols on Binance");
    } else {
        LOG_ERROR("Failed to unsubscribe from symbols on Binance");
    }
    
    return success;
}

std::vector<core::Symbol> BinanceWebSocketClient::getSubscribedSymbols() const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    return subscribedSymbols_;
}

bool BinanceWebSocketClient::sendMessage(const std::string& message) {
    if (!connected_.load()) {
        LOG_ERROR("Cannot send message - not connected");
        return false;
    }
    
    return sendWebSocketMessage(message);
}

void BinanceWebSocketClient::setMessageCallback(MessageCallback callback) {
    messageCallback_ = callback;
}

void BinanceWebSocketClient::setConnectionCallback(ConnectionCallback callback) {
    connectionCallback_ = callback;
}

core::ExchangeId BinanceWebSocketClient::getExchangeId() const {
    return core::ExchangeId::binance();
}

std::string BinanceWebSocketClient::getExchangeName() const {
    return "Binance";
}

std::string BinanceWebSocketClient::getEndpoint() const {
    return endpoint_;
}

int BinanceWebSocketClient::getMessageCount() const {
    return messageCount_.load();
}

double BinanceWebSocketClient::getAverageLatency() const {
    int count = messageCount_.load();
    return count > 0 ? totalLatency_.load() / count : 0.0;
}

core::Timestamp BinanceWebSocketClient::getLastMessageTime() const {
    return lastMessageTime_;
}

bool BinanceWebSocketClient::subscribeToStreams(const std::vector<std::string>& streams) {
    return sendSubscriptionRequest(streams, true);
}

bool BinanceWebSocketClient::unsubscribeFromStreams(const std::vector<std::string>& streams) {
    return sendSubscriptionRequest(streams, false);
}

std::vector<std::string> BinanceWebSocketClient::getSubscribedStreams() const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    return subscribedStreams_;
}

void BinanceWebSocketClient::runIoLoop() {
    LOG_INFO("Starting WebSocket IO loop for Binance");
    
    try {
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);
        ctx.set_verify_callback([](bool preverified, ssl::verify_context& ctx) {
            return true; // Accept all certificates for now
        });
        
        tcp::resolver resolver{ioc};
        
        wsStream_ = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(ioc, ctx);
        
        if (!SSL_set_tlsext_host_name(wsStream_->next_layer().native_handle(), host_.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }
        
        auto const results = resolver.resolve(host_, port_);
        beast::get_lowest_layer(*wsStream_).connect(results);
        
        wsStream_->next_layer().handshake(ssl::stream_base::client);
        
        wsStream_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent, 
                    std::string(BOOST_BEAST_VERSION_STRING) + " trading-system-websocket");
            }));
        
        wsStream_->handshake(host_, "/stream");
        
        LOG_INFO("WebSocket handshake completed successfully");
        
        connected_.store(true);
        status_.store(ConnectionStatus::CONNECTED);
        
        if (connectionCallback_) {
            connectionCallback_(ConnectionStatus::CONNECTED, "WebSocket connected successfully");
        }
        
        onWebSocketOpen();
        
        beast::flat_buffer buffer;
        
        while (keep_running_ && connected_.load()) {
            try {
                if (!keep_running_) {
                    LOG_INFO("Received shutdown signal, exiting message loop");
                    break;
                }
                
                beast::get_lowest_layer(*wsStream_).expires_after(std::chrono::seconds(30));
                
                wsStream_->read(buffer);
                
                std::string message = beast::buffers_to_string(buffer.data());
                buffer.consume(buffer.size());
                
                onWebSocketMessage(message);
                
                beast::get_lowest_layer(*wsStream_).expires_never();
                
            } catch (const beast::system_error& se) {
                if (se.code() == beast::error::timeout) {
                    try {
                        wsStream_->ping({});
                        continue;
                    } catch (...) {
                        LOG_ERROR("Failed to send ping, disconnecting");
                        break;
                    }
                } else if (se.code() == websocket::error::closed) {
                    LOG_INFO("WebSocket closed by peer");
                    break;
                } else {
                    LOG_ERROR("WebSocket error: " + std::string(se.what()));
                    break;
                }
            } catch (const std::exception& e) {
                LOG_ERROR("Message loop error: " + std::string(e.what()));
                break;
            }
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("WebSocket connection failed: " + std::string(e.what()));
        status_.store(ConnectionStatus::ERROR);
        
        if (connectionCallback_) {
            connectionCallback_(ConnectionStatus::ERROR, "Connection error: " + std::string(e.what()));
        }
    }
    
    connected_.store(false);
    if (status_.load() != ConnectionStatus::ERROR) {
        status_.store(ConnectionStatus::DISCONNECTED);
    }
    
    LOG_INFO("WebSocket IO loop finished");
}

bool BinanceWebSocketClient::sendWebSocketMessage(const std::string& message) {
    if (!connected_.load()) {
        LOG_ERROR("Cannot send message - not connected");
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(sendMutex_);
        
        if (!wsStream_ || !wsStream_->is_open()) {
            LOG_ERROR("Cannot send message - WebSocket not available");
            return false;
        }
        
        LOG_INFO("Sending WebSocket message: " + message);
        
        wsStream_->write(net::buffer(message));
        
        LOG_INFO("Message sent successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error sending WebSocket message: " + std::string(e.what()));
        return false;
    }
}

void BinanceWebSocketClient::onWebSocketOpen() {
    LOG_INFO("WebSocket connection opened - ready for subscriptions");
}
 
void BinanceWebSocketClient::onWebSocketMessage(const std::string& message) {
    try {
        messageCount_.fetch_add(1);
        lastMessageTime_ = std::chrono::system_clock::now();
        
        json data = json::parse(message);
        
        // Handle subscription responses
        if (data.contains("id") && (data.contains("result") || data.contains("error"))) {
            handleResponse(data);
            return;
        }
        
        // Handle combined stream format: {"stream": "...", "data": {...}}
        if (data.contains("stream") && data.contains("data")) {
            handleStreamMessage(data);
            return;
        }
        
        // Handle DIRECT order book data: {"lastUpdateId": ..., "bids": [...], "asks": [...]}
        if (data.contains("lastUpdateId") && data.contains("bids") && data.contains("asks")) {
            // Extract symbol from subscribed symbols (since it's not in the message)
            std::string symbol = "UNKNOWN";
            if (!subscribedSymbols_.empty()) {
                symbol = subscribedSymbols_[0].base + subscribedSymbols_[0].quote; // Use first subscribed symbol
            }
            processOrderBookData(symbol, "depth", data);
            return;
        }
        
        // Fallback for unknown format
        if (dataCallback_) {
            std::vector<uint8_t> dataBytes(message.begin(), message.end());
            dataCallback_(core::ExchangeId::binance(), "", dataBytes);
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing WebSocket message: " + std::string(e.what()));
    }
}

void BinanceWebSocketClient::onWebSocketClose(int code, const std::string& reason) {
    LOG_INFO("WebSocket connection closed: [" + std::to_string(code) + "] " + reason);
}

void BinanceWebSocketClient::onWebSocketError(const std::string& error) {
    LOG_ERROR("WebSocket error: " + error);
}

void BinanceWebSocketClient::handleResponse(const json& data) {
    try {
        int id = data.value("id", -1);
        
        if (data.contains("result")) {
            LOG_INFO("Subscription response [" + std::to_string(id) + "]: Success");
        } else if (data.contains("error")) {
            json error = data.value("error", json{});
            int code = error.value("code", -1);
            std::string msg = error.value("msg", "Unknown error");
            LOG_ERROR("Subscription error [" + std::to_string(id) + "]: " + std::to_string(code) + " - " + msg);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing response: " + std::string(e.what()));
    }
}

void BinanceWebSocketClient::handleStreamMessage(const json& data) {
    try {
        if (data.contains("stream") && data.contains("data")) {
            std::string stream = data.value("stream", "");
            json stream_data = data.value("data", json{});
            
            std::string symbol = extractSymbolFromStream(stream);
            std::string event_type = extractEventTypeFromStream(stream);
            if (event_type.find("depth") != std::string::npos) {
                processOrderBookData(symbol, event_type, stream_data);
            } else if (event_type == "trade") {
                processTrade(symbol, stream_data);
            } else if (event_type == "ticker") {
                processTicker(symbol, stream_data);
            } else if (event_type == "bookTicker") {
                processBookTicker(symbol, stream_data);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error handling stream message: " + std::string(e.what()));
    }
}

void BinanceWebSocketClient::processOrderBookData(const core::Symbol& symbol, 
                                                const std::string& event_type, 
                                                const json& data) {
    if (!dataCallback_) {
        return;
    }
    
    try {
        std::string json_str = data.dump();
        std::vector<uint8_t> dataBytes(json_str.begin(), json_str.end());
        dataCallback_(core::ExchangeId::binance(), symbol.toString(), dataBytes);
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing order book data: " + std::string(e.what()));
    }
}

void BinanceWebSocketClient::processTrade(const core::Symbol& symbol, const json& data) {
    if (!dataCallback_) {
        return;
    }
    
    try {
        std::string json_str = data.dump();
        std::vector<uint8_t> dataBytes(json_str.begin(), json_str.end());
        dataCallback_(core::ExchangeId::binance(), symbol.toString(), dataBytes);
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing trade data: " + std::string(e.what()));
    }
}

void BinanceWebSocketClient::processTicker(const core::Symbol& symbol, const json& data) {
    if (!dataCallback_) {
        return;
    }
    
    try {
        std::string json_str = data.dump();
        std::vector<uint8_t> dataBytes(json_str.begin(), json_str.end());
        dataCallback_(core::ExchangeId::binance(), symbol.toString(), dataBytes);
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing ticker data: " + std::string(e.what()));
    }
}

void BinanceWebSocketClient::processBookTicker(const core::Symbol& symbol, const json& data) {
    if (!dataCallback_) {
        return;
    }
    
    try {
        std::string json_str = data.dump();
        std::vector<uint8_t> dataBytes(json_str.begin(), json_str.end());
        dataCallback_(core::ExchangeId::binance(), symbol.toString(), dataBytes);
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing book ticker data: " + std::string(e.what()));
    }
}

bool BinanceWebSocketClient::sendSubscriptionRequest(const std::vector<std::string>& streams, bool subscribe) {
    if (!connected_.load()) {
        return false;
    }
    
    try {
        json request;
        request["id"] = subscriptionIdCounter_++;
        request["method"] = subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE";
        request["params"] = streams;
        
        std::string message = request.dump();
        return sendWebSocketMessage(message);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error sending subscription request: " + std::string(e.what()));
        return false;
    }
}

std::string BinanceWebSocketClient::symbolToStream(const core::Symbol& symbol, const std::string& streamType) const {
    std::string symbolStr = symbol.base + symbol.quote;
    std::transform(symbolStr.begin(), symbolStr.end(), symbolStr.begin(), ::tolower);
    
    if (streamType == "depth") {
        return symbolStr + "@depth20@100ms";
    }
    
    return symbolStr + "@" + streamType;
}

core::Symbol BinanceWebSocketClient::streamToSymbol(const std::string& stream) const {
    size_t atPos = stream.find('@');
    if (atPos != std::string::npos) {
        std::string symbolStr = stream.substr(0, atPos);
        return core::Symbol::fromString(symbolStr);
    }
    return core::Symbol();
}

std::string BinanceWebSocketClient::extractSymbolFromStream(const std::string& stream) const {
    size_t at_pos = stream.find('@');
    if (at_pos != std::string::npos) {
        std::string symbol = stream.substr(0, at_pos);
        std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
        return symbol;
    }
    return "UNKNOWN";
}

std::string BinanceWebSocketClient::extractEventTypeFromStream(const std::string& stream) const {
    size_t at_pos = stream.find('@');
    if (at_pos != std::string::npos && at_pos + 1 < stream.length()) {
        return stream.substr(at_pos + 1);
    }
    return "";
}

void BinanceWebSocketClient::updateStatistics() {
    messageCount_.fetch_add(1);
    lastMessageTime_ = std::chrono::system_clock::now();
}

void BinanceWebSocketClient::workerFunction() {
    // Implementation for background processing if needed
}

void BinanceWebSocketClient::scheduleReconnection() {
    if (!autoReconnect_) {
        return;
    }
    
    reconnectAttempts_++;
    
    if (reconnectAttempts_ > maxReconnectAttempts_) {
        LOG_ERROR("Max reconnection attempts reached (" + std::to_string(maxReconnectAttempts_) + 
                 "), giving up");
        return;
    }
    
    auto delay = std::chrono::seconds(
        static_cast<long>(reconnectBaseDelay_.count() * std::pow(2, reconnectAttempts_ - 1))
    );
    
    delay = std::min(delay, maxReconnectDelay_);
    
    LOG_INFO("Scheduling reconnection attempt " + std::to_string(reconnectAttempts_) + 
            " in " + std::to_string(delay.count()) + " seconds");
    
    std::thread([this, delay]() {
        std::this_thread::sleep_for(delay);
        
        if (keep_running_) {
            LOG_INFO("Attempting reconnection...");
            connect();
        }
    }).detach();
}

void BinanceWebSocketClient::initializeConnection() {
    try {
        LOG_INFO("Initializing WebSocket connection to Binance");
        
        keep_running_ = true;
        reconnectAttempts_ = 0;
        
        io_thread_ = std::thread(&BinanceWebSocketClient::runIoLoop, this);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (connected_.load()) {
            LOG_INFO("WebSocket connection thread started successfully");
        } else {
            LOG_WARN("WebSocket connection thread started but connection not yet established");
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to initialize WebSocket connection: " + std::string(e.what()));
        throw;
    }
}

void BinanceWebSocketClient::closeConnection() {
    try {
        LOG_INFO("Closing WebSocket connection");
        
        keep_running_ = false;
        
        if (wsStream_) {
            try {
                if (wsStream_->is_open()) {
                    wsStream_->close(websocket::close_code::normal);
                }
            } catch (const std::exception& e) {
                LOG_WARN("Error closing WebSocket stream: " + std::string(e.what()));
            } catch (...) {
                LOG_WARN("Unknown error closing WebSocket stream");
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        if (io_thread_.joinable()) {
            LOG_INFO("Waiting for IO thread to terminate...");
            
            auto future = std::async(std::launch::async, [this]() {
                io_thread_.join();
            });
            
            if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                LOG_ERROR("IO thread did not terminate within timeout");
            } else {
                LOG_INFO("IO thread terminated successfully");
            }
        }
        
        wsStream_.reset();
        
        LOG_INFO("WebSocket connection closed successfully");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error closing WebSocket connection: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Unknown error closing WebSocket connection");
    }
}

} // namespace data_ingestion
} // namespace trading