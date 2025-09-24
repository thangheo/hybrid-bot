# tests/execution/CMakeLists.txt

# Find required packages
find_package(GTest REQUIRED)
find_package(GMock REQUIRED)

# Source files
set(EXECUTION_TEST_SOURCES
    # test_api_client_manager.cpp
    # test_authentication_service.cpp
    # test_binance_api_client.cpp
    # test_exchange_api_client.cpp
    # test_execution_algorithm_factory.cpp
    # test_execution_algorithms.cpp
    # test_execution_engine_integration.cpp
    # # test_execution_report.cpp
    # test_execution_service.cpp
    # test_execution_service_impl.cpp
    # test_iceberg_algorithm.cpp
    # test_order_generator.cpp
    # test_order_lifecycle.cpp
    # test_orders.cpp
    # test_order_type_selector.cpp
    # test_rate_limit_controller.cpp
    # test_request_prioritizer.cpp
    # # test_smart_order_router.cpp
    # test_twap_algorithm.cpp
    # test_vwap_algorithm.cpp
    # integration_test_order_pipeline.cpp
    # order_lifecycle_tracker_tests.cpp
    # order_state_integration_tests.cpp
    # fill_reconciliation_tests.cpp
    integration_test_order_pipeline.cpp
    preflight_test.cpp
)

# Mock headers - don't need to be compiled, just included
set(EXECUTION_TEST_MOCKS
    mock_api_client_manager.hpp
    mock_order_generator.hpp
    mock_smart_order_router.hpp
)

# Main execution test executable
add_executable(execution_tests ${EXECUTION_TEST_SOURCES})
target_link_libraries(execution_tests
    PRIVATE
        execution
        common
        data_ingestion
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
)
target_include_directories(execution_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(execution_tests)