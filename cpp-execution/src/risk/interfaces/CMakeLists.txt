# src/risk/CMakeLists.txt
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Collect all source files
file(GLOB_RECURSE DATA_INTERFACE_SOURCES
    "*.cpp"
    "tests/data_interface_integration_test.cpp"
    "tests/data_interface_unit_test.cpp"
)
# find protobuf
find_package(Protobuf REQUIRED)
if(NOT Protobuf_FOUND)
    message(FATAL_ERROR "Protobuf not found")
endif()
message(STATUS "Using protobuf ${Protobuf_VERSION}")
message(STATUS "Protobuf include dirs: ${Protobuf_INCLUDE_DIRS}")
message(STATUS "Protobuf libraries: ${Protobuf_LIBRARIES}")

add_library(data_interface STATIC ${DATA_INTERFACE_SOURCES})
set_target_properties(data_interface PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Include directories
target_include_directories(data_interface
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        # ${CMAKE_SOURCE_DIR}/include/risk
        ${CMAKE_SOURCE_DIR}/../generated/include
)

# Link libraries
target_link_libraries(data_interface
    PUBLIC
        common
        # execution
    PRIVATE
        data_ingestion
        nlohmann_json::nlohmann_json
        protobuf::libprotobuf
        gRPC::grpc++
)

# Add compiler-specific flags
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(data_interface PRIVATE 
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
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../../bin)

add_executable(data_interface_integration_test tests/data_interface_integration_test.cpp)
target_link_libraries(data_interface_integration_test PRIVATE 
    data_interface
    risk
    strategy
    data_ingestion
    common
    nlohmann_json::nlohmann_json
    protobuf::libprotobuf
        )

# Add format library for std::format (if using GCC < 13)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
    target_link_libraries(risk PRIVATE )
    target_compile_definitions(risk PRIVATE USE_FMT_FORMAT)
endif()