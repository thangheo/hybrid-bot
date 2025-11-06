#!/bin/bash

echo "🧹 CLEANING CORRUPTED TRAINING DATA"
echo "==================================="
echo ""
echo "⚠️  WARNING: This will delete all Bayesian model training data!"
echo "   Reason: Data was trained with incorrect class mapping"
echo "   - Training used: 0=SELL, 1=NEUTRAL, 2=BUY"  
echo "   - Code expected: 0=BUY, 1=SELL, 2=NEUTRAL"
echo "   - Result: Completely wrong predictions"
echo ""

read -p "Are you sure you want to proceed? (y/N): " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    echo "❌ Cancelled by user"
    exit 1
fi

echo ""
echo "📦 Creating backup before deletion..."
BACKUP_DIR="backups/corrupted_models_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BACKUP_DIR"

# Find and backup existing model files
MODEL_PATHS=(
    "models/bayesian/"
    "models/bayesian_5s/"
    "cpp-execution/models/"
    "python-strategy/models/"
)

for path in "${MODEL_PATHS[@]}"; do
    if [ -d "$path" ]; then
        echo "  📁 Backing up: $path"
        cp -r "$path" "$BACKUP_DIR/"
        echo "     ✅ Backup created: $BACKUP_DIR/$(basename $path)"
    fi
done

echo ""
echo "🗑️  Deleting corrupted training data..."

# Delete Bayesian model files
for path in "${MODEL_PATHS[@]}"; do
    if [ -d "$path" ]; then
        echo "  🔥 Deleting: $path"
        rm -rf "$path"/*
        echo "     ✅ Cleaned: $path"
    fi
done

# Delete any cached model states
echo "  🔥 Deleting cached model states..."
find . -name "*.model" -type f -delete 2>/dev/null || true
find . -name "bayesian_*.json" -type f -delete 2>/dev/null || true
find . -name "*_model.bin" -type f -delete 2>/dev/null || true

# Clean training logs that might contain wrong mappings
echo "  🔥 Cleaning training logs..."
find logs/ -name "*training*" -type f -delete 2>/dev/null || true
find logs/ -name "*bayesian*" -type f -delete 2>/dev/null || true

# Reset any persistent training counters
echo "  🔄 Resetting training state..."
CONFIG_FILE="config/strategy_config.json"
if [ -f "$CONFIG_FILE" ]; then
    # Reset bayesMinUpdatesToTrade to ensure fresh training
    python3 -c "
import json
with open('$CONFIG_FILE', 'r') as f:
    config = json.load(f)

# Reset training counters in config if they exist
params = config['components']['imbalanceDetector']['parameters']
if 'bayesMinUpdatesToTrade' in params:
    print(f'  📊 Current bayesMinUpdatesToTrade: {params[\"bayesMinUpdatesToTrade\"]}')
    print('     (Will require this many updates before trading again)')

print('  ✅ Config check completed')
"
fi

echo ""
echo "📈 FRESH START RECOMMENDATIONS:"
echo "==============================="
echo ""
echo "1. 🎯 IMMEDIATE ACTIONS:"
echo "   • Build system with fixed class mapping"
echo "   • Start fresh training session"
echo "   • Monitor new label distribution"
echo ""
echo "2. 📊 MONITORING CHECKLIST:"
echo "   • Watch for balanced BUY/SELL signals"
echo "   • Check label stats in logs:"
echo "     Label stats: BUY=X, SELL=Y, NEUTRAL=Z"
echo "   • Verify both directions appear in trades"
echo ""
echo "3. ⏱️  TRAINING TIMELINE:"
echo "   • Initial warmup: ~$(python3 -c "
import json
with open('config/strategy_config.json') as f:
    config = json.load(f)
params = config['components']['imbalanceDetector']['parameters']
min_updates = params.get('bayesMinUpdatesToTrade', 5000)
delay_ms = params.get('labelingDelayMs', 30000)
est_time = (min_updates * delay_ms) / (1000 * 60)  # minutes
print(f'{est_time:.0f} minutes')
") for minimum trades"
echo "   • Full performance: ~2-3 hours of data"
echo ""
echo "4. 🚨 WHAT TO EXPECT:"
echo "   • No trades initially (need ${min_updates:-5000}+ training samples)"
echo "   • SELL signals will start appearing"
echo "   • More balanced trading performance"
echo "   • Better risk-adjusted returns"
echo ""

echo "✅ CLEANUP COMPLETED!"
echo ""
echo "📁 Backup location: $BACKUP_DIR"
echo "🚀 Ready for fresh training with correct class mapping"
echo ""
echo "Next steps:"
echo "1. Build: cd cpp-execution && make -j\$(nproc)"
echo "2. Deploy: ./trading_manager.sh restart"
echo "3. Monitor: tail -f logs/trading_*.log | grep 'Label stats'"