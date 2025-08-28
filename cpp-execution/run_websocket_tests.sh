#!/bin/bash
# Script to run WebSocket tests for the data ingestion layer

# Exit on error
set -e

# Define colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}Building WebSocket tests...${NC}"

# Create build directory if it doesn't exist
mkdir -p build
cd build

# Build the tests
cmake .. -DBUILD_TESTS=ON
make -j $(nproc) websocket_unit_tests websocket_integration_tests

echo -e "${YELLOW}Running WebSocket unit tests...${NC}"
./tests/websocket_unit_tests

echo ""
echo -e "${YELLOW}Running WebSocket integration tests...${NC}"
./tests/websocket_integration_tests

echo ""
echo -e "${GREEN}All tests completed successfully!${NC}"
