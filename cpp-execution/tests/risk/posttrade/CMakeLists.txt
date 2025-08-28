# tests/risk/posttrade/CMakeLists.txt

# Source files for posttrade risk tests
set(RISK_POSTTRADE_TEST_SOURCES
    test_balance_reconciliation.cpp
    test_circuit_breaker.cpp
    test_drawdown_monitor.cpp
    test_post_trade_monitor.cpp
    test_stop_loss_manager.cpp
)

# Posttrade risk test executable
add_executable(risk_posttrade_tests ${RISK_POSTTRADE_TEST_SOURCES})
target_link_libraries(risk_posttrade_tests
    PRIVATE
        risk
        common
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
)
target_include_directories(risk_posttrade_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(risk_posttrade_tests)