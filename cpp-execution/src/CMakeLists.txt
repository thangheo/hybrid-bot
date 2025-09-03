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
# add_executable(tactical ./communication/full_bridge.cpp)
# target_link_libraries(tactical
#     PRIVATE
#         common
#         data_ingestion
#         # strategy
#         execution
#         risk
#         monitoring
#         # market_intelligence
# )