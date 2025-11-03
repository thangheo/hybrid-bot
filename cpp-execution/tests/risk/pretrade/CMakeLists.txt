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
include(GoogleTest)

# Build each test file as its own executable to avoid multiple-definition-of-main
foreach(test_src ${RISK_PRETRADE_TEST_SOURCES})
    get_filename_component(test_name ${test_src} NAME_WE)
    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name}
        PRIVATE
            risk
            common
            GTest::GTest
            GMock::GMock
            Threads::Threads
    )
    target_include_directories(${test_name}
        PRIVATE
            ${CMAKE_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/..
            ${CMAKE_CURRENT_SOURCE_DIR}/../mocks
    )
    # Register test with CTest using add_test to avoid executing the binary during the build
    add_test(NAME ${test_name} COMMAND ${test_name})

    # If this is the failing fat-finger test, enable AddressSanitizer to get a helpful
    # stacktrace and memory error diagnostics during execution.
    if (${test_name} STREQUAL "test_fat_finger_protection")
        target_compile_options(${test_name} PRIVATE -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer)
        target_link_options(${test_name} PRIVATE -fsanitize=address -fsanitize=undefined)
    endif()
endforeach()

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