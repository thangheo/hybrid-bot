# cmake_minimum_required(VERSION 3.14)
# project(hawkes_signal_generator)

# # Source files
# set(SOURCES
#     imbalance_signal_generator.cpp
#     # Add other .cpp files here if needed
# )

# # Header files (optional, for IDEs)
# set(HEADERS
#     imbalance_signal_generator.hpp
#     # Add other .hpp files here if needed
# )

# # Create library (static or shared)
# add_library(hawkes_signal_generator STATIC ${SOURCES} ${HEADERS})

# # Include directories
# # (adjust the relative paths as needed)
# target_include_directories(hawkes_signal_generator PUBLIC
#     ${CMAKE_CURRENT_SOURCE_DIR}
#     ${CMAKE_SOURCE_DIR}/include
#     ${CMAKE_SOURCE_DIR}/src/strategy/types
#     ${CMAKE_SOURCE_DIR}/src/strategy/signal/hawkes
#     ${CMAKE_SOURCE_DIR}/src/core
# )

# # Link dependencies (if any)
# # target_link_libraries(hawkes_signal_generator ...)

# # Set C++ standard
# set_target_properties(hawkes_signal_generator PROPERTIES
#     CXX_STANDARD 17
#     CXX_STANDARD_REQUIRED YES
#     CXX_EXTENSIONS NO
# )
