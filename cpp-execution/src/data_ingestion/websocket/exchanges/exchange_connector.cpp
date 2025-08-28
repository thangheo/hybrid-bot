// src/data_ingestion/exchange_connector.cpp
#include "data_ingestion/websocket/exchanges/exchange_connector.hpp"
#include <chrono>
#include <iostream>

namespace trading {
namespace data_ingestion {

ExchangeConnector::ExchangeConnector(const core::ExchangeId& exchangeId, 
                                     MarketDataCallback callback)
    : exchangeId_(exchangeId), 
      data_callback_(std::move(callback)), 
      connected_(false),
      reconnect_attempt_limit_(5),
      current_reconnect_attempts_(0),
      reconnect_delay_ms_(1000) {
    
    std::cout << "Initializing connector for exchange: " << exchangeId.toString() << std::endl;
}

void ExchangeConnector::reportError(const std::string& error_msg, bool is_fatal) {
    std::lock_guard<std::mutex> lock(error_mutex_);
    std::cerr << "[" << exchangeId_.toString() << "] Error: " << error_msg << std::endl;
    
    last_error_ = error_msg;
    last_error_time_ = std::chrono::system_clock::now();
    
    // If fatal, disconnect and possibly attempt reconnection
    if (is_fatal && connected_) {
        std::cout << "[" << exchangeId_.toString() << "] Fatal error, disconnecting..." << std::endl;
        disconnect();
        tryReconnect();
    }
}

std::string ExchangeConnector::getLastError() const {
    std::lock_guard<std::mutex> lock(error_mutex_);
    return last_error_;
}

void ExchangeConnector::setReconnectionParameters(int attempt_limit, int delay_ms) {
    reconnect_attempt_limit_ = attempt_limit;
    reconnect_delay_ms_ = delay_ms;
}

bool ExchangeConnector::tryReconnect() {
    if (current_reconnect_attempts_ >= reconnect_attempt_limit_) {
        std::cerr << "[" << exchangeId_.toString() << "] Exceeded reconnection attempt limit." << std::endl;
        return false;
    }
    
    current_reconnect_attempts_++;
    std::cout << "[" << exchangeId_.toString() << "] Attempting reconnect (" 
              << current_reconnect_attempts_ << "/" 
              << reconnect_attempt_limit_ << ")..." << std::endl;
    
    // Sleep before reconnection attempt to avoid rapid reconnection attempts
    std::this_thread::sleep_for(std::chrono::milliseconds(
        reconnect_delay_ms_ * current_reconnect_attempts_));
    
    // Try to reconnect
    bool success = connect();
    
    if (success) {
        std::cout << "[" << exchangeId_.toString() << "] Reconnected successfully." << std::endl;
        current_reconnect_attempts_ = 0;
        
        // Resubscribe to all previously subscribed data
        for (const auto& request : active_subscriptions_) {
            subscribe(request);
        }
    }
    
    return success;
}

void ExchangeConnector::addSubscriptionRequest(const SubscriptionRequest& request) {
    active_subscriptions_.push_back(request);
}

void ExchangeConnector::clearSubscriptionRequests() {
    active_subscriptions_.clear();
}

std::vector<SubscriptionRequest> ExchangeConnector::getActiveSubscriptions() const {
    return active_subscriptions_;
}

DataType ExchangeConnector::stringToDataType(const std::string& type_str) {
    if (type_str == "trades") return DataType::TRADES;
    if (type_str == "orderbook") return DataType::ORDER_BOOK;
    if (type_str == "ticker") return DataType::TICKER;
    throw std::invalid_argument("Invalid data type string: " + type_str);
}

std::string ExchangeConnector::dataTypeToString(DataType type) {
    switch (type) {
        case DataType::TRADES: return "trades";
        case DataType::ORDER_BOOK: return "orderbook";
        case DataType::TICKER: return "ticker";
        default: throw std::invalid_argument("Invalid data type");
    }
}

} // namespace data_ingestion
} // namespace trading