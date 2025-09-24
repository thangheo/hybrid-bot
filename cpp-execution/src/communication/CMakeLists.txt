
cmake_minimum_required(VERSION 3.10)
project(communication)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Threads REQUIRED)
find_package(ZeroMQ REQUIRED)
# find_package(cppzmq REQUIRED)
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


# Add protobuf-generated files to the SOURCES list
file(GLOB PROTO_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../generated/cpp/*.cc)
file(GLOB WEBSOCKET_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/data_ingestion/websocket/*.cpp)
file(GLOB WEBSOCKET_EX_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/data_ingestion/websocket/exchanges/*.cpp)
file(GLOB ORDERBOOK_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/data_ingestion/orderbook/*.cpp)
file(GLOB PIPELINE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/data_ingestion/pipeline/*.cpp)
file(GLOB COMMON_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/common/*.cpp)
file(GLOB NORMALIZATION_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/data_ingestion/normalization/*.cpp)
file(GLOB DATA_QUALITY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/data_ingestion/data_quality/*.cpp)
file(GLOB TIME_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/data_ingestion/time/*.cpp)
file(GLOB PUBLISH_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/data_ingestion/publisher/*.cpp)
# protocol source
file(GLOB PROTOCOL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/data_ingestion/protocol/*.cpp)
file(GLOB MARKET_INTEL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../src/market_intelligence/adaptive_intelligence.cpp)

# Add the sources to the SOURCES list
set(SOURCES
    zmq_server.cpp
    zmq_manager.cpp
    zmq_client.cpp
    market_data_subscriber.cpp

    # full_bridge.cpp
    ${PROTO_SOURCES}  # Include protobuf-generated files
    ${WEBSOCKET_SOURCES}  # Add the expanded list of websocket .cpp files
    ${WEBSOCKET_EX_SOURCES}  # Add the expanded list of websocket exchange .cpp files
    # addd all source above
    ${ORDERBOOK_SOURCES}
    ${PIPELINE_SOURCES}
    ${COMMON_SOURCES}
    ${NORMALIZATION_SOURCES}
    ${PROTOCOL_SOURCES}
    ${DATA_QUALITY_SOURCES}
    ${TIME_SOURCES}
    ${PUBLISH_SOURCES}
    ${MARKET_INTEL_SOURCES}

)

# Include directories
include_directories(
    /usr/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/data_ingestion
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/common
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/execution
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/communication
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/risk
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/market_intelligence
    ${CMAKE_CURRENT_SOURCE_DIR}/../../generated/cpp/
    ${CMAKE_SOURCE_DIR}/third_parties/readerwriterqueue

)


# Add the main communication library
add_library(communication STATIC ${SOURCES})
set_target_properties(communication PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Link libraries
target_link_libraries(communication zmq jsoncpp pthread protobuf gRPC::grpc++ proto_lib)

# Set the output directory for executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin)
# Add executable for full_bridge.cpp
add_executable(tactical full_bridge.cpp    ${CMAKE_CURRENT_SOURCE_DIR}/../../src/market_intelligence/adaptive_intelligence.cpp)
target_link_libraries(tactical communication zmq jsoncpp pthread protobuf data_ingestion)
#

# Build tests
# enable_testing()
# foreach(test_file ${TEST_SOURCES})
#     get_filename_component(test_name ${test_file} NAME_WE)
#     add_executable(${test_name} ${test_file})
#     target_link_libraries(${test_name} communication zmq jsoncpp pthread protobuf proto_lib)
#     add_test(NAME ${test_name} COMMAND ${test_name})
# endforeach()
