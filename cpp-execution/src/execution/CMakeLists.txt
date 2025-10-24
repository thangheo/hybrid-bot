# src/execution/CMakeLists.txt

# Find required packages
find_package(PkgConfig REQUIRED)
find_package(CURL REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(GTest REQUIRED)

set(CMAKE_CXX_STANDARD 23)

# Find libuuid
pkg_check_modules(UUID REQUIRED uuid)

# Collect all source files except test files
file(GLOB_RECURSE EXECUTION_SOURCES
    "*.cpp"
    "algorithms/*.cpp"
    "exchanges/*.cpp"
    "grpc/*.cpp"
    "models/*.cpp"
    "filter/*.cpp"
    "preflight/*.cpp"
    # "risk/risk_config.cpp"
)

# Remove test files from library sources
list(FILTER EXECUTION_SOURCES EXCLUDE REGEX ".*test.*\\.cpp$")

# Define library
add_library(execution ${EXECUTION_SOURCES})

# Include directories
target_include_directories(execution
    PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include/execution
    ${CMAKE_SOURCE_DIR}/proto/generated
    ${Protobuf_INCLUDE_DIRS}
    ${gRPC_INCLUDE_DIRS}
    ${UUID_INCLUDE_DIRS}
    
)

# Link libraries for the execution library
target_link_libraries(execution
    PUBLIC
    common
    PRIVATE
    risk
    proto_lib
    ${Protobuf_LIBRARIES}
    ${gRPC_LIBRARIES}
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl          # Add libcurl
    jsoncpp_lib            # Add JsonCpp
    ${UUID_LIBRARIES}      # Add libuuid
)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin)

# Add compiler definitions for UUID
target_compile_definitions(execution PRIVATE ${UUID_CFLAGS_OTHER})

# Test executable
add_executable(test_execution test_execution.cpp)

target_link_libraries(test_execution
    PRIVATE
    execution
    common
    proto_lib
    ${Protobuf_LIBRARIES}
    ${gRPC_LIBRARIES}
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl          # Add libcurl for test
    jsoncpp_lib            # Add JsonCpp for test
    ${UUID_LIBRARIES}      # Add libuuid for test
    Threads::Threads
)

# Balance validator unit test (added so test_balance_validator can be run)
add_executable(test_balance_validator ../../tests/execution/test_balance_validator.cpp)

target_link_libraries(test_balance_validator
    PRIVATE
    execution
    common
    proto_lib
    ${Protobuf_LIBRARIES}
    ${gRPC_LIBRARIES}
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    CURL::libcurl
    jsoncpp_lib
    ${UUID_LIBRARIES}
    Threads::Threads
    GTest::GTest
    GTest::Main
)

# Register with CTest
enable_testing()
add_test(NAME test_balance_validator COMMAND test_balance_validator)