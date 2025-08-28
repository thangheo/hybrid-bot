find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)

# Define Python module targets for various components
pybind11_add_module(py_data_ingestion 
    bindings/data_ingestion_bindings.cpp
)
target_link_libraries(py_data_ingestion PRIVATE data_ingestion common)

pybind11_add_module(py_strategy
    bindings/strategy_bindings.cpp
)
target_link_libraries(py_strategy PRIVATE strategy data_ingestion common)

pybind11_add_module(py_risk
    bindings/risk_bindings.cpp
)
target_link_libraries(py_risk PRIVATE risk common)

pybind11_add_module(py_execution
    bindings/execution_bindings.cpp
)
target_link_libraries(py_execution PRIVATE execution common)

# Installation rules
install(TARGETS 
    py_data_ingestion
    py_strategy
    py_risk
    py_execution
    LIBRARY DESTINATION ${Python3_SITELIB}/order_imbalance_system
)

# Copy Python modules to build directory for testing
add_custom_target(copy_python_modules ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/python
)

# Install the pure Python modules
install(
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/
    DESTINATION ${Python3_SITELIB}/order_imbalance_system
    FILES_MATCHING 
    PATTERN "*.py"
    PATTERN "__pycache__" EXCLUDE
    PATTERN "venv" EXCLUDE
)

# Create a virtual environment for development if requested
option(CREATE_VENV "Create a Python virtual environment for development" OFF)
if(CREATE_VENV)
    add_custom_target(create_venv
        COMMAND ${Python3_EXECUTABLE} -m venv ${CMAKE_BINARY_DIR}/venv
        COMMAND ${CMAKE_BINARY_DIR}/venv/bin/pip install -r ${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt
        COMMENT "Creating Python virtual environment and installing dependencies"
    )
endif()