#!/bin/bash
# Compile Protocol Buffers

# Ensure we're in the project root
PROJECT_ROOT=$(git rev-parse --show-toplevel 2>/dev/null || echo ".")
cd "$PROJECT_ROOT"

# Create output directories if they don't exist
mkdir -p include/proto
mkdir -p src/proto
mkdir -p python/proto

# Compile C++ proto
for proto_file in proto/*.proto; do
  echo "Compiling C++ for $proto_file..."
  protoc -I=proto --cpp_out=src/proto "$proto_file"
  protoc -I=proto --cpp_out=include/proto "$proto_file"
done

# Compile Python proto
for proto_file in proto/*.proto; do
  echo "Compiling Python for $proto_file..."
  protoc -I=proto --python_out=python/proto "$proto_file"
done

# Create Python __init__.py
touch python/proto/__init__.py

echo "Protocol Buffers compilation finished!"