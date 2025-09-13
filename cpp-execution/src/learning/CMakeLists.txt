# CMakeLists.txt addition for PTCA Learning Components
# Add this to your existing CMakeLists.txt

# Find required packages for PTCA
find_package(nlohmann_json REQUIRED)

# PTCA Learning Library
set(PTCA_SOURCES
    pavlovian_credit_assignment.cpp
    ptca_integration_manager.cpp
)

set(PTCA_HEADERS
    ${CMAKE_SOURCE_DIR}/include/learning/pavlovian_credit_assignment.hpp
    ${CMAKE_SOURCE_DIR}/include/learning/ptca_integration_manager.hpp
)

# Create PTCA learning library
add_library(ptca_learning STATIC ${PTCA_SOURCES} ${PTCA_HEADERS})

# Include directories
# target_include_directories(ptca_learning
#     PUBLIC
#         ${CMAKE_SOURCE_DIR}/include
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/include/learning
# )

target_link_libraries(ptca_learning
    PUBLIC
        nlohmann_json::nlohmann_json
        # Add your existing common libraries here
        common
        data_ingestion
        execution
        strategy
        execution
    PRIVATE
        ${CMAKE_THREAD_LIBS_INIT}
)

# Set C++ standard
target_compile_features(ptca_learning PUBLIC cxx_std_20)

# Compiler-specific options
if(MSVC)
    target_compile_options(ptca_learning PRIVATE /W4)
else()
    target_compile_options(ptca_learning PRIVATE -Wall -Wextra -Wpedantic)
endif()


# Installation rules (optional)
install(TARGETS ptca_learning
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES ${PTCA_HEADERS}
    DESTINATION include/learning
)
