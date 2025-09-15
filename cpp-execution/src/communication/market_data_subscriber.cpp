/**
 * @file market_data_subscriber.cpp
 * @brief ZeroMQ subscriber implementation
 */

#include "communication/market_data_subscriber.hpp"
#include "common/logger.hpp"
#include <iostream>
#include <chrono>

namespace trading {
namespace communication {

MarketDataSubscriber::MarketDataSubscriber(const std::string& publisherEndpoint)
    : publisherEndpoint_(publisherEndpoint) {
    LOG_INFO("Created MarketDataSubscriber for endpoint: " + publisherEndpoint);
}

MarketDataSubscriber::~MarketDataSubscriber() {
    stop();
}

bool MarketDataSubscriber::start() {
    if (running_.load()) {
        LOG_WARN("MarketDataSubscriber already running");
        return true;
    }

    try {
        // Create ZeroMQ context and subscriber socket
        context_ = std::make_unique<zmq::context_t>(1);
        subscriber_ = std::make_unique<zmq::socket_t>(*context_, zmq::socket_type::sub);
        
        // Connect to publisher
        subscriber_->connect(publisherEndpoint_);
        LOG_INFO("Connected to market data publisher: " + publisherEndpoint_);
        
        // Set high water mark to prevent message loss
        int hwm = 10000;
        subscriber_->set(zmq::sockopt::rcvhwm, hwm);
        
        running_ = true;
        
        // Start subscription thread
        subscriptionThread_ = std::thread([this]() {
            subscriptionLoop();
        });
        
        LOG_INFO("MarketDataSubscriber started successfully");
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to start MarketDataSubscriber: " + std::string(e.what()));
        return false;
    }
}

void MarketDataSubscriber::stop() {
    if (!running_.exchange(false)) {
        return; // Already stopped
    }
    
    LOG_INFO("Stopping MarketDataSubscriber...");
    
    try {
        // Close socket to interrupt subscription loop
        if (subscriber_) {
            subscriber_->close();
        }
        
        // Wait for thread to finish
        if (subscriptionThread_.joinable()) {
            subscriptionThread_.join();
        }
        
        // Cleanup
        subscriber_.reset();
        context_.reset();
        
        LOG_INFO("MarketDataSubscriber stopped");
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error stopping MarketDataSubscriber: " + std::string(e.what()));
    }
}

void MarketDataSubscriber::subscribeToOrderBooks(const std::vector<std::string>& symbols) {
    if (!subscriber_) {
        LOG_ERROR("Subscriber not initialized");
        return;
    }
    
    try {
        if (symbols.empty()) {
            // Subscribe to all order book topics
            subscriber_->set(zmq::sockopt::subscribe, "orderbook.");
            LOG_INFO("Subscribed to all order book topics");
        } else {
            for (const auto& symbol : symbols) {
                std::string topic = "orderbook.BINANCE." + symbol;
                subscriber_->set(zmq::sockopt::subscribe, topic);
                LOG_INFO("Subscribed to order book topic: " + topic);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error subscribing to order books: " + std::string(e.what()));
    }
}

void MarketDataSubscriber::subscribeToIntelligence(const std::vector<std::string>& symbols) {
    if (!subscriber_) {
        LOG_ERROR("Subscriber not initialized");
        return;
    }
    
    try {
        if (symbols.empty()) {
            // Subscribe to all intelligence topics
            subscriber_->set(zmq::sockopt::subscribe, "intelligence.");
            LOG_INFO("Subscribed to all intelligence topics");
        } else {
            for (const auto& symbol : symbols) {
                std::string topic = "intelligence." + symbol;
                subscriber_->set(zmq::sockopt::subscribe, topic);
                LOG_INFO("Subscribed to intelligence topic: " + topic);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Error subscribing to intelligence: " + std::string(e.what()));
    }
}

void MarketDataSubscriber::subscribeToAll() {
    if (!subscriber_) {
        LOG_ERROR("Subscriber not initialized");
        return;
    }
    
    try {
        // Subscribe to all topics
        subscriber_->set(zmq::sockopt::subscribe, "");
        LOG_INFO("Subscribed to all topics");
    } catch (const std::exception& e) {
        LOG_ERROR("Error subscribing to all topics: " + std::string(e.what()));
    }
}

void MarketDataSubscriber::subscriptionLoop() {
    LOG_INFO("Starting subscription loop");
    
    while (running_.load()) {
        try {
            zmq::message_t topicMsg, dataMsg;
            
            // Receive topic
            auto result = subscriber_->recv(topicMsg, zmq::recv_flags::dontwait);
            if (!result) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            
            // Receive data
            result = subscriber_->recv(dataMsg, zmq::recv_flags::none);
            if (!result) {
                continue;
            }
            
            std::string topic(static_cast<char*>(topicMsg.data()), topicMsg.size());
            std::string data(static_cast<char*>(dataMsg.data()), dataMsg.size());
            
            // Route message based on topic
            if (topic.find("orderbook.") == 0) {
                handleOrderBookMessage(topic, data);
            } else if (topic.find("intelligence.") == 0) {
                handleIntelligenceMessage(topic, data);
            }
            
        } catch (const zmq::error_t& e) {
            if (e.num() == ETERM) {
                break; // Context terminated
            }
            LOG_ERROR("ZMQ error in subscription loop: " + std::string(e.what()));
        } catch (const std::exception& e) {
            LOG_ERROR("Error in subscription loop: " + std::string(e.what()));
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    LOG_INFO("Subscription loop finished");
}

void MarketDataSubscriber::handleOrderBookMessage(const std::string& topic, const std::string& data) {
    try {
        json msg = json::parse(data);
        
        OrderBookData orderBook;
        orderBook.exchange = msg.value("exchange", "");
        orderBook.symbol = msg.value("symbol", "");
        orderBook.sequence = msg.value("sequence", 0ULL);
        orderBook.timestamp = msg.value("timestamp", 0ULL);
        
        // Parse bids
        if (msg.contains("bids") && msg["bids"].is_array()) {
            for (const auto& bid : msg["bids"]) {
                if (bid.is_array() && bid.size() >= 2) {
                    double price = bid[0].get<double>();
                    double quantity = bid[1].get<double>();
                    orderBook.bids.emplace_back(price, quantity);
                }
            }
        }
        
        // Parse asks
        if (msg.contains("asks") && msg["asks"].is_array()) {
            for (const auto& ask : msg["asks"]) {
                if (ask.is_array() && ask.size() >= 2) {
                    double price = ask[0].get<double>();
                    double quantity = ask[1].get<double>();
                    orderBook.asks.emplace_back(price, quantity);
                }
            }
        }
        
        // Callback
        if (orderBookCallback_) {
            orderBookCallback_(orderBook);
        }
        
        orderBooksReceived_.fetch_add(1);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing order book message: " + std::string(e.what()));
    }
}

void MarketDataSubscriber::handleIntelligenceMessage(const std::string& topic, const std::string& data) {
    try {
        json msg = json::parse(data);
        
        MarketIntelligenceData intel;
        intel.symbol = msg.value("symbol", "");
        intel.bidAskSpread = msg.value("bid_ask_spread", 0.0);
        intel.liquidityRatio = msg.value("liquidity_ratio", 0.0);
        intel.orderBookImbalance = msg.value("order_book_imbalance", 0.0);
        intel.priceMomentum = msg.value("price_momentum", 0.0);
        intel.volatilityEstimate = msg.value("volatility_estimate", 0.0);
        intel.currentRegime = msg.value("current_regime", 0);
        intel.regimeConfidence = msg.value("regime_confidence", 0.0);
        intel.timestamp = msg.value("timestamp", 0ULL);
        
        // Callback
        if (intelligenceCallback_) {
            intelligenceCallback_(intel);
        }
        
        intelligenceReceived_.fetch_add(1);
        
    } catch (const std::exception& e) {
        LOG_ERROR("Error parsing intelligence message: " + std::string(e.what()));
    }
}

} // namespace communication
} // namespace trading
