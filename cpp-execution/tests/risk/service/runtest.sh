#!/bin/bash
# Script to build and run risk management system tests

# Build tests
mkdir -p build
cd build
cmake ..
make -j$(nproc)

# Run unit tests
echo "Running risk validation service tests..."
./test_risk_validation_service

echo "Running risk service tests..."
./test_risk_service

echo "Running risk manager tests..."
./test_risk_manager

# Run integration tests
echo "Running risk system integration tests..."
./test_risk_system_integration

# Report summary
echo "Test summary:"
ctest --output-on-failure