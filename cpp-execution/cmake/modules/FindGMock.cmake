# FindGMock.cmake
# --------------
#
# Find Google Mock (GMock) include directories and libraries
#
# This module defines the following variables:
#
# GMock_FOUND            : True if GMock was found
# GMock_INCLUDE_DIRS     : Include directories for GMock
# GMock_LIBRARIES        : Libraries to link against GMock
# GMock_MAIN_LIBRARIES   : Libraries to link against GMock's main
#
# The module will also define targets:
#   GMock::GMock         : Target for the GMock library
#   GMock::Main          : Target for the GMock main library

find_path(GMock_INCLUDE_DIR
    NAMES gmock/gmock.h
    HINTS
        ENV GMOCK_ROOT
        ${GMOCK_ROOT}
        /usr/include
        /usr/local/include
)

find_library(GMock_LIBRARY
    NAMES gmock
    HINTS
        ENV GMOCK_ROOT
        ${GMOCK_ROOT}
        /usr/lib
        /usr/local/lib
)

find_library(GMock_MAIN_LIBRARY
    NAMES gmock_main
    HINTS
        ENV GMOCK_ROOT
        ${GMOCK_ROOT}
        /usr/lib
        /usr/local/lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GMock DEFAULT_MSG
    GMock_LIBRARY
    GMock_INCLUDE_DIR
)

mark_as_advanced(
    GMock_INCLUDE_DIR
    GMock_LIBRARY
    GMock_MAIN_LIBRARY
)

if(GMock_FOUND)
    set(GMock_INCLUDE_DIRS ${GMock_INCLUDE_DIR})
    set(GMock_LIBRARIES ${GMock_LIBRARY})
    set(GMock_MAIN_LIBRARIES ${GMock_MAIN_LIBRARY})
    
    if(NOT TARGET GMock::GMock)
        add_library(GMock::GMock UNKNOWN IMPORTED)
        set_target_properties(GMock::GMock PROPERTIES
            IMPORTED_LOCATION "${GMock_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GMock_INCLUDE_DIR}"
        )
    endif()
    
    if(NOT TARGET GMock::Main)
        add_library(GMock::Main UNKNOWN IMPORTED)
        set_target_properties(GMock::Main PROPERTIES
            IMPORTED_LOCATION "${GMock_MAIN_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${GMock_INCLUDE_DIR}"
            INTERFACE_LINK_LIBRARIES "GMock::GMock"
        )
    endif()
endif()