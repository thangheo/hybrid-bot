// tests/data_ingestion/websocket/mock_exchange_server.cpp
#include "mock_exchange_server.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <random>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace trading {
namespace testing {

MockExchangeServer::MockExchangeServer(uint16_t port)
    : port_(port),
      running_(false),
      next_client_id_(1),
      random_engine_(std::random_device{}()) {
    
    // Set up the test data
    initializeTestData();
}

MockExchangeServer::~MockExchangeServer() {
    stop();
}

void MockExchangeServer::start() {
    if (running_) {
        return;
    }
    
    try {
        // Configure the WebSocket server
        server_.clear_access_channels(websocketpp::log::alevel::all);
        server_.set_access_channels(websocketpp::log::alevel::app);
        
        server_.init_asio();
        
        // Set up event handlers
        server_.set_open_handler(std::bind(&MockExchangeServer::onOpen, this, std::placeholders::_1));
        server_.set_close_handler(std::bind(&MockExchangeServer::onClose, this, std::placeholders::_1));
        server_.set_message_handler(std::bind(&MockExchangeServer::onMessage, this, 
                                             std::placeholders::_1, std::placeholders::_2));
        
        // Start listening for connections
        server_.listen(port_);
        server_.start_accept();
        
        // Mark as running and start the server thread
        running_ = true;
        server_thread_ = std::thread([this]() {
            try {
                server_.run();
            } catch (const std::exception& e) {
                std::cerr << "Server exception: " << e.what() << std::endl;
            }
        });
        
        // Start the data publisher thread
        publisher_thread_ = std::thread(&MockExchangeServer::publishData, this);
        
        std::cout << "Mock exchange server started on port " << port_ << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error starting mock exchange server: " << e.what() << std::endl;
        throw;
    }
}

void MockExchangeServer::stop() {
    if (!running_) {
        return;
    }
    
    try {
        std::cout << "Stopping mock exchange server..." << std::endl;
        
        // Set the flag to stop the publisher thread
        running_ = false;
        
        // Wait for the publisher thread to exit
        if (publisher_thread_.joinable()) {
            publisher_thread_.join();
        }
        
        // Close all connections and stop the server
        server_.stop_listening();
        
        // Close all open connections
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            for (auto& conn : connections_) {
                try {
                    server_.close(conn.first, websocketpp::close::status::normal, "Server shutting down");
                } catch (...) {
                    // Ignore errors when closing connections
                }
            }
            connections_.clear();
        }
        
        // Stop the ASIO io_service
        server_.stop();
        
        // Wait for the server thread to exit
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        
        std::cout << "Mock exchange server stopped" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "Error stopping mock exchange server: " << e.what() << std::endl;
    }
}

bool MockExchangeServer::isRunning() const {
    return running_;
}

int MockExchangeServer::getConnectionCount() const {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    return connections_.size();
}

void MockExchangeServer::onOpen(websocketpp::connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    // Assign a client ID and store the connection
    int id = next_client_id_++;
    connections_[hdl] = ClientInfo{id, {}};
    
    std::cout << "Client " << id << " connected" << std::endl;
}

void MockExchangeServer::onClose(websocketpp::connection_hdl hdl) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(hdl);
    if (it != connections_.end()) {
        std::cout << "Client " << it->second.id << " disconnected" << std::endl;
        connections_.erase(it);
    }
}

void MockExchangeServer::onMessage(websocketpp::connection_hdl hdl, 
                                 server_type::message_ptr msg) {
    try {
        // Parse the message as JSON
        json request = json::parse(msg->get_payload());
        
        std::cout << "Received message: " << request.dump() << std::endl;
        
        // Check if this is a subscription/unsubscription request
        if (request.contains("method") && request.contains("params") && request.contains("id")) {
            std::string method = request["method"];
            int id = request["id"];
            
            if (method == "SUBSCRIBE") {
                handleSubscription(hdl, id, request["params"]);
            }
            else if (method == "UNSUBSCRIBE") {
                handleUnsubscription(hdl, id, request["params"]);
            }
            else {
                // Unknown method
                sendErrorResponse(hdl, id, "Method not supported");
            }
        }
        else {
            // Unknown request format
            sendErrorResponse(hdl, 0, "Invalid request format");
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error processing message: " << e.what() << std::endl;
        try {
            // Try to send an error response
            sendErrorResponse(hdl, 0, std::string("Error: ") + e.what());
        } catch (...) {
            // Ignore errors when sending error response
        }
    }
}

void MockExchangeServer::handleSubscription(websocketpp::connection_hdl hdl, 
                                          int id, 
                                          const json& streams) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(hdl);
    if (it == connections_.end()) {
        return;
    }
    
    // Add the streams to the connection's subscriptions
    for (const auto& stream : streams) {
        std::string stream_name = stream;
        it->second.subscriptions.insert(stream_name);
        
        std::cout << "Client " << it->second.id << " subscribed to " << stream_name << std::endl;
    }
    
    // Send success response
    sendSuccessResponse(hdl, id);
}

void MockExchangeServer::handleUnsubscription(websocketpp::connection_hdl hdl, 
                                            int id, 
                                            const json& streams) {
    std::lock_guard<std::mutex> lock(connections_mutex_);
    
    auto it = connections_.find(hdl);
    if (it == connections_.end()) {
        return;
    }
    
    // Remove the streams from the connection's subscriptions
    for (const auto& stream : streams) {
        std::string stream_name = stream;
        it->second.subscriptions.erase(stream_name);
        
        std::cout << "Client " << it->second.id << " unsubscribed from " << stream_name << std::endl;
    }
    
    // Send success response
    sendSuccessResponse(hdl, id);
}

void MockExchangeServer::sendSuccessResponse(websocketpp::connection_hdl hdl, int id) {
    try {
        json response;
        response["id"] = id;
        response["result"] = nullptr;
        
        server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
    }
    catch (const std::exception& e) {
        std::cerr << "Error sending success response: " << e.what() << std::endl;
    }
}

void MockExchangeServer::sendErrorResponse(websocketpp::connection_hdl hdl, 
                                         int id, 
                                         const std::string& message) {
    try {
        json response;
        response["id"] = id;
        
        json error;
        error["code"] = -1;
        error["msg"] = message;
        
        response["error"] = error;
        
        server_.send(hdl, response.dump(), websocketpp::frame::opcode::text);
    }
    catch (const std::exception& e) {
        std::cerr << "Error sending error response: " << e.what() << std::endl;
    }
}

void MockExchangeServer::publishData() {
    // Random number generation for realistic price movements
    std::normal_distribution<> price_change(0.0, 0.05);  // Mean 0, std dev 0.05
    std::uniform_real_distribution<> volume(0.001, 10.0);
    std::uniform_int_distribution<> trade_count(1, 5);
    std::uniform_real_distribution<> trade_probability(0.0, 1.0);
    
    // Publication intervals
    const auto order_book_interval = std::chrono::milliseconds(100);
    const auto trade_interval = std::chrono::milliseconds(500);
    const auto ticker_interval = std::chrono::milliseconds(1000);
    
    // Timers for each type of update
    auto last_order_book_time = std::chrono::steady_clock::now();
    auto last_trade_time = std::chrono::steady_clock::now();
    auto last_ticker_time = std::chrono::steady_clock::now();
    
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        
        // Update prices based on random walk
        for (auto& pair : test_data_) {
            // Update bid and ask prices with small random changes
            pair.second.bid_price *= (1.0 + price_change(random_engine_));
            pair.second.ask_price *= (1.0 + price_change(random_engine_));
            
            // Ensure ask is always higher than bid
            if (pair.second.bid_price >= pair.second.ask_price) {
                pair.second.ask_price = pair.second.bid_price * 1.001;
            }
        }
        
        // Publish order book updates
        if (now - last_order_book_time >= order_book_interval) {
            for (const auto& pair : test_data_) {
                publishOrderBookUpdate(pair.first);
            }
            last_order_book_time = now;
        }
        
        // Publish trades
        if (now - last_trade_time >= trade_interval) {
            for (const auto& pair : test_data_) {
                // Not every trading pair gets a trade in each interval
                if (trade_probability(random_engine_) < 0.3) {
                    // Generate 1-5 trades
                    int num_trades = trade_count(random_engine_);
                    for (int i = 0; i < num_trades; ++i) {
                        publishTrade(pair.first);
                    }
                }
            }
            last_trade_time = now;
        }
        
        // Publish ticker updates
        if (now - last_ticker_time >= ticker_interval) {
            for (const auto& pair : test_data_) {
                publishTickerUpdate(pair.first);
            }
            last_ticker_time = now;
        }
        
        // Sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void MockExchangeServer::publishOrderBookUpdate(const std::string& symbol) {
    try {
        if (!running_) return;
        
        std::lock_guard<std::mutex> lock(connections_mutex_);
        
        // Format: symbol@depth
        std::string stream = symbol + "@depth";
        
        // Find connections subscribed to this stream
        for (auto& conn : connections_) {
            if (conn.second.subscriptions.find(stream) != conn.second.subscriptions.end() ||
                conn.second.subscriptions.find(stream + "@100ms") != conn.second.subscriptions.end()) {
                
                // Create an order book update
                json data = createOrderBookUpdate(symbol);
                
                // Wrap in stream format
                json message;
                message["stream"] = stream;
                message["data"] = data;
                
                // Send the message
                server_.send(conn.first, message.dump(), websocketpp::frame::opcode::text);
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error publishing order book update: " << e.what() << std::endl;
    }
}

void MockExchangeServer::publishTrade(const std::string& symbol) {
    try {
        if (!running_) return;
        
        std::lock_guard<std::mutex> lock(connections_mutex_);
        
        // Format: symbol@trade
        std::string stream = symbol + "@trade";
        
        // Find connections subscribed to this stream
        for (auto& conn : connections_) {
            if (conn.second.subscriptions.find(stream) != conn.second.subscriptions.end()) {
                
                // Create a trade update
                json data = createTradeUpdate(symbol);
                
                // Wrap in stream format
                json message;
                message["stream"] = stream;
                message["data"] = data;
                
                // Send the message
                server_.send(conn.first, message.dump(), websocketpp::frame::opcode::text);
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error publishing trade: " << e.what() << std::endl;
    }
}

void MockExchangeServer::publishTickerUpdate(const std::string& symbol) {
    try {
        if (!running_) return;
        
        std::lock_guard<std::mutex> lock(connections_mutex_);
        
        // Format: symbol@ticker
        std::string stream = symbol + "@ticker";
        
        // Find connections subscribed to this stream
        for (auto& conn : connections_) {
            if (conn.second.subscriptions.find(stream) != conn.second.subscriptions.end()) {
                
                // Create a ticker update
                json data = createTickerUpdate(symbol);
                
                // Wrap in stream format
                json message;
                message["stream"] = stream;
                message["data"] = data;
                
                // Send the message
                server_.send(conn.first, message.dump(), websocketpp::frame::opcode::text);
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error publishing ticker update: " << e.what() << std::endl;
    }
}

nlohmann::json MockExchangeServer::createOrderBookUpdate(const std::string& symbol) {
    auto& data = test_data_[symbol];
    
    nlohmann::json update;
    update["e"] = "depthUpdate";
    update["E"] = getCurrentTimestampMs();
    update["s"] = symbol;
    update["U"] = data.last_update_id + 1;
    update["u"] = data.last_update_id + 2;
    
    // Generate some random bids
    nlohmann::json bids = nlohmann::json::array();
    double bid_price = data.bid_price;
    for (int i = 0; i < 5; ++i) {
        nlohmann::json level = nlohmann::json::array();
        level.push_back(formatPrice(bid_price - i * 0.01 * bid_price));
        level.push_back(formatQuantity(getRandomQuantity()));
        bids.push_back(level);
    }
    update["b"] = bids;
    
    // Generate some random asks
    nlohmann::json asks = nlohmann::json::array();
    double ask_price = data.ask_price;
    for (int i = 0; i < 5; ++i) {
        nlohmann::json level = nlohmann::json::array();
        level.push_back(formatPrice(ask_price + i * 0.01 * ask_price));
        level.push_back(formatQuantity(getRandomQuantity()));
        asks.push_back(level);
    }
    update["a"] = asks;
    
    // Update the last update id
    data.last_update_id += 2;
    
    return update;
}

nlohmann::json MockExchangeServer::createTradeUpdate(const std::string& symbol) {
    auto& data = test_data_[symbol];
    
    nlohmann::json trade;
    trade["e"] = "trade";
    trade["E"] = getCurrentTimestampMs();
    trade["s"] = symbol;
    trade["t"] = data.next_trade_id++;
    
    // Determine if this is a buy or sell
    bool is_buyer_maker = (std::uniform_real_distribution<>(0, 1)(random_engine_) < 0.5);
    
    // Price is at the bid for a seller-initiated trade, or at the ask for a buyer-initiated trade
    double price = is_buyer_maker ? data.bid_price : data.ask_price;
    
    // Add some random noise to the price
    price *= (1.0 + std::normal_distribution<>(0, 0.0005)(random_engine_));
    
    // Generate a random quantity
    double quantity = getRandomQuantity();
    
    trade["p"] = formatPrice(price);
    trade["q"] = formatQuantity(quantity);
    trade["b"] = data.next_buyer_order_id++;
    trade["a"] = data.next_seller_order_id++;
    trade["T"] = getCurrentTimestampMs();
    trade["m"] = is_buyer_maker;
    
    // Update last price
    data.last_price = price;
    data.volume += quantity;
    
    return trade;
}

nlohmann::json MockExchangeServer::createTickerUpdate(const std::string& symbol) {
    auto& data = test_data_[symbol];
    
    nlohmann::json ticker;
    ticker["e"] = "24hrTicker";
    ticker["E"] = getCurrentTimestampMs();
    ticker["s"] = symbol;
    
    // 24-hour price change (random for this mock)
    double change = data.last_price - data.open_price;
    double change_percent = (change / data.open_price) * 100.0;
    
    ticker["p"] = formatPrice(change);
    ticker["P"] = formatPrice(change_percent);
    ticker["w"] = formatPrice((data.high_price + data.low_price) / 2.0); // Weighted average
    ticker["x"] = formatPrice(data.open_price);
    ticker["c"] = formatPrice(data.last_price);  // Last price
    ticker["Q"] = formatQuantity(getRandomQuantity());  // Last quantity
    ticker["b"] = formatPrice(data.bid_price);
    ticker["B"] = formatQuantity(getRandomQuantity());  // Bid quantity
    ticker["a"] = formatPrice(data.ask_price);
    ticker["A"] = formatQuantity(getRandomQuantity());  // Ask quantity
    ticker["o"] = formatPrice(data.open_price);
    ticker["h"] = formatPrice(data.high_price);
    ticker["l"] = formatPrice(data.low_price);
    ticker["v"] = formatQuantity(data.volume);  // Total volume
    ticker["q"] = formatPrice(data.volume * data.last_price);  // Quote volume
    
    return ticker;
}

void MockExchangeServer::initializeTestData() {
    // Add some test trading pairs
    addTradingPair("btcusdt", 45000.0);
    addTradingPair("ethusdt", 3000.0);
    addTradingPair("bnbusdt", 400.0);
    addTradingPair("solusdt", 100.0);
    addTradingPair("adausdt", 1.5);
}

void MockExchangeServer::addTradingPair(const std::string& symbol, double base_price) {
    TradingPairData data;
    
    // Initialize with slightly different bid/ask for a small spread
    data.bid_price = base_price * 0.999;
    data.ask_price = base_price * 1.001;
    data.last_price = base_price;
    data.open_price = base_price * (1.0 - std::uniform_real_distribution<>(0, 0.05)(random_engine_));
    data.high_price = base_price * (1.0 + std::uniform_real_distribution<>(0, 0.05)(random_engine_));
    data.low_price = base_price * (1.0 - std::uniform_real_distribution<>(0, 0.05)(random_engine_));
    data.volume = std::uniform_real_distribution<>(1000, 10000)(random_engine_);
    data.last_update_id = 1000000;
    data.next_trade_id = 1;
    data.next_buyer_order_id = 1000;
    data.next_seller_order_id = 2000;
    
    test_data_[symbol] = data;
}

double MockExchangeServer::getRandomQuantity() {
    // Log-normal distribution for more realistic quantities
    std::lognormal_distribution<> dist(0.1, 1.0);
    return dist(random_engine_);
}

std::string MockExchangeServer::formatPrice(double price) {
    std::ostringstream oss;
    oss.precision(8);
    oss << std::fixed << price;
    return oss.str();
}

std::string MockExchangeServer::formatQuantity(double quantity) {
    std::ostringstream oss;
    oss.precision(8);
    oss << std::fixed << quantity;
    return oss.str();
}

int64_t MockExchangeServer::getCurrentTimestampMs() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

}  // namespace testing
}  // namespace trading
// End of file