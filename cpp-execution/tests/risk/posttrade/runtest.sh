# Create a build directory
mkdir build && cd build

# Configure with CMake
cmake ..

# Build the tests
cmake --build . --target risk_posttrade_tests

# Run the tests
ctest -V

# Test Coverage
# These tests provide comprehensive coverage of the post-trade risk control components:

# StopLossManager Tests:

# Stop loss creation and initialization
# Setting and retrieving stop losses
# Removing stop losses
# Stop loss triggering for long and short positions
# Trailing stop loss adjustment
# Auto-liquidation of positions


# DrawdownMonitor Tests:

# High water mark tracking
# Drawdown calculation
# Warning and critical threshold breach detection
# Strategy-specific drawdown tracking
# Portfolio-level drawdown tracking
# High water mark resetting


# CircuitBreaker Tests:

# State transitions (OFF → WARNING → TRIGGERED → COOLING → OFF)
# Metric evaluation against thresholds
# Manual triggering
# Automatic and manual reset
# Trading permission management
# Extended halt periods after multiple triggers


# BalanceReconciliation Tests:

# Position reconciliation with and without discrepancies
# Handling missing and extra positions
# Auto-correction behavior
# Manual correction
# Handling failures in fetching positions
# Configuration updates


# PostTradeMonitor Tests:

# Coordination of sub-components
# Risk event generation and propagation
# Risk mode transitions
# Processing of execution reports and market data
# Performance metric updates
# Component access



# These tests should provide a solid foundation for ensuring the reliability and correctness of the post-trade risk control system. They cover both normal and edge case scenarios, ensuring the system behaves as expected under various conditions.