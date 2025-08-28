# tests/risk/portfolio/CMakeLists.txt

# Source files for portfolio risk tests
set(RISK_PORTFOLIO_TEST_SOURCES
    test_collateral_manager.cpp
    test_portfolio_manager.cpp
    test_position_tracker.cpp
    test_rebalancing_engine.cpp
    test_main.cpp
)

# Portfolio risk test executable
add_executable(risk_portfolio_tests ${RISK_PORTFOLIO_TEST_SOURCES})
target_link_libraries(risk_portfolio_tests
    PRIVATE
        risk
        common
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
)
target_include_directories(risk_portfolio_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(risk_portfolio_tests)