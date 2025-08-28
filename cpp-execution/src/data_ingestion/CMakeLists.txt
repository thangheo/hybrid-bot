# src/data_ingestion/CMakeLists.txt

# Collect all source files
file(GLOB_RECURSE DATA_INGESTION_SOURCES
    "*.cpp"
    "blockchain/*.cpp"
    "cache/*.cpp"
    "data_quality/*.cpp"
    "news/*.cpp"
    "normalization/*.cpp"
    "orderbook/*.cpp"
    "pipeline/*.cpp"
    "protocol/*.cpp"
    "publisher/*.cpp"
    "rest/*.cpp"
    "sentiment/*.cpp"
    "storage/*.cpp"
    "time/*.cpp"
    "websocket/*.cpp"
)

# Define library
add_library(data_ingestion ${DATA_INGESTION_SOURCES})

# Include directories
target_include_directories(data_ingestion
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/data_ingestion
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