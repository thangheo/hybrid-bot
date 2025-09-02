// src/data_ingestion/websocket/exchanges/binance_websocket_client.cpp
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

// BinanceWebSocketClient::BinanceWebSocketClient(const std::string& host,
//                                              const std::string& port,
//                                              std::function<void(const core::ExchangeId&, const std::string&, const std::vector<uint8_t>&)> callback)
//     : host_(host)
//     , port_(port)
//     , endpoint_("wss://" + host + ":" + port + "/ws")
//     , dataCallback_(callback)
//     , lastMessageTime_(std::chrono::system_clock::now()) {
    
//     LOG_INFO("Created BinanceWebSocketClient for " + endpoint_);
// }
// EMERGENCY FIX: Replace BinanceWebSocketClient constructor and connect method

BinanceWebSocketClient::BinanceWebSocketClient(const std::string& host,
                                             const std::string& port,
                                             std::function<void(const core::ExchangeId&, const std::string&, const std::vector<uint8_t>&)> callback)
    : host_(host)
    , port_(port)
    , endpoint_("wss://" + host + ":" + port + "/ws")
    , dataCallback_(callback)
    , lastMessageTime_(std::chrono::system_clock::now())
    , keep_running_(false)  // INITIALIZE THIS
    , reconnectAttempts_(0)
    , subscriptionIdCounter_(1) {
    
    LOG_INFO("Created BinanceWebSocketClient for " + endpoint_);
}

// SAFER connect method
bool BinanceWebSocketClient::connect() {
    if (connected_.load()) {
        LOG_WARN("Already connected to Binance WebSocket");
        return true;
    }
    
    try {
        status_.store(ConnectionStatus::CONNECTING);
        
        // CRITICAL: Check if thread is already running
        if (io_thread_.joinable()) {
            LOG_WARN("Previous IO thread still running, cleaning up...");
            keep_running_ = false;
            io_thread_.join();
        }
        
        // Reset flags
        keep_running_ = true;
        connected_.store(false);
        
        // Create thread with explicit lambda capture
        io_thread_ = std::thread([this]() {
            try {
                this->runIoLoop();
            } catch (const std::exception& e) {
                LOG_ERROR("IO thread exception: " + std::string(e.what()));
            } catch (...) {
                LOG_ERROR("Unknown IO thread exception");
            }
        });
        
        // Wait for connection with timeout
        int attempts = 0;
        while (!connected_.load() && attempts < 50) { // 5 seconds max
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

BinanceWebSocketClient::~BinanceWebSocketClient() {
    try {
        // Signal all threads to stop
        keep_running_ = false;
        connected_.store(false);
        
        // Close WebSocket connection first to interrupt any blocking operations
        if (wsStream_ && wsStream_->is_open()) {
            try {
                wsStream_->close(websocket::close_code::normal);
            } catch (...) {
                // Ignore close errors during destruction
            }
        }
        
        // Join the IO thread if it's joinable
        if (io_thread_.joinable()) {
            LOG_INFO("Waiting for IO thread to finish in destructor...");
            io_thread_.join();
            LOG_INFO("IO thread joined successfully");
        }
        
        // Clean up resources
        wsStream_.reset();
        
        LOG_INFO("BinanceWebSocketClient destroyed successfully");
        
    } catch (const std::exception& e) {
        // Log error but don't throw from destructor
        LOG_ERROR("Error in BinanceWebSocketClient destructor: " + std::string(e.what()));
    } catch (...) {
        // Catch any other exceptions to prevent terminate
        LOG_ERROR("Unknown error in BinanceWebSocketClient destructor");
    }
}

void BinanceWebSocketClient::closeConnection() {
    try {
        LOG_INFO("Closing WebSocket connection");
        
        // Signal threads to stop FIRST
        keep_running_ = false;
        
        // Close WebSocket stream to interrupt any blocking read operations
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
        
        // Give the thread a moment to notice the close and exit gracefully
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Join the IO thread if it exists and is joinable
        if (io_thread_.joinable()) {
            LOG_INFO("Waiting for IO thread to terminate...");
            
            // Use a timeout to avoid hanging indefinitely
            auto future = std::async(std::launch::async, [this]() {
                io_thread_.join();
            });
            
            if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
                LOG_ERROR("IO thread did not terminate within timeout - this may cause issues");
                // Note: We can't detach here safely because the object might be destroyed
            } else {
                LOG_INFO("IO thread terminated successfully");
            }
        }
        
        // Clean up WebSocket stream
        wsStream_.reset();
        
        LOG_INFO("WebSocket connection closed successfully");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error closing WebSocket connection: " + std::string(e.what()));
    } catch (...) {
        LOG_ERROR("Unknown error closing WebSocket connection");
    }
}

// // ClientInterface implementation
// bool BinanceWebSocketClient::connect() {
//     if (connected_.load()) {
//         LOG_WARN("Already connected to Binance WebSocket");
//         return true;
//     }
    
//     try {
//         status_.store(ConnectionStatus::CONNECTING);
        
//         // TODO: Implement actual WebSocket connection using boost::beast
//         // For now, simulate connection
//         initializeConnection();
        
//         connected_.store(true);
//         status_.store(ConnectionStatus::CONNECTED);
        
//         if (connectionCallback_) {
//             connectionCallback_(ConnectionStatus::CONNECTED, "Connected to Binance WebSocket");
//         }
        
//         LOG_INFO("Connected to Binance WebSocket: " + endpoint_);
//         return true;
        
//     } catch (const std::exception& e) {
//         status_.store(ConnectionStatus::ERROR);
//         connected_.store(false);
        
//         if (connectionCallback_) {
//             connectionCallback_(ConnectionStatus::ERROR, e.what());
//         }
        
//         LOG_ERROR("Failed to connect to Binance WebSocket: " + std::string(e.what()));
//         return false;
//     }
// }

void BinanceWebSocketClient::disconnect() {
    if (!connected_.load()) {
        LOG_INFO("Already disconnected from Binance WebSocket");
        return;
    }
    
    LOG_INFO("Disconnecting from Binance WebSocket...");
    
    // Update status first
    connected_.store(false);
    status_.store(ConnectionStatus::DISCONNECTING);
    
    // Signal threads to stop
    keep_running_ = false;
    
    // Close the actual connection
    closeConnection();
    
    // Update final status
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
        // Tạo các stream cho mỗi symbol
        std::string depthStream = symbolToStream(symbol, "depth");
        // std::string tradeStream = symbolToStream(symbol, "trade");
        // std::string tickerStream = symbolToStream(symbol, "ticker");

        streams.push_back(depthStream);
        // streams.push_back(tradeStream);
        // streams.push_back(tickerStream);

        // Thêm vào subscribedStreams_ nếu chưa có
        if (std::find(subscribedStreams_.begin(), subscribedStreams_.end(), depthStream) == subscribedStreams_.end())
            subscribedStreams_.push_back(depthStream);
        // if (std::find(subscribedStreams_.begin(), subscribedStreams_.end(), tradeStream) == subscribedStreams_.end())
        //     subscribedStreams_.push_back(tradeStream);
        // if (std::find(subscribedStreams_.begin(), subscribedStreams_.end(), tickerStream) == subscribedStreams_.end())
        //     subscribedStreams_.push_back(tickerStream);

        // Thêm vào subscribedSymbols_ nếu chưa có
        if (std::find(subscribedSymbols_.begin(), subscribedSymbols_.end(), symbol) == subscribedSymbols_.end()) {
            subscribedSymbols_.push_back(symbol);
            LOG_INFO("Added symbol to subscription: " + symbol.base + symbol.quote);
        }
    }

    bool success = subscribeToStreams(streams);

    if (success) {
        LOG_INFO("Successfully subscribed to " + std::to_string(symbols.size()) + " symbols on Binance");
        LOG_INFO("Total subscribed symbols: " + std::to_string(subscribedSymbols_.size()));
        LOG_INFO("Total subscribed streams: " + std::to_string(subscribedStreams_.size()));
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
 
void BinanceWebSocketClient::runIoLoop() {
    LOG_INFO("Starting real WebSocket IO loop for Binance");
    
    try {
        // Create IO context and SSL context
        net::io_context ioc;
        ssl::context ctx{ssl::context::tlsv12_client};
        
        // Load default root certificates
        ctx.set_default_verify_paths();
        ctx.set_verify_mode(ssl::verify_peer);
        
        // These objects perform our I/O
        tcp::resolver resolver{ioc};
        
        // CREATE the WebSocket stream and STORE it in wsStream_
        wsStream_ = std::make_unique<websocket::stream<beast::ssl_stream<beast::tcp_stream>>>(ioc, ctx);

        // Set SNI Hostname
        if (!SSL_set_tlsext_host_name(wsStream_->next_layer().native_handle(), host_.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }
        
        auto const results = resolver.resolve(host_, port_);
        
        // Connect TCP through Beast
        beast::get_lowest_layer(*wsStream_).connect(results);
        
        // Perform the SSL handshake
        wsStream_->next_layer().handshake(ssl::stream_base::client);
        
        // Set decorator
        wsStream_->set_option(websocket::stream_base::decorator(
            [](websocket::request_type& req) {
                req.set(beast::http::field::user_agent, 
                    std::string(BOOST_BEAST_VERSION_STRING) + " trading-system-websocket");
            }));
        
        // Perform the websocket handshake
        wsStream_->handshake(host_, "/ws");
        
        LOG_INFO("WebSocket handshake completed successfully");
        
        // *** DO NOT SEND SUBSCRIPTION HERE - WAIT FOR EXPLICIT CALL ***
        LOG_INFO("WebSocket ready - waiting for subscription calls");
        
        // Connection successful - update status
        connected_.store(true);
        status_.store(ConnectionStatus::CONNECTED);
        
        // Notify connection callback
        if (connectionCallback_) {
            connectionCallback_(ConnectionStatus::CONNECTED, "WebSocket connected successfully");
        }
        
        // Call onWebSocketOpen (but it won't send subscription)
        onWebSocketOpen();
        
        // Main message loop
        beast::flat_buffer buffer;
        
        while (keep_running_ && connected_.load()) {
            try {
                // Set timeout for read operation
                beast::get_lowest_layer(*wsStream_).expires_after(std::chrono::seconds(30));
                
                // Read a message
                wsStream_->read(buffer);
                
                // Convert buffer to string
                std::string message = beast::buffers_to_string(buffer.data());
                buffer.consume(buffer.size());
                
                // Process the message
                onWebSocketMessage(message);
                
                // Reset timeout
                beast::get_lowest_layer(*wsStream_).expires_at(std::chrono::steady_clock::time_point::max());
                
            } catch (const beast::system_error& se) {
                if (se.code() == beast::error::timeout) {
                    LOG_WARN("WebSocket read timeout - sending ping");
                    
                    try {
                        wsStream_->ping({});
                        continue;
                    } catch (const std::exception& ping_error) {
                        LOG_ERROR("Failed to send ping: " + std::string(ping_error.what()));
                        break;
                    }
                } else if (se.code() == websocket::error::closed) {
                    LOG_INFO("WebSocket connection closed by remote");
                    break;
                } else {
                    LOG_ERROR("WebSocket read error: " + std::string(se.what()));
                    break;
                }
            }
        }
        
        LOG_INFO("Exiting WebSocket message loop");
        
        // Graceful close
        try {
            if (wsStream_ && wsStream_->is_open()) {
                wsStream_->close(websocket::close_code::normal);
            }
        } catch (const std::exception& e) {
            LOG_WARN("Error during WebSocket close: " + std::string(e.what()));
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR("WebSocket connection failed: " + std::string(e.what()));
        
        // Update status to error
        status_.store(ConnectionStatus::ERROR);
        connected_.store(false);
        
        if (connectionCallback_) {
            connectionCallback_(ConnectionStatus::ERROR, "WebSocket connection error: " + std::string(e.what()));
        }
    }
    
    // Final cleanup
    connected_.store(false);
    status_.store(ConnectionStatus::DISCONNECTED);
    wsStream_.reset();
    
    LOG_INFO("WebSocket IO loop terminated");
}

// // Enhanced sendWebSocketMessage with real WebSocket support
// bool BinanceWebSocketClient::sendWebSocketMessage(const std::string& message) {
//     if (!connected_.load() || !wsStream_) {
//         LOG_ERROR("Cannot send message - WebSocket not connected");
//         return false;
//     }
    
//     try {
//         std::lock_guard<std::mutex> lock(sendMutex_);
        
//         // Send the message
//         wsStream_->write(net::buffer(message));
        
//         LOG_DEBUG("Sent WebSocket message: " + message.substr(0, 100) + 
//                  (message.length() > 100 ? "..." : ""));
        
//         return true;
        
//     } catch (const std::exception& e) {
//         LOG_ERROR("Error sending WebSocket message: " + std::string(e.what()));
//         return false;
//     }
// }

// Fix sendWebSocketMessage to work with the stored wsStream_
bool BinanceWebSocketClient::sendWebSocketMessage(const std::string& message) {
    if (!connected_.load()) {
        LOG_ERROR("Cannot send message - WebSocket not connected (connected_=" + std::to_string(connected_.load()) + ")");
        return false;
    }
    
    if (!wsStream_) {
        LOG_ERROR("Cannot send message - wsStream_ is null");
        return false;
    }
    
    if (!wsStream_->is_open()) {
        LOG_ERROR("Cannot send message - wsStream_ is not open");
        return false;
    }
    
    try {
        std::lock_guard<std::mutex> lock(sendMutex_);
        
        LOG_INFO("Sending WebSocket message: " + message);
        
        // Send the message using the stored wsStream_
        wsStream_->write(net::buffer(message));
        
        LOG_INFO("Message sent successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error sending WebSocket message: " + std::string(e.what()));
        return false;
    }
}
// Add reconnection logic
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
    
    // Calculate backoff delay (exponential backoff)
    auto delay = std::chrono::seconds(
        static_cast<long>(reconnectBaseDelay_.count() * std::pow(2, reconnectAttempts_ - 1))
    );
    
    // Cap the delay to maximum
    delay = std::min(delay, maxReconnectDelay_);
    
    LOG_INFO("Scheduling reconnection attempt " + std::to_string(reconnectAttempts_) + 
            " in " + std::to_string(delay.count()) + " seconds");
    
    // Schedule reconnection in a separate thread
    std::thread([this, delay]() {
        std::this_thread::sleep_for(delay);
        
        if (keep_running_) {
            LOG_INFO("Attempting reconnection...");
            connect();
        }
    }).detach();
}

// Enhanced connection management with proper cleanup
void BinanceWebSocketClient::initializeConnection() {
    try {
        LOG_INFO("Initializing real WebSocket connection to Binance");
        
        // Set up connection parameters
        keep_running_ = true;
        reconnectAttempts_ = 0;
        
        // Start the IO loop in a separate thread
        io_thread_ = std::thread(&BinanceWebSocketClient::runIoLoop, this);
        
        // Wait a bit for connection to establish
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

// void BinanceWebSocketClient::closeConnection() {
//     try {
//         LOG_INFO("Closing WebSocket connection");
        
//         // Signal threads to stop
//         keep_running_ = false;
        
//         // Close WebSocket if it exists
//         if (wsStream_ && wsStream_->is_open()) {
//             try {
//                 wsStream_->close(websocket::close_code::normal);
//             } catch (const std::exception& e) {
//                 LOG_WARN("Error closing WebSocket stream: " + std::string(e.what()));
//             }
//         }
        
//         // Join the IO thread
//         if (io_thread_.joinable()) {
//             LOG_INFO("Waiting for IO thread to terminate...");
//             io_thread_.join();
//             LOG_INFO("IO thread terminated");
//         }
        
//         // Clean up WebSocket stream
//         wsStream_.reset();
        
//         LOG_INFO("WebSocket connection closed successfully");
        
//     } catch (const std::exception& e) {
//         LOG_ERROR("Error closing WebSocket connection: " + std::string(e.what()));
//     }
// }


//  void BinanceWebSocketClient::onWebSocketOpen() {
//     std::cout << "Connected to Binance WebSocket" << std::endl;
    
//     // Resubscribe to previously subscribed streams
//     std::lock_guard<std::mutex> lock(subscriptionMutex_);
//     if (!subscribedStreams_.empty()) {
//         std::cout << "Resubscribing to " << subscribedStreams_.size() << " streams" << std::endl;
//         subscribeToStreams(subscribedStreams_);
//     }
// }

// Fix onWebSocketOpen - DON'T auto-subscribe
void BinanceWebSocketClient::onWebSocketOpen() {
    LOG_INFO("WebSocket connection opened - ready for subscriptions");
    // DO NOT automatically resubscribe - wait for explicit subscribe() calls
}

void BinanceWebSocketClient::onWebSocketMessage(const std::string& message) {
    try {
        // Add debug output for first few messages
        static std::atomic<int> messageDebugCount{0};
        if (messageDebugCount.fetch_add(1) < 3) {
            LOG_INFO("Processing WebSocket message: " + message.substr(0, 100) + "...");
        }
        
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
            std::string event_type = data.value("e", "");
            
            std::string symbol = data.value("s", "");
            
            if (event_type == "depthUpdate") {
                processOrderBookData(symbol, "depth", data);
            } else if (event_type == "trade") {
                processTrade(symbol, data);
            } else if (event_type == "24hrTicker") {
                processTicker(symbol, data);
            } else if (event_type == "bookTicker") {
                processBookTicker(symbol, data);
            } else {
                LOG_DEBUG("Unhandled event type: " + event_type + " for symbol: " + symbol);
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
        // Check if this is a stream wrapper or direct data
        if (data.contains("stream") && data.contains("data")) {
            std::string stream = data.value("stream", "");
            json stream_data = data.value("data", json{});
            
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
                LOG_DEBUG("Unhandled stream event: " + event_type + " for symbol: " + symbol);
            }
        } else {
            LOG_DEBUG("handleStreamMessage called with non-stream data");
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing Binance stream message: " << e.what() << std::endl;
    }
}

void BinanceWebSocketClient::handleResponse(const json& data) {
    try {
        int id = data.value("id", -1);
        
        if (data.contains("result")) {
            // Successful response
            std::cout << "Binance subscription response [" << id << "]: Success" << std::endl;
        }
        else if (data.contains("error")) {
            // Error response
            json error = data.value("error", json{});
            int code = error.value("code", -1);
            std::string msg = error.value("msg", "Unknown error");
            std::cerr << "Binance subscription error [" << id << "]: " 
                      << code << " - " << msg << std::endl;
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
        
        // Update subscription list (NOTE: caller must hold subscriptionMutex_!)
        if (success && subscribe) {
            // Add new streams to the list
            for (const auto& stream : streams) {
                if (std::find(subscribedStreams_.begin(), subscribedStreams_.end(), stream) == subscribedStreams_.end()) {
                    subscribedStreams_.push_back(stream);
                }
            }
            LOG_INFO("Added " + std::to_string(streams.size()) + " streams to subscription list");
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

void BinanceWebSocketClient::generateOrderBookUpdate(const std::string& symbol, double basePrice, std::mt19937& gen) {
    try {
        // Generate realistic order book update
        json orderBook;
        orderBook["e"] = "depthUpdate";
        orderBook["E"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        orderBook["s"] = symbol;
        
        // Generate bids (below current price)
        json bids = json::array();
        for (int i = 0; i < 5; ++i) {
            double price = basePrice * (1.0 - (i + 1) * 0.0001);
            double quantity = std::uniform_real_distribution<double>(0.1, 10.0)(gen);
            bids.push_back({std::to_string(price), std::to_string(quantity)});
        }
        
        // Generate asks (above current price)
        json asks = json::array();
        for (int i = 0; i < 5; ++i) {
            double price = basePrice * (1.0 + (i + 1) * 0.0001);
            double quantity = std::uniform_real_distribution<double>(0.1, 10.0)(gen);
            asks.push_back({std::to_string(price), std::to_string(quantity)});
        }
        
        orderBook["b"] = bids;
        orderBook["a"] = asks;
        
        // Send to callback
        std::string message = orderBook.dump();
        
        // Add debug output for first few messages
        static std::atomic<int> debugCount{0};
        if (debugCount.fetch_add(1) < 5) {
            LOG_INFO("Generated order book update for " + symbol + " (price: " + std::to_string(basePrice) + ")");
        }
        
        onWebSocketMessage(message);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error generating order book update: " + std::string(e.what()));
    }
}

void BinanceWebSocketClient::generateTradeUpdate(const std::string& symbol, double basePrice, std::mt19937& gen) {
    try {
        json trade;
        trade["e"] = "trade";
        trade["E"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        trade["s"] = symbol;
        trade["t"] = gen() % 1000000;  // Random trade ID
        
        // Random price near base price
        double priceVariation = std::uniform_real_distribution<double>(-0.001, 0.001)(gen);
        double price = basePrice * (1.0 + priceVariation);
        double quantity = std::uniform_real_distribution<double>(0.01, 5.0)(gen);
        
        trade["p"] = std::to_string(price);
        trade["q"] = std::to_string(quantity);
        trade["T"] = trade["E"];  // Trade time = event time
        trade["m"] = gen() % 2 == 0;  // Random buyer/seller
        
        std::string message = trade.dump();
        onWebSocketMessage(message);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error generating trade update: " + std::string(e.what()));
    }
}

void BinanceWebSocketClient::generateTickerUpdate(const std::string& symbol, double basePrice, std::mt19937& gen) {
    try {
        json ticker;
        ticker["e"] = "24hrTicker";
        ticker["E"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        ticker["s"] = symbol;
        
        // Generate realistic 24hr stats
        double change = std::uniform_real_distribution<double>(-0.05, 0.05)(gen);
        ticker["c"] = std::to_string(basePrice);  // Close price
        ticker["o"] = std::to_string(basePrice * (1.0 - change));  // Open price
        ticker["h"] = std::to_string(basePrice * (1.0 + std::abs(change) * 1.5));  // High
        ticker["l"] = std::to_string(basePrice * (1.0 - std::abs(change) * 1.5));  // Low
        ticker["v"] = std::to_string(std::uniform_real_distribution<double>(10000, 100000)(gen));  // Volume
        
        std::string message = ticker.dump();
        onWebSocketMessage(message);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error generating ticker update: " + std::string(e.what()));
    }
}

} // namespace data_ingestion
} // namespace trading