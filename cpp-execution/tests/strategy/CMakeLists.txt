# tests/strategy/CMakeLists.txt

add_executable(test_thread_pool test_thread_pool.cpp)
add_dependencies(test_thread_pool tactical)
target_link_libraries(test_thread_pool PRIVATE GTest::GTest GTest::Main pthread)

add_executable(test_imbalance_teardown test_imbalance_engine_teardown.cpp)
add_dependencies(test_imbalance_teardown tactical)
target_link_libraries(test_imbalance_teardown PRIVATE GTest::GTest GTest::Main pthread strategy)

add_test(NAME test_imbalance_teardown COMMAND test_imbalance_teardown)

enable_testing()
add_test(NAME test_thread_pool COMMAND test_thread_pool)
