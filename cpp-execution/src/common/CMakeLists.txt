# src/common/CMakeLists.txt
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Define library
add_library(common
    exchange_types.cpp
    logger.cpp
    event_bus.cpp
    # orderbook_cache_service.cpp
)

# Include directories
target_include_directories(common
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/common
)
# add protobuf
find_package(Protobuf REQUIRED)
# target_include_directories(common PRIVATE ${Protobuf_INCLUDE_DIRS})
# target_link_libraries(common PRIVATE ${Protobuf_LIBRARIES})
# Link libraries
target_link_libraries(common
    PUBLIC
        Threads::Threads
        cppzmq
        ZeroMQ::ZeroMQ
        ${Protobuf_LIBRARIES}
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
    ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(test_cache_app PRIVATE
    common              # Your common library with OrderBookCacheService
    communication       # communication provides topic utilities used by data_ingestion
    data_ingestion      # Your data ingestion library with OrderBook
    ${ZMQ_LIBRARIES}
    pthread
    ${Protobuf_LIBRARIES}
)