# tests/CMakeLists.txt

# Find Google Test
find_package(GTest REQUIRED)
find_package(GMock REQUIRED)

# Include directories for tests
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${GTEST_INCLUDE_DIRS}
    ${GMOCK_INCLUDE_DIRS}
)

# Add subdirectories for component tests
# add_subdirectory(data_ingestion)
# add_subdirectory(communication)
# Enable risk tests (posttrade/pretrade/integration)
add_subdirectory(risk)
# add_subdirectory(execution)
# add_subdirectory(strategy)

# Python tests - We'll use CMake to run Python unit tests
find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME python_monitoring_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests/monitoring
)

add_test(
    NAME python_alerts_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests/alerts
)

add_test(
    NAME python_api_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests/api
)

add_test(
    NAME python_system_tests
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${CMAKE_SOURCE_DIR}/tests/system
)

# Code coverage target
if(ENABLE_COVERAGE AND CMAKE_COMPILER_IS_GNUCXX)
    include(CodeCoverage)
    append_coverage_compiler_flags()
    
    setup_target_for_coverage_lcov(
        NAME coverage
        EXECUTABLE ctest
        EXCLUDE "/usr/*" "${CMAKE_SOURCE_DIR}/tests/*" "${CMAKE_BINARY_DIR}/*"
        DEPENDENCIES all_tests
    )
endif()