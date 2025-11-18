# Tests for strategy

find_package(GTest REQUIRED)
include(GoogleTest)
add_executable(strategy_config_loader_tests test_strategy_config_loader.cpp)
add_executable(adaptive_exit_history_tests test_adaptive_exit_history.cpp)

target_link_libraries(strategy_config_loader_tests
    PRIVATE
        strategy
        GTest::GTest
        GTest::Main
)

target_link_libraries(adaptive_exit_history_tests
    PRIVATE
        strategy
        GTest::GTest
        GTest::Main
)

gtest_discover_tests(adaptive_exit_history_tests)

gtest_discover_tests(strategy_config_loader_tests)
