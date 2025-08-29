# tests/risk/CMakeLists.txt

# Find required packages
find_package(GTest REQUIRED)
find_package(GMock REQUIRED)

# Add subdirectories for specialized risk test components
add_subdirectory(pretrade)
add_subdirectory(posttrade)
add_subdirectory(integration)
add_subdirectory(interfaces)

# Source files for general risk tests
set(RISK_TEST_SOURCES
    test_risk_manager.cpp
)

# Main risk test executable
add_executable(risk_general_tests ${RISK_TEST_SOURCES})
target_link_libraries(risk_general_tests
    PRIVATE
        risk
        common
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
)
target_include_directories(risk_general_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/mocks
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(risk_general_tests)