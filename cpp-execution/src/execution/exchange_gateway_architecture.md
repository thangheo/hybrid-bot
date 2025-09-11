# Centralized Exchange Gateway Architecture

## Overview
Thay vì để mỗi module (Risk, Portfolio, Execution) gọi exchange APIs trực tiếp, chúng ta tạo một **ExchangeGateway** centralized để:
- Quản lý tất cả exchange connections
- Enforce rate limits globally  
- Cache data để giảm API calls
- Đảm bảo thread safety
- Broadcast data updates to all subscribers

## Architecture Flow

```
┌─────────────────────────────────────────────────────────────┐
│                 APPLICATION LAYER                           │
├─────────────────┬─────────────────┬─────────────────────────┤
│ Risk Management │ Portfolio Mgmt  │ Execution Engine        │
│                 │                 │                         │
│ - Position      │ - Balance       │ - Order Placement       │
│ - Exposure      │ - P&L Calc      │ - Order Tracking        │
│ - Drawdown      │ - Rebalancing   │ - Fill Processing       │
└─────────────────┴─────────────────┴─────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                 EXCHANGE GATEWAY SERVICE                    │
├─────────────────────────────────────────────────────────────┤
│ - Centralized Exchange Access                               │
│ - Global Rate Limiting                                      │
│ - Data Caching & Broadcasting                               │
│ - Connection Pooling                                        │
│ - Automatic Reconnection                                    │
└─────────────────────────────────────────────────────────────┘
                           │
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                 EXCHANGE CLIENTS                            │
├───────────────┬───────────────┬─────────────────────────────┤
│ Binance API   │ Coinbase API  │ Other Exchange APIs         │
│ Client        │ Client        │                             │
└───────────────┴───────────────┴─────────────────────────────┘
```

## Key Components

### 1. ExchangeGateway (Central Hub)
```cpp
class ExchangeGateway {
public:
    // Order operations (for Execution)
    std::future<ExecutionReport> placeOrder(const Order& order);
    std::future<ExecutionReport> cancelOrder(const std::string& orderId);
    
    // Account operations (for Portfolio & Risk)
    std::future<AccountBalances> getAccountBalances(ExchangeId exchange);
    std::future<Position> getPosition(ExchangeId exchange, const Symbol& symbol);
    
    // Market data (for all modules)
    std::future<MarketData> getMarketData(ExchangeId exchange, const Symbol& symbol);
    
    // Subscription for real-time updates
    void subscribeToBalanceUpdates(std::function<void(const BalanceUpdate&)> callback);
    void subscribeToPositionUpdates(std::function<void(const PositionUpdate&)> callback);
    
private:
    std::shared_ptr<ApiClientManager> apiClientManager_;
    std::shared_ptr<RateLimitCoordinator> rateLimitCoordinator_;
    std::shared_ptr<DataCache> dataCache_;
    std::shared_ptr<EventBus> eventBus_;
};
```

### 2. RateLimitCoordinator
```cpp
class RateLimitCoordinator {
public:
    bool canMakeRequest(ExchangeId exchange, RequestType type);
    void recordRequest(ExchangeId exchange, RequestType type);
    std::chrono::milliseconds getWaitTime(ExchangeId exchange, RequestType type);
    
private:
    std::unordered_map<ExchangeId, ExchangeRateLimiter> limiters_;
    std::mutex mutex_;
};
```

### 3. DataCache (Thread-Safe)
```cpp
class DataCache {
public:
    void updateBalance(ExchangeId exchange, const AccountBalances& balances);
    std::optional<AccountBalances> getCachedBalance(ExchangeId exchange);
    
    void updatePosition(ExchangeId exchange, const Position& position);
    std::optional<Position> getCachedPosition(ExchangeId exchange, const Symbol& symbol);
    
private:
    mutable std::shared_mutex balanceMutex_;
    mutable std::shared_mutex positionMutex_;
    
    std::unordered_map<ExchangeId, CachedBalance> balanceCache_;
    std::unordered_map<std::string, CachedPosition> positionCache_;
};
```

## Integration Steps

### Step 1: Create ExchangeGateway Service
1. Implement ExchangeGateway class
2. Move ApiClientManager under ExchangeGateway
3. Add RateLimitCoordinator
4. Add DataCache with TTL

### Step 2: Modify Execution Service
```cpp
class ExecutionService {
public:
    ExecutionService(std::shared_ptr<ExchangeGateway> gateway);
    
    ExecutionResponse executeSignal(const TradingSignal& signal) {
        // Use gateway instead of direct API calls
        auto order = createOrderFromSignal(signal);
        auto result = gateway_->placeOrder(order).get();
        return result;
    }
    
private:
    std::shared_ptr<ExchangeGateway> gateway_;
};
```

### Step 3: Modify Risk Management
```cpp
class PositionTracker {
public:
    PositionTracker(std::shared_ptr<ExchangeGateway> gateway);
    
    std::vector<Position> getCurrentPositions() {
        // Get from cache first, then API if needed
        return gateway_->getAllPositions();
    }
    
private:
    std::shared_ptr<ExchangeGateway> gateway_;
};
```

### Step 4: Modify Portfolio Management
```cpp
class PortfolioManager {
public:
    PortfolioManager(std::shared_ptr<ExchangeGateway> gateway);
    
    AccountBalances getBalances() {
        // Get from gateway (cache + API)
        return gateway_->getAccountBalances().get();
    }
    
private:
    std::shared_ptr<ExchangeGateway> gateway_;
};
```

## Thread Safety Strategy

### 1. Read-Write Locks for Cache
```cpp
class DataCache {
private:
    mutable std::shared_mutex dataMutex_;
    
public:
    std::optional<Data> read() const {
        std::shared_lock lock(dataMutex_);
        return cachedData_;
    }
    
    void write(const Data& data) {
        std::unique_lock lock(dataMutex_);
        cachedData_ = data;
        notifySubscribers();
    }
};
```

### 2. Async Event Broadcasting
```cpp
class ExchangeGateway {
    void broadcastBalanceUpdate(const BalanceUpdate& update) {
        // Async notification to avoid blocking
        eventBus_->publishAsync(update);
    }
};
```

### 3. Connection Pooling
```cpp
class ApiClientManager {
private:
    std::unordered_map<ExchangeId, ConnectionPool> connectionPools_;
    
public:
    std::shared_ptr<ExchangeApiClient> getClient(ExchangeId exchange) {
        return connectionPools_[exchange].getConnection();
    }
};
```

## Data Consistency Strategy

### 1. Cache Invalidation
```cpp
class DataCache {
public:
    void invalidateAfter(std::chrono::seconds ttl) {
        auto now = std::chrono::steady_clock::now();
        if (now - lastUpdate_ > ttl) {
            clearCache();
        }
    }
};
```

### 2. Reconciliation Service
```cpp
class DataReconciliationService {
public:
    void reconcilePositions() {
        // Compare cached vs fresh API data
        // Report discrepancies
        // Update cache if needed
    }
};
```

## Benefits

1. **Centralized Control**: Tất cả exchange access qua một điểm
2. **Rate Limit Safety**: Global coordination tránh violations
3. **Data Consistency**: Single source of truth với caching
4. **Thread Safety**: Proper synchronization mechanisms
5. **Performance**: Caching giảm unnecessary API calls
6. **Monitoring**: Central logging và metrics
7. **Resilience**: Automatic retry và error handling

## Implementation Priority

1. **Phase 1**: ExchangeGateway skeleton + basic caching
2. **Phase 2**: Integration với Execution Service
3. **Phase 3**: Integration với Risk Management
4. **Phase 4**: Integration với Portfolio Management
5. **Phase 5**: Advanced features (reconciliation, monitoring)
