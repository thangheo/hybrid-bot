# tests/data_ingestion/CMakeLists.txt

# Find required packages
find_package(GTest REQUIRED)
find_package(GMock REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED COMPONENTS system thread)

# Source files
set(DATA_INGESTION_TEST_SOURCES
    # test_order_book.cpp
    test_order_book_reconstructor.cpp
    # test_exchange_manager.cpp
    mock_exchange_server.cpp
)

# Websocket specific tests
set(WEBSOCKET_TEST_SOURCES
    # websocket/test_connection_pool.cpp
    # websocket/test_websocket_client_base.cpp
    # websocket/test_websocket_integration.cpp
    websocket/mock_exchange_server.cpp
    # websocket/exchanges/test_binance_websocket_client.cpp
)

# Main data ingestion test executable
add_executable(data_ingestion_tests ${DATA_INGESTION_TEST_SOURCES})
target_link_libraries(data_ingestion_tests
    PRIVATE
        data_ingestion
        common
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
)
target_include_directories(data_ingestion_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# WebSocket specific test executable
add_executable(websocket_tests ${WEBSOCKET_TEST_SOURCES})
target_link_libraries(websocket_tests
    PRIVATE
        data_ingestion
        common
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
        Boost::system
        Boost::thread
        OpenSSL::SSL
)
target_include_directories(websocket_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(data_ingestion_tests)
gtest_discover_tests(websocket_tests)