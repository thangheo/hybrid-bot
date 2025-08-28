# Type System Migration Guide

This document provides instructions for transitioning to the new type system.

## Overview

    The type system has been restructured to:
    1. Eliminate duplication across components
    2. Provide clear ownership of types
    3. Establish explicit conversions between domains
    4. Improve maintainability and type safety

## Directory Structure
    include/
    ├── core/                      # Core types used across all components
    │   ├── time.hpp               # Time-related types
    │   ├── direction.hpp          # Direction enums
    │   ├── market_types.hpp       # Market-related structures
    │   └── confidence.hpp         # Confidence level enums
    ├── strategy/                  # Strategy-specific types
    │   ├── types/                 # Type definitions
    │   │   ├── trading_signal.hpp # Strategy signal representation
    │   │   └── imbalance_type.hpp # Imbalance enumeration
    │   ├── converters/            # Conversion functions
    │   └── types.hpp              # Combined header for convenience
    └── execution/                 # Execution-specific types
        └── types/                 # Type definitions
            └── trading_signal.hpp # Execution signal representation

## Migration Steps

### 1. Update Includes

    Replace old includes with new ones:

    ```cpp
    // OLD
    #include "strategy/types.hpp"

    // NEW
    #include "strategy/types.hpp"

    Or include specific types:
    // OLD
    trading::strategy::TradingSignal signal;

    // NEW - unchanged if using strategy types
    trading::strategy::TradingSignal signal;

    // NEW - for execution component
    trading::execution::TradingSignal signal;

    // NEW - for core types
    trading::core::Direction direction = trading::core::Direction::BUY;

3. Use Conversion Functions
    When passing signals between components:

    // Convert from strategy to execution
    strategy::TradingSignal strat_signal = /* ... */;
    execution::TradingSignal exec_signal = strategy::toExecutionSignal(strat_signal);

    // Convert from execution to strategy
    execution::TradingSignal exec_signal = /* ... */;
    strategy::TradingSignal strat_signal = execution::toStrategySignal(exec_signal);

4. Use New Helper Methods
    Take advantage of new helper methods:
    // Get trading pair using getter
    std::string pair = signal.getTradingPair();

    // Check for buy/sell signal
    if (signal.isBuySignal()) {
        // Handle buy signal
    }
    Common Issues
Compile Errors
If you encounter compilation errors, check for:

Missing includes
Type mismatches between components
Missing conversion functions
Use of deprecated types or fields

Runtime Issues
Watch for:

Signal conversion issues
Direction/side mapping issues
Timestamp precision differences

Reporting Issues
If you encounter any issues with the new type system, please report them to the core team.

## Testing and Integration

1. Implement unit tests for each new component
2. Start with a limited integration into a single module
3. Gradually expand to other modules
4. Add integration tests for cross-component interactions

This comprehensive refactoring establishes a solid foundation for your trading system's type system. The approach uses clear separation of concerns while providing convenient conversion methods to move data between subsystems.