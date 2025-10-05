#!/bin/bash
# this script will start cd to cpp-execution and run cmake .. and then make -j$(nproc)
cd cpp-execution || exit 1
mkdir -p build
cd build || exit 1
cmake .. || exit 1
make -j"$(nproc)" || exit 1
cd ../.. || exit 1
echo "Build completed successfully."

#then it would source venv/bin/activate and then cd to python-strategy and run python regime.py with nohup and & at the end
source venv/bin/activate
cd python-strategy || exit 1
nohup python regime.py --config config/trading_bot.yaml &
echo "regime.py started successfully."

#then sleep for at least 1 hour waiting for regime.py finish training and generating model files,
#  then cd back to root directory and run cpp-execution/bin/semi_hft with nohup and & at the end
#this script should also run in background with nohup so that the user can run and go to sleep
sleep 3600
cd ../ || exit 1
nohup cpp-execution/bin/semi_hft &
echo "All processes started successfully."
# this script should also run in background with nohup so that the user can run and go to sleep