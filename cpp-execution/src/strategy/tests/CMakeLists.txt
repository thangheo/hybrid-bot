# Tests for strategy

add_executable(strategy_config_loader_tests test_strategy_config_loader.cpp)

target_link_libraries(strategy_config_loader_tests
    PRIVATE
        strategy
        # GTest::GTest
        # GTest::Main
)

gtest_discover_tests(strategy_config_loader_tests)
