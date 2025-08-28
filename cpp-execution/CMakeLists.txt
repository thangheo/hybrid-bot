cmake_minimum_required(VERSION 3.16)
project(HybridTradingExecution)

set(CMAKE_CXX_STANDARD 20)

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
find_package(Protobuf REQUIRED)

# Include directories
include_directories(include)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Add subdirectories
add_subdirectory(src)
