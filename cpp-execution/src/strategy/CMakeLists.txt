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
    # "signal/hawkes/*.cpp"
    "*.cpp"  # Include any cpp files in the strategy root directory
)
list(FILTER STRATEGY_SOURCES EXCLUDE REGEX "signal/hawkes/imbalance_signal_generator\\.cpp$")
list(FILTER STRATEGY_SOURCES EXCLUDE REGEX "signal/hawkes/hawkes_predictor\\.cpp$")

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
        nlohmann_json::nlohmann_json
    PRIVATE
        Boost::system
)

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin)

# Create the executable - ONLY link libraries, don't add source files manually
add_executable(strategy_app strategy_app.cpp)

target_link_libraries(strategy_app
    PRIVATE
        strategy          # Your strategy library
        data_ingestion    # Required dependencies
        common
        communication
        zmq
        jsoncpp
        pthread
        protobuf
)

# Ensure the executable can find headers
target_include_directories(strategy_app
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include

)