# src/strategy/CMakeLists.txt
# Exclude hawkes/imbalance_signal_generator.cpp from sources
# Collect all source files for the strategy library
file(GLOB_RECURSE STRATEGY_SOURCES
    "core_analysis/*.cpp"
    "engine/*.cpp" 
    "communication/*.cpp"
    "optimization/*.cpp"
    "pattern/*.cpp"
    "signal/*.cpp"
    "exit/*.cpp"
    # "signal/hawkes/*.cpp"
    "*.cpp"  # Include any cpp files in the strategy root directory
)

list(FILTER STRATEGY_SOURCES EXCLUDE REGEX "signal/hawkes/imbalance_signal_generator\\.cpp$")
list(FILTER STRATEGY_SOURCES EXCLUDE REGEX "signal/hawkes/hawkes_predictor\\.cpp$")

# Exclude any tests directory under strategy from the library source list so
# test code is built into separate test targets that link to GTest. This
# prevents GoogleTest symbols leaking into the shared `strategy` library and
# causing undefined reference errors when `semi_hft` links it.
list(FILTER STRATEGY_SOURCES EXCLUDE REGEX "tests/.*\\.cpp$")

# Remove strategy_app.cpp from the library sources if it exists
list(FILTER STRATEGY_SOURCES EXCLUDE REGEX "strategy_app\\.cpp$")

# Define the strategy library
add_library(strategy ${STRATEGY_SOURCES})

# Include directories for the library
target_include_directories(strategy
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/strategy
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/common
        ${CMAKE_SOURCE_DIR}/include/data_ingestion
        ${CMAKE_SOURCE_DIR}/include/core
)

# Link required libraries to the strategy library
target_link_libraries(strategy
    PUBLIC
        common
        communication
        proto_lib
        data_ingestion
        perception
        # Eigen3::Eigen
        nlohmann_json::nlohmann_json
    PRIVATE
        Boost::system
)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin)

# Include tests subdirectory so that unit tests under src/strategy/tests are built
add_subdirectory(tests)

# Create the executable - DISABLED (test file removed)
# add_executable(strategy_app strategy_app.cpp)
# 
# target_link_libraries(strategy_app
#     PRIVATE
#         strategy          # Your strategy library
#         data_ingestion    # Required dependencies
#         common
#         communication
#         zmq
#         jsoncpp
#         pthread
#         protobuf
# )
# 
# # Ensure the executable can find headers
# target_include_directories(strategy_app
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/include
# 
# )