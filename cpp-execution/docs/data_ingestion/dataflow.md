Based on the code we've been working with, here's an overview of the data flow through the data ingestion module with the normalization pipeline:

## Data Ingestion Module Flow

### 1. WebSocket/REST Data Collection

The flow begins with receiving raw market data from cryptocurrency exchanges:

1. **WebSocket Clients** connect to exchange APIs (Binance, FTX, etc.)
2. **Connection Manager** handles connection pools and authentication
3. **REST Layer** provides fallback when WebSocket connections fail
4. Raw market data is collected in `MarketDataUpdate` format

### 2. Normalization Pipeline

The market data then flows through a multi-stage pipeline:

1. **Protocol Adapter Stage**
   - Takes raw `MarketDataUpdate` from WebSocket/REST clients
   - Uses exchange-specific adapters to convert to standardized format
   - Normalizes trading pair format (e.g., "BTCUSDT" → "BTC/USDT")
   - Outputs standardized `OrderBookUpdate`

2. **Order Book Reconstruction Stage**
   - Updates in-memory order book state with new data
   - Maintains full order book for each trading pair
   - Preserves price level information with quantities
   - Passes updated `OrderBookUpdate` to next stage

3. **Data Quality Stage**
   - Applies various quality filters (checksum, price range, sequence gaps)
   - Performs validation on data structure and values
   - Detects anomalies in prices, spreads, and volumes
   - Rejects or flags problematic updates
   - Forwards validated `OrderBookUpdate` to next stage

4. **Timestamp Synchronization Stage**
   - Adds processing timestamp
   - Adjusts exchange timestamps for clock drift
   - Ensures consistent timing across exchanges
   - Forwards time-normalized `OrderBookUpdate` to next stage

5. **Publisher Stage**
   - Distributes normalized order book data to consumers
   - Publishes via ZeroMQ or other message bus
   - May store data in time-series database
   - Creates snapshots periodically for complete state

### 3. Data Distribution

The normalized data is then made available for other system components:

1. **Strategy Engine** subscribes to normalized order book data
2. **Trading Performance Components** monitor data quality and metrics
3. **Analytics Components** may store historical data
4. **Monitoring Systems** track system performance and data flow

## Key Components in the Flow

- **ThreadSafeQueues**: Connect pipeline stages and handle backpressure
- **ExchangeAdapters**: Convert exchange-specific formats (e.g., BinanceAdapter)
- **OrderBookManager**: Maintains in-memory order book state
- **DataQualityControl**: Applies filters and validation rules
- **TimeSync**: Handles timestamp synchronization
- **OrderBookPublisher**: Distributes normalized data
- **PipelineMetrics**: Tracks performance and data statistics

## Data Flow Diagram

```
[Exchange WebSockets/REST APIs]
           ↓
[MarketDataUpdate] → ThreadSafeQueue
           ↓
[Protocol Adapter Stage] → Convert to standard format
           ↓
[OrderBookUpdate] → ThreadSafeQueue
           ↓
[Order Book Stage] → Update in-memory order books
           ↓
[OrderBookUpdate] → ThreadSafeQueue
           ↓
[Data Quality Stage] → Apply quality checks
           ↓
[OrderBookUpdate] → ThreadSafeQueue
           ↓
[Timestamp Stage] → Synchronize timestamps
           ↓
[OrderBookUpdate] → ThreadSafeQueue
           ↓
[Publisher Stage] → Distribute data
           ↓
[ZeroMQ/Message Bus]
           ↓
[Strategy Engine, Monitoring, Storage, etc.]
```

This architecture provides a robust, extensible system for processing cryptocurrency market data. The pipeline design ensures separation of concerns while the threading model allows for efficient parallel processing. The quality controls ensure that bad data doesn't propagate to trading strategies.