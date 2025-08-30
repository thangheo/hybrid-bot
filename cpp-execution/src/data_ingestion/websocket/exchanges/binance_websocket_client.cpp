// src/data_ingestion/websocket/exchanges/binance_websocket_client.cpp
#include "data_ingestion/websocket/exchanges/binance_websocket_client.hpp"
#include "common/logger.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <chrono>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/stream.hpp>

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
    , endpoint_("wss://" + host + ":" + port + "/ws")
    , dataCallback_(callback)
    , lastMessageTime_(std::chrono::system_clock::now()) {
    
    LOG_INFO("Created BinanceWebSocketClient for " + endpoint_);
}

BinanceWebSocketClient::~BinanceWebSocketClient() {
    disconnect();
}

// ClientInterface implementation
bool BinanceWebSocketClient::connect() {
    if (connected_.load()) {
        LOG_WARN("Already connected to Binance WebSocket");
        return true;
    }
    
    try {
        status_.store(ConnectionStatus::CONNECTING);
        
        // TODO: Implement actual WebSocket connection using boost::beast
        // For now, simulate connection
        initializeConnection();
        
        connected_.store(true);
        status_.store(ConnectionStatus::CONNECTED);
        
        if (connectionCallback_) {
            connectionCallback_(ConnectionStatus::CONNECTED, "Connected to Binance WebSocket");
        }
        
        LOG_INFO("Connected to Binance WebSocket: " + endpoint_);
        return true;
        
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
        return;
    }
    
    connected_.store(false);
    status_.store(ConnectionStatus::DISCONNECTED);
    
    closeConnection();
    
    if (connectionCallback_) {
        connectionCallback_(ConnectionStatus::DISCONNECTED, "Disconnected from Binance WebSocket");
    }
    
    LOG_INFO("Disconnected from Binance WebSocket");
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
    
    std::vector<std::string> streams;
    for (const auto& symbol : symbols) {
        // Create different stream types for each symbol
        streams.push_back(symbolToStream(symbol, "depth"));    // Order book
        streams.push_back(symbolToStream(symbol, "trade"));    // Trades
        streams.push_back(symbolToStream(symbol, "ticker"));   // 24hr stats
        
        // Add to subscribed symbols if not already present
        if (std::find(subscribedSymbols_.begin(), subscribedSymbols_.end(), symbol) == subscribedSymbols_.end()) {
            subscribedSymbols_.push_back(symbol);
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
    
    std::vector<std::string> streams;
    for (const auto& symbol : symbols) {
        streams.push_back(symbolToStream(symbol, "depth"));
        streams.push_back(symbolToStream(symbol, "trade"));
        streams.push_back(symbolToStream(symbol, "ticker"));
        
        // Remove from subscribed symbols
        subscribedSymbols_.erase(
            std::remove(subscribedSymbols_.begin(), subscribedSymbols_.end(), symbol),
            subscribedSymbols_.end()
        );
    }
    
    return unsubscribeFromStreams(streams);
}

std::vector<core::Symbol> BinanceWebSocketClient::getSubscribedSymbols() const {
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    return subscribedSymbols_;
}

bool BinanceWebSocketClient::sendMessage(const std::string& message) {
    if (!connected_.load()) {
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

// Binance-specific methods
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

// Private implementation methods
void BinanceWebSocketClient::initializeConnection() {
    try {
        // Create SSL context
        ssl_context_ = std::make_unique<ssl::context>(ssl::context::tlsv12_client);
        ssl_context_->set_default_verify_paths();
        ssl_context_->set_verify_mode(ssl::verify_peer);
        
        // Create WebSocket SSL stream
        ws_stream_ = std::make_unique<websocket::stream<ssl::stream<tcp::socket>>>(
            io_context_, *ssl_context_);
        
        // Set SNI hostname for SSL
        if (!SSL_set_tlsext_host_name(
            ws_stream_->next_layer().native_handle(), 
            host_.c_str())) {
            throw std::runtime_error("Failed to set SNI hostname");
        }
        
        // Resolve host
        tcp::resolver resolver(io_context_);
        auto const results = resolver.resolve(host_, port_);
        
        // Connect TCP socket
        auto endpoint = net::connect(beast::get_lowest_layer(*ws_stream_), results);
        
        // Perform SSL handshake
        ws_stream_->next_layer().handshake(ssl::stream_base::client);
        
        // Perform WebSocket handshake
        ws_stream_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_stream_->handshake(host_, "/ws");
        
        // Start IO thread for receiving messages
        keep_running_ = true;
        io_thread_ = std::thread(&BinanceWebSocketClient::runIoLoop, this);
        
        LOG_INFO("Real WebSocket connection established to " + host_);
        
    } catch (const std::exception& e) {
        LOG_ERROR("WebSocket connection failed: " + std::string(e.what()));
        throw;
    }
    LOG_INFO("WebSocket connection initialized (placeholder implementation)");

}
void BinanceWebSocketClient::runIoLoop() {
    beast::flat_buffer buffer;
    
    LOG_INFO("Starting WebSocket IO loop");
    
    try {
        while (keep_running_ && connected_.load()) {
            // Read message from WebSocket
            ws_stream_->read(buffer);
            
            // Convert to string
            std::string message = beast::buffers_to_string(buffer.data());
            buffer.consume(buffer.size());
            
            // Process the message
            onWebSocketMessage(message);
            
            // Update statistics
            updateStatistics();
        }
    } catch (const std::exception& e) {
        if (keep_running_) {
            LOG_ERROR("WebSocket IO loop error: " + std::string(e.what()));
            connected_.store(false);
            onWebSocketError(e.what());
        }
    }
    
    LOG_INFO("WebSocket IO loop stopped");
}
void BinanceWebSocketClient::closeConnection() {
    try {
        keep_running_ = false;
        
        if (ws_stream_ && ws_stream_->is_open()) {
            ws_stream_->close(websocket::close_code::normal);
        }
        
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
        
        ws_stream_.reset();
        ssl_context_.reset();
        
        LOG_INFO("WebSocket connection closed");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error closing WebSocket: " + std::string(e.what()));
    }
}
bool BinanceWebSocketClient::sendWebSocketMessage(const std::string& message) {
    if (!connected_.load() || !ws_stream_ || !ws_stream_->is_open()) {
        LOG_ERROR("Cannot send message - WebSocket not connected");
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(write_mutex_);
        ws_stream_->write(net::buffer(message));
        LOG_DEBUG("Sent real WebSocket message: " + message);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to send WebSocket message: " + std::string(e.what()));
        connected_.store(false);
        return false;
    }
}
void BinanceWebSocketClient::onWebSocketOpen() {
    std::cout << "Connected to Binance WebSocket" << std::endl;
    
    // Resubscribe to previously subscribed streams
    std::lock_guard<std::mutex> lock(subscriptionMutex_);
    if (!subscribedStreams_.empty()) {
        std::cout << "Resubscribing to " << subscribedStreams_.size() << " streams" << std::endl;
        subscribeToStreams(subscribedStreams_);
    }
}

void BinanceWebSocketClient::onWebSocketMessage(const std::string& message) {
    try {
        // Parse the JSON message
        json data = json::parse(message);
        
        // Update statistics
        updateStatistics();
        
        // Call the message callback
        if (messageCallback_) {
            messageCallback_(message, std::chrono::system_clock::now());
        }
        
        // Handle subscription responses
        if (data.contains("id") && (data.contains("result") || data.contains("error"))) {
            handleResponse(data);
            return;
        }
        
        // Handle stream messages
        if (data.contains("stream") && data.contains("data")) {
            handleStreamMessage(data);
            return;
        }
        
        // Handle direct data format (not wrapped in stream)
        if (data.contains("e")) {
            std::string event_type = data["e"];
            std::string symbol = data.contains("s") ? data["s"] : "";
            
            if (event_type == "depthUpdate") {
                processOrderBookData(symbol, "depth", data);
            } else if (event_type == "trade") {
                processTrade(symbol, data);
            } else if (event_type == "24hrTicker") {
                processTicker(symbol, data);
            } else if (event_type == "bookTicker") {
                processBookTicker(symbol, data);
            }
            return;
        }
        
        std::cout << "Unhandled Binance message: " << message << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing Binance message: " << e.what() << std::endl;
        std::cerr << "Message: " << message << std::endl;
    }
}

void BinanceWebSocketClient::onWebSocketClose(int code, const std::string& reason) {
    std::cout << "Disconnected from Binance WebSocket: [" << code << "] " << reason << std::endl;
}

void BinanceWebSocketClient::onWebSocketError(const std::string& error) {
    std::cerr << "Binance WebSocket error: " << error << std::endl;
}

void BinanceWebSocketClient::handleStreamMessage(const json& data) {
    try {
        std::string stream = data["stream"];
        json stream_data = data["data"];
        
        // Extract symbol and event type from the stream name
        std::string symbol = extractSymbolFromStream(stream);
        std::string event_type = extractEventTypeFromStream(stream);
        
        // Process based on the event type
        if (event_type.find("depth") != std::string::npos) {
            processOrderBookData(symbol, event_type, stream_data);
        }
        else if (event_type == "trade") {
            processTrade(symbol, stream_data);
        }
        else if (event_type == "ticker") {
            processTicker(symbol, stream_data);
        }
        else if (event_type == "bookTicker") {
            processBookTicker(symbol, stream_data);
        }
        else {
            std::cout << "Unhandled Binance stream type: " << event_type << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing Binance stream message: " << e.what() << std::endl;
    }
}

void BinanceWebSocketClient::handleResponse(const json& data) {
    try {
        int id = data["id"];
        
        if (data.contains("result")) {
            // Successful response
            std::cout << "Binance subscription response [" << id << "]: Success" << std::endl;
        }
        else if (data.contains("error")) {
            // Error response
            std::cerr << "Binance subscription error [" << id << "]: " 
                      << data["error"]["code"] << " - " << data["error"]["msg"] << std::endl;
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing Binance response: " << e.what() << std::endl;
    }
}

void BinanceWebSocketClient::processOrderBookData(const std::string& symbol, 
                                                const std::string& event_type, 
                                                const json& data) {
    if (!dataCallback_) {
        return;
    }
    
    try {
        // Create data for callback
        std::string json_str = data.dump();
        std::vector<uint8_t> dataBytes(json_str.begin(), json_str.end());
        
        // Call the callback with the expected signature
        dataCallback_(core::ExchangeId::binance(), symbol, dataBytes);
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing Binance order book data: " << e.what() << std::endl;
    }
}

void BinanceWebSocketClient::processTrade(const std::string& symbol, const json& data) {
    if (!dataCallback_) {
        return;
    }
    
    try {
        // Create data for callback
        std::string json_str = data.dump();
        std::vector<uint8_t> dataBytes(json_str.begin(), json_str.end());
        
        // Call the callback with the expected signature
        dataCallback_(core::ExchangeId::binance(), symbol, dataBytes);
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing Binance trade data: " << e.what() << std::endl;
    }
}

void BinanceWebSocketClient::processTicker(const std::string& symbol, const json& data) {
    if (!dataCallback_) {
        return;
    }
    
    try {
        // Create data for callback
        std::string json_str = data.dump();
        std::vector<uint8_t> dataBytes(json_str.begin(), json_str.end());
        
        // Call the callback with the expected signature
        dataCallback_(core::ExchangeId::binance(), symbol, dataBytes);
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing Binance ticker data: " << e.what() << std::endl;
    }
}

void BinanceWebSocketClient::processBookTicker(const std::string& symbol, const json& data) {
    if (!dataCallback_) {
        return;
    }
    
    try {
        // Create data for callback
        std::string json_str = data.dump();
        std::vector<uint8_t> dataBytes(json_str.begin(), json_str.end());
        
        // Call the callback with the expected signature
        dataCallback_(core::ExchangeId::binance(), symbol, dataBytes);
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing Binance book ticker data: " << e.what() << std::endl;
    }
}

bool BinanceWebSocketClient::sendSubscriptionRequest(const std::vector<std::string>& streams, bool subscribe) {
    if (!connected_.load()) {
        return false;
    }
    
    try {
        // Prepare the subscription message
        json request;
        request["id"] = subscriptionIdCounter_++;
        request["method"] = subscribe ? "SUBSCRIBE" : "UNSUBSCRIBE";
        request["params"] = streams;
        
        // Send the request
        std::string message = request.dump();
        bool success = sendWebSocketMessage(message);
        
        // Update subscription list
        std::lock_guard<std::mutex> lock(subscriptionMutex_);
        if (success && subscribe) {
            // Add new streams to the list
            for (const auto& stream : streams) {
                if (std::find(subscribedStreams_.begin(), subscribedStreams_.end(), stream) == subscribedStreams_.end()) {
                    subscribedStreams_.push_back(stream);
                }
            }
        } else if (success && !subscribe) {
            // Remove streams from the list
            for (const auto& stream : streams) {
                auto it = std::find(subscribedStreams_.begin(), subscribedStreams_.end(), stream);
                if (it != subscribedStreams_.end()) {
                    subscribedStreams_.erase(it);
                }
            }
        }
        
        return success;
    }
    catch (const std::exception& e) {
        std::cerr << "Error sending Binance subscription request: " << e.what() << std::endl;
        return false;
    }
}

std::string BinanceWebSocketClient::symbolToStream(const core::Symbol& symbol, const std::string& streamType) const {
    std::string symbolStr = symbol.base + symbol.quote;
    std::transform(symbolStr.begin(), symbolStr.end(), symbolStr.begin(), ::tolower);
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
        
        // Convert to uppercase
        std::transform(symbol.begin(), symbol.end(), symbol.begin(), ::toupper);
        
        return symbol;
    }
    return "";
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
    // TODO: Calculate actual latency if needed
}

void BinanceWebSocketClient::workerFunction() {
    // TODO: Implement if needed for background processing
}

} // namespace data_ingestion
} // namespace trading