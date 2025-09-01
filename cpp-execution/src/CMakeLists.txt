# src/CMakeLists.txt

# Add subdirectories for each component
add_subdirectory(common)
add_subdirectory(data_ingestion)
add_subdirectory(strategy)
add_subdirectory(execution)
add_subdirectory(risk)
add_subdirectory(monitoring)
add_subdirectory(communication)
# Main executable
add_executable(trading_system main.cpp)
target_link_libraries(trading_system
    PRIVATE
        common
        data_ingestion
        strategy
        execution
        risk
        monitoring
)