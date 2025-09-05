/**
 * @file trading_events.hpp
 * @brief Event definitions for the trading system
 */

#pragma once

#include "common/event_bus.hpp"
#include "strategy/types.hpp"
#include "data_ingestion/types.hpp"  // Add this include
#include <mutex>
#include <vector>
#include <string>
#include "data_ingestion/websocket/exchanges/exchange_connector.hpp"

namespace trading {
namespace events {

/**
 * Event fired when order book metrics are updated
 */
class MetricsUpdatedEvent : public common::Event {
public:
    strategy::OrderBookMetrics metrics;
    
    explicit MetricsUpdatedEvent(const strategy::OrderBookMetrics& m) : metrics(m) {}
};

/**
 * Event fired when a trading signal is generated
 */
class SignalGeneratedEvent : public common::Event {
public:
    strategy::TradingSignal signal;
    
    explicit SignalGeneratedEvent(const strategy::TradingSignal& s) : signal(s) {}
};

/**
 * Event fired when an imbalance is detected
 */
class ImbalanceDetectedEvent : public common::Event {
public:
    strategy::ImbalanceEvent imbalance;
    
    explicit ImbalanceDetectedEvent(const strategy::ImbalanceEvent& i) : imbalance(i) {}
};

// ADD THESE NEW EVENTS FOR DATA INGESTION:

/**
 * Event for subscription requests
 */
class SubscriptionRequestEvent : public common::Event {
public:
    std::vector<std::string> tradingPairs;
    std::vector<data_ingestion::DataType> dataTypes;
    int32_t depth;
    std::string requestId;  // For tracking responses
    
    SubscriptionRequestEvent(const std::vector<std::string>& pairs,
                           const std::vector<data_ingestion::DataType>& types,
                           int32_t d,
                           const std::string& id)
        : tradingPairs(pairs), dataTypes(types), depth(d), requestId(id) {}
};

/**
 * Event for subscription responses
 */
class SubscriptionResponseEvent : public common::Event {
public:
    std::string requestId;
    bool success;
    std::string errorMessage;
    
    SubscriptionResponseEvent(const std::string& id, bool s, const std::string& error = "")
        : requestId(id), success(s), errorMessage(error) {}
};

/**
 * Event fired when connection status changes
 */
class ConnectionStatusEvent : public common::Event {
public:
    core::ExchangeId exchangeId;
    bool isConnected;
    std::string statusMessage;
    
    ConnectionStatusEvent(const core::ExchangeId& exchange, bool connected, const std::string& message = "")
        : exchangeId(exchange), isConnected(connected), statusMessage(message) {}
};

} // namespace events
} // namespace trading