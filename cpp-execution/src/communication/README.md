# Generic ZeroMQ Server for Trading System

A production-ready, high-performance ZeroMQ server designed for crypto trading system communications. This generic server replaces the original intelligence-specific server and provides a unified communication layer for all trading system components.

## Features

### 🚀 High Performance
- **Multi-threaded Architecture**: Separate threads for publishing, request handling, monitoring, and heartbeats
- **Lock-free Queuing**: Minimizes contention between publisher threads
- **Priority-based Message Processing**: Critical messages are processed first
- **Configurable Threading**: Adjustable thread pools for different workloads
- **Zero-copy Messaging**: Efficient memory usage and data transfer

### 🔧 Production Ready
- **Comprehensive Error Handling**: Robust error recovery and reconnection logic
- **Health Monitoring**: Built-in health checks and system metrics
- **Circuit Breaker Pattern**: Prevents cascading failures
- **Graceful Shutdown**: Clean resource cleanup and thread management
- **Connection Pooling**: Efficient resource utilization

### 📊 Monitoring & Observability
- **Real-time Statistics**: Message counts, throughput, queue utilization
- **Heartbeat System**: Regular health broadcasts
- **Structured Logging**: JSON-formatted logs with correlation IDs
- **Performance Metrics**: Latency tracking and error rates
- **Queue Management**: Overflow detection and backpressure handling

### 🎯 Trading System Integration
- **Topic-based Publishing**: Hierarchical topic structure for different data types
- **Request-Reply Pattern**: Synchronous communication for critical operations
- **Message Metadata**: Rich context information with each message
- **Type-safe Templates**: C++17/20 template support for typed messages
- **Protocol Buffer Support**: Native protobuf serialization

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                Generic ZMQ Server                            │
│                                                             │
│  ┌───────────────┐    ┌───────────────┐    ┌─────────────┐  │
│  │   Publisher   │    │   Request     │    │ Monitoring  │  │
│  │   Threads     │    │   Handler     │    │   Thread    │  │
│  │   (2-8)       │    │   Threads     │    │             │  │
│  └───────┬───────┘    └───────┬───────┘    └──────┬──────┘  │
│          │                    │                   │         │
│          └────────────┬───────┴─────────┬─────────┘         │
│                       │                 │                   │
│    ┌─────────────────▼─────────────────▼─────────────────┐  │
│    │            Message Queue System                     │  │
│    │          (Priority + FIFO Queues)                  │  │
│    └─────────────────────────────────────────────────────┘  │
│                                                             │
│    ┌─────────────────┐              ┌─────────────────────┐ │
│    │   PUB Socket    │              │   ROUTER Socket     │ │
│    │  (Publishers)   │              │  (Request/Reply)    │ │
│    └─────────┬───────┘              └─────────┬───────────┘ │
└──────────────┼──────────────────────────────────┼───────────┘
               │                                  │
         ┌─────▼──────┐                    ┌─────▼──────┐
         │ Trading    │                    │ Risk       │
         │ Components │                    │ Management │
         │ (SUB)      │                    │ (REQ)      │
         └────────────┘                    └────────────┘
```

## Topic Hierarchy

The server uses a hierarchical topic structure for organized message routing:

```
market.orderbook.{exchange}.{symbol}      # Order book updates
market.trades.{exchange}.{symbol}         # Trade executions
strategy.signals.{symbol}                 # Trading signals
strategy.parameters.{strategy_id}         # Strategy configuration
risk.events.{symbol}                      # Risk management events
risk.positions.{exchange}                 # Position updates
execution.orders.{order_id}               # Order lifecycle events
execution.fills.{order_id}                # Fill notifications
system.heartbeat                          # Server health
system.metrics.{component}                # Component statistics
system.alerts.{severity}                  # System alerts
```

## Quick Start

### 1. Server Setup

```cpp
#include "generic_zmq_server.hpp"

using namespace trading::communication;

// Create configuration
ZmqServerConfig config;
config.publisher_endpoint = "tcp://*:5555";
config.request_endpoint = "tcp://*:5556";
config.publisher_threads = 4;
config.request_handler_threads = 2;
config.max_queue_size = 100000;
config.enable_monitoring = true;
config.enable_heartbeat = true;

// Create and start server
GenericZmqServer server(config);

// Register request handlers
server.registerRequestHandler("risk_validation", [](const std::string& data, auto metadata) {
    // Process risk validation request
    nlohmann::json request = nlohmann::json::parse(data);
    nlohmann::json response;
    response["approved"] = true;
    response["reason"] = "Order approved";
    return response.dump();
});

// Start server
if (server.start()) {
    std::cout << "Server started successfully!" << std::endl;
} else {
    std::cerr << "Failed to start server" << std::endl;
}
```

### 2. Publishing Messages

```cpp
// Publish order book data
nlohmann::json orderbook;
orderbook["symbol"] = "BTC/USDT";
orderbook["exchange"] = "binance";
orderbook["bids"] = {{{"price", 45000.0}, {"quantity", 1.5}}};
orderbook["asks"] = {{{"price", 45001.0}, {"quantity", 2.0}}};

server.publishAsync("market.orderbook.binance.BTC/USDT", 
                   orderbook.dump(), 
                   MessagePriority::HIGH);

// Publish with metadata
std::unordered_map<std::string, std::string> metadata;
metadata["source"] = "strategy_engine";
metadata["version"] = "1.0";

server.publishWithMetadata("strategy.signals.BTC/USDT", 
                          signal_data, 
                          metadata, 
                          MessagePriority::HIGH);

// Type-safe publishing
struct TradingSignal {
    std::string symbol;
    std::string direction;
    double strength;
};

TradingSignal signal{"BTC/USDT", "buy", 0.85};
server.publish("strategy.signals.BTC/USDT", signal, MessagePriority::HIGH);
```

### 3. Client Usage

```cpp
#include "zmq_client.hpp"

using namespace trading::communication;

// Create client
ZmqClientConfig config;
config.server_pub_endpoint = "tcp://localhost:5555";
config.server_req_endpoint = "tcp://localhost:5556";

GenericZmqClient client(config);

// Connect to server
if (!client.connect()) {
    std::cerr << "Failed to connect to server" << std::endl;
    return 1;
}

// Subscribe to market data
client.subscribe("market.orderbook.*", [](const std::string& topic, 
                                         const std::string& data,
                                         const auto& metadata) {
    std::cout << "Received orderbook update for " << topic << std::endl;
    auto orderbook = nlohmann::json::parse(data);
    // Process orderbook data...
});

// Send request
std::unordered_map<std::string, std::string> metadata;
metadata["order_id"] = "order_123";

std::string response = client.sendRequest("risk_validation", 
                                        order_data,
                                        metadata);

auto result = nlohmann::json::parse(response);
if (result["approved"].get<bool>()) {
    std::cout << "Order approved!" << std::endl;
}
```

## Configuration Options

### Server Configuration

```cpp
struct ZmqServerConfig {
    // Network endpoints
    std::string publisher_endpoint = "tcp://*:5555";
    std::string request_endpoint = "tcp://*:5556";
    
    // Socket options
    int publisher_hwm = 10000;         // High water mark
    int publisher_linger = 1000;       // Linger time (ms)
    bool publisher_immediate = true;   // Send immediately
    
    // Threading configuration
    int publisher_threads = 2;         // Publisher worker threads
    int request_handler_threads = 4;   // Request handler threads
    int io_threads = 2;                // ZMQ I/O threads
    
    // Queue management
    size_t max_queue_size = 100000;    // Maximum queued messages
    std::chrono::milliseconds queue_timeout{100};  // Queue wait timeout
    
    // Monitoring
    bool enable_monitoring = true;     // Enable statistics collection
    std::chrono::seconds stats_interval{60};  // Statistics logging interval
    
    // Reliability
    bool enable_heartbeat = true;      // Enable heartbeat messages
    std::chrono::seconds heartbeat_interval{30};  // Heartbeat frequency
    int max_reconnect_attempts = 5;    // Reconnection attempts
    std::chrono::milliseconds reconnect_delay{1000};  // Reconnection delay
};
```

### Client Configuration

```cpp
struct ZmqClientConfig {
    std::string server_pub_endpoint = "tcp://localhost:5555";
    std::string server_req_endpoint = "tcp://localhost:5556";
    std::chrono::milliseconds request_timeout{30000};  // Request timeout
    int io_threads = 1;                // ZMQ I/O threads
    bool auto_reconnect = true;        // Enable auto-reconnection
    std::chrono::milliseconds reconnect_interval{5000};  // Reconnect frequency
};
```

## Message Priorities

The server supports four priority levels for message processing:

```cpp
enum class MessagePriority {
    LOW = 0,        // Background data, analytics
    NORMAL = 1,     // Regular market data
    HIGH = 2,       // Trading signals, order events
    CRITICAL = 3    // Risk events, system alerts
};
```

Higher priority messages are processed first, ensuring critical data reaches consumers immediately.

## Error Handling

### Server-side Error Handling

```cpp
// Check server health
if (!server.healthCheck()) {
    std::cout << "Server health check failed!" << std::endl;
    std::cout << "Last error: " << server.getLastError() << std::endl;
}

// Monitor statistics
auto stats = server.getStatistics();
std::cout << "Messages published: " << stats.messages_published << std::endl;
std::cout << "Queue utilization: " << server.getQueueUtilization() * 100 << "%" << std::endl;

// Handle queue overflow
if (server.getQueueUtilization() > 0.9) {
    std::cout << "Warning: Queue utilization high!" << std::endl;
    // Consider reducing message rate or scaling
}
```

### Client-side Error Handling

```cpp
// Check connection status
if (!client.isConnected()) {
    std::cout << "Client not connected, attempting reconnection..." << std::endl;
    client.connect();
}

// Handle request failures
std::string response = client.sendRequest("risk_validation", data);
if (response.empty()) {
    std::cout << "Request failed: " << client.getLastError() << std::endl;
}

// Monitor client statistics
auto stats = client.getStats();
std::cout << "Messages received: " << stats.messages_received << std::endl;
std::cout << "Request success rate: " 
          << (double)stats.requests_sent / (stats.requests_sent + stats.requests_failed)
          << std::endl;
```

## Performance Tuning

### Threading Configuration

```cpp
// CPU-bound workloads
config.publisher_threads = std::thread::hardware_concurrency();
config.request_handler_threads = std::thread::hardware_concurrency() / 2;

// I/O-bound workloads
config.publisher_threads = 2;
config.request_handler_threads = std::thread::hardware_concurrency() * 2;

// Memory-bound workloads
config.max_queue_size = 50000;  // Reduce memory usage
config.publisher_hwm = 5000;    // Reduce socket buffer
```

### Network Optimization

```cpp
// Low latency configuration
config.publisher_immediate = true;   // Send immediately
config.publisher_linger = 0;        // Don't wait on close
config.queue_timeout = std::chrono::milliseconds(10);  // Fast queue ops

// High throughput configuration
config.publisher_hwm = 50000;       // Large socket buffer
config.max_queue_size = 500000;     // Large internal queue
config.publisher_threads = 8;       // More workers
```

## Integration Examples

### Risk Management Integration

```cpp
// Risk validation handler
server.registerRequestHandler("risk_validation", 
    [&risk_manager](const std::string& data, const auto& metadata) {
        auto request = nlohmann::json::parse(data);
        
        RiskValidationRequest risk_req;
        risk_req.order_id = request["order_id"];
        risk_req.symbol = request["symbol"];
        risk_req.quantity = request["quantity"];
        risk_req.price = request["price"];
        
        auto result = risk_manager.validateOrder(risk_req);
        
        nlohmann::json response;
        response["approved"] = result.approved;
        response["reason"] = result.reason;
        response["adjusted_size"] = result.adjusted_size;
        
        return response.dump();
    });
```

### Strategy Engine Integration

```cpp
// Publish strategy signals
void publishSignal(const TradingSignal& signal) {
    nlohmann::json signal_data;
    signal_data["signal_id"] = signal.signal_id;
    signal_data["symbol"] = signal.symbol;
    signal_data["direction"] = signal.direction;
    signal_data["strength"] = signal.strength;
    signal_data["timestamp"] = signal.timestamp;
    
    std::string topic = "strategy.signals." + signal.symbol;
    server.publishAsync(topic, signal_data.dump(), MessagePriority::HIGH, signal.signal_id);
}

// Subscribe to parameter updates
client.subscribe("strategy.parameters.*", 
    [&strategy](const std::string& topic, const std::string& data, const auto& metadata) {
        auto params = nlohmann::json::parse(data);
        strategy.updateParameters(params);
    });
```

### Execution Engine Integration

```cpp
// Publish order events
void publishOrderUpdate(const OrderUpdate& update) {
    nlohmann::json order_data;
    order_data["order_id"] = update.order_id;
    order_data["status"] = update.status;
    order_data["filled_quantity"] = update.filled_quantity;
    order_data["remaining_quantity"] = update.remaining_quantity;
    
    std::string topic = "execution.orders." + update.order_id;
    server.publishAsync(topic, order_data.dump(), MessagePriority::HIGH);
}
```

## Building and Dependencies

### Requirements

- C++17 or C++20 compiler (GCC 8+, Clang 9+, MSVC 2019+)
- ZeroMQ 4.3+ (`libzmq-dev`)
- cppzmq (`libzmq3-dev`)  
- nlohmann/json (`nlohmann-json3-dev`)
- CMake 3.15+

### CMake Configuration

```cmake
# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(generic_zmq_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)

find_package(nlohmann_json REQUIRED)

add_executable(zmq_server_example
    generic_zmq_server.cpp
    zmq_client.cpp
    zmq_server_example.cpp
)

target_link_libraries(zmq_server_example
    ${ZMQ_LIBRARIES}
    nlohmann_json::nlohmann_json
    pthread
)

target_include_directories(zmq_server_example PRIVATE ${ZMQ_INCLUDE_DIRS})
target_compile_options(zmq_server_example PRIVATE ${ZMQ_CFLAGS_OTHER})
```

### Building

```bash
# Install dependencies (Ubuntu/Debian)
sudo apt-get install libzmq3-dev nlohmann-json3-dev

# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run example
./zmq_server_example
```

## Testing

The package includes comprehensive tests and examples:

```bash
# Run server example
./zmq_server_example

# In another terminal, run client tests
./zmq_client_test

# Monitor with htop to see threading behavior
htop
```

## Production Deployment

### Docker Configuration

```dockerfile
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libzmq5 \
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

COPY generic_zmq_server /usr/local/bin/
COPY config.json /etc/trading/

EXPOSE 5555 5556

CMD ["/usr/local/bin/generic_zmq_server", "/etc/trading/config.json"]
```

### Kubernetes Deployment

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: zmq-server
spec:
  replicas: 2
  selector:
    matchLabels:
      app: zmq-server
  template:
    metadata:
      labels:
        app: zmq-server
    spec:
      containers:
      - name: zmq-server
        image: trading/zmq-server:latest
        ports:
        - containerPort: 5555
        - containerPort: 5556
        resources:
          requests:
            memory: "512Mi"
            cpu: "500m"
          limits:
            memory: "2Gi"
            cpu: "2000m"
---
apiVersion: v1
kind: Service
metadata:
  name: zmq-server-service
spec:
  selector:
    app: zmq-server
  ports:
  - name: pub
    port: 5555
    targetPort: 5555
  - name: req
    port: 5556
    targetPort: 5556
  type: LoadBalancer
```

## Monitoring and Observability

### Prometheus Metrics

The server can export metrics to Prometheus:

```cpp
// Enable metrics export
server.publishAsync("system.metrics.prometheus", metrics.dump(), MessagePriority::LOW);
```

### Grafana Dashboard

Import the provided dashboard configuration for real-time monitoring:

- Message throughput rates
- Queue utilization trends
- Error rate percentages  
- Latency distributions
- Connection health status

## Migration Guide

### From Original Intelligence Server

1. **Replace includes**:
   ```cpp
   // Old
   #include "communication/zmq_server.hpp"
   
   // New  
   #include "generic_zmq_server.hpp"
   ```

2. **Update server creation**:
   ```cpp
   // Old
   TradingCommunicationServer server(config);
   
   // New
   GenericZmqServer server(config);
   ```

3. **Update publishing calls**:
   ```cpp
   // Old
   server.PublishMarketIntelligence(intel);
   
   // New
   server.publishAsync("market.intelligence", intel.SerializeAsString());
   ```

4. **Update topic structure**:
   ```cpp
   // Old: Single "MARKET_INTEL" topic
   
   // New: Hierarchical topics
   "market.orderbook.binance.BTC/USDT"
   "market.trades.coinbase.ETH/USDT" 
   "strategy.signals.BTC/USDT"
   ```

## Troubleshooting

### Common Issues

**Server won't start**
```bash
# Check if ports are available
netstat -tulpn | grep :5555
netstat -tulpn | grep :5556

# Check ZeroMQ installation
pkg-config --cflags --libs libzmq
```

**High queue utilization**
```cpp
// Monitor queue metrics
if (server.getQueueUtilization() > 0.8) {
    // Scale up publisher threads
    // Reduce message rate
    // Increase queue size
}
```

**Message loss**
```cpp
// Check for dropped messages
auto stats = server.getStatistics();
if (stats.messages_dropped > 0) {
    // Increase queue size
    // Add more publisher threads
    // Review message priorities
}
```

**Client connection issues**
```cpp
// Enable auto-reconnection
ZmqClientConfig config;
config.auto_reconnect = true;
config.reconnect_interval = std::chrono::seconds(5);
```

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes with tests
4. Submit a pull request

## Support

For questions and support:
- Create an issue on GitHub
- Check the troubleshooting section
- Review the example code

---

**Note**: This server is designed for high-frequency trading systems where performance and reliability are critical. Ensure proper testing in your specific environment before production deployment.
