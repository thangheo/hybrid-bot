# src/risk/CMakeLists.txt
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
find_package(fmt REQUIRED)
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
        fmt::fmt
)

# Add compiler-specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(risk PRIVATE 
        -Wall 
        -Wextra 
        -Wpedantic
        -Wno-unused-parameter        # Suppress unused parameter warnings
        -Wno-unused-variable         # Suppress unused variable warnings
        -Wno-unused-function         # Suppress unused function warnings
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(risk PRIVATE 
        /W4
        /wd4100  # unreferenced formal parameter
        /wd4101  # unreferenced local variable
        /wd4189  # local variable initialized but not referenced
    )
endif()
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../bin)

# create executable for risk demo
add_executable(risk_demo risk_demo.cpp)
target_link_libraries(risk_demo PRIVATE 
        risk
        fmt::fmt
        data_ingestion
        strategy
        execution
        common
        nlohmann_json::nlohmann_json
        )

# Add format library for std::format (if using GCC < 13)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
    find_package(fmt REQUIRED)
    target_link_libraries(risk PRIVATE fmt::fmt)
    target_compile_definitions(risk PRIVATE USE_FMT_FORMAT)
endif()