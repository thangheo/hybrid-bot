cmake_minimum_required(VERSION 3.10)
project(position_sync_tests)

find_package(GTest REQUIRED)

add_executable(position_sync_integration_test
    position_sync_integration_test.cpp
)

target_link_libraries(position_sync_integration_test
    PRIVATE
    GTest::GTest
    GTest::Main
    risk
    execution
    cppzmq
    -lzmq -lpthread
)

add_test(NAME position_sync_integration_test COMMAND position_sync_integration_test --gtest_color=yes)

# Ensure project headers are visible
target_include_directories(position_sync_integration_test PRIVATE ${CMAKE_SOURCE_DIR}/include)
