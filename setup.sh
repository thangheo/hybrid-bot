#!/bin/bash
# Hybrid Trading System Setup Script
# Cherry pick approach for clean integration

set -e

echo "🚀 Setting up Hybrid Trading System..."

# Configuration
PYTHON_REPO="git@github.com:thangheo/py_trade.git"
CPP_REPO="git@github.com:thangheo/hft.git"
HYBRID_DIR="/home/e8/works/hybrid-bot"

# Step 1: Create hybrid repository structure
echo "📁 Creating hybrid repository structure..."
# mkdir -p $HYBRID_DIR
cd $HYBRID_DIR

# Initialize git
# git init
# git config --local user.name "Your Name"
# git config --local user.email "your.email@domain.com"

# # Create directory structure
# mkdir -p {proto,generated/{python,cpp}}
# mkdir -p python-strategy/src/{core,data,execution,communication,monitoring}
# mkdir -p cpp-execution/{include,src}/{core,strategy,execution,communication,data}
# mkdir -p integration/{orchestrator,bridge,testing}
# mkdir -p {configs,monitoring,deployment}

# Step 2: Clone original repos temporarily
# echo "⬇️  Cloning original repositories..."
# cd ..
# git clone $PYTHON_REPO temp-python-repo
# git clone $CPP_REPO temp-cpp-repo
# cd $HYBRID_DIR

# # Step 3: Cherry pick Python components
# echo "🐍 Cherry picking Python components..."

# # Core Strategy Components
# cp ../temp-python-repo/src/strategy/strategies/* python-strategy/src/core/
# # cp ../temp-python-repo/src/strategy/.py python-strategy/src/core/
# # cp ../temp-python-repo/src/strategy/unified_manager.py python-strategy/src/core/
# cp ../temp-python-repo/src/strategy/base_strategy.py python-strategy/src/core/

# # Risk Management
# cp -r ../temp-python-repo/src/risk python-strategy/src/
# # Remove specific files we don't need in hybrid
# # rm -f python-strategy/src/risk/risk_config_example.py

# # Portfolio Management  
# cp -r ../temp-python-repo/src/portfolio python-strategy/src/

# # Data Management (selective)
# cp ../temp-python-repo/src/data/manager.py python-strategy/src/data/
# cp ../temp-python-repo/src/data/etf_flow_manager.py python-strategy/src/data/
# cp ../temp-python-repo/src/data/dynamic_correlation_system.py python-strategy/src/data/
# cp ../temp-python-repo/src/data/spy_data_manager.py python-strategy/src/data/

# # Monitoring (selective)
# cp ../temp-python-repo/src/monitoring/system_monitor.py python-strategy/src/monitoring/
# cp ../temp-python-repo/src/monitoring/alert_manager.py python-strategy/src/monitoring/

# # Execution (will be heavily modified for hybrid)
# cp ../temp-python-repo/src/execution/smart_executor.py python-strategy/src/execution/
# cp ../temp-python-repo/src/execution/order_manager.py python-strategy/src/execution/

# # IPC components (base for communication layer)
# cp -r ../temp-python-repo/src/ipc python-strategy/src/communication/
# mv python-strategy/src/communication/ipc/* python-strategy/src/communication/
# rmdir python-strategy/src/communication/ipc

# echo "✅ Python components extracted"

# Step 4: Cherry pick C++ components  
# echo "⚡ Cherry picking C++ components..."

# # Core Types and Utilities
# cp -r ../temp-cpp-repo/include/core cpp-execution/include/
# cp -r ../temp-cpp-repo/include/common cpp-execution/include/

# # Strategy Analysis Components
# cp -r ../temp-cpp-repo/include/strategy/core_analysis cpp-execution/include/strategy/
# cp -r ../temp-cpp-repo/include/strategy/pattern cpp-execution/include/strategy/
# cp -r ../temp-cpp-repo/include/strategy/signal cpp-execution/include/strategy/
# cp -r ../temp-cpp-repo/include/strategy/types cpp-execution/include/strategy/

# # Execution Components
# cp -r ../temp-cpp-repo/include/execution cpp-execution/include/
# # Remove components we don't need initially
# rm -rf cpp-execution/include/execution/grpc

# # Data Ingestion (selective - order book focus)
# mkdir -p cpp-execution/include/data
# cp -r ../temp-cpp-repo/include/data_ingestion/orderbook cpp-execution/include/data/
# cp -r ../temp-cpp-repo/include/data_ingestion/cache cpp-execution/include/data/
# cp ../temp-cpp-repo/include/data_ingestion/types.hpp cpp-execution/include/data/

# # Risk Components (selective)
# cp -r ../temp-cpp-repo/include/risk/pretrade cpp-execution/include/risk/
# cp -r ../temp-cpp-repo/include/risk/posttrade cpp-execution/include/risk/
# cp ../temp-cpp-repo/include/risk/risk_types.hpp cpp-execution/include/risk/

# # Monitoring
# cp -r ../temp-cpp-repo/include/monitoring cpp-execution/include/

echo "✅ C++ components extracted"

# Step 5: Create initial configuration files
echo "⚙️  Creating configuration files..."

# Docker Compose
cat > docker-compose.yml << 'EOF'
version: '3.8'
services:
  python-strategy:
    build: ./python-strategy
    environment:
      - TRADING_MODE=paper
      - RISK_MULTIPLIER=0.1
    depends_on:
      - cpp-execution
    volumes:
      - ./configs:/app/configs
      
  cpp-execution:
    build: ./cpp-execution
    environment:
      - EXECUTION_MODE=simulation
    volumes:
      - ./configs:/app/configs
      
  monitoring:
    build: ./monitoring
    ports:
      - "8080:8080"
    depends_on:
      - python-strategy
      - cpp-execution
EOF

# Basic Python requirements
cat > python-strategy/requirements.txt << 'EOF'
pandas>=1.5.0
numpy>=1.21.0
zmq>=0.0.0
protobuf>=4.0.0
asyncio>=3.9.0
pydantic>=1.10.0
python-dotenv>=0.19.0
loguru>=0.6.0
EOF

# Basic C++ CMakeLists.txt
cat > cpp-execution/CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.16)
project(HybridTradingExecution)

set(CMAKE_CXX_STANDARD 20)

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(ZMQ REQUIRED libzmq)
find_package(Protobuf REQUIRED)

# Include directories
include_directories(include)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Add subdirectories
add_subdirectory(src)
EOF

# Basic README
cat > README.md << 'EOF'
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
EOF

# Step 6: Clean up temporary repos
echo "🧹 Cleaning up..."
cd ..
rm -rf temp-python-repo temp-cpp-repo
cd $HYBRID_DIR

# Step 7: Initial git commit
echo "📝 Initial git commit..."
git add .
git commit -m "Initial hybrid trading system setup

Cherry picked components:
- Python: Core strategy, risk management, portfolio tracking
- C++: Order book analysis, execution algorithms, market intelligence
- Architecture: ZeroMQ communication layer ready for implementation

Next steps:
1. Implement protobuf message definitions
2. Create ZeroMQ communication bridges
3. Adapt extracted code for hybrid architecture"

echo "✅ Hybrid trading system setup complete!"
echo ""
echo "Next steps:"
echo "1. cd $HYBRID_DIR"
echo "2. Implement proto/trading_messages.proto"
echo "3. Create communication bridges"
echo "4. Adapt extracted code for hybrid architecture"
echo "5. Run initial tests"
