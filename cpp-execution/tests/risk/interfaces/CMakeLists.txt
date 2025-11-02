# tests/risk/interfaces/CMakeLists.txt

# Source files for risk interfaces tests
set(RISK_INTERFACES_TEST_SOURCES
    # data_interface_test.cpp
    # execution_interface_test.cpp
    strategy_interface_test.cpp
)

# Risk interfaces test executable
add_executable(risk_interfaces_tests ${RISK_INTERFACES_TEST_SOURCES})
target_link_libraries(risk_interfaces_tests
    PRIVATE
        risk
        common
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
)
target_include_directories(risk_interfaces_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(risk_interfaces_tests)