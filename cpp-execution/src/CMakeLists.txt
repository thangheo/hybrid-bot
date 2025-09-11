# src/CMakeLists.txt

# Add subdirectories for each component
add_subdirectory(common)
add_subdirectory(data_ingestion)
add_subdirectory(strategy)
add_subdirectory(execution)
add_subdirectory(risk)
add_subdirectory(monitoring)
add_subdirectory(communication)
# add_subdirectory(market_intelligence)
# Main executable
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)

add_executable(semi_hft main.cpp)
target_link_libraries(semi_hft
    PRIVATE
        common
        data_ingestion
        strategy
        execution
        risk
        monitoring
        # market_intelligence
)