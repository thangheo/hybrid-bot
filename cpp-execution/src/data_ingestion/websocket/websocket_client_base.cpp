/**
 * @file websocket_client_base.cpp
 * @brief Implementation of base WebSocket client functionality
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: April 30, 2025
 */

#include "data_ingestion/websocket/client_base.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <random>

namespace trading {
namespace data_ingestion {

WebSocketClientBase::WebSocketClientBase(const std::string& host, 
                                         const std::string& port,
                                         bool use_ssl)
    : host_(host),
      port_(port),
      use_ssl_(use_ssl),
      connected_(false),
      keep_running_(false),
      reconnect_attempt_limit_(5),
      current_reconnect_attempts_(0),
      reconnect_delay_ms_(1000),
      reconnect_backoff_factor_(1.5f),
      last_pong_time_(std::chrono::steady_clock::now()),
      ping_interval_ms_(30000) {
    
    std::cout << "Initializing WebSocket client for " << host << ":" << port << std::endl;
    
    // Initialize SSL context if needed
    if (use_ssl_) {
        ssl_context_ = std::make_unique<ssl::context>(ssl::context::tlsv12_client);
        ssl_context_->set_default_verify_paths();
        ssl_context_->set_verify_mode(ssl::verify_peer);
    }
}

WebSocketClientBase::~WebSocketClientBase() {
    disconnect();
}

bool WebSocketClientBase::connect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (connected_) {
        std::cout << "Already connected to " << host_ << ":" << port_ << std::endl;
        return true;
    }
    
    try {
        std::cout << "Connecting to " << host_ << ":" << port_ << "..." << std::endl;
        
        // Reset and restart the I/O context
        io_context_.restart();
        
        // Resolve the host
        tcp::resolver resolver(io_context_);
        auto const results = resolver.resolve(host_, port_);
        
        if (use_ssl_) {
            // Create the SSL stream
            ssl_stream_ = std::make_unique<beast::ssl_stream<tcp::socket>>(io_context_, *ssl_context_);
            
            // Set SNI hostname (many hosts need this for certificate validation)
            if (!SSL_set_tlsext_host_name(ssl_stream_->native_handle(), host_.c_str())) {
                throw beast::system_error(
                    beast::error_code(static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()),
                    "Failed to set SNI hostname");
            }
            
            // Connect to the endpoint
            auto& socket = beast::get_lowest_layer(*ssl_stream_);
            socket.connect(results.begin()->endpoint());
            
            // Perform SSL handshake
            ssl_stream_->handshake(ssl::stream_base::client);
            
            // Create WebSocket stream - Fixed: Move the ssl_stream_ instead of creating reference
            ws_ssl_stream_ = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(std::move(*ssl_stream_));
            
            // Set WebSocket options
            ws_ssl_stream_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
            
            // Perform WebSocket handshake
            ws_ssl_stream_->handshake(host_, "/");
        } 
        else {
            // Create regular WebSocket stream
            ws_stream_ = std::make_unique<websocket::stream<tcp::socket>>(io_context_);
            
            // Connect to the endpoint
            auto& socket = beast::get_lowest_layer(*ws_stream_);
            socket.connect(results.begin()->endpoint());
            
            // Set WebSocket options
            ws_stream_->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
            
            // Perform WebSocket handshake
            ws_stream_->handshake(host_, "/");
        }
        
        // Set connected flag
        connected_ = true;
        current_reconnect_attempts_ = 0;
        
        // Start the IO thread
        keep_running_ = true;
        io_thread_ = std::thread(&WebSocketClientBase::runIoService, this);
        
        // Start ping timer
        ping_timer_ = std::thread(&WebSocketClientBase::pingTask, this);
        
        // Notify derived classes
        onOpen();
        
        std::cout << "Connected to " << host_ << ":" << port_ << std::endl;
        return true;
    }
    catch (const std::exception& e) {
        std::cerr << "Connection error: " << e.what() << std::endl;
        
        // Clean up resources
        if (ws_ssl_stream_) ws_ssl_stream_.reset();
        if (ssl_stream_) ssl_stream_.reset();
        if (ws_stream_) ws_stream_.reset();
        
        connected_ = false;
        return false;
    }
}

void WebSocketClientBase::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!connected_) {
        return;
    }
    
    try {
        std::cout << "Disconnecting from " << host_ << ":" << port_ << "..." << std::endl;
        
        // Set the stopping flag for threads
        keep_running_ = false;
        
        // Close the WebSocket connections
        if (ws_ssl_stream_) {
            ws_ssl_stream_->close(websocket::close_code::normal);
            ws_ssl_stream_.reset();
        }
        
        if (ws_stream_) {
            ws_stream_->close(websocket::close_code::normal);
            ws_stream_.reset();
        }
        
        // Clean up SSL stream
        if (ssl_stream_) {
            ssl_stream_.reset();
        }
        
        // Stop the IO context and join the threads
        io_context_.stop();
        
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
        
        if (ping_timer_.joinable()) {
            ping_timer_.join();
        }
        
        connected_ = false;
        onClose(websocket::close_code::normal, "Normal closure");
        
        std::cout << "Disconnected from " << host_ << ":" << port_ << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Disconnection error: " << e.what() << std::endl;
        connected_ = false;
    }
}

bool WebSocketClientBase::isConnected() const {
    return connected_;
}

void WebSocketClientBase::send(const std::string& message) {
    if (!connected_) {
        std::cerr << "Cannot send message, not connected" << std::endl;
        return;
    }
    
    try {
        std::lock_guard<std::mutex> lock(write_mutex_);
        
        if (ws_ssl_stream_) {
            ws_ssl_stream_->write(net::buffer(message));
        } 
        else if (ws_stream_) {
            ws_stream_->write(net::buffer(message));
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Send error: " << e.what() << std::endl;
        handleError(e.what());
    }
}

void WebSocketClientBase::setPingInterval(int interval_ms) {
    ping_interval_ms_ = interval_ms;
}

void WebSocketClientBase::setReconnectionParameters(int attempt_limit, 
                                                   int delay_ms, 
                                                   float backoff_factor) {
    reconnect_attempt_limit_ = attempt_limit;
    reconnect_delay_ms_ = delay_ms;
    reconnect_backoff_factor_ = backoff_factor;
}

void WebSocketClientBase::runIoService() {
    try {
        // Set up the read buffer
        beast::flat_buffer buffer;
        
        while (keep_running_ && connected_) {
            try {
                // Read a message into our buffer
                if (ws_ssl_stream_) {
                    ws_ssl_stream_->read(buffer);
                } 
                else if (ws_stream_) {
                    ws_stream_->read(buffer);
                }
                
                // Extract the message as a string
                std::string msg = boost::beast::buffers_to_string(buffer.data());
                buffer.consume(buffer.size());
                
                // Process the message
                onMessage(msg);
            }
            catch (const boost::system::system_error& e) {
                if (keep_running_) {
                    std::cerr << "WebSocket error: " << e.what() << std::endl;
                    
                    // Handle the error
                    handleError(e.what());
                    break;
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "IO thread error: " << e.what() << std::endl;
        handleError(e.what());
    }
    
    connected_ = false;
}

void WebSocketClientBase::pingTask() {
    while (keep_running_ && connected_) {
        try {
            // Sleep for the ping interval
            std::this_thread::sleep_for(std::chrono::milliseconds(ping_interval_ms_));
            
            if (!keep_running_ || !connected_) {
                break;
            }
            
            // Send a ping
            {
                std::lock_guard<std::mutex> lock(write_mutex_);
                
                if (ws_ssl_stream_) {
                    ws_ssl_stream_->ping({});
                    // Update last_pong_time_ when we send a ping
                    // In older versions, pong responses are handled internally
                    last_pong_time_ = std::chrono::steady_clock::now();
                } 
                else if (ws_stream_) {
                    ws_stream_->ping({});
                    last_pong_time_ = std::chrono::steady_clock::now();
                }
            }
        }
        catch (const std::exception& e) {
            std::cerr << "Ping error: " << e.what() << std::endl;
            
            if (keep_running_ && connected_) {
                handleReconnection();
            }
        }
    }
}

void WebSocketClientBase::handlePong() {
    last_pong_time_ = std::chrono::steady_clock::now();
}

void WebSocketClientBase::handleError(const std::string& error) {
    // Notify derived classes
    onError(error);
    
    // Attempt reconnection
    if (keep_running_) {
        handleReconnection();
    }
}

void WebSocketClientBase::handleReconnection() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Check if we're already disconnected
        if (!connected_) {
            return;
        }
        
        // Clean up existing connections
        if (ws_ssl_stream_) ws_ssl_stream_.reset();
        if (ssl_stream_) ssl_stream_.reset();
        if (ws_stream_) ws_stream_.reset();
        
        connected_ = false;
        
        // Notify about the disconnection
        onClose(websocket::close_code::abnormal, "Connection lost");
    }
    
    // Check if we've reached the reconnection limit
    if (current_reconnect_attempts_ >= reconnect_attempt_limit_) {
        std::cerr << "Exceeded reconnection attempt limit (" << reconnect_attempt_limit_ << ")" << std::endl;
        return;
    }
    
    // Calculate backoff delay with jitter
    int delay = static_cast<int>(reconnect_delay_ms_ * std::pow(reconnect_backoff_factor_, current_reconnect_attempts_));
    
    // Add jitter (±20%)
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> jitter(-delay / 5, delay / 5);
    delay += jitter(gen);
    
    current_reconnect_attempts_++;
    
    std::cout << "Attempting reconnection " << current_reconnect_attempts_ << "/" 
              << reconnect_attempt_limit_ << " after " << delay << "ms delay..." << std::endl;
    
    // Sleep before reconnection attempt
    std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    
    // Attempt to reconnect
    if (connect()) {
        std::cout << "Reconnected successfully" << std::endl;
        onReconnect();
    } else {
        std::cerr << "Reconnection attempt failed" << std::endl;
        
        // Schedule another reconnection attempt if we haven't reached the limit
        if (current_reconnect_attempts_ < reconnect_attempt_limit_ && keep_running_) {
            handleReconnection();
        }
    }
}

} // namespace data_ingestion
} // namespace trading