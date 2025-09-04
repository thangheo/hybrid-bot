# src/common/CMakeLists.txt

# Define library
add_library(common
    exchange_types.cpp
    logger.cpp
    zeromq_client.cpp
    event_bus.cpp
)

# Include directories
target_include_directories(common
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/common
)

# Link libraries
target_link_libraries(common
    PUBLIC
        Threads::Threads
        cppzmq
        ZeroMQ::ZeroMQ
        nlohmann_json::nlohmann_json
)