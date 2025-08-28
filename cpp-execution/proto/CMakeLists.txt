set(PROTO_FILES
    market_data.proto
    order_book.proto
    execution_service.proto
    common.proto
)

# Set explicit paths for required executables
if(NOT PROTOBUF_PROTOC_EXECUTABLE)
    find_program(PROTOBUF_PROTOC_EXECUTABLE protoc REQUIRED)
    message(STATUS "Found protoc: ${PROTOBUF_PROTOC_EXECUTABLE}")
endif()

if(NOT GRPC_CPP_PLUGIN_EXECUTABLE)
    # First, try using the system path without quotes
    set(GRPC_CPP_PLUGIN_EXECUTABLE /usr/bin/grpc_cpp_plugin)
    
    if(NOT EXISTS ${GRPC_CPP_PLUGIN_EXECUTABLE})
        # Fall back to find_program if explicit path doesn't work
        find_program(GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin REQUIRED)
    endif()
    
    message(STATUS "Using gRPC plugin: ${GRPC_CPP_PLUGIN_EXECUTABLE}")
endif()

# Define output directories
set(GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated")
set(BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")

# Create output directories if they don't exist
file(MAKE_DIRECTORY ${GENERATED_DIR})
file(MAKE_DIRECTORY ${BINARY_DIR})

# Generate protobuf and gRPC C++ files
set(PROTO_SRCS)
set(PROTO_HDRS)
set(GRPC_SRCS)
set(GRPC_HDRS)

# Generate files in both locations (generated dir for include, binary dir for build)
foreach(proto ${PROTO_FILES})
    get_filename_component(proto_name ${proto} NAME_WE)
    
    # Define output files in generated directory (for includes)
    set(GEN_PROTO_SRC "${GENERATED_DIR}/${proto_name}.pb.cc")
    set(GEN_PROTO_HDR "${GENERATED_DIR}/${proto_name}.pb.h")
    set(GEN_GRPC_SRC "${GENERATED_DIR}/${proto_name}.grpc.pb.cc")
    set(GEN_GRPC_HDR "${GENERATED_DIR}/${proto_name}.grpc.pb.h")
    
    # Define output files in binary directory (for build)
    set(BIN_PROTO_SRC "${BINARY_DIR}/${proto_name}.pb.cc")
    set(BIN_PROTO_HDR "${BINARY_DIR}/${proto_name}.pb.h")
    set(BIN_GRPC_SRC "${BINARY_DIR}/${proto_name}.grpc.pb.cc")
    set(BIN_GRPC_HDR "${BINARY_DIR}/${proto_name}.grpc.pb.h")
    
    # Add binary files to lists for the library
    list(APPEND PROTO_SRCS ${BIN_PROTO_SRC})
    list(APPEND PROTO_HDRS ${BIN_PROTO_HDR})
    list(APPEND GRPC_SRCS ${BIN_GRPC_SRC})
    list(APPEND GRPC_HDRS ${BIN_GRPC_HDR})
    
    # Generate protobuf files in GENERATED_DIR
    add_custom_command(
        OUTPUT ${GEN_PROTO_SRC} ${GEN_PROTO_HDR}
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
        ARGS --cpp_out=${GENERATED_DIR}
             -I ${CMAKE_CURRENT_SOURCE_DIR}
             ${CMAKE_CURRENT_SOURCE_DIR}/${proto}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${proto}
        COMMENT "Generating protobuf files for ${proto} in generated directory"
        VERBATIM
    )
    
    # Generate gRPC files in GENERATED_DIR
    add_custom_command(
        OUTPUT ${GEN_GRPC_SRC} ${GEN_GRPC_HDR}
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
        ARGS --grpc_out=${GENERATED_DIR}
             --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
             -I ${CMAKE_CURRENT_SOURCE_DIR}
             ${CMAKE_CURRENT_SOURCE_DIR}/${proto}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${proto} ${GEN_PROTO_SRC} ${GEN_PROTO_HDR}
        COMMENT "Generating gRPC files for ${proto} in generated directory"
        VERBATIM
    )
    
    # Generate protobuf files in BINARY_DIR
    add_custom_command(
        OUTPUT ${BIN_PROTO_SRC} ${BIN_PROTO_HDR}
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
        ARGS --cpp_out=${BINARY_DIR}
             -I ${CMAKE_CURRENT_SOURCE_DIR}
             ${CMAKE_CURRENT_SOURCE_DIR}/${proto}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${proto}
        COMMENT "Generating protobuf files for ${proto} in binary directory"
        VERBATIM
    )
    
    # Generate gRPC files in BINARY_DIR
    add_custom_command(
        OUTPUT ${BIN_GRPC_SRC} ${BIN_GRPC_HDR}
        COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
        ARGS --grpc_out=${BINARY_DIR}
             --plugin=protoc-gen-grpc=${GRPC_CPP_PLUGIN_EXECUTABLE}
             -I ${CMAKE_CURRENT_SOURCE_DIR}
             ${CMAKE_CURRENT_SOURCE_DIR}/${proto}
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${proto} ${BIN_PROTO_SRC} ${BIN_PROTO_HDR}
        COMMENT "Generating gRPC files for ${proto} in binary directory"
        VERBATIM
    )
    
    # Add custom target to ensure files are generated
    add_custom_target(
        generate_${proto_name} ALL
        DEPENDS 
            ${GEN_PROTO_SRC} ${GEN_PROTO_HDR} 
            ${GEN_GRPC_SRC} ${GEN_GRPC_HDR}
            ${BIN_PROTO_SRC} ${BIN_PROTO_HDR}
            ${BIN_GRPC_SRC} ${BIN_GRPC_HDR}
    )
endforeach()

# Create a library from the generated files
add_library(proto_lib ${PROTO_SRCS} ${GRPC_SRCS})
# REMOVED: add_dependencies(proto_lib ${PROTO_TARGETS})

# Link against required libraries
target_link_libraries(proto_lib 
    PUBLIC 
    protobuf::libprotobuf 
    gRPC::grpc++
)

# Set include directories for users of this library
target_include_directories(proto_lib 
    PUBLIC 
    ${BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}  # To include from proto/generated
    cxx_std_20
)

# Add compile definitions
target_compile_definitions(proto_lib PUBLIC 
    GRPC_USE_PROTO_LITE
    GOOGLE_PROTOBUF_NO_RTTI
    GPR_FORBID_UNREACHABLE_CODE
)

# Export the generated directory path for other CMake files to use
set(PROTO_GENERATED_DIR ${GENERATED_DIR} CACHE INTERNAL "Path to proto generated files")

# Print the paths for debugging
message(STATUS "Proto generated directory: ${GENERATED_DIR}")
message(STATUS "Proto binary directory: ${BINARY_DIR}")

# Create a list of all generated files for installation if needed
set(ALL_GENERATED_FILES
    ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
)

# Make sure other targets can find the generated headers
set_target_properties(proto_lib PROPERTIES
    PUBLIC_HEADER "${PROTO_HDRS};${GRPC_HDRS}"
)