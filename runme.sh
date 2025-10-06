#!/bin/bash
# setup: nohup ./start_all.sh > logs/master.log 2>&1 &

# Create log directory
mkdir -p logs

echo "=== Build process started at $(date) ===" | tee -a logs/build.log

# Step 1: Build cpp-execution
cd cpp-execution || exit 1
mkdir -p build
cd build || exit 1

cmake .. >> ../../logs/build.log 2>&1 || exit 1
make -j"$(nproc)" >> ../../logs/build.log 2>&1 || exit 1

cd ../.. || exit 1
echo "Build completed successfully at $(date)" | tee -a logs/build.log

# Step 2: Activate venv
echo "Activating virtual environment..." | tee -a logs/python.log
source venv/bin/activate >> logs/python.log 2>&1

# Step 3: Run regime_publisher.py
echo "Starting regime_publisher.py..." | tee -a logs/python.log
nohup python3 python-strategy/regime_publisher.py \
  --config config/trading_bot.yaml \
  --interval 67 >> logs/regime_publisher.log 2>&1 &
echo $! >> pids.txt
echo "regime_publisher.py started with PID $!" | tee -a logs/python.log

# Step 4: Wait before launching semi_hft
echo "Sleeping 120s before launching semi_hft..." | tee -a logs/master.log
sleep 120

# Step 5: Run semi_hft
echo "Starting semi_hft..." | tee -a logs/master.log
nohup cpp-execution/bin/semi_hft >> logs/semi_hft.log 2>&1 &
echo $! >> pids.txt
echo "semi_hft started with PID $!" | tee -a logs/master.log

echo "=== All processes started successfully at $(date) ===" | tee -a logs/master.log
