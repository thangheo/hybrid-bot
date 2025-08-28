#!/bin/bash

# Script to generate the directory structure and empty files for the Execution Engine component
# Author: Claude
# Date: April 29, 2025
# Usage: ./create_execution_engine_files.sh [base_directory]

set -e  # Exit on error

# Define base directory from argument or use current directory
BASE_DIR=${1:-.}
echo "Creating Execution Engine files in: $BASE_DIR"

# Create function to generate file with header comment
create_file() {
    local file_path="$1"
    local description="$2"
    local file_type="$3"  # cpp, h, py, proto, yaml, md

    # Create directory if it doesn't exist
    mkdir -p "$(dirname "$file_path")"
    
    # Define file header based on file type
    case "$file_type" in
        cpp|h)
            cat > "$file_path" << EOF
/**
 * @file $(basename "$file_path")
 * @brief $description
 * @date $(date +"%Y-%m-%d")
 * 
 * Order Imbalance Strategy - Execution Engine
 */

$(if [[ "$file_type" == "h" ]]; then
    # Include guard for header files
    guard=$(basename "$file_path" | tr '[:lower:].' '[:upper:]_')
    echo "#ifndef ${guard}"
    echo "#define ${guard}"
    echo ""
    echo "// TODO: Implement $description"
    echo ""
    echo "#endif // ${guard}"
else
    echo "// TODO: Implement $description"
fi)
EOF
            ;;
        py)
            cat > "$file_path" << EOF
#!/usr/bin/env python3
"""
$description

Order Imbalance Strategy - Execution Engine
Date: $(date +"%Y-%m-%d")
"""

# TODO: Implement $description

def main():
    pass

if __name__ == "__main__":
    main()
EOF
            ;;
        proto)
            cat > "$file_path" << EOF
// $description
// Order Imbalance Strategy - Execution Engine
// Generated: $(date +"%Y-%m-%d")

syntax = "proto3";

package execution;

// TODO: Define service and message types for $description

EOF
            ;;
        yaml)
            cat > "$file_path" << EOF
# $description
# Order Imbalance Strategy - Execution Engine
# Generated: $(date +"%Y-%m-%d")

# TODO: Define configuration parameters for Execution Engine

EOF
            ;;
        md)
            cat > "$file_path" << EOF
# $(basename "$file_path" .md)

**Date:** $(date +"%Y-%m-%d")

## Overview

$description

## TODO

- Complete documentation
- Add diagrams
- Add examples

EOF
            ;;
        *)
            cat > "$file_path" << EOF
# $description
# Order Imbalance Strategy - Execution Engine
# Generated: $(date +"%Y-%m-%d")

# TODO: Implement $description
EOF
            ;;
    esac

    echo "Created: $file_path"
}

# Create necessary directories first
mkdir -p "$BASE_DIR"/{include,src,tests,docs,config,proto,python}/execution
mkdir -p "$BASE_DIR"/include/execution/{algorithms,exchanges,models,grpc}
mkdir -p "$BASE_DIR"/src/execution/{algorithms,exchanges,models,grpc}
mkdir -p "$BASE_DIR"/python/execution
mkdir -p "$BASE_DIR"/docs/api
mkdir -p "$BASE_DIR"/tests/execution

# Order Management System (OMS)
create_file "$BASE_DIR/include/execution/order_generator.h" "Defines the OrderGenerator class interface for creating orders from strategy signals" "h"
create_file "$BASE_DIR/src/execution/order_generator.cpp" "Implements the OrderGenerator class for translating strategy signals into executable orders" "cpp"

create_file "$BASE_DIR/include/execution/smart_order_router.h" "Defines the SmartOrderRouter class interface for exchange selection and routing logic" "h"
create_file "$BASE_DIR/src/execution/smart_order_router.cpp" "Implements exchange selection algorithms for balancing liquidity, fees, and latency" "cpp"

create_file "$BASE_DIR/include/execution/order_type_selector.h" "Defines the OrderTypeSelector class interface for choosing appropriate order types" "h"
create_file "$BASE_DIR/src/execution/order_type_selector.cpp" "Implements selection logic between limit, market, and advanced orders" "cpp"

create_file "$BASE_DIR/include/execution/execution_algorithm_factory.h" "Defines the ExecutionAlgorithmFactory interface for creating execution algorithms" "h"
create_file "$BASE_DIR/src/execution/execution_algorithm_factory.cpp" "Implements the factory pattern for execution algorithms" "cpp"

create_file "$BASE_DIR/include/execution/algorithms/twap_algorithm.h" "Defines the TWAPAlgorithm class for Time-Weighted Average Price execution" "h"
create_file "$BASE_DIR/src/execution/algorithms/twap_algorithm.cpp" "Implements TWAP execution logic with time-slicing calculations" "cpp"

create_file "$BASE_DIR/include/execution/algorithms/vwap_algorithm.h" "Defines the VWAPAlgorithm class for Volume-Weighted Average Price execution" "h"
create_file "$BASE_DIR/src/execution/algorithms/vwap_algorithm.cpp" "Implements VWAP execution logic with volume profile calculations" "cpp"

create_file "$BASE_DIR/include/execution/algorithms/iceberg_algorithm.h" "Defines the IcebergAlgorithm class for hidden order execution" "h"
create_file "$BASE_DIR/src/execution/algorithms/iceberg_algorithm.cpp" "Implements Iceberg order logic for chunking large orders" "cpp"

# Exchange Connectivity
create_file "$BASE_DIR/include/execution/api_client_manager.h" "Defines the APIClientManager interface for managing exchange API connections" "h"
create_file "$BASE_DIR/src/execution/api_client_manager.cpp" "Implements connection management logic with connection pooling and recycling" "cpp"

create_file "$BASE_DIR/include/execution/exchange_api_client.h" "Defines the base ExchangeAPIClient interface for common exchange operations" "h"
create_file "$BASE_DIR/src/execution/exchange_api_client.cpp" "Implements common exchange API client functionality with shared request/response handling" "cpp"

create_file "$BASE_DIR/include/execution/exchanges/binance_api_client.h" "Defines Binance-specific API client implementation" "h"
create_file "$BASE_DIR/src/execution/exchanges/binance_api_client.cpp" "Implements Binance-specific API calls with authentication and response parsing" "cpp"

create_file "$BASE_DIR/include/execution/exchanges/ftx_api_client.h" "Defines FTX-specific API client implementation" "h"
create_file "$BASE_DIR/src/execution/exchanges/ftx_api_client.cpp" "Implements FTX-specific API calls with authentication and response parsing" "cpp"

create_file "$BASE_DIR/include/execution/authentication_service.h" "Defines the AuthenticationService interface for secure credential management" "h"
create_file "$BASE_DIR/src/execution/authentication_service.cpp" "Implements secure credential storage and API key management" "cpp"

create_file "$BASE_DIR/include/execution/rate_limit_controller.h" "Defines the RateLimitController interface for tracking and enforcing rate limits" "h"
create_file "$BASE_DIR/src/execution/rate_limit_controller.cpp" "Implements token bucket rate limiting algorithm with exchange-specific configurations" "cpp"

create_file "$BASE_DIR/include/execution/request_prioritizer.h" "Defines the RequestPrioritizer interface for request classification and queuing" "h"
create_file "$BASE_DIR/src/execution/request_prioritizer.cpp" "Implements request priority logic with queue management" "cpp"

# Order State Management
create_file "$BASE_DIR/include/execution/order_book.h" "Defines the ExecutionOrderBook class interface for tracking placed orders" "h"
create_file "$BASE_DIR/src/execution/order_book.cpp" "Implements order tracking data structures with efficient lookups" "cpp"

create_file "$BASE_DIR/include/execution/fill_reconciliation.h" "Defines the FillReconciliation interface for matching expected and actual fills" "h"
create_file "$BASE_DIR/src/execution/fill_reconciliation.cpp" "Implements fill matching algorithms and validation logic" "cpp"

create_file "$BASE_DIR/include/execution/order_lifecycle_tracker.h" "Defines the OrderLifecycleTracker interface for monitoring order state transitions" "h"
create_file "$BASE_DIR/src/execution/order_lifecycle_tracker.cpp" "Implements state machine for order lifecycle with timeout detection" "cpp"

# Config and Data Models
create_file "$BASE_DIR/include/execution/models/order.h" "Defines the Order data structure with fields for order parameters" "h"
create_file "$BASE_DIR/src/execution/models/order.cpp" "Implements Order class methods with validation logic" "cpp"

create_file "$BASE_DIR/include/execution/models/execution_report.h" "Defines the ExecutionReport data structure for execution details" "h"
create_file "$BASE_DIR/src/execution/models/execution_report.cpp" "Implements ExecutionReport class methods for exchange response parsing" "cpp"

create_file "$BASE_DIR/config/execution_engine.yaml" "Configuration file for execution engine parameters" "yaml"

# Service Layer
create_file "$BASE_DIR/include/execution/execution_service.h" "Defines the ExecutionService interface with high-level execution methods" "h"
create_file "$BASE_DIR/src/execution/execution_service.cpp" "Implements the main execution service with component orchestration" "cpp"

create_file "$BASE_DIR/include/execution/grpc/execution_service_impl.h" "Defines the gRPC interface implementation for the execution service" "h"
create_file "$BASE_DIR/src/execution/grpc/execution_service_impl.cpp" "Implements gRPC service methods with request validation" "cpp"

create_file "$BASE_DIR/proto/execution_service.proto" "Defines the gRPC service interface for the execution engine" "proto"

# Python Support Tools
create_file "$BASE_DIR/python/execution/order_analysis.py" "Provides tools for analyzing order execution quality and slippage" "py"
create_file "$BASE_DIR/python/execution/exchange_simulator.py" "Simulates exchange behavior for testing with order matching engine" "py"

# Tests
create_file "$BASE_DIR/tests/execution/test_order_generator.cpp" "Unit tests for OrderGenerator class with different signal types" "cpp"
create_file "$BASE_DIR/tests/execution/test_smart_order_router.cpp" "Unit tests for SmartOrderRouter class with routing decisions" "cpp"
create_file "$BASE_DIR/tests/execution/test_order_type_selector.cpp" "Unit tests for OrderTypeSelector class with market conditions" "cpp"
create_file "$BASE_DIR/tests/execution/test_execution_algorithms.cpp" "Unit tests for execution algorithms (TWAP, VWAP, Iceberg)" "cpp"
create_file "$BASE_DIR/tests/execution/test_api_client_manager.cpp" "Unit tests for APIClientManager class with connection management" "cpp"
create_file "$BASE_DIR/tests/execution/test_rate_limit_controller.cpp" "Unit tests for RateLimitController class with rate limiting behavior" "cpp"
create_file "$BASE_DIR/tests/execution/test_order_lifecycle.cpp" "Unit tests for order lifecycle tracking with state transitions" "cpp"
create_file "$BASE_DIR/tests/execution/integration_test_execution_engine.cpp" "Integration tests for execution engine components with end-to-end scenarios" "cpp"

# Documentation
create_file "$BASE_DIR/docs/api/execution_service_api.md" "Documents the execution service API with request/response examples" "md"
create_file "$BASE_DIR/docs/execution_engine_architecture.md" "Describes the execution engine architecture with component interactions" "md"
create_file "$BASE_DIR/docs/execution_algorithms.md" "Documents execution algorithms with parameter explanations" "md"
create_file "$BASE_DIR/docs/exchange_connectivity.md" "Documents exchange integration details with authentication requirements" "md"

# Make the script executable
chmod +x "$BASE_DIR/create_execution_engine_files.sh"

echo "All Execution Engine files have been created successfully!"
echo "Total files created: 58"