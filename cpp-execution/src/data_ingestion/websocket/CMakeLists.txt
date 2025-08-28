# src/data_ingestion/websocket/CMakeLists.txt

# This is a specialized CMakeLists for websocket functionality
# It's included as part of the data_ingestion library, but
# can have specific options or components

# Since the websocket sources are already included in the parent data_ingestion library,
# we're just adding any specific requirements or settings here

# If you have specific websocket executables or examples, define them here
if(BUILD_EXAMPLES)
    add_executable(websocket_example examples_websocket.cpp)
    target_link_libraries(websocket_example
        PRIVATE
            data_ingestion
            common
    )
endif()