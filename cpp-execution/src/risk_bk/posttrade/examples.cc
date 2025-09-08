// Create and initialize post-trade monitor
RiskConfig config = loadConfiguration(); // Load from file
auto postTradeMonitor = std::make_unique<PostTradeMonitor>(config);
postTradeMonitor->initialize();

// Register for risk events
postTradeMonitor->registerRiskEventCallback([](const RiskEvent& event) {
    // Log the event
    std::cout << "Risk event: " << event.details << std::endl;
    
    // Take appropriate action based on event type
    if (event.type == RiskEventType::CIRCUIT_BREAKER) {
        if (event.circuitBreakerEvent->newState == CircuitBreakerState::TRIGGERED) {
            // Alert operations team
            sendAlert("Circuit breaker triggered: " + event.circuitBreakerEvent->reason);
        }
    }
});

// Example of processing an execution report
ExecutionReport report = getExecutionReport(); // From exchange
postTradeMonitor->processExecutionReport(report);

// Example of updating market data
MarketData marketData = getMarketData(); // From market data service
postTradeMonitor->updateMarketData(marketData);

// Example of updating performance metrics
PerformanceMetrics metrics = calculateMetrics(); // From performance tracker
postTradeMonitor->updatePerformanceMetrics(metrics);

// Example of manually reconciling positions
postTradeMonitor->reconcilePositions();

// Check if trading is allowed
if (postTradeMonitor->getCircuitBreaker().isTradingAllowed()) {
    // Proceed with trading
} else {
    // Trading halted, wait for circuit breaker reset
}