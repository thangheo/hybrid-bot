# Hybrid Trading System

## Architecture
- **Python Strategy Layer**: Signal generation, risk management, portfolio tracking
- **C++ Execution Layer**: Order book analysis, smart execution, market intelligence
- **Communication**: ZeroMQ + Protocol Buffers

## Quick Start
```bash
# Build and run
docker-compose up --build

# Run tests
make test

# Monitor system
make monitor
```

## Components
- `python-strategy/`: Python strategy engine
- `cpp-execution/`: C++ execution engine  
- `integration/`: Communication bridge
- `proto/`: Message definitions
============USage
# Start with log filtering
./trading_manager.sh start

# Monitor disk usage
./trading_manager.sh disk

# Clean up when logs get large
./trading_manager.sh cleanup

# Follow filtered logs in real-time
./trading_manager.sh follow cpp