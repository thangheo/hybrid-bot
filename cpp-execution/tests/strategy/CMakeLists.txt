# tests/strategy/CMakeLists.txt

# Find required packages
find_package(GTest REQUIRED)
find_package(GMock REQUIRED)

# Source files
set(STRATEGY_TEST_SOURCES
    test_order_book_analyzer.cpp
    test_imbalance_detection.cpp
    test_signal_generation.cpp
    test_integration_tests.cpp
    mock_imbalance_detection.cpp
)

# Main strategy test executable
add_executable(strategy_tests ${STRATEGY_TEST_SOURCES})
target_link_libraries(strategy_tests
    PRIVATE
        strategy
        data_ingestion
        common
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
)
target_include_directories(strategy_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(strategy_tests)