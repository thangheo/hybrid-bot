#!/bin/bash

# Create base directories if they don't exist
mkdir -p include/strategy
mkdir -p src/strategy
mkdir -p python/strategy/visualization
mkdir -p python/strategy/backtest
mkdir -p config/strategy
mkdir -p tests/strategy

# Core Components - Header files
touch include/strategy/order_book_analyzer.hpp
touch include/strategy/imbalance_detection_engine.hpp
touch include/strategy/volume_profile_analyzer.hpp
touch include/strategy/liquidity_flow_monitor.hpp

# Core Components - Implementation files
touch src/strategy/order_book_analyzer.cpp
touch src/strategy/imbalance_detection_engine.cpp
touch src/strategy/volume_profile_analyzer.cpp
touch src/strategy/liquidity_flow_monitor.cpp

# Alpha Generation Components - Header files
touch include/strategy/imbalance_signal_generator.hpp
touch include/strategy/momentum_correlation.hpp
touch include/strategy/threshold_manager.hpp
touch include/strategy/signal_decay_model.hpp

# Alpha Generation Components - Implementation files
touch src/strategy/imbalance_signal_generator.cpp
touch src/strategy/momentum_correlation.cpp
touch src/strategy/threshold_manager.cpp
touch src/strategy/signal_decay_model.cpp

# Strategy Optimization Components - Header files
touch include/strategy/imbalance_parameter_tuner.hpp
touch include/strategy/exchange_calibration.hpp
touch include/strategy/ab_testing_framework.hpp
touch include/strategy/adaptive_mechanism.hpp

# Strategy Optimization Components - Implementation files
touch src/strategy/imbalance_parameter_tuner.cpp
touch src/strategy/exchange_calibration.cpp
touch src/strategy/ab_testing_framework.cpp
touch src/strategy/adaptive_mechanism.cpp

# Integration and Utilities - Header files
touch include/strategy/strategy_engine.hpp
touch include/strategy/strategy_types.hpp
touch include/strategy/strategy_metrics.hpp

# Integration and Utilities - Implementation files
touch src/strategy/strategy_engine.cpp
touch src/strategy/strategy_config_loader.cpp
touch src/strategy/strategy_metrics.cpp

# Python Bindings and Tools
touch python/strategy/order_book_analyzer.py
touch python/strategy/imbalance_detector.py
touch python/strategy/parameter_optimizer.py
touch python/strategy/visualization/order_book_visualizer.py
touch python/strategy/backtest/imbalance_strategy_backtest.py

# Configuration and Tests
touch config/strategy/default_parameters.json
touch config/strategy/exchange_specific_parameters.json
touch tests/strategy/test_order_book_analyzer.cpp
touch tests/strategy/test_imbalance_detection.cpp
touch tests/strategy/test_signal_generation.cpp
touch tests/strategy/test_integration_tests.cpp

echo "Successfully created all Order Imbalance Strategy Engine files."