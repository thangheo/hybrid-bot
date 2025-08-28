set(MONITORING_SOURCES
    # Core monitoring components
    health_monitor.cpp
    latency_tracker.cpp
    resource_monitor.cpp
    log_collector.cpp
    
    # Alerting system
    alert_manager.cpp
    escalation_policy.cpp
    incident_manager.cpp
    oncall_manager.cpp
    
    # Trading performance monitoring
    pnl_calculator.cpp
    execution_analyzer.cpp
    strategy_performance.cpp
    risk_metrics.cpp
)

add_library(monitoring ${MONITORING_SOURCES})

target_link_libraries(monitoring
    PUBLIC
        common
        execution
        strategy
        risk
        Boost::system
        Boost::filesystem
        ZeroMQ::ZeroMQ
        cppzmq
        Threads::Threads
)

target_include_directories(monitoring
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
)

# Install headers
install(
    DIRECTORY ${CMAKE_SOURCE_DIR}/include/monitoring/
    DESTINATION include/monitoring
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install the library
install(TARGETS monitoring
    EXPORT MonitoringTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)