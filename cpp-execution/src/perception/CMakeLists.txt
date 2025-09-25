cmake_minimum_required(VERSION 3.12)
project(BayesianTradingModel)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Eigen3 3.3 REQUIRED NO_MODULE)
pkg_check_modules(JSONCPP jsoncpp)

# If jsoncpp not found via pkg-config, try find_package
if(NOT JSONCPP_FOUND)
    find_package(jsoncpp REQUIRED)
    set(JSONCPP_LIBRARIES jsoncpp_lib)
endif()

# Define library
add_library(perception
    bayesian_trading_model.cpp
)

# Include directories
target_include_directories(perception
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/perception
)

# Link libraries to the main library
target_link_libraries(perception
    PUBLIC
        Eigen3::Eigen
        ${JSONCPP_LIBRARIES}
)

if(JSONCPP_FOUND)
    target_compile_definitions(perception PRIVATE ${JSONCPP_CFLAGS_OTHER})
endif()

# Create example executable
add_executable(example_usage
    example_usage.cpp
)

# Include directories for executable
target_include_directories(example_usage
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/perception
)

# Link libraries to example
target_link_libraries(example_usage
    PRIVATE
        perception
        Eigen3::Eigen
        ${JSONCPP_LIBRARIES}
        Threads::Threads
)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin)

# Print summary
message(STATUS "")
message(STATUS "Perception Module Configuration Summary:")
message(STATUS "  Eigen3 Version: ${EIGEN3_VERSION}")
if(JSONCPP_FOUND)
    message(STATUS "  JsonCpp: Found via pkg-config")
else()
    message(STATUS "  JsonCpp: Using find_package")
endif()
message(STATUS "")
