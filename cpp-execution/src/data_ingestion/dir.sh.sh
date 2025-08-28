#!/bin/bash

# Script to generate Data Ingestion & Processing Layer file structure
# Created on: April 30, 2025

# Base directories
BASE_DIR="$(pwd)/crypto_trading_system"
INCLUDE_DIR="$BASE_DIR/include"
SRC_DIR="$BASE_DIR/src"
TESTS_DIR="$BASE_DIR/tests"
CONFIG_DIR="$BASE_DIR/config"
DOCKER_DIR="$BASE_DIR/docker"
PYTHON_DIR="$BASE_DIR/python"
SCRIPTS_DIR="$BASE_DIR/scripts"

# Colors for terminal output
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to create directory and print status
create_dir() {
    mkdir -p "$1"
    echo -e "${GREEN}Created directory:${NC} $1"
}

# Function to create file with header comment
create_file() {
    local file_path="$1"
    local description="$2"
    local file_type="$3"
    
    # Create directory if it doesn't exist
    mkdir -p "$(dirname "$file_path")"
    
    # Create file with appropriate header based on file type
    if [[ "$file_type" == "cpp" || "$file_type" == "hpp" ]]; then
        cat > "$file_path" << EOF
/**
 * @file $(basename "$file_path")
 * @brief $description
 * 
 * @details 
 * Part of the Data Ingestion & Processing Layer for the Crypto Trading System.
 * 
 * Created on: $(date +"%B %d, %Y")
 */

EOF
        # Add include guards for header files
        if [[ "$file_type" == "hpp" ]]; then
            guard_name=$(basename "$file_path" | tr '[:lower:].' '[:upper:]_')
            cat >> "$file_path" << EOF
#ifndef ${guard_name}
#define ${guard_name}

// TODO: Implement $description

#endif // ${guard_name}
EOF
        else
            cat >> "$file_path" << EOF
// TODO: Implement $description
EOF
        fi
    elif [[ "$file_type" == "py" ]]; then
        cat > "$file_path" << EOF
#!/usr/bin/env python3
"""
$description

Part of the Data Ingestion & Processing Layer for the Crypto Trading System.

Created on: $(date +"%B %d, %Y")
"""

# TODO: Implement $description

def main():
    pass

if __name__ == "__main__":
    main()
EOF
        chmod +x "$file_path"
    elif [[ "$file_type" == "yaml" || "$file_type" == "json" ]]; then
        cat > "$file_path" << EOF
# $description
# Created on: $(date +"%B %d, %Y")

# TODO: Add configuration settings
EOF
    else
        cat > "$file_path" << EOF
# $description
# Created on: $(date +"%B %d, %Y")

# TODO: Implement $description
EOF
    fi
    
    echo -e "${GREEN}Created file:${NC} $file_path"
}

echo -e "${YELLOW}Creating directory structure...${NC}"

# Create base directories
create_dir "$BASE_DIR"
create_dir "$INCLUDE_DIR/data_ingestion"
create_dir "$SRC_DIR/data_ingestion"
create_dir "$TESTS_DIR/data_ingestion"
create_dir "$CONFIG_DIR/data_ingestion"
create_dir "$DOCKER_DIR/data_ingestion"
create_dir "$DOCKER_DIR/kubernetes/data_ingestion"
create_dir "$PYTHON_DIR/research"
create_dir "$PYTHON_DIR/visualization"
create_dir "$SCRIPTS_DIR/monitoring"

echo -e "${YELLOW}Generating files for Core Infrastructure...${NC}"

# Core Infrastructure
create_file "$INCLUDE_DIR/data_ingestion/config.hpp" "Configuration structures for all data ingestion components" "hpp"
create_file "$INCLUDE_DIR/data_ingestion/types.hpp" "Common data structures used throughout the data ingestion layer" "hpp"
create_file "$INCLUDE_DIR/data_ingestion/exceptions.hpp" "Custom exception classes for data ingestion errors" "hpp"
create_file "$INCLUDE_DIR/data_ingestion/logging.hpp" "Specialized logging functionality for data ingestion components" "hpp"
create_file "$SRC_DIR/data_ingestion/logging.cpp" "Implementation of logging functionality for data ingestion components" "cpp"

echo -e "${YELLOW}Generating files for Real-Time Market Data Collection...${NC}"

# Real-Time Market Data Collection
create_file "$INCLUDE_DIR/data_ingestion/websocket/client_interface.hpp" "Abstract interface for WebSocket client implementations" "hpp"
create_file "$INCLUDE_DIR/data_ingestion/websocket/client_base.hpp" "Base implementation of WebSocket client with common functionality" "hpp"
create_file "$SRC_DIR/data_ingestion/websocket/client_base.cpp" "Implementation of base WebSocket client functionality" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/websocket/connection_pool.hpp" "Management of multiple WebSocket connections" "hpp"
create_file "$SRC_DIR/data_ingestion/websocket/connection_pool.cpp" "Implementation of WebSocket connection pool" "cpp"

# Exchange-specific WebSocket clients
for exchange in binance coinbase ftx; do
    create_file "$INCLUDE_DIR/data_ingestion/websocket/exchanges/${exchange}_client.hpp" "${exchange^}-specific WebSocket client implementation" "hpp"
    create_file "$SRC_DIR/data_ingestion/websocket/exchanges/${exchange}_client.cpp" "Implementation of ${exchange^}-specific WebSocket client" "cpp"
done

# REST API clients
create_file "$INCLUDE_DIR/data_ingestion/rest/client_interface.hpp" "Abstract interface for REST API client implementations" "hpp"
create_file "$SRC_DIR/data_ingestion/rest/client_interface.cpp" "Implementation of REST API client interface" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/rest/client_base.hpp" "Base implementation of REST client with common functionality" "hpp"
create_file "$SRC_DIR/data_ingestion/rest/client_base.cpp" "Implementation of base REST client functionality" "cpp"

# Exchange-specific REST clients
for exchange in binance coinbase ftx; do
    create_file "$INCLUDE_DIR/data_ingestion/rest/exchanges/${exchange}_client.hpp" "${exchange^}-specific REST client implementation" "hpp"
    create_file "$SRC_DIR/data_ingestion/rest/exchanges/${exchange}_client.cpp" "Implementation of ${exchange^}-specific REST client" "cpp"
done

create_file "$INCLUDE_DIR/data_ingestion/data_source_manager.hpp" "Coordination of data collection across different sources" "hpp"
create_file "$SRC_DIR/data_ingestion/data_source_manager.cpp" "Implementation of data source manager" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/connection_monitor.hpp" "Monitoring service that tracks health of all connections" "hpp"
create_file "$SRC_DIR/data_ingestion/connection_monitor.cpp" "Implementation of connection monitoring service" "cpp"

echo -e "${YELLOW}Generating files for Protocol Adapters...${NC}"

# Protocol Adapters
create_file "$INCLUDE_DIR/data_ingestion/protocol/adapter_interface.hpp" "Abstract interface for protocol adapter implementations" "hpp"

for exchange in binance coinbase ftx; do
    create_file "$INCLUDE_DIR/data_ingestion/protocol/${exchange}_adapter.hpp" "${exchange^}-specific message format conversion" "hpp"
    create_file "$SRC_DIR/data_ingestion/protocol/${exchange}_adapter.cpp" "Implementation of ${exchange^}-specific message format conversion" "cpp"
done

echo -e "${YELLOW}Generating files for Market Data Normalization...${NC}"

# Market Data Normalization
create_file "$INCLUDE_DIR/data_ingestion/orderbook/reconstructor.hpp" "Core order book reconstruction logic" "hpp"
create_file "$SRC_DIR/data_ingestion/orderbook/reconstructor.cpp" "Implementation of order book reconstruction" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/orderbook/book.hpp" "Data structure for efficient order book representation" "hpp"
create_file "$SRC_DIR/data_ingestion/orderbook/book.cpp" "Implementation of order book data structure" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/orderbook/level.hpp" "Implementation of price level in the order book" "hpp"
create_file "$SRC_DIR/data_ingestion/orderbook/level.cpp" "Implementation of order book price level" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/time/synchronizer.hpp" "Timestamp synchronization across exchanges" "hpp"
create_file "$SRC_DIR/data_ingestion/time/synchronizer.cpp" "Implementation of timestamp synchronization" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/normalization/normalizer.hpp" "Data standardization across different exchanges" "hpp"
create_file "$SRC_DIR/data_ingestion/normalization/normalizer.cpp" "Implementation of data normalization" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/data_quality/filter.hpp" "Anomaly detection and filtering for market data" "hpp"
create_file "$SRC_DIR/data_ingestion/data_quality/filter.cpp" "Implementation of market data filtering" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/data_quality/validator.hpp" "Data validation rules for market data" "hpp"
create_file "$SRC_DIR/data_ingestion/data_quality/validator.cpp" "Implementation of market data validation" "cpp"

echo -e "${YELLOW}Generating files for Additional Data Sources...${NC}"

# Additional Data Sources
create_file "$INCLUDE_DIR/data_ingestion/news/api_client.hpp" "Client for crypto news API services" "hpp"
create_file "$SRC_DIR/data_ingestion/news/api_client.cpp" "Implementation of news API client" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/news/processor.hpp" "NLP preprocessing for news data" "hpp"
create_file "$SRC_DIR/data_ingestion/news/processor.cpp" "Implementation of news data processing" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/blockchain/data_collector.hpp" "On-chain data collection from blockchain nodes" "hpp"
create_file "$SRC_DIR/data_ingestion/blockchain/data_collector.cpp" "Implementation of blockchain data collection" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/blockchain/transaction_monitor.hpp" "Analysis of blockchain transactions" "hpp"
create_file "$SRC_DIR/data_ingestion/blockchain/transaction_monitor.cpp" "Implementation of blockchain transaction monitoring" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/sentiment/collector.hpp" "Social media and forum data collection" "hpp"
create_file "$SRC_DIR/data_ingestion/sentiment/collector.cpp" "Implementation of sentiment data collection" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/sentiment/analyzer.hpp" "Sentiment analysis for collected social media data" "hpp"
create_file "$SRC_DIR/data_ingestion/sentiment/analyzer.cpp" "Implementation of sentiment analysis" "cpp"

echo -e "${YELLOW}Generating files for Data Distribution and Storage...${NC}"

# Data Distribution and Storage
create_file "$INCLUDE_DIR/data_ingestion/publisher/zeromq_publisher.hpp" "ZeroMQ-based market data publisher" "hpp"
create_file "$SRC_DIR/data_ingestion/publisher/zeromq_publisher.cpp" "Implementation of ZeroMQ market data publisher" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/storage/influxdb_writer.hpp" "Time-series database writer for market data persistence" "hpp"
create_file "$SRC_DIR/data_ingestion/storage/influxdb_writer.cpp" "Implementation of InfluxDB writer" "cpp"
create_file "$INCLUDE_DIR/data_ingestion/cache/order_book_cache.hpp" "In-memory cache for recent order book states" "hpp"
create_file "$SRC_DIR/data_ingestion/cache/order_book_cache.cpp" "Implementation of order book cache" "cpp"

echo -e "${YELLOW}Generating files for Integration & Testing...${NC}"

# Integration & Testing
create_file "$TESTS_DIR/data_ingestion/websocket_client_tests.cpp" "Unit tests for WebSocket client functionality" "cpp"
create_file "$TESTS_DIR/data_ingestion/order_book_reconstructor_tests.cpp" "Unit tests for order book reconstruction" "cpp"
create_file "$TESTS_DIR/data_ingestion/mock_exchange_server.hpp" "Mock server for testing exchange clients" "hpp"
create_file "$TESTS_DIR/data_ingestion/mock_exchange_server.cpp" "Implementation of mock exchange server" "cpp"
create_file "$PYTHON_DIR/research/data_quality_analysis.py" "Analysis scripts for evaluating data quality" "py"
create_file "$PYTHON_DIR/visualization/order_book_viewer.py" "Visualization tool for inspecting order book state" "py"

echo -e "${YELLOW}Generating files for Configuration & Deployment...${NC}"

# Configuration & Deployment
create_file "$CONFIG_DIR/data_ingestion/exchanges.yaml" "Exchange-specific configuration settings" "yaml"
create_file "$CONFIG_DIR/data_ingestion/data_sources.yaml" "Configuration for additional data sources" "yaml"
create_file "$DOCKER_DIR/data_ingestion/Dockerfile" "Docker configuration for data ingestion services" "dockerfile"
create_file "$DOCKER_DIR/kubernetes/data_ingestion/deployment.yaml" "Kubernetes deployment configuration" "yaml"
create_file "$SCRIPTS_DIR/monitoring/data_ingestion_dashboard.json" "Grafana dashboard configuration for monitoring" "json"

# Create a CMakeLists.txt file
create_file "$BASE_DIR/CMakeLists.txt" "Main CMake configuration file for building the project" "cmake"

# Create a README.md file
cat > "$BASE_DIR/README.md" << EOF
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

\`\`\`bash
mkdir build && cd build
cmake ..
make
\`\`\`

## Running Tests

\`\`\`bash
cd build
make test
\`\`\`

## Directory Structure

- \`include/data_ingestion/\`: Header files
- \`src/data_ingestion/\`: Implementation files
- \`tests/data_ingestion/\`: Unit and integration tests
- \`config/data_ingestion/\`: Configuration files
- \`docker/data_ingestion/\`: Docker and Kubernetes configurations
- \`python/\`: Python scripts for research and visualization

## Dependencies

- Boost.Asio for async I/O
- WebSocketpp or Beast for WebSocket connections
- ZeroMQ for message distribution
- InfluxDB client for time-series data storage
- Protocol Buffers or FlatBuffers for message serialization
EOF

echo -e "${GREEN}README.md created${NC}"

# Create run script
cat > "$BASE_DIR/build.sh" << EOF
#!/bin/bash

# Script to build the Data Ingestion components

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake ..

# Build
make -j\$(nproc)

# Run tests
make test
EOF

chmod +x "$BASE_DIR/build.sh"
echo -e "${GREEN}build.sh created${NC}"

echo -e "${YELLOW}File generation complete!${NC}"
echo -e "Generated $(find "$BASE_DIR" -type f | wc -l) files in $BASE_DIR"
echo -e "${GREEN}You can now navigate to the directory and explore the structure:${NC}"
echo -e "  cd $BASE_DIR"
echo -e "${GREEN}To build the project, run:${NC}"
echo -e "  ./build.sh"