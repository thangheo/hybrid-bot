# FindZeroMQ.cmake - Find ZeroMQ library
#
# This module defines the following variables:
#   ZeroMQ_FOUND - True if ZeroMQ is found
#   ZeroMQ_INCLUDE_DIRS - ZeroMQ include directories
#   ZeroMQ_LIBRARIES - ZeroMQ libraries
#
# It also defines the following imported targets:
#   ZeroMQ::ZeroMQ - The ZeroMQ library

# Try to find ZeroMQ with pkg-config first
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_ZeroMQ QUIET libzmq)
endif()

# Find the include directory
find_path(ZeroMQ_INCLUDE_DIR 
  NAMES zmq.h
  PATHS 
    ${PC_ZeroMQ_INCLUDE_DIRS}
    /usr/include
    /usr/local/include
  DOC "ZeroMQ include directory"
)

# Find the library
find_library(ZeroMQ_LIBRARY
  NAMES zmq libzmq
  PATHS 
    ${PC_ZeroMQ_LIBRARY_DIRS}
    /usr/lib
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
  DOC "ZeroMQ library"
)

# Set version variables
if(PC_ZeroMQ_VERSION)
  set(ZeroMQ_VERSION ${PC_ZeroMQ_VERSION})
elseif(ZeroMQ_INCLUDE_DIR AND EXISTS "${ZeroMQ_INCLUDE_DIR}/zmq.h")
  # Extract version from zmq.h
  file(STRINGS "${ZeroMQ_INCLUDE_DIR}/zmq.h" ZeroMQ_VERSION_MAJOR_LINE REGEX "^#define ZMQ_VERSION_MAJOR [0-9]+$")
  file(STRINGS "${ZeroMQ_INCLUDE_DIR}/zmq.h" ZeroMQ_VERSION_MINOR_LINE REGEX "^#define ZMQ_VERSION_MINOR [0-9]+$")
  file(STRINGS "${ZeroMQ_INCLUDE_DIR}/zmq.h" ZeroMQ_VERSION_PATCH_LINE REGEX "^#define ZMQ_VERSION_PATCH [0-9]+$")
  
  string(REGEX REPLACE "^#define ZMQ_VERSION_MAJOR ([0-9]+)$" "\\1" ZeroMQ_VERSION_MAJOR "${ZeroMQ_VERSION_MAJOR_LINE}")
  string(REGEX REPLACE "^#define ZMQ_VERSION_MINOR ([0-9]+)$" "\\1" ZeroMQ_VERSION_MINOR "${ZeroMQ_VERSION_MINOR_LINE}")
  string(REGEX REPLACE "^#define ZMQ_VERSION_PATCH ([0-9]+)$" "\\1" ZeroMQ_VERSION_PATCH "${ZeroMQ_VERSION_PATCH_LINE}")
  
  set(ZeroMQ_VERSION "${ZeroMQ_VERSION_MAJOR}.${ZeroMQ_VERSION_MINOR}.${ZeroMQ_VERSION_PATCH}")
endif()

# Standard handling of the package
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ZeroMQ
  REQUIRED_VARS ZeroMQ_LIBRARY ZeroMQ_INCLUDE_DIR
  VERSION_VAR ZeroMQ_VERSION
)

# Set output variables
if(ZeroMQ_FOUND)
  set(ZeroMQ_LIBRARIES ${ZeroMQ_LIBRARY})
  set(ZeroMQ_INCLUDE_DIRS ${ZeroMQ_INCLUDE_DIR})
endif()

# Create imported target
if(ZeroMQ_FOUND AND NOT TARGET ZeroMQ::ZeroMQ)
  add_library(ZeroMQ::ZeroMQ UNKNOWN IMPORTED)
  set_target_properties(ZeroMQ::ZeroMQ PROPERTIES
    IMPORTED_LOCATION "${ZeroMQ_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${ZeroMQ_INCLUDE_DIR}"
  )
endif()

# Mark advanced variables
mark_as_advanced(ZeroMQ_INCLUDE_DIR ZeroMQ_LIBRARY)