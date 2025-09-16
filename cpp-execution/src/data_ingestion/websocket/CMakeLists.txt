# # src/data_ingestion/websocket/CMakeLists.txt
# # Build websocket examples - enabled for testing
# # add_executable(websocket_examples websocket_examples.cpp)


# # Link with the already built libraries
# target_link_libraries(websocket_examples
#     PRIVATE
#         data_ingestion
#         common
#         proto_lib
#         Threads::Threads
#         ${CMAKE_DL_LIBS}
# )

# # Find additional dependencies that might be needed
# find_package(OpenSSL REQUIRED)
# find_package(Boost REQUIRED COMPONENTS system filesystem)

# # Link additional dependencies for websocket functionality
# target_link_libraries(websocket_examples
#     PRIVATE
#         OpenSSL::SSL
#         OpenSSL::Crypto
#         Boost::system
#         Boost::filesystem
# )

# # Set output directory for the executable
# set_target_properties(websocket_examples PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
#     CXX_STANDARD 17
#     CXX_STANDARD_REQUIRED ON
# )

# # Include directories for the executable
# target_include_directories(websocket_examples
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/include
#         ${CMAKE_SOURCE_DIR}/include/data_ingestion
#         ${CMAKE_SOURCE_DIR}/include/common
#         ${CMAKE_SOURCE_DIR}/include/core
# )

# # Add compile definitions if needed
# target_compile_definitions(websocket_examples
#     PRIVATE
#         -DBOOST_ALL_NO_LIB
# )
