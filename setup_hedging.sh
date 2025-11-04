#!/bin/bash

# Quick Setup Script for Hedging Integration
# Run this script to quickly configure hedging for your trading system

GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() {
    local color=$1
    local message=$2
    echo -e "${color}[$(date '+%H:%M:%S')] ${message}${NC}"
}

print_status "$BLUE" "🚀 HEDGING INTEGRATION SETUP"
echo "=============================================="

# Check if we're in the right directory
if [[ ! -f "trading_manager.sh" ]]; then
    print_status "$RED" "ERROR: Please run this script from the hybrid-bot root directory"
    exit 1
fi

# 1. Check dependencies
print_status "$BLUE" "📦 Checking dependencies..."

# Check if ccxt is installed
if python3 -c "import ccxt" 2>/dev/null; then
    print_status "$GREEN" "✅ ccxt library is installed"
else
    print_status "$YELLOW" "⚠️  Installing ccxt library..."
    if pip3 install ccxt; then
        print_status "$GREEN" "✅ ccxt installed successfully"
    else
        print_status "$RED" "❌ Failed to install ccxt"
        exit 1
    fi
fi

# Check if jq is available
if command -v jq &> /dev/null; then
    print_status "$GREEN" "✅ jq is available"
else
    print_status "$YELLOW" "⚠️  jq not found. Installing..."
    if sudo apt-get update && sudo apt-get install -y jq; then
        print_status "$GREEN" "✅ jq installed successfully"
    else
        print_status "$RED" "❌ Failed to install jq. Please install manually: sudo apt-get install jq"
    fi
fi

# 2. Setup hedging config
print_status "$BLUE" "⚙️  Setting up hedging configuration..."

hedging_config_path="python-strategy/hedging_config.json"

if [[ -f "$hedging_config_path" ]]; then
    print_status "$YELLOW" "Config file already exists: $hedging_config_path"
else
    print_status "$BLUE" "Creating hedging config file..."
    cat > "$hedging_config_path" << 'EOF'
{
  "hedging": {
    "enabled": false,
    "api_key": "",
    "api_secret": "",
    "testnet": true,
    "emergency_p_trend_threshold": 0.01,
    "close_p_trend_threshold": 0.9,
    "hedge_amount": 0.01,
    "leverage": 10,
    "symbols": ["BTC/USDT"]
  }
}
EOF
    print_status "$GREEN" "✅ Created hedging config file"
fi

# 3. Interactive configuration
print_status "$BLUE" "🔧 Interactive Configuration"
echo ""

read -p "Do you want to configure hedging now? (y/N): " configure_now
if [[ "$configure_now" =~ ^[Yy]$ ]]; then
    
    # Enable hedging
    read -p "Enable hedging? (y/N): " enable_hedging
    if [[ "$enable_hedging" =~ ^[Yy]$ ]]; then
        jq '.hedging.enabled = true' "$hedging_config_path" > "${hedging_config_path}.tmp" && \
        mv "${hedging_config_path}.tmp" "$hedging_config_path"
        print_status "$GREEN" "✅ Hedging enabled"
    fi
    
    # API configuration
    echo ""
    print_status "$YELLOW" "📡 API Configuration"
    echo "You can either:"
    echo "1. Set environment variables: BINANCE_API_KEY and BINANCE_API_SECRET"
    echo "2. Enter API keys directly in the config file"
    echo ""
    
    read -p "Do you want to enter API keys now? (y/N): " enter_keys
    if [[ "$enter_keys" =~ ^[Yy]$ ]]; then
        read -p "Enter Binance API Key: " api_key
        read -s -p "Enter Binance API Secret: " api_secret
        echo ""
        
        # Update config with API keys
        jq --arg key "$api_key" --arg secret "$api_secret" \
           '.hedging.api_key = $key | .hedging.api_secret = $secret' \
           "$hedging_config_path" > "${hedging_config_path}.tmp" && \
        mv "${hedging_config_path}.tmp" "$hedging_config_path"
        
        print_status "$GREEN" "✅ API keys saved to config"
    fi
    
    # Trading mode
    echo ""
    read -p "Use testnet mode for testing? (Y/n): " use_testnet
    if [[ ! "$use_testnet" =~ ^[Nn]$ ]]; then
        jq '.hedging.testnet = true' "$hedging_config_path" > "${hedging_config_path}.tmp" && \
        mv "${hedging_config_path}.tmp" "$hedging_config_path"
        print_status "$GREEN" "✅ Testnet mode enabled (SAFE FOR TESTING)"
    else
        jq '.hedging.testnet = false' "$hedging_config_path" > "${hedging_config_path}.tmp" && \
        mv "${hedging_config_path}.tmp" "$hedging_config_path"
        print_status "$RED" "⚠️  MAINNET mode enabled (REAL MONEY!)"
    fi
    
    # Thresholds
    echo ""
    print_status "$BLUE" "🎚️  Threshold Configuration"
    echo "Current emergency threshold: 0.01 (triggers short when p_trend_up <= 0.01)"
    echo "Current close threshold: 0.9 (closes short when p_trend_up >= 0.9)"
    read -p "Keep default thresholds? (Y/n): " keep_defaults
    
    if [[ "$keep_defaults" =~ ^[Nn]$ ]]; then
        read -p "Enter emergency threshold (0-1, default 0.01): " emergency_threshold
        read -p "Enter close threshold (0-1, default 0.9): " close_threshold
        
        emergency_threshold=${emergency_threshold:-0.01}
        close_threshold=${close_threshold:-0.9}
        
        jq --arg et "$emergency_threshold" --arg ct "$close_threshold" \
           '.hedging.emergency_p_trend_threshold = ($et | tonumber) | .hedging.close_p_trend_threshold = ($ct | tonumber)' \
           "$hedging_config_path" > "${hedging_config_path}.tmp" && \
        mv "${hedging_config_path}.tmp" "$hedging_config_path"
        
        print_status "$GREEN" "✅ Thresholds updated"
    fi
fi

# 4. Show final configuration
echo ""
print_status "$BLUE" "📋 Final Configuration:"
./trading_manager.sh hedge-config

# 5. Setup instructions
echo ""
print_status "$BLUE" "🎯 Next Steps:"
echo ""
echo "1. Test the configuration:"
echo "   cd python-strategy && python demo_hedging_integration.py"
echo ""
echo "2. Check hedging config anytime:"
echo "   ./trading_manager.sh hedge-config"
echo ""
echo "3. Start the trading system:"
echo "   ./trading_manager.sh start"
echo ""
echo "4. Monitor hedging logs:"
echo "   ./trading_manager.sh logs hedging"
echo ""
echo "5. Enable/disable hedging:"
echo "   ./trading_manager.sh hedge-enable"
echo "   ./trading_manager.sh hedge-disable"

print_status "$GREEN" "✅ Setup completed!"

if jq -r '.hedging.testnet' "$hedging_config_path" | grep -q "true"; then
    print_status "$YELLOW" "⚠️  Remember: You're in TESTNET mode. Switch to mainnet when ready for live trading."
else
    print_status "$RED" "🚨 WARNING: You're in MAINNET mode. Double-check everything before starting!"
fi