# FindGTest.cmake - Find Google Test library
#
# This module defines the following variables:
#   GTEST_FOUND - True if Google Test is found
#   GTEST_INCLUDE_DIRS - Google Test include directories
#   GTEST_LIBRARIES - Google Test libraries
#   GTEST_MAIN_LIBRARIES - Google Test main libraries
#   GTEST_BOTH_LIBRARIES - Both gtest and gtest_main libraries
#
# It also defines the following imported targets:
#   GTest::GTest - The Google Test library
#   GTest::Main - The Google Test main library

# Try to find GTest with pkg-config first
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_GTEST QUIET gtest)
endif()

# Find the include directory
find_path(GTEST_INCLUDE_DIR 
  NAMES gtest/gtest.h
  PATHS 
    ${PC_GTEST_INCLUDE_DIRS}
    /usr/include
    /usr/local/include
  DOC "Google Test include directory"
)

# Find the libraries
find_library(GTEST_LIBRARY
  NAMES gtest
  PATHS 
    ${PC_GTEST_LIBRARY_DIRS}
    /usr/lib
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
  DOC "Google Test library"
)

find_library(GTEST_MAIN_LIBRARY
  NAMES gtest_main
  PATHS 
    ${PC_GTEST_LIBRARY_DIRS}
    /usr/lib
    /usr/local/lib
    /usr/lib/x86_64-linux-gnu
  DOC "Google Test main library"
)

# Set version variables
if(PC_GTEST_VERSION)
  set(GTEST_VERSION ${PC_GTEST_VERSION})
endif()

# Standard handling of the package
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GTest
  REQUIRED_VARS GTEST_LIBRARY GTEST_MAIN_LIBRARY GTEST_INCLUDE_DIR
  VERSION_VAR GTEST_VERSION
)

# Set output variables
if(GTEST_FOUND)
  set(GTEST_LIBRARIES ${GTEST_LIBRARY})
  set(GTEST_MAIN_LIBRARIES ${GTEST_MAIN_LIBRARY})
  set(GTEST_BOTH_LIBRARIES ${GTEST_LIBRARIES} ${GTEST_MAIN_LIBRARIES})
  set(GTEST_INCLUDE_DIRS ${GTEST_INCLUDE_DIR})
endif()

# Check for pthread dependency
find_package(Threads QUIET)

# Create imported targets
if(GTEST_FOUND AND NOT TARGET GTest::GTest)
  add_library(GTest::GTest UNKNOWN IMPORTED)
  set_target_properties(GTest::GTest PROPERTIES
    IMPORTED_LOCATION "${GTEST_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}"
  )
  if(TARGET Threads::Threads)
    set_target_properties(GTest::GTest PROPERTIES
      INTERFACE_LINK_LIBRARIES Threads::Threads
    )
  endif()
endif()

if(GTEST_FOUND AND NOT TARGET GTest::Main)
  add_library(GTest::Main UNKNOWN IMPORTED)
  set_target_properties(GTest::Main PROPERTIES
    IMPORTED_LOCATION "${GTEST_MAIN_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${GTEST_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "GTest::GTest"
  )
endif()

# Mark advanced variables
mark_as_advanced(GTEST_INCLUDE_DIR GTEST_LIBRARY GTEST_MAIN_LIBRARY)