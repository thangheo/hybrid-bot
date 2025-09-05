# src/common/CMakeLists.txt

# Define library
add_library(common
    exchange_types.cpp
    logger.cpp
    zeromq_client.cpp
    event_bus.cpp
    orderbook_cache_service.cpp
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
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin)

add_executable(test_cache_app test_cache_app.cpp)

# Include directories (adjust paths according to your project structure)
target_include_directories(test_cache_app PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/common
    ${CMAKE_SOURCE_DIR}/include/data_ingestion
    ${CMAKE_SOURCE_DIR}/include/core
    ${ZMQ_INCLUDE_DIRS}
)
target_link_libraries(test_cache_app PRIVATE
    common              # Your common library with OrderBookCacheService
    data_ingestion      # Your data ingestion library with OrderBook
    ${ZMQ_LIBRARIES}
    pthread
)