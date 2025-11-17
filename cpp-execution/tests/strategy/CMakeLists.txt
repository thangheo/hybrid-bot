# tests/strategy/CMakeLists.txt

add_executable(test_thread_pool test_thread_pool.cpp)
add_dependencies(test_thread_pool tactical)
target_link_libraries(test_thread_pool PRIVATE GTest::GTest GTest::Main pthread)

enable_testing()
add_test(NAME test_thread_pool COMMAND test_thread_pool)
