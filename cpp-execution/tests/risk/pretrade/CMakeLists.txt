# tests/risk/pretrade/CMakeLists.txt

# Source files for pretrade risk tests
set(RISK_PRETRADE_TEST_SOURCES
    # test_exposure_calculator.cpp
    test_fat_finger_protection.cpp
    test_order_validator.cpp
    test_position_limit_checker.cpp
    # test_pretrade_risk_filter.cpp
    integration_test_pretrade_risk_filter.cpp
)

# Pretrade risk test executable
add_executable(risk_pretrade_tests ${RISK_PRETRADE_TEST_SOURCES})
target_link_libraries(risk_pretrade_tests
    PRIVATE
        risk
        common
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
)
target_include_directories(risk_pretrade_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(risk_pretrade_tests)

# Temporary standalone target to build only the integration test (avoids multiple-main collisions
# when other test files also provide main). This is useful while we iteratively fix this single test.
add_executable(risk_pretrade_integration integration_test_pretrade_risk_filter.cpp)
target_link_libraries(risk_pretrade_integration
    PRIVATE
        risk
        common
        GTest::GTest
        GMock::GMock
        Threads::Threads
)
target_include_directories(risk_pretrade_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)