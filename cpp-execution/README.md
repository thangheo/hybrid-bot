# Crypto Trading System - Data Ingestion & Processing Layer

This directory contains the implementation of the Data Ingestion & Processing Layer for the Crypto Trading System.

## Overview

The Data Ingestion & Processing Layer is responsible for:

1. **Real-Time Market Data Collection**
   - WebSocket and REST API connections to exchanges
   - Connection pooling and failover mechanisms
   - Data source management and coordination

2. **Protocol Adaptation**
   - Exchange-specific message format conversion
   - Standardization of data formats

3. **Market Data Normalization**
   - Order book reconstruction
   - Timestamp synchronization
   - Cross-exchange data normalization
   - Data quality control

4. **Additional Data Sources**
   - News and events integration
   - On-chain data collection
   - Sentiment analysis

## Building the Project

```bash
mkdir build && cd build
cmake ..
make
```

## Running Tests

```bash
cd build
make test
```

## Directory Structure

- `include/data_ingestion/`: Header files
- `src/data_ingestion/`: Implementation files
- `tests/data_ingestion/`: Unit and integration tests
- `config/data_ingestion/`: Configuration files
- `docker/data_ingestion/`: Docker and Kubernetes configurations
- `python/`: Python scripts for research and visualization

## Dependencies

- Boost.Asio for async I/O
- WebSocketpp or Beast for WebSocket connections
- ZeroMQ for message distribution
- InfluxDB client for time-series data storage
- Protocol Buffers or FlatBuffers for message serialization
