#!/bin/bash

echo "=== Debug Position Limit Check ==="

# Kiểm tra cấu hình hiện tại
echo "1. Current position limits from config:"
cat config/risk_config.json | grep -A 20 "positionLimit"

echo -e "\n2. Checking current positions and balances:"

# Kiểm tra các file log để tìm thông tin về position hiện tại
echo "Looking for recent position data in logs..."
find logs/ -name "*.log" -exec grep -l "position\|balance\|notional" {} \; 2>/dev/null

echo -e "\n3. Looking for price cache issues:"
find logs/ -name "*.log" -exec grep -l "price.*cache\|getCurrentPrice\|market.*data" {} \; 2>/dev/null

echo -e "\n4. Potential causes for position limit failure:"
echo "   a) Current position + new order > USD limit"
echo "   b) Price cache returning 0 or invalid price"
echo "   c) Position tracker returning incorrect current position"
echo "   d) Asset limit too low compared to current market value"

echo -e "\n=== Debugging Steps ==="
echo "1. Check current BTC price and your position"
echo "2. Calculate: position_quantity * btc_price = notional_usd"
echo "3. Compare with limit from config (BTC: 5000 USD)"
echo "4. If notional > 5000, that's why signal is rejected"