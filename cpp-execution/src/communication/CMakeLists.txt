cmake_minimum_required(VERSION 3.10)
project(communication)

# Set C++ standard
# set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Add protobuf-generated files to the SOURCES list
file(GLOB PROTO_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../../proto/generated/cpp/*.cc)
set(SOURCES
    zmq_server.cpp
    ${PROTO_SOURCES}  # Include protobuf-generated files
)
# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../proto/generated/cpp
)
 

# Test files
set(TEST_SOURCES
    # tests/zmq_test.cpp
    tests/zmq_server_test.cpp
    # tests/test_zmq_subscriber.cpp
)

# Add the main communication library
add_library(communication STATIC ${SOURCES})

# Link libraries
target_link_libraries(communication zmq jsoncpp pthread protobuf)

# Build tests
enable_testing()
foreach(test_file ${TEST_SOURCES})
    get_filename_component(test_name ${test_file} NAME_WE)
    add_executable(${test_name} ${test_file})
    target_link_libraries(${test_name} communication zmq jsoncpp pthread)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()