# Order Book Publisher Integration Guide

This guide outlines how to integrate the ZeroMQ-based order book publisher into your existing trading system architecture.

## Overview

The `OrderBookPublisher` provides a high-performance way to distribute normalized order book data to multiple consumers within your trading system. Key features include:

- Protocol Buffer serialization for efficient, versioned message encoding
- Support for multiple compression algorithms (Snappy, Zstandard, LZ4)
- Back-pressure handling to manage high-volume scenarios
- Configurable publishing rates for traffic shaping
- Heartbeat messages for monitoring publisher health
- Detailed performance statistics for monitoring

## Integration Points

### 1. Data Ingestion Layer Integration

The publisher should be integrated at the output of your order book normalization pipeline:

```
WebSocket Client → Order Book Reconstructor → Normalizer → Publisher
```

#### Key Classes to Interface With:

- `OrderBookNormalizer`: Should call `publishOrderBookUpdate()` after normalization
- `ExchangeManager`: Could own the publisher instance and manage its lifecycle

### 2. Required Dependencies

- ZeroMQ (libzmq and cppzmq)
- Protocol Buffers (protobuf)
- Optional: Compression libraries (Snappy, Zstandard, LZ4)

### 3. Build System Integration

- Add the publisher source files to your build system
- Add Protocol Buffer compilation for the message schemas
- Link against required libraries

## Implementation Steps

### 1. Initialize the Publisher

```cpp
#include "data_ingestion/order_book_publisher.hpp"

// Configure the publisher
trading::data_ingestion::PublisherConfig config;
config.zmq_endpoint = "tcp://*:5555";  // Adjust endpoint as needed
config.max_queue_size = 1000;
config.max_publish_rate = 5000;  // Adjust based on your system's needs
config.compression = trading::data_ingestion::CompressionAlgorithm::ZSTD;

// Create publisher instance
auto publisher = std::make_unique<trading::data_ingestion::OrderBookPublisher>(config);

// Start the publisher
publisher->start();
```

### 2. Integrate with Order Book Normalizer

Modify your `OrderBookNormalizer` to publish updates after normalization:

```cpp
// Inside OrderBookNormalizer::processMarketData or similar method
void OrderBookNormalizer::processMarketData(const MarketDataUpdate& update) {
    // ... existing normalization code ...
    
    // Create normalized order book update
    OrderBookUpdate normalized_update = normalizeUpdate(update);
    
    // Notify callback (existing behavior)
    if (callback_) {
        callback_(normalized_update);
    }
    
    // Publish to ZeroMQ (new behavior)
    if (publisher_) {
        publisher_->publishOrderBookUpdate(normalized_update);
    }
}
```

### 3. Handle Back Pressure

Register a callback to handle back pressure scenarios:

```cpp
publisher->registerBackPressureCallback([this](const OrderBookUpdate& update) {
    // Options for handling back pressure:
    
    // 1. Log the event
    logger_->warn("Back pressure applied, dropping update for {}: {}",
                 update.exchangeId, update.tradingPair);
    
    // 2. Store in a secondary buffer for later processing
    backlog_buffer_.push(update);
    
    // 3. Apply thinning (e.g., for non-critical pairs)
    if (!isHighPriorityPair(update.tradingPair)) {
        // Discard silently
        return;
    }
    
    // 4. Try sending at lower priority
    emergency_publisher_->publishOrderBookUpdate(update);
});
```

### 4. Monitor Publisher Performance

Regularly check publisher statistics for monitoring:

```cpp
// In your monitoring system
void MonitoringSystem::checkPublisherHealth() {
    auto stats = publisher_->getStats();
    
    // Record metrics
    metrics_->gauge("order_book_publisher.queue_size", stats.current_queue_size);
    metrics_->counter("order_book_publisher.messages_published", stats.messages_published);
    metrics_->counter("order_book_publisher.messages_dropped", stats.messages_dropped);
    
    // Check for issues
    if (stats.messages_dropped > 0) {
        logger_->warn("Order book publisher dropping messages: {} dropped", 
                     stats.messages_dropped);
    }
    
    if (stats.current_queue_size > stats.max_queue_size_reached * 0.9) {
        logger_->warn("Order book publisher queue near capacity: {}/{}", 
                     stats.current_queue_size, config_.max_queue_size);
    }
}
```

## Consumer Implementation

Implement subscribers in other components that need order book data:

1. **Strategy Engine**: Subscribe to normalized order book data for computing imbalance metrics
2. **Risk Management**: Subscribe to monitor market conditions
3. **Monitoring System**: Subscribe to track data flow

Example subscriber implementation:
#include <zmq.hpp>
#include "generated/order_book.pb.h"

// Initialize ZeroMQ context and subscriber socket
zmq::context_t context(1);
zmq::socket_t subscriber(context, zmq::socket_type::sub);

// Connect to the publisher
subscriber.connect("tcp://publisher-host:5555");

// Subscribe to specific topics
subscriber.set(zmq::sockopt::subscribe, "market.orderbook.normalized.binance.BTC/USDT");
// Or subscribe to all order books
// subscriber.set(zmq::sockopt::subscribe, "market.orderbook.normalized.");

// Main processing loop
while (running) {
    zmq::message_t topic;
    zmq::message_t message;
    
    // Receive topic
    bool received = subscriber.recv(topic);
    
    // Receive message
    received = subscriber.recv(message);
    
    // Process message
    std::vector<uint8_t> data(
        static_cast<const uint8_t*>(message.data()),
        static_cast<const uint8_t*>(message.data()) + message.size());
    
    // Decompress if needed
    std::vector<uint8_t> decompressed_data = tryDecompress(data);
    
    // Parse Protocol Buffer
    trading::data_ingestion::proto::OrderBookUpdate update;
    if (update.ParseFromArray(decompressed_data.data(), decompressed_data.size())) {
        // Process the order book update
        processOrderBookUpdate(update);
    }
}