/**
 * @file trading_events.hpp
 * @brief Event definitions for the trading system
 */

#pragma once

#include "common/event_bus.hpp"
#include "strategy/types.hpp"
#include <mutex>
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

} // namespace events
} // namespace trading