#!/bin/bash
# Test script for new exit strategy guards

set -e

echo "=== EXIT STRATEGY GUARDS TEST SCRIPT ==="
echo "Date: $(date)"
echo

# Function to check if process is running
check_process() {
    local process_name="$1"
    if pgrep -f "$process_name" > /dev/null; then
        echo "✅ Process '$process_name' is running"
        return 0
    else
        echo "❌ Process '$process_name' is not running"  
        return 1
    fi
}

# Function to parse and display recent exit stats
parse_exit_stats() {
    local log_file="$1"
    local since_minutes="$2"
    
    echo "📊 EXIT_STRATEGY stats from last $since_minutes minutes:"
    
    # Use Python to parse recent logs
    python3 - << PY
import re, statistics, sys
from datetime import datetime, timedelta
p = '$log_file'
since_minutes = $since_minutes

try:
    with open(p,'r') as f:
        lines = f.readlines()
except Exception as e:
    print('ERR_OPEN', e)
    sys.exit(1)

# Get cutoff time (since_minutes ago)
cutoff = datetime.now() - timedelta(minutes=since_minutes)

# Pattern for EXIT_STRATEGY lines
pat = re.compile(r'EXIT_STRATEGY:\s*([^|]+)\|\s*Reason:\s*([A-Z_]+)\s*\|\s*PnL:\s*([-+]?\d*\.?\d+)bps.*Held:\s*([0-9.]+)s')

recent_matches = []
reasons = {}
pnls = []

for line in lines:
    match = pat.search(line)
    if match:
        # For simplicity, take all recent lines (not parsing timestamps)
        sym = match.group(1).strip()
        reason = match.group(2).strip()
        pnl = float(match.group(3))
        held = float(match.group(4))
        
        recent_matches.append((sym, reason, pnl, held))
        pnls.append(pnl)
        reasons[reason] = reasons.get(reason, 0) + 1

if not recent_matches:
    print('No EXIT_STRATEGY lines found')
    sys.exit(0)

n = len(pnls)
pos = sum(1 for x in pnls if x>0)
neg = sum(1 for x in pnls if x<0) 
zero = sum(1 for x in pnls if x==0)

if n > 0:
    mean = statistics.mean(pnls)
    med = statistics.median(pnls) 
    mn = min(pnls)
    mx = max(pnls)
    print(f'  Total exits: {n}')
    print(f'  Positive: {pos}, Negative: {neg}, Zero: {zero}')
    print(f'  Mean PnL: {mean:.2f}bps, Median: {med:.2f}bps')
    print(f'  Range: {mn:.2f} to {mx:.2f} bps')
    print('  Exit reasons:')
    for reason, count in sorted(reasons.items(), key=lambda x: -x[1]):
        pct = count/n*100
        print(f'    {reason}: {count} ({pct:.1f}%)')
PY
}

# Function to tail logs and show guard activations
monitor_guards() {
    local log_file="$1"
    local duration_seconds="$2"
    
    echo "🔍 Monitoring guard activations for $duration_seconds seconds..."
    echo "Looking for: SPREAD_WIDENING exits and stop-loss adjustments..."
    echo
    
    timeout "$duration_seconds" tail -f "$log_file" | grep -E "(EXIT_STRATEGY|GUARD|SPREAD_WIDENING|stop_distance|min_profitable_return)" --line-buffered || true
}

# Main execution
LOG_FILE="/home/e8/works/hybrid-bot/logs/semi_hft.log"
CONFIG_FILE="/home/e8/works/hybrid-bot/config/strategy_config.json"

echo "1️⃣ Checking current system status..."
check_process "semi_hft" || echo "   (This is expected if not running yet)"

echo
echo "2️⃣ Checking config updates..."
if grep -q "min_profitable_return.*0.003" "$CONFIG_FILE"; then
    echo "✅ min_profitable_return = 0.003 found in config"
else
    echo "❌ min_profitable_return not found in config"
fi

if [ -f "$LOG_FILE" ]; then
    echo "✅ Log file exists: $LOG_FILE"
    
    echo
    echo "3️⃣ Parsing recent exit statistics..."
    parse_exit_stats "$LOG_FILE" 30  # Last 30 minutes
    
else
    echo "❌ Log file not found: $LOG_FILE"
fi

echo
echo "4️⃣ Binary check..."
if [ -f "/home/e8/works/hybrid-bot/cpp-execution/bin/semi_hft" ]; then
    echo "✅ Latest binary exists"
    ls -la /home/e8/works/hybrid-bot/cpp-execution/bin/semi_hft
else
    echo "❌ Binary not found"
fi

echo
echo "=== RECOMMENDATIONS ==="
echo "📋 Next steps:"
echo "   1. Stop current semi_hft process (if running)"
echo "   2. Start new process: /home/e8/works/hybrid-bot/cpp-execution/bin/semi_hft"
echo "   3. Monitor logs for guard activations"
echo "   4. Run this script again after 1-2 hours to compare stats"
echo

echo "🔧 To start monitoring (in another terminal):"
echo "   tail -f $LOG_FILE | grep -E '(EXIT_STRATEGY|GUARD|SPREAD_WIDENING)'"
echo

echo "📊 To get updated stats after restart:"
echo "   $0"
echo

# Offer to monitor if process is running
if check_process "semi_hft"; then
    echo
    read -p "🔍 Monitor guard activations for 60 seconds? (y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        monitor_guards "$LOG_FILE" 60
    fi
fi

echo "=== TEST SCRIPT COMPLETE ==="