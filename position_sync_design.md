# Position Synchronization System Design

## Vấn đề hiện tại
- Khi strategy muốn exit position nhưng risk module không tìm thấy position
- Độ trễ từ vài giây đến vài chục giây
- Gây ra hậu quả nghiêm trọng trong trading

## Thiết kế giải pháp

### 1. Position Publisher Service
Tạo một service chuyên publish position updates qua ZMQ:

```
Position Event Flow:
Strategy/ExecutionService → PositionTracker → PositionPublisher → ZMQ Bus → Position Subscribers
```

### 2. ZMQ Topics cho Position Events
```cpp
// Thêm vào zmq_server_client.hpp
const std::string POSITION_UPDATES_TOPIC = "position.*";
const std::string POSITION_PUBLISH_ENDPOINT = "inproc://position_pub"; 
const std::string POSITION_REQUEST_ENDPOINT = "inproc://position_req";
```

### 3. Position Event Types
```cpp
enum class PositionEventType {
    POSITION_OPENED,    // Khi mở position mới
    POSITION_UPDATED,   // Khi update quantity/price  
    POSITION_CLOSED,    // Khi đóng position
    POSITION_RECONCILED // Khi reconcile với exchange
};

struct PositionEvent {
    std::string eventId;
    PositionEventType eventType;
    std::string symbol;
    std::string exchangeId; 
    std::string strategyId;
    double quantity;
    double avgPrice;
    double unrealizedPnl;
    std::chrono::system_clock::time_point timestamp;
    std::string metadata; // JSON cho thông tin bổ sung
};
```

### 4. Components cần implement

#### A. PositionPublisher 
- Subscribe vào PositionTracker callbacks
- Publish position events lên ZMQ bus
- Handle different event types
- Maintain event ordering

#### B. PositionSubscriber (cho Risk module)
- Subscribe to position events từ ZMQ bus
- Maintain local position cache đồng bộ với PositionTracker
- Provide quick position lookup cho risk checks

#### C. Position Synchronizer
- Periodic reconciliation between modules
- Handle network issues và message loss
- Provide health checks

### 5. Integration Points

#### Strategy Module
```cpp
// Khi strategy tạo/update position
void Strategy::updatePosition(...) {
    // Update internal position
    // Trigger position event via PositionTracker
    positionTracker_->processExecutionReport(executionReport);
}
```

#### Risk Module  
```cpp
// Khi risk cần check position
bool RiskManager::hasPosition(const std::string& symbol) {
    // Check local synchronized cache first (fast)
    if (positionSubscriber_->hasPosition(symbol)) {
        return true;
    }
    
    // Fallback to direct PositionTracker query if needed
    return positionTracker_->getPosition(exchangeId, tradingPair).has_value();
}
```

### 6. Performance Optimizations

#### Message Batching
- Batch multiple position updates trong short time window
- Reduce ZMQ message overhead

#### Priority Levels  
- CRITICAL: Position closed/opened
- NORMAL: Position quantity updates
- LOW: Reconciliation events

#### Caching Strategy
- Local cache tại subscriber với TTL
- Immediate cache invalidation on critical events
- Periodic full sync để đảm bảo consistency

### 7. Error Handling & Resilience

#### Message Ordering
- Sequence numbers trong position events
- Out-of-order detection và handling

#### Network Failures
- Event replay mechanism
- Subscriber reconnection logic
- Graceful degradation khi ZMQ unavailable

#### Data Consistency
- Checksum/hash cho position state
- Periodic full state sync
- Conflict resolution strategies

## Implementation Plan

### Phase 1: Basic Position Publishing
1. Extend PositionTracker với publish capabilities
2. Create PositionPublisher service
3. Add ZMQ topics và endpoints
4. Basic position event publishing

### Phase 2: Risk Module Integration  
1. Create PositionSubscriber
2. Integrate với RiskManager
3. Local cache implementation
4. Basic synchronization

### Phase 3: Advanced Features
1. Message batching và priority
2. Error handling & resilience
3. Performance monitoring
4. Health checks

### Phase 4: Production Hardening
1. Comprehensive testing
2. Performance tuning
3. Monitoring & alerting
4. Documentation

## Expected Benefits

1. **Reduced Latency**: Position lookups từ local cache thay vì cross-module calls
2. **Better Consistency**: Real-time position synchronization
3. **Improved Reliability**: Redundant position tracking
4. **Easier Debugging**: Centralized position event logging
5. **Scalability**: Có thể add thêm subscribers dễ dàng

## Configuration Example

```yaml
position_sync:
  publisher:
    endpoint: "inproc://position_pub"
    batch_size: 10
    batch_timeout_ms: 100
    priority_levels: true
    
  subscriber:
    endpoints: ["inproc://position_pub"]
    cache_ttl_ms: 30000
    max_queue_size: 1000
    
  reconciliation:
    interval_seconds: 300
    timeout_ms: 5000
    max_retries: 3
```