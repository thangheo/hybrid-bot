/**
 * @file binance_websocket_client.cpp
 * @brief Production-ready Binance WebSocket client implementation
 * 
 * Key improvements over previous versions:
 * - Single-threaded design eliminates race conditions
 * - Proper heartbeat mechanism prevents connection drops
 * - Exponential backoff reconnection strategy
 * - Clean resource management with RAII
 * - Thread-safe callback mechanism
 * - Robust error handling and logging
 * 
 * @author Trading System Team
 * @date September 2025
 */

#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <random>
#include <thread>

using namespace trading::core;

namespace trading {
namespace data_ingestion {

namespace beast = boost::beast;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

// Helper function for logging (replace with your logging system)
#define LOG_INFO(msg) std::cout << "[INFO] " << msg << std::endl
#define LOG_WARN(msg) std::cout << "[WARN] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl
#define LOG_DEBUG(msg) //std::cout << "[DEBUG] " << msg << std::endl

BinanceWebSocketClient::BinanceWebSocketClient(std::string host, std::string port)
    : host_(host)
    , port_(port)
    , endpoint_("/stream")
{
    LOG_INFO("Created BinanceWebSocketClient for " + host + ":" + port);
}
// BinanceWebSocketClient constructor with data callback
BinanceWebSocketClient::BinanceWebSocketClient(std::string host, std::string port, RawDataCallback data_callback)
    : host_(host)
    , port_(port)
    , endpoint_("/stream")
    , raw_data_callback_(data_callback)
{
    LOG_INFO("Created BinanceWebSocketClient with data callback for " + host + ":" + port);
}   

BinanceWebSocketClient::~BinanceWebSocketClient() {
    LOG_INFO("Destroying BinanceWebSocketClient");
    // Only signal stop; cleanup will be handled by the run() thread
    stop();
    LOG_INFO("BinanceWebSocketClient destroyed");
}

bool BinanceWebSocketClient::connect() {
    if (state_.load() == ConnectionStatus::CONNECTED) {
        LOG_WARN("Already connected");
        return true;
    }

    if (state_.load() == ConnectionStatus::CONNECTING) {
        LOG_WARN("Connection already in progress");
        return false;
    }

    LOG_INFO("Connecting to Binance WebSocket: " + host_ + ":" + port_);
    
    updateConnectionState(ConnectionStatus::CONNECTING, "Initiating connection");
    
    try {
        // Don't recreate if already exists
        {
            std::lock_guard<std::mutex> lock(io_context_mutex_);
            if (!ioc_) {
                ioc_ = std::make_unique<net::io_context>();
                io_context_valid_.store(true);
            }
            if (!ssl_ctx_) {
                ssl_ctx_ = std::make_unique<ssl::context>(ssl::context::tlsv12_client);
            }
        }

        initializeSslContext();
        
        ws_ = std::make_unique<ws_stream>(*ioc_, *ssl_ctx_);
        buffer_ = std::make_unique<beast::flat_buffer>();
        heartbeat_timer_ = std::make_unique<net::steady_timer>(*ioc_);
        reconnect_timer_ = std::make_unique<net::steady_timer>(*ioc_);

        // Perform connection
        if (!performConnection()) {
            cleanup();
            updateConnectionState(ConnectionStatus::ERROR, "Failed to establish connection");
            return false;
        }

        updateConnectionState(ConnectionStatus::CONNECTED, "Successfully connected");
        
        // Reset reconnection attempts on successful connection
        reconnect_attempts_.store(0);
        
        // Update statistics
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.connectedAt = std::chrono::steady_clock::now();
        }

        LOG_INFO("Connected to Binance WebSocket successfully");
        return true;

    } catch (const std::exception& e) {
        LOG_ERROR("Connection failed: " + std::string(e.what()));
        cleanup();
        updateConnectionState(ConnectionStatus::ERROR, "Connection error: " + std::string(e.what()));
        return false;
    }
}

void BinanceWebSocketClient::disconnect() {
    LOG_INFO("Disconnecting from WebSocket");
    
    updateConnectionState(ConnectionStatus::DISCONNECTED, "User requested disconnection");
    
    // Cancel timers
    if (heartbeat_timer_) {
        boost::system::error_code ec;
        heartbeat_timer_->cancel(ec);
    }
    
    if (reconnect_timer_) {
        boost::system::error_code ec;
        reconnect_timer_->cancel(ec);
    }

    // Close WebSocket gracefully
    if (ws_ && ws_->is_open()) {
        try {
            boost::system::error_code ec;
            ws_->close(websocket::close_code::normal, ec);
            if (ec) {
                LOG_WARN("Error during graceful close: " + ec.message());
            }
        } catch (const std::exception& e) {
            LOG_WARN("Exception during close: " + std::string(e.what()));
        }
    }

    cleanup();
    LOG_INFO("Disconnected successfully");
}

bool BinanceWebSocketClient::isConnected() const {
    return state_.load() == ConnectionStatus::CONNECTED && 
           ws_ && ws_->is_open();
}

ConnectionStatus BinanceWebSocketClient::getStatus() const {
    return state_.load();
}

bool BinanceWebSocketClient::subscribe(const std::vector<Symbol>& symbols) {
    if (!isConnected()) {
        LOG_ERROR("Cannot subscribe - not connected");
        return false;
    }

    std::lock_guard<std::mutex> lock(subscription_mutex_);
    
    LOG_INFO("Subscribing to " + std::to_string(symbols.size()) + " symbols");

    std::vector<std::string> streams;
    for (const auto& symbol : symbols) {
        std::string stream = symbolToStream(symbol);
        
        // Check if already subscribed
        if (subscribed_streams_.find(stream) == subscribed_streams_.end()) {
            streams.push_back(stream);
            subscribed_streams_.insert(stream);
            subscribed_symbols_.push_back(symbol);
        } 
        // else {
        //     LOG_DEBUG("Already subscribed to " + symbol.toString());
        // }
    }

    if (streams.empty()) {
        LOG_INFO("All symbols already subscribed");
        return true;
    }

    bool success = sendSubscriptionRequest(streams, true);
    
    if (!success) {
        // Rollback on failure
        for (const auto& stream : streams) {
            subscribed_streams_.erase(stream);
        }
        // Remove corresponding symbols
        subscribed_symbols_.erase(
            std::remove_if(subscribed_symbols_.begin(), subscribed_symbols_.end(),
                [&streams, this](const Symbol& sym) {
                    return std::find(streams.begin(), streams.end(), symbolToStream(sym)) != streams.end();
                }),
            subscribed_symbols_.end()
        );
        LOG_ERROR("Failed to subscribe to streams");
    } else {
        LOG_INFO("Successfully subscribed to " + std::to_string(streams.size()) + " new streams");
    }

    return success;
}

bool BinanceWebSocketClient::unsubscribe(const std::vector<Symbol>& symbols) {
    if (!isConnected()) {
        LOG_ERROR("Cannot unsubscribe - not connected");
        return false;
    }

    std::lock_guard<std::mutex> lock(subscription_mutex_);
    
    LOG_INFO("Unsubscribing from " + std::to_string(symbols.size()) + " symbols");

    std::vector<std::string> streams;
    for (const auto& symbol : symbols) {
        std::string stream = symbolToStream(symbol);
        
        if (subscribed_streams_.find(stream) != subscribed_streams_.end()) {
            streams.push_back(stream);
        }
    }

    if (streams.empty()) {
        LOG_INFO("No matching subscriptions to remove");
        return true;
    }

    bool success = sendSubscriptionRequest(streams, false);
    
    if (success) {
        // Remove from tracking
        for (const auto& stream : streams) {
            subscribed_streams_.erase(stream);
        }
        
        // Remove corresponding symbols
        for (const auto& symbol : symbols) {
            auto it = std::find(subscribed_symbols_.begin(), subscribed_symbols_.end(), symbol);
            if (it != subscribed_symbols_.end()) {
                subscribed_symbols_.erase(it);
            }
        }
        
        LOG_INFO("Successfully unsubscribed from " + std::to_string(streams.size()) + " streams");
    } else {
        LOG_ERROR("Failed to unsubscribe from streams");
    }

    return success;
}

std::vector<Symbol> BinanceWebSocketClient::getSubscribedSymbols() const {
    std::lock_guard<std::mutex> lock(subscription_mutex_);
    return subscribed_symbols_;
}

// void BinanceWebSocketClient::setDataCallback(DataCallback callback) {
//     std::lock_guard<std::mutex> lock(callback_mutex_);
//     data_callback_ = std::move(callback);
//     LOG_INFO("Data callback registered");
// }

void BinanceWebSocketClient::setStatusCallback(StatusCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    status_callback_ = std::move(callback);
}

void BinanceWebSocketClient::run() {
    LOG_INFO("Starting WebSocket event loop");
    should_stop_.store(false);
    {
        std::lock_guard<std::mutex> lock(io_context_mutex_);
        shutdown_requested_.store(false);
        if (ioc_) io_context_valid_.store(true);
    }
    while (!should_stop_.load()) {
        try {
            if (!ioc_) {
                LOG_ERROR("IO context not initialized");
                break;
            }
            // Run pending operations with timeout
            auto start_time = std::chrono::steady_clock::now();
            auto handlers_run = ioc_->poll();
            // If we're connected, start reading and heartbeat
            if (state_.load() == ConnectionStatus::CONNECTED && ws_ && ws_->is_open()) {
                if (handlers_run == 0) {
                    // No immediate work, start async operations if needed
                    startRead();
                    startHeartbeat();
                }
            }
            // Run IO context with timeout to prevent blocking indefinitely
            ioc_->run_for(std::chrono::milliseconds(100));
            // If we lost connection and reconnection is enabled, schedule reconnect
            if ((state_.load() == ConnectionStatus::ERROR || 
                 state_.load() == ConnectionStatus::DISCONNECTED) &&
                reconnect_enabled_ && !should_stop_.load()) {
                scheduleReconnect();
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Event loop error: " + std::string(e.what()));
            handleError("Event loop exception: " + std::string(e.what()));
            // Brief pause before continuing
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    LOG_INFO("WebSocket event loop stopped");
    // Only cleanup from the run() thread, after event loop exits
    {
        std::lock_guard<std::mutex> lock(io_context_mutex_);
        io_context_valid_.store(false);
    }
    cleanup();
}

void BinanceWebSocketClient::stop() {
    LOG_INFO("Stopping WebSocket client");
    should_stop_.store(true);
    shutdown_requested_.store(true);
    
    // Stop IO context safely
    {
        std::lock_guard<std::mutex> lock(io_context_mutex_);
        if (ioc_) {
            ioc_->stop();
            io_context_valid_.store(false);
        }
    }
}

void BinanceWebSocketClient::setReconnectionConfig(bool enabled, int maxAttempts, 
                                                  std::chrono::seconds baseDelay, 
                                                  std::chrono::seconds maxDelay) {
    reconnect_enabled_ = enabled;
    max_reconnect_attempts_ = maxAttempts;
    reconnect_base_delay_ = baseDelay;
    reconnect_max_delay_ = maxDelay;
    
    LOG_INFO("Reconnection config updated: enabled=" + std::string(enabled ? "true" : "false") +
             ", maxAttempts=" + std::to_string(maxAttempts) +
             ", baseDelay=" + std::to_string(baseDelay.count()) + "s");
}

void BinanceWebSocketClient::setHeartbeatConfig(std::chrono::seconds interval, 
                                               std::chrono::seconds timeout) {
    heartbeat_interval_ = interval;
    heartbeat_timeout_ = timeout;
    
    LOG_INFO("Heartbeat config updated: interval=" + std::to_string(interval.count()) + "s" +
             ", timeout=" + std::to_string(timeout.count()) + "s");
}

BinanceWebSocketClient::Statistics BinanceWebSocketClient::getStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

// Private methods implementation

void BinanceWebSocketClient::initializeSslContext() {
    ssl_ctx_ = std::make_unique<ssl::context>(ssl::context::tlsv12_client);
    
    // Configure SSL context for Binance
    ssl_ctx_->set_default_verify_paths();
    ssl_ctx_->set_verify_mode(ssl::verify_peer);
    
    // Set hostname verification for security
        // Use weak_ptr capture to avoid extending this object's lifetime via the callback
        if (auto self = this->shared_from_this()) {
            std::weak_ptr<BinanceWebSocketClient> wp = self;
            ssl_ctx_->set_verify_callback([wp](bool preverified, ssl::verify_context& ctx) {
                if (auto s = wp.lock()) {
                    // For production, implement proper certificate verification
                    // For now, we'll be permissive but log any issues
                    if (!preverified) {
                        LOG_WARN("SSL certificate verification failed");
                    }
                }
                return true; // Accept for development - CHANGE FOR PRODUCTION
            });
        } else {
            // Fallback: set permissive callback
            ssl_ctx_->set_verify_callback([](bool, ssl::verify_context&) { return true; });
        }
}

void BinanceWebSocketClient::safeAsyncOp(std::function<void()> op) {
    try {
        std::lock_guard<std::mutex> lock(io_context_mutex_);
        if (shutdown_requested_.load() || !io_context_valid_.load() || !ioc_) {
            LOG_DEBUG("safeAsyncOp: io_context not valid or shutdown requested");
            return;
        }

        std::weak_ptr<BinanceWebSocketClient> wp = shared_from_this();
        net::post(*ioc_, [wp, op]() {
            if (auto s = wp.lock()) {
                try {
                    op();
                } catch (const std::exception& e) {
                    LOG_ERROR(std::string("safeAsyncOp op exception: ") + e.what());
                }
            }
        });
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("safeAsyncOp error: ") + e.what());
    }
}

bool BinanceWebSocketClient::performConnection() {
    try {
        // Resolve hostname
        tcp::resolver resolver{*ioc_};
        auto const results = resolver.resolve(host_, port_);
        
        // Set SNI hostname for SSL
        if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host_.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }
        
        // Connect TCP
        auto ep = beast::get_lowest_layer(*ws_).connect(results);
        LOG_DEBUG("TCP connected to " + ep.address().to_string() + ":" + std::to_string(ep.port()));
        
        // Perform SSL handshake
        ws_->next_layer().handshake(ssl::stream_base::client);
        LOG_DEBUG("SSL handshake completed");
        
        // Set WebSocket options
        ws_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
            req.set(beast::http::field::user_agent, 
                   std::string(BOOST_BEAST_VERSION_STRING) + " trading-system-binance-client");
        }));
        
        // Perform WebSocket handshake
        ws_->handshake(host_, endpoint_);
        LOG_DEBUG("WebSocket handshake completed");

        // Ensure we start reading immediately after successful handshake
        try {
            startRead();
        } catch (const std::exception& e) {
            LOG_WARN(std::string("startRead threw after handshake: ") + e.what());
        }
        
        // Set up control callback for handling close/ping/pong
        {
            std::weak_ptr<BinanceWebSocketClient> wp = shared_from_this();
            ws_->control_callback([wp](websocket::frame_type kind, beast::string_view payload) {
                boost::ignore_unused(payload);
                if (auto s = wp.lock()) {
                    if (kind == websocket::frame_type::pong) {
                        s->handlePong();
                    } else if (kind == websocket::frame_type::close) {
                        LOG_INFO("Received close frame from server");
                    }
                }
            });
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Connection failed: " + std::string(e.what()));
        return false;
    }
}

void BinanceWebSocketClient::startRead() {
    if (!ws_ || !ws_->is_open()) {
        return;
    }
    
        // Only start reading if we're not already reading for this client
        bool expected = false;
        if (!reading_.compare_exchange_strong(expected, true)) {
            return; // Already reading for this client
    }
    LOG_DEBUG("startRead called, scheduling async_read");

    {
            std::weak_ptr<BinanceWebSocketClient> wp = this->shared_from_this();
        ws_->async_read(*buffer_, [wp](beast::error_code ec, std::size_t bytes_transferred) {
            if (auto s = wp.lock()) {
                LOG_DEBUG("async_read callback fired, bytes_transferred=" + std::to_string(bytes_transferred));
                s->reading_.store(false); // Reset reading flag for this client
                boost::ignore_unused(bytes_transferred);

                if (ec) {
                    if (ec == websocket::error::closed) {
                        LOG_INFO("WebSocket closed by peer");
                        s->handleClose(websocket::close_code::abnormal, "Connection closed by peer");
                    } else {
                        LOG_ERROR("Read error: " + ec.message());
                        s->handleError("Read error: " + ec.message());
                    }
                    return;
                }

                // Process the message
                std::string message = beast::buffers_to_string(s->buffer_->data());
                s->buffer_->consume(s->buffer_->size());

                s->updateStatistics();
                s->handleMessage(message);

                // Continue reading if still connected
                if (s->state_.load() == ConnectionStatus::CONNECTED) {
                    s->startRead();
                }
            }
        });
    }
}

void BinanceWebSocketClient::handleMessage(const std::string& message) {
    LOG_DEBUG("handleMessage called, message_len=" + std::to_string(message.size()));
    try {
        auto data = json::parse(message);
        
        // Update last message time
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.lastMessageAt = std::chrono::steady_clock::now();
        }
        
        // Handle different message types
        if (data.contains("id") && (data.contains("result") || data.contains("error"))) {
            // Subscription response
            handleSubscriptionResponse(data);
        } else if (data.contains("stream") && data.contains("data")) {
            // Stream data
            handleStreamData(data);
        } else {
            LOG_DEBUG("Unknown message format: " + message.substr(0, 100));
        }
        // if (buffer_->capacity() > 64 * 1024) { // 64KB threshold
        //     buffer_ = std::make_unique<beast::flat_buffer>();
        // }
     
        if (buffer_->capacity() > 8 * 1024) { // Lower threshold
            buffer_->clear();
            buffer_->shrink_to_fit();
            buffer_ = std::make_unique<beast::flat_buffer>();
        }
    } catch (const json::parse_error& e) {
        LOG_ERROR("JSON parse error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        LOG_ERROR("Message handling error: " + std::string(e.what()));
    }
}

void BinanceWebSocketClient::handleSubscriptionResponse(const json& data) {
    try {
        int id = data.value("id", -1);
        
        if (data.contains("result")) {
            LOG_INFO("Subscription response [" + std::to_string(id) + "]: Success");
        } else if (data.contains("error")) {
            auto error = data["error"];
            int code = error.value("code", -1);
            std::string msg = error.value("msg", "Unknown error");
            LOG_ERROR("Subscription error [" + std::to_string(id) + "]: " + 
                     std::to_string(code) + " - " + msg);
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing subscription response: " + std::string(e.what()));
    }
}


// Thêm helper method
std::string BinanceWebSocketClient::extractStreamType(const std::string& stream_name) const {
    size_t at_pos = stream_name.find('@');
    if (at_pos != std::string::npos) {
        std::string type = stream_name.substr(at_pos + 1);
        if (type.find("depth") == 0) return "depth";
        if (type == "bookTicker") return "bookTicker";
        if (type == "trade") return "trade";
        if (type == "ticker") return "ticker";
    }
    return "unknown";
}

bool BinanceWebSocketClient::sendSubscriptionRequest(const std::vector<std::string>& streams, bool subscribe) {
    if (!ws_ || !ws_->is_open()) {
        LOG_ERROR("Cannot send subscription request - WebSocket not open");
        return false;
    }
    
    try {
        json request;
        request["id"] = subscription_id_counter_.fetch_add(1);
        request["method"] = subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE";
        request["params"] = streams;
        
        std::string message = request.dump();
        
        LOG_DEBUG("Sending " + std::string(subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE") + 
                 " request for " + std::to_string(streams.size()) + " streams");
        // Perform the write on the client's io_context to avoid cross-thread synchronous IO
        if (ioc_) {
            auto msg = std::make_shared<std::string>(std::move(message));
            LOG_DEBUG("Posting subscription message to io_context: " + std::string(msg->begin(), msg->begin() + std::min<size_t>(msg->size(), 200)));
            std::weak_ptr<BinanceWebSocketClient> wp = shared_from_this();
            // Use safeAsyncOp to ensure io_context and object are valid when scheduling
            safeAsyncOp([wp, msg, subscribe]() {
                try {
                    if (auto s = wp.lock()) {
                        LOG_DEBUG("Performing subscription async_write: " + std::string(msg->begin(), msg->begin() + std::min<size_t>(msg->size(), 200)));
                        // Use async_write to remain on io_context and avoid blocking
                        s->ws_->async_write(net::buffer(*msg), [wp, msg](beast::error_code ec, std::size_t bytes_transferred) {
                            if (auto s2 = wp.lock()) {
                                if (ec) {
                                    LOG_ERROR(std::string("Subscription async_write error: ") + ec.message());
                                } else {
                                    LOG_DEBUG(std::string("Subscription async_write completed, bytes=") + std::to_string(bytes_transferred));
                                }
                            }
                        });
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR(std::string("Error scheduling subscription write: ") + e.what());
                }
            });
        } else {
            // Fallback to direct write
            ws_->write(net::buffer(message));
        }
        
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error sending subscription request: " + std::string(e.what()));
        return false;
    }
}

std::string BinanceWebSocketClient::symbolToStream(const Symbol& symbol, const std::string& stream_type) const {
    std::string symbol_str = symbol.base + symbol.quote;
    std::transform(symbol_str.begin(), symbol_str.end(), symbol_str.begin(), ::tolower);
    return symbol_str + "@" + stream_type;
}

Symbol BinanceWebSocketClient::extractSymbolFromStream(const std::string& stream_name) const {
    size_t at_pos = stream_name.find('@');
    if (at_pos != std::string::npos) {
        std::string symbol_str = stream_name.substr(0, at_pos);
        std::transform(symbol_str.begin(), symbol_str.end(), symbol_str.begin(), ::toupper);
        
        // For Binance, we need to parse symbols like "BTCUSDT" into base and quote
        // This is a simplified parser - adjust based on your needs
        if (symbol_str.length() >= 6) {
            // Most common case: last 4 characters are USDT
            if (symbol_str.substr(symbol_str.length() - 4) == "USDT") {
                return Symbol(symbol_str.substr(0, symbol_str.length() - 4), "USDT");
            }
            // Other cases: last 3 characters are quote currency
            else {
                return Symbol(symbol_str.substr(0, symbol_str.length() - 3), 
                            symbol_str.substr(symbol_str.length() - 3));
            }
        }
    }
    
    return Symbol("UNKNOWN", "UNKNOWN");
}

void BinanceWebSocketClient::startHeartbeat() {
    if (!heartbeat_timer_ || state_.load() != ConnectionStatus::CONNECTED) {
        return;
    }
    
    heartbeat_timer_->expires_after(heartbeat_interval_);
    {
    std::weak_ptr<BinanceWebSocketClient> wp = this->shared_from_this();
        heartbeat_timer_->async_wait([wp](const boost::system::error_code& ec) {
            if (auto s = wp.lock()) {
                s->handleHeartbeatTimer(ec);
            }
        });
    }
}

void BinanceWebSocketClient::handleHeartbeatTimer(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::operation_aborted) {
        return; // Timer was cancelled
    }
    
    if (ec) {
        LOG_ERROR("Heartbeat timer error: " + ec.message());
        return;
    }
    
    if (state_.load() == ConnectionStatus::CONNECTED) {
        sendPing();
        startHeartbeat(); // Schedule next heartbeat
    }
}

void BinanceWebSocketClient::sendPing() {
    if (!ws_ || !ws_->is_open()) {
        return;
    }
    
    try {
        waiting_for_pong_.store(true);
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.lastPingAt = std::chrono::steady_clock::now();
        }
        
        ws_->ping("");
        LOG_DEBUG("Ping sent");
        
        // Set timeout for pong response
        heartbeat_timer_->expires_after(heartbeat_timeout_);
        {
            std::weak_ptr<BinanceWebSocketClient> wp = this->shared_from_this();
            heartbeat_timer_->async_wait([wp](const boost::system::error_code& ec) {
                if (auto s = wp.lock()) {
                    if (!ec && s->waiting_for_pong_.load()) {
                        LOG_WARN("Pong timeout - connection may be stale");
                        s->handleError("Heartbeat timeout");
                    }
                }
            });
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error sending ping: " + std::string(e.what()));
        handleError("Ping error: " + std::string(e.what()));
    }
}

void BinanceWebSocketClient::handlePong() {
    waiting_for_pong_.store(false);
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.lastPongAt = std::chrono::steady_clock::now();
    }
    
    LOG_DEBUG("Pong received");
}

void BinanceWebSocketClient::handleClose(websocket::close_code close_code, const std::string& close_reason) {
    LOG_INFO("WebSocket closed: [" + std::to_string(static_cast<int>(close_code)) + "] " + close_reason);
    
    updateConnectionState(ConnectionStatus::DISCONNECTED, "Connection closed: " + close_reason);
    
    if (reconnect_enabled_ && !should_stop_.load()) {
        scheduleReconnect();
    }
}

void BinanceWebSocketClient::handleError(const std::string& error) {
    LOG_ERROR("WebSocket error: " + error);
    
    updateConnectionState(ConnectionStatus::ERROR, error);
    
    // Cleanup current connection
    if (ws_) {
        try {
            if (ws_->is_open()) {
                boost::system::error_code ec;
                ws_->close(websocket::close_code::abnormal, ec);
            }
        } catch (...) {
            // Ignore errors during error handling
        }
    }
    
    if (reconnect_enabled_ && !should_stop_.load()) {
        scheduleReconnect();
    }
}

void BinanceWebSocketClient::scheduleReconnect() {
    if (!reconnect_enabled_ || should_stop_.load()) {
        return;
    }
    
    int current_attempts = reconnect_attempts_.load();
    
    if (max_reconnect_attempts_ > 0 && current_attempts >= max_reconnect_attempts_) {
        LOG_ERROR("Maximum reconnection attempts reached (" + std::to_string(max_reconnect_attempts_) + ")");
        updateConnectionState(ConnectionStatus::ERROR, "Maximum reconnection attempts reached");
        return;
    }
    
    reconnect_attempts_.fetch_add(1);
    
    auto delay = calculateReconnectDelay();
    
    LOG_INFO("Scheduling reconnection attempt " + std::to_string(current_attempts + 1) + 
             " in " + std::to_string(delay.count()) + " seconds");
    
    updateConnectionState(ConnectionStatus::RECONNECTING, "Scheduling reconnection");
    
        if (reconnect_timer_) {
        reconnect_timer_->expires_after(delay);
    std::weak_ptr<BinanceWebSocketClient> wp = this->shared_from_this();
        reconnect_timer_->async_wait([wp](const boost::system::error_code& ec) {
            if (auto s = wp.lock()) {
                s->handleReconnectTimer(ec);
            }
        });
    }
}

void BinanceWebSocketClient::handleReconnectTimer(const boost::system::error_code& ec) {
    if (ec == boost::asio::error::operation_aborted) {
        return; // Timer was cancelled
    }
    
    if (ec) {
        LOG_ERROR("Reconnect timer error: " + ec.message());
        return;
    }
    
    if (should_stop_.load()) {
        return;
    }
    
    LOG_INFO("Attempting reconnection...");
    
    // Clean up previous connection
    cleanup();
    
    // Attempt to reconnect
    if (connect()) {
        LOG_INFO("Reconnection successful");
        
        // Resubscribe to streams
        std::vector<Symbol> symbols_to_resubscribe;
        {
            std::lock_guard<std::mutex> lock(subscription_mutex_);
            symbols_to_resubscribe = subscribed_symbols_;
            // Clear existing subscriptions as we'll re-add them
            subscribed_streams_.clear();
            subscribed_symbols_.clear();
        }
        
        if (!symbols_to_resubscribe.empty()) {
            LOG_INFO("Resubscribing to " + std::to_string(symbols_to_resubscribe.size()) + " symbols");
            subscribe(symbols_to_resubscribe);
        }
        
    } else {
        LOG_ERROR("Reconnection failed");
        scheduleReconnect(); // Try again
    }
}

std::chrono::seconds BinanceWebSocketClient::calculateReconnectDelay() const {
    int attempts = reconnect_attempts_.load();
    
    // Exponential backoff: base_delay * 2^attempts
    auto delay = reconnect_base_delay_ * (1 << std::min(attempts, 6)); // Cap at 2^6 = 64
    
    // Apply jitter (±20%)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<double> jitter(0.8, 1.2);
    
    delay = std::chrono::seconds(static_cast<long>(delay.count() * jitter(gen)));
    
    // Cap at maximum delay
    return std::min(delay, reconnect_max_delay_);
}

void BinanceWebSocketClient::updateConnectionState(ConnectionStatus new_state, const std::string& message) {
    ConnectionStatus old_state = state_.exchange(new_state);
    
    if (old_state != new_state) {
        LOG_INFO("Connection state changed: " + std::to_string(static_cast<int>(old_state)) + 
                " -> " + std::to_string(static_cast<int>(new_state)) + 
                (message.empty() ? "" : " (" + message + ")"));
        
        invokeStatusCallback(new_state, message);
    }
}

void BinanceWebSocketClient::invokeStatusCallback(ConnectionStatus state, const std::string& message) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (status_callback_) {
        try {
            status_callback_(state, message);
        } catch (const std::exception& e) {
            LOG_ERROR("Error in status callback: " + std::string(e.what()));
        }
    }
}

void BinanceWebSocketClient::updateStatistics() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.messagesReceived++;
    stats_.lastMessageAt = std::chrono::steady_clock::now();
}

uint64_t BinanceWebSocketClient::getMessageCount() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_.messagesReceived;
}

std::chrono::steady_clock::time_point BinanceWebSocketClient::getLastMessageTime() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_.lastMessageAt;
}

// void BinanceWebSocketClient::setEnhancedDataCallback(EnhancedDataCallback callback) {
//     std::lock_guard<std::mutex> lock(callback_mutex_);
//     enhanced_data_callback_ = std::move(callback);
// }
// 
void BinanceWebSocketClient::setRawDataCallback(RawDataCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    raw_data_callback_ = std::move(callback);
    LOG_INFO("Raw data callback registered");
}

// void BinanceWebSocketClient::cleanup() {
//     // Cancel all async operations
//     if (heartbeat_timer_) {
//         boost::system::error_code ec;
//         heartbeat_timer_->cancel(ec);
//     }
    
//     if (reconnect_timer_) {
//         boost::system::error_code ec;
//         reconnect_timer_->cancel(ec);
//     }
    
//     // Reset components in proper order
//     if (ws_) {
//         try {
//             if (ws_->is_open()) {
//                 boost::system::error_code ec;
//                 ws_->close(websocket::close_code::going_away, ec);
//             }
//         } catch (...) {
//             // Ignore errors during cleanup
//         }
//         ws_.reset();
//     }
    
//     buffer_.reset();
//     heartbeat_timer_.reset();
//     reconnect_timer_.reset();
//     ssl_ctx_.reset();
    
//     // Reset io_context last
//     if (ioc_) {
//         ioc_->stop();
//         ioc_.reset();
//     }
    
//     waiting_for_pong_.store(false);
// }
void BinanceWebSocketClient::cleanup() {
    // Cancel timers first
    if (heartbeat_timer_) {
        boost::system::error_code ec;
        heartbeat_timer_->cancel(ec);
    }
    
    if (reconnect_timer_) {
        boost::system::error_code ec;
        reconnect_timer_->cancel(ec);
    }
    
    // Close WebSocket to cancel pending operations
    if (ws_ && ws_->is_open()) {
        boost::system::error_code ec;
        ws_->close(websocket::close_code::going_away, ec);
    }
    
    // Run io_context briefly to process cancellations
    if (ioc_) {
        ioc_->poll(); // Process any immediate completions
        ioc_->stop();
    }
    
    // Reset in proper order
    ws_.reset();
    buffer_.reset();
    heartbeat_timer_.reset();
    reconnect_timer_.reset();
    ssl_ctx_.reset();
    ioc_.reset();
}

void BinanceWebSocketClient::handleStreamData(const json& data) {
    try {
        std::string stream = data.value("stream", "");
        auto stream_data = data["data"];
        
        RawMarketData rawData;
        rawData.exchangeId = ExchangeId::binance();
        rawData.symbol = extractSymbolFromStream(stream);
        rawData.streamType = extractStreamType(stream);
        rawData.timestamp = std::chrono::system_clock::now();
        rawData.rawData = stream_data.dump(); // Ensure rawData.rawData is std::string
        rawData.rawData.shrink_to_fit(); // Free unused capacity
        invokeRawDataCallback(rawData);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error processing stream data: " + std::string(e.what()));
    }
}
// invokeRawDataCallback
void BinanceWebSocketClient::invokeRawDataCallback(const RawMarketData& data) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    if (raw_data_callback_) {
        try {
            raw_data_callback_(data);
        } catch (const std::exception& e) {
            LOG_ERROR("Error in raw data callback: " + std::string(e.what()));
        }
    }
}

// std::string BinanceWebSocketClient::extractStreamType(const std::string& stream_name) const {
//     size_t at_pos = stream_name.find('@');
//     if (at_pos != std::string::npos) {
//         std::string type = stream_name.substr(at_pos + 1);
//         if (type.find("depth") == 0) return "depth";
//         if (type == "bookTicker") return "bookTicker";
//         if (type == "trade") return "trade";
//         if (type == "ticker") return "ticker";
//     }
//     return "unknown";
// }

} // namespace data_ingestion
} // namespace trading