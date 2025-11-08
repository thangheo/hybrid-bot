#!/bin/bash

# Script to disable pthread priority scheduling for the trading system
# This prevents the "Fatal glibc error: tpp.c:83" error on systems without RT privileges

echo "Setting environment variables to disable pthread priority scheduling..."

# Disable ZeroMQ thread priority and scheduling
export ZMQ_THREAD_PRIORITY=-1
export ZMQ_THREAD_SCHED_POLICY=0

# Disable any other real-time scheduling attempts
export NO_RT_SCHEDULING=1
export DISABLE_THREAD_PRIORITY=1
export PTHREAD_INHERIT_SCHED=1

# Run the trading system with disabled RT scheduling
echo "Environment variables set:"
echo "  ZMQ_THREAD_PRIORITY=$ZMQ_THREAD_PRIORITY"
echo "  ZMQ_THREAD_SCHED_POLICY=$ZMQ_THREAD_SCHED_POLICY"
echo "  NO_RT_SCHEDULING=$NO_RT_SCHEDULING"
echo "  DISABLE_THREAD_PRIORITY=$DISABLE_THREAD_PRIORITY"
echo "  PTHREAD_INHERIT_SCHED=$PTHREAD_INHERIT_SCHED"

# Check current ulimit settings
echo ""
echo "Current system limits:"
echo "  Real-time priority: $(ulimit -r)"
echo "  Scheduling priority: $(ulimit -e)"

if [ $(ulimit -r) -eq 0 ]; then
    echo "  WARNING: Real-time priority is disabled (0)"
    echo "  This is expected on most VPS systems for security reasons"
fi

echo ""
echo "You can now run your trading system. Example:"
echo "  source disable_rt_scheduling.sh && ./your_trading_executable"