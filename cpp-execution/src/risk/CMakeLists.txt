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
    "storage/*.cpp"
)
#find libsqlite3
find_package(SQLite3 REQUIRED)
if(NOT SQLite3_FOUND)
    message(FATAL_ERROR "SQLite3 not found")
endif()
message(STATUS "Using SQLite3 ${SQLite3_VERSION}")

# find protobuf
find_package(Protobuf REQUIRED)
if(NOT Protobuf_FOUND)
    message(FATAL_ERROR "Protobuf not found")
endif()
message(STATUS "Using protobuf ${Protobuf_VERSION}")
message(STATUS "Protobuf include dirs: ${Protobuf_INCLUDE_DIRS}")
message(STATUS "Protobuf libraries: ${Protobuf_LIBRARIES}")
#add directory for interfaces
add_subdirectory(interfaces)

# unit tests for risk - event_store batching
add_subdirectory(event_store_test)

add_library(risk STATIC ${RISK_SOURCES})
set_target_properties(risk PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Include directories
target_include_directories(risk
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include/risk
        ${CMAKE_SOURCE_DIR}/../generated/include
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
        protobuf::libprotobuf
        gRPC::grpc++
        SQLite::SQLite3
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
        data_ingestion
        strategy
        execution
        common
        nlohmann_json::nlohmann_json
        protobuf::libprotobuf
        SQLite::SQLite3
        )

# Add format library for std::format (if using GCC < 13)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
    target_link_libraries(risk PRIVATE )
    target_compile_definitions(risk PRIVATE USE_FMT_FORMAT)
endif()