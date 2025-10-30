find_package(GTest REQUIRED)

add_executable(event_store_batch_test
    ${CMAKE_CURRENT_SOURCE_DIR}/test_event_store_batching.cpp
)
target_link_libraries(event_store_batch_test
    PRIVATE
        risk
        common
        GTest::GTest
        GTest::Main
        Threads::Threads
)
target_include_directories(event_store_batch_test
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_SOURCE_DIR}/include/risk
)
add_test(NAME event_store_batch_test COMMAND event_store_batch_test --gtest_color=yes)
