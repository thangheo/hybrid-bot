# src/CMakeLists.txt
# find and search for protobuf 

# Find required packages
find_package(Threads REQUIRED)
find_package(ZeroMQ REQUIRED)
# find_package(cppzmq REQUIRED)
find_package(Boost REQUIRED COMPONENTS system program_options filesystem thread)
find_package(nlohmann_json REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Protobuf)
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
# Add subdirectories for each component
add_subdirectory(common)
add_subdirectory(data_ingestion)
add_subdirectory(strategy)
add_subdirectory(execution)
add_subdirectory(risk)
add_subdirectory(monitoring)
add_subdirectory(communication)
add_subdirectory(learning)
# add_subdirectory(market_intelligence)
# Main executable
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)

set(SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../src/main.cpp 
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/market_intelligence/adaptive_intelligence.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/../src/communication/zmq_server.cpp
            )
add_executable(semi_hft ${SOURCES})
target_link_libraries(semi_hft
    PRIVATE
        common
        data_ingestion
        strategy
        execution
        risk
        monitoring
        ptca_learning
        protobuf::libprotobuf
        gRPC::grpc++
        Boost::system
        Boost::program_options
        Boost::filesystem
        OpenSSL::SSL
        # market_intelligence
)

# Data ingestion publisher executable
add_executable(data_ingestion_publisher 
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/data_ingestion/data_ingestion_publisher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/market_intelligence/adaptive_intelligence.cpp
)
target_link_libraries(data_ingestion_publisher
    PRIVATE
        common
        data_ingestion
        communication
        protobuf::libprotobuf
        Boost::system
        Boost::program_options
        Boost::filesystem
        OpenSSL::SSL
        Threads::Threads
        gRPC::grpc++
        zmq
)

# ZeroMQ pub/sub test executable
add_executable(test_zeromq_pubsub 
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/test_zeromq_pubsub.cpp
)
target_link_libraries(test_zeromq_pubsub
    PRIVATE
        common
        communication
        Threads::Threads
)