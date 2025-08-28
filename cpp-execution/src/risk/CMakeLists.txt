# src/risk/CMakeLists.txt

# Collect all source files
file(GLOB_RECURSE RISK_SOURCES
    "*.cpp"
    "interfaces/*.cpp"
    "portfolio/*.cpp"
    "posttrade/*.cpp"
    "pretrade/*.cpp"
    "service/*.cpp"
    "utils/*.cpp"
)

# Define library
add_library(risk ${RISK_SOURCES})

# Include directories
target_include_directories(risk
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/risk
)

# Link libraries
target_link_libraries(risk
    PUBLIC
        common
        execution
    PRIVATE
        strategy
        data_ingestion
        nlohmann_json::nlohmann_json
)