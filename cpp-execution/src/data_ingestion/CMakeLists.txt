# src/data_ingestion/CMakeLists.txt
# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# find fmt
find_package(fmt REQUIRED)

# find_package(readerwriterqueue REQUIRED)

# Collect all source files
file(GLOB_RECURSE DATA_INGESTION_SOURCES
    "*.cpp"
    "blockchain/*.cpp"
    "cache/*.cpp"
    "data_quality/*.cpp"
    "normalization/*.cpp"
    "orderbook/*.cpp"
    "pipeline/*.cpp"
    "protocol/*.cpp"
    "publisher/*.cpp"
    "rest/*.cpp"
    "storage/*.cpp"
    "time/*.cpp"
    "websocket/*.cpp"
    "config/config.cpp"
    ${CMAKE_SOURCE_DIR}/src/execution/rate_limit_controller.cpp
)

# Remove example files from sources
list(FILTER DATA_INGESTION_SOURCES EXCLUDE REGEX ".*example\.cpp$")
list(FILTER DATA_INGESTION_SOURCES EXCLUDE REGEX ".*examples.*\.cpp$")

# Define library
add_library(data_ingestion ${DATA_INGESTION_SOURCES})

# Include directories
target_include_directories(data_ingestion
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/third_party/readerwriterqueue  
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/data_ingestion
        ${CMAKE_SOURCE_DIR}/src/data_ingestion
        ${Protobuf_INCLUDE_DIRS}
        ${gRPC_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(data_ingestion
    PUBLIC
        common
        proto_lib
        Boost::system
        Boost::filesystem
        OpenSSL::SSL
        OpenSSL::Crypto
    PRIVATE
        ${Protobuf_LIBRARIES}
        ${gRPC_LIBRARIES}
        fmt::fmt
        # readerwriterqueue
)
# Add websocket directory for specific build rules if needed
add_subdirectory(websocket)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin)

# Create executable for pipeline example
add_executable(data_ingestion_main
    data_ingestion_main.cpp
)

# Include directories for pipeline example
target_include_directories(data_ingestion_main
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/data_ingestion
        ${CMAKE_SOURCE_DIR}/src/data_ingestion
)

# Link libraries for pipeline example
target_link_libraries(data_ingestion_main
    PRIVATE
        data_ingestion
        common
        proto_lib
        Boost::system
        Boost::filesystem
        OpenSSL::SSL
        OpenSSL::Crypto
)

# # Set output directory for pipeline example
# set_target_properties(data_ingestion_main PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
# )