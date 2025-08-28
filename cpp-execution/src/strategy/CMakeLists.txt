# src/strategy/CMakeLists.txt

# Collect all source files
file(GLOB_RECURSE STRATEGY_SOURCES
    "*.cpp"
    "core_analysis/*.cpp"
    "engine/*.cpp"
    "optimization/*.cpp"
    "pattern/*.cpp"
    "signal/*.cpp"
)

# Define library
add_library(strategy ${STRATEGY_SOURCES})

# Include directories
target_include_directories(strategy
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/strategy
)

# Link libraries
target_link_libraries(strategy
    PUBLIC
        common
        data_ingestion
    PRIVATE
        Boost::system
        nlohmann_json::nlohmann_json
)