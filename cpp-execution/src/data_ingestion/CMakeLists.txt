# src/data_ingestion/CMakeLists.txt

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
)
# Add websocket directory for specific build rules if needed
add_subdirectory(websocket)

# Create executable for pipeline example
add_executable(pipeline_example
    pipeline/pipeline_example.cpp
)

# Include directories for pipeline example
target_include_directories(pipeline_example
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/data_ingestion
        ${CMAKE_SOURCE_DIR}/src/data_ingestion
)

# Link libraries for pipeline example
target_link_libraries(pipeline_example
    PRIVATE
        data_ingestion
        common
        proto_lib
        Boost::system
        Boost::filesystem
        OpenSSL::SSL
        OpenSSL::Crypto
)

# Set output directory for pipeline example
set_target_properties(pipeline_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)