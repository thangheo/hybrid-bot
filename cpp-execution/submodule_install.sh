#!/bin/bash

# Script to clone all submodules to current directory
# Make sure you're in the root directory of your project
mkdir -p third_parties/
echo "Cloning all submodules..."

echo "Cloning googletest..."
git clone https://github.com/google/googletest.git third_parties/googletest

echo "Cloning nlohmann_json..."
git clone https://github.com/nlohmann/json.git third_parties/nlohmann_json

echo "Cloning boost..."
git clone https://github.com/boostorg/boost.git third_parties/boost

echo "Cloning prometheus-cpp..."
git clone https://github.com/jupp0r/prometheus-cpp.git third_parties/prometheus-cpp

echo "Cloning protobuf..."
git clone https://github.com/protocolbuffers/protobuf.git third_parties/protobuf

echo "Cloning hiredis..."
git clone https://github.com/redis/hiredis.git third_parties/hiredis

echo "Cloning cpp-httplib..."
git clone https://github.com/yhirose/cpp-httplib.git third_parties/cpp-httplib

echo "Cloning timescaledb..."
git clone https://github.com/timescale/timescaledb.git third_parties/timescaledb

echo "Cloning redis-plus-plus..."
git clone https://github.com/sewenew/redis-plus-plus.git third_parties/redis-plus-plus

echo "Cloning spdlog..."
git clone https://github.com/gabime/spdlog.git third_parties/spdlog

echo "Cloning pqxx..."
git clone https://github.com/jtv/libpqxx.git third_parties/pqxx

echo "Cloning openssl..."
git clone https://github.com/openssl/openssl.git third_parties/openssl

echo "Cloning abseil-cpp..."
git clone https://github.com/abseil/abseil-cpp.git third_parties/abseil-cpp

echo "Done! All submodules have been cloned."
echo ""
echo "To initialize boost submodules (if needed), run:"
echo "cd third_parties/boost && git submodule update --init --recursive"