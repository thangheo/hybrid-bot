#!/bin/bash
set -e

echo "Cleaning any existing build directory"
rm -rf /app/build

echo "Creating build directory"
mkdir -p /app/build

echo "Navigating to build directory"
cd /app/build

echo "Running CMake"
cmake ..

echo "Building with Make"
make -j$(nproc)

echo "Build completed successfully"
