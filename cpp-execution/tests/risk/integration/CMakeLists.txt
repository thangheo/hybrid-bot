# tests/risk/integration/CMakeLists.txt

# Source files for risk integration tests
set(RISK_INTEGRATION_TEST_SOURCES
    risk_interfaces_integration_test.cc
    test_portfolio_integration.cpp
    # Skip empty modules: test_portfolio_manager.cpp, test_post_trade_monitor.cpp, test_pretrade_risk_filter.cpp, test_risk_service.cpp
    test_risk_system.cpp
    test_risk_system_integration.cpp
)

# Risk integration test executable
add_executable(risk_integration_tests ${RISK_INTEGRATION_TEST_SOURCES})
target_link_libraries(risk_integration_tests
    PRIVATE
        risk
        common
        execution
        strategy
        data_ingestion
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
)
target_include_directories(risk_integration_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(risk_integration_tests)