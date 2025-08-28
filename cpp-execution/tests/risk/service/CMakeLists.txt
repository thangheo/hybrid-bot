# tests/risk/service/CMakeLists.txt

# Source files for risk service tests
set(RISK_SERVICE_TEST_SOURCES
    test_risk_service.cpp
    test_risk_validation_service.cpp
)

# Risk service test executable
add_executable(risk_service_tests ${RISK_SERVICE_TEST_SOURCES})
target_link_libraries(risk_service_tests
    PRIVATE
        risk
        common
        GTest::GTest
        GTest::Main
        GMock::GMock
        Threads::Threads
)
target_include_directories(risk_service_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/..
        ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(risk_service_tests)