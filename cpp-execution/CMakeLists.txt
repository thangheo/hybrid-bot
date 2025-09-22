# Root CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(tactical_cpp VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ")

# Hoặc kiểm tra compiler support
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS "11.0")
        message(FATAL_ERROR "GCC 11+ required for C++23 support")
    endif()
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set CMake module path to find custom Find*.cmake modules
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# Build settings
option(BUILD_TESTS "Build the test suite" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ENABLE_COVERAGE "Enable code coverage" OFF)
option(USE_SANITIZERS "Use address and undefined sanitizers in debug builds" ON)

# Find required packages
find_package(Threads REQUIRED)
find_package(ZeroMQ REQUIRED)
find_package(cppzmq REQUIRED)
find_package(Boost REQUIRED COMPONENTS system program_options filesystem thread)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)

# Find Protobuf and gRPC
find_package(Protobuf)
if(NOT Protobuf_FOUND)
    message(STATUS "Using pkg-config to find Protocol Buffers")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(Protobuf REQUIRED protobuf)
    set(Protobuf_LIBRARIES ${Protobuf_LIBRARIES})
    set(Protobuf_INCLUDE_DIRS ${Protobuf_INCLUDE_DIRS})
    set(PROTOBUF_PROTOC_EXECUTABLE "/usr/bin/protoc")
endif()

find_package(gRPC)
if(NOT gRPC_FOUND)
    message(STATUS "Using pkg-config to find gRPC")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(gRPC REQUIRED grpc++)
    set(gRPC_LIBRARIES ${gRPC_LIBRARIES})
    set(gRPC_INCLUDE_DIRS ${gRPC_INCLUDE_DIRS})
    set(GRPC_CPP_PLUGIN_EXECUTABLE "/usr/bin/grpc_cpp_plugin")
endif()

# Set compilation flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")

# Setup sanitizers if requested
if(USE_SANITIZERS AND CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address,undefined")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address,undefined")
endif()

# Add components
add_subdirectory(proto)
add_subdirectory(src)

# Testing - completely disabled due to old/incompatible code
# if(BUILD_TESTS)
#     enable_testing()
#     add_subdirectory(tests)
# endif()

# Install rules
install(TARGETS tactical
    RUNTIME DESTINATION bin
)

# CPack configuration for binary distribution
include(InstallRequiredSystemLibraries)
set(CPACK_PACKAGE_VENDOR "Your Company")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Tactical System")
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
include(CPack)