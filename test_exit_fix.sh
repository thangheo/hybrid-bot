#!/bin/bash

# Test script for exit order fix
echo "🚀 TESTING EXIT ORDER FIX"
echo "========================="

# 1. Backup current process if running
echo "1. Checking for running semi_hft process..."
PID=$(pgrep -f "semi_hft" || true)
if [ -n "$PID" ]; then
    echo "   Found running process PID: $PID"
    echo "   Stopping gracefully..."
    kill -TERM $PID
    sleep 5
    if pgrep -f "semi_hft" > /dev/null; then
        echo "   Force killing..."
        pkill -9 -f "semi_hft"
    fi
    echo "   ✅ Process stopped"
else
    echo "   ℹ️  No running process found"
fi

# 2. Verify build
echo ""
echo "2. Verifying build..."
if [ -f "/home/e8/works/hybrid-bot/cpp-execution/bin/semi_hft" ]; then
    echo "   ✅ Binary exists"
else
    echo "   ❌ Binary missing - rebuilding..."
    cd /home/e8/works/hybrid-bot/cpp-execution
    cmake --build build -j$(nproc)
fi

# 3. Start new process with logging
echo ""
echo "3. Starting semi_hft with exit fix..."
cd /home/e8/works/hybrid-bot

# Clear old logs
> logs/semi_hft.log

# Start in background
nohup ./cpp-execution/bin/semi_hft > logs/startup.log 2>&1 &
NEW_PID=$!

echo "   🚀 Started with PID: $NEW_PID"
echo "   📝 Logs: logs/semi_hft.log"
echo "   📋 Startup: logs/startup.log"

# 4. Monitor for first few minutes
echo ""
echo "4. Monitoring for 60 seconds..."
echo "   Looking for:"
echo "   - ✅ EXIT CALLBACK registration"
echo "   - ✅ EXIT order execution (not just logging)"
echo "   - ✅ Reduced FREQUENCY BLOCK for exits"

# Wait for startup
sleep 10

# Check for exit-related log messages
echo ""
echo "📊 LOG ANALYSIS (last 60 seconds):"
timeout 50 bash -c '
while true; do
    # Look for exit-related messages
    if grep -q "EXIT CALLBACK" logs/semi_hft.log; then
        echo "✅ EXIT CALLBACK found in logs"
        break
    fi
    if grep -q "EXECUTING EXIT ORDER" logs/semi_hft.log; then
        echo "✅ EXIT ORDER execution found"
        break  
    fi
    sleep 2
done
'

echo ""
echo "📈 RECENT EXIT_STRATEGY ACTIVITY:"
tail -20 logs/semi_hft.log | grep "EXIT_STRATEGY" || echo "   (No EXIT_STRATEGY lines yet)"

echo ""
echo "🚫 FREQUENCY BLOCKS:"  
tail -50 logs/semi_hft.log | grep "FREQUENCY BLOCK" | tail -5 || echo "   (No frequency blocks yet)"

echo ""
echo "✅ TEST SETUP COMPLETE"
echo "========================="
echo "🔍 Monitor with: tail -f logs/semi_hft.log | grep -E 'EXIT|FREQUENCY'"
echo "📊 PnL analysis: python3 -c \"
import re
with open('logs/semi_hft.log') as f:
    lines = f.readlines()
exits = [l for l in lines if 'EXIT_STRATEGY:' in l]
print(f'Total exits: {len(exits)}')
for e in exits[-5:]:
    print(f'  {e.strip()}')
\""

echo ""
echo "⚠️  WATCH FOR:"
echo "   - Fewer TIME_LIMIT exits"  
echo "   - More actual SELL order executions"
echo "   - EXIT orders bypassing frequency blocks"
echo ""
echo "Let it run for 30+ minutes then check results!"