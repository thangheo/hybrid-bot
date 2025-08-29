# src/data_ingestion/websocket/CMakeLists.txt

# This is a specialized CMakeLists for websocket functionality
# It's included as part of the data_ingestion library, but
# can have specific options or components

# Since the websocket sources are already included in the parent data_ingestion library,
# we're just adding any specific requirements or settings here

# Build websocket examples - disabled for now
# add_executable(examples_websocket examples_websocket.cpp)
# target_link_libraries(examples_websocket
#     PRIVATE
#         data_ingestion
#         common
#         proto_lib
#         Threads::Threads
#         ${CMAKE_DL_LIBS}
# )

# # Set output directory for the executable
# set_target_properties(examples_websocket PROPERTIES
#     RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
# )

# # Include directories for the executable
# target_include_directories(examples_websocket
#     PRIVATE
#         ${CMAKE_SOURCE_DIR}/include
#         ${CMAKE_SOURCE_DIR}/include/data_ingestion
# )