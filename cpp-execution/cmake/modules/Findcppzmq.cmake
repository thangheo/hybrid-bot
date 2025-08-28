# FindCppZMQ.cmake - Find CppZMQ header-only library
#
# This module defines the following variables:
#   cppzmq_FOUND - True if cppzmq is found
#   cppzmq_INCLUDE_DIRS - cppzmq include directories
#
# It also defines the following imported targets:
#   cppzmq - The cppzmq interface library

# First check if we have the cppzmq files in external directory
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/external/cppzmq/zmq.hpp")
    # Found in the project's include/external/cppzmq directory
    set(cppzmq_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/include/external/cppzmq")
    set(cppzmq_FOUND TRUE)
else()
    # Try to find it in the system directories
    find_path(cppzmq_INCLUDE_DIR
        NAMES zmq.hpp
        PATHS
            /usr/include
            /usr/local/include
        DOC "cppzmq include directory"
    )
endif()

# Standard handling of the package
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(cppzmq
    REQUIRED_VARS cppzmq_INCLUDE_DIR
)

# Set output variables
if(cppzmq_FOUND)
    set(cppzmq_INCLUDE_DIRS ${cppzmq_INCLUDE_DIR})
endif()

# Create imported target
if(cppzmq_FOUND AND NOT TARGET cppzmq)
    find_package(ZeroMQ REQUIRED)
    
    add_library(cppzmq INTERFACE)
    target_include_directories(cppzmq INTERFACE ${cppzmq_INCLUDE_DIR})
    target_link_libraries(cppzmq INTERFACE ZeroMQ::ZeroMQ)
endif()

# Mark advanced variables
mark_as_advanced(cppzmq_INCLUDE_DIR)