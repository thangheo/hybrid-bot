# tests/risk/posttrade/CMakeLists.txt

# Source files for posttrade risk tests
set(RISK_POSTTRADE_TEST_SOURCES
    test_circuit_breaker.cpp
    # Uncomment additional tests gradually after circuit breaker passes
    test_post_trade_monitor.cpp
    test_post_trade_ttl_behavior.cpp
    test_event_store_batching.cpp
    test_stop_loss_manager_fixed.cpp
    test_circuit_breaker.cpp
    test_drawdown_monitor.cpp
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

# Ensure the project's config files are available in the test working directory
add_custom_command(TARGET risk_posttrade_tests PRE_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/config
            ${CMAKE_CURRENT_BINARY_DIR}/config
    COMMENT "Copying config files to test working directory"
)

# Register the test with CTest without auto-discovery to avoid running heavy
# initialization during the build (which can cause timeouts/crashes).
add_test(NAME risk_posttrade_tests COMMAND risk_posttrade_tests)