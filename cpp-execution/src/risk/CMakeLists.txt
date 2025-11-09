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
# include sync directory sources
file(GLOB_RECURSE RISK_SYNC_SOURCES
    "sync/*.cpp"
)
list(APPEND RISK_SOURCES ${RISK_SYNC_SOURCES})

# Exclude any test sources under interfaces/tests or other tests folders so
# the static library does not accidentally contain test mains (integration
# tests) which would be pulled into other test executables via linking.
file(GLOB_RECURSE RISK_TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/interfaces/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../tests/risk/**/*.cpp"
)

# Also exclude demo files that contain main functions
set(RISK_DEMO_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/risk_demo.cpp"
)

# Remove test sources and demo sources from the library
if(RISK_TEST_SOURCES)
    list(REMOVE_ITEM RISK_SOURCES ${RISK_TEST_SOURCES})
endif()
if(RISK_DEMO_SOURCES)
    list(REMOVE_ITEM RISK_SOURCES ${RISK_DEMO_SOURCES})
endif()
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

# Find Crypto++ (cryptopp) for checksum/hash utilities used in sync types
find_package(CryptoPP QUIET)
if(NOT CryptoPP_FOUND)
    # Try pkg-config as a fallback to find include dirs and libs
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(CRYPTOPP cryptopp)
        if(CRYPTOPP_FOUND)
            message(STATUS "Found Crypto++ via pkg-config")
            target_include_directories(risk PRIVATE ${CRYPTOPP_INCLUDE_DIRS})
            target_link_libraries(risk PRIVATE ${CRYPTOPP_LIBRARIES})
            set(CryptoPP_FOUND TRUE)
        endif()
    endif()
endif()

if(CryptoPP_FOUND)
    message(STATUS "Linking Crypto++ into risk target")
    # If find_package provided imported targets, use them; otherwise link by name
    if(TARGET CryptoPP::CryptoPP)
        target_link_libraries(risk PRIVATE CryptoPP::CryptoPP)
    else()
        # common library name on many distros is cryptopp or crypto++
        find_library(CRYPTOPP_LIB NAMES cryptopp CryptoPP crypto++)
        if(CRYPTOPP_LIB)
            target_link_libraries(risk PRIVATE ${CRYPTOPP_LIB})
        endif()
    endif()
else()
    message(WARNING "Crypto++ (cryptopp) not found; using OpenSSL SHA256 fallback if available. Install libcryptopp-dev to use Crypto++ implementation.")
    # Use OpenSSL fallback if available
    find_package(OpenSSL QUIET)
    if(OPENSSL_FOUND)
        message(STATUS "OpenSSL found; enabling OpenSSL SHA256 fallback for risk target")
        target_compile_definitions(risk PRIVATE USE_OPENSSL_SHA256)
        target_include_directories(risk PRIVATE ${OPENSSL_INCLUDE_DIR})
        target_link_libraries(risk PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    else()
        message(WARNING "OpenSSL not found; checksum implementation will be unavailable and build may fail.")
    endif()
endif()

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
# 
add_executable(position_sync_demo sync/position_sync_demo.cpp)
target_link_libraries(position_sync_demo PRIVATE 
        risk
        data_ingestion
        strategy
        execution
        common
        nlohmann_json::nlohmann_json
        protobuf::libprotobuf
        SQLite::SQLite3
        )
# create executable for risk demo
# add_executable(risk_demo risk_demo.cpp)
# target_link_libraries(risk_demo PRIVATE 
#         risk
#         data_ingestion
#         strategy
#         execution
#         common
#         nlohmann_json::nlohmann_json
#         protobuf::libprotobuf
#         SQLite::SQLite3
#         )

# Add format library for std::format (if using GCC < 13)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "13.0")
    target_link_libraries(risk PRIVATE )
    target_compile_definitions(risk PRIVATE USE_FMT_FORMAT)
endif()