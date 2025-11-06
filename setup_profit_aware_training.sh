#!/bin/bash

# Demo script for profit-aware training
# Thay đổi cách training để chỉ tạo signals khi expected profit > 0.1%

echo "========================================="
echo "PROFIT-AWARE TRAINING DEMO"
echo "Giải quyết vấn đề: trades bị lỗ do fees 0.1%"
echo "========================================="

# Navigate to python-strategy directory
cd /home/e8/works/hybrid-bot/python-strategy

# Create training directory if it doesn't exist
mkdir -p src/training

# Make sure the enhanced training script is executable
chmod +x enhanced_regime_training.py

echo "1. Chạy enhanced training với profit-aware labeling..."
python3 enhanced_regime_training.py --min-profit 0.002 --confidence 0.6

echo ""
echo "2. Kết quả được lưu tại:"
echo "   - enhanced_regime_analysis.png (visualization)"  
echo "   - enhanced_regime_results.csv (data)"
echo "   - profit_aware_model.joblib (model)"

echo ""
echo "3. So sánh traditional vs profit-aware:"
echo "   - Traditional: tạo signals dựa trên percentile threshold"
echo "   - Profit-aware: chỉ tạo signals khi expected profit > 0.2% (sau trừ 0.1% fee)"

echo ""
echo "========================================="
echo "HƯỚNG DẪN TÍCH HỢP VÀO C++ SYSTEM:"
echo "========================================="
echo "1. C++ code đã được enhance trong:"
echo "   - /cpp-execution/include/strategy/types/profit_aware_labeling.hpp"
echo "   - /cpp-execution/src/strategy/pattern/imbalance_detection_engine.cpp"
echo ""
echo "2. Compile C++ system:"
echo "   cd /home/e8/works/hybrid-bot/cpp-execution"
echo "   mkdir -p build && cd build"
echo "   cmake .. && make"
echo ""
echo "3. Key changes trong C++:"
echo "   - ProfitAwareLabeling class tracks historical profit/loss"
echo "   - Chỉ label BUY/SELL khi calculateExpectedProfit() > threshold"
echo "   - Update realized profit sau mỗi trade để improve model"
echo ""
echo "4. Config thay đổi:"
echo "   - minProfitableReturn: 0.003 (0.3% = 0.1% fee + 0.2% profit)"
echo "   - confidenceThreshold: 0.65 (chỉ trade khi confidence cao)"
echo "   - tradingCost: 0.001 (0.1% fee)"

echo ""
echo "========================================="
echo "VÍ DỤ SỬ DỤNG TRONG LIVE TRADING:"
echo "========================================="

cat << 'EOF'
// Before labeling:
double expectedProfit = g_profitLabeling.calculateExpectedProfit(
    normalizedReturn, regimeIdx, confidence, vol
);

if (expectedProfit > 0.002) {  // 0.2% expected profit
    // Only then create BUY/SELL label
    labelDirection = normalizedReturn > 0 ? BUY : SELL;
} else {
    labelDirection = NEUTRAL;  // Skip this trade
}

// After trade execution:
double realizedProfit = actualReturn - 0.001;  // Subtract 0.1% fee
g_profitLabeling.updateHistory(
    normalizedReturn, realizedProfit, regimeIdx, confidence, vol
);
EOF

echo ""
echo "=========================================" 
echo "EXPECTED RESULTS:"
echo "========================================="
echo "- Giảm số lượng trades không profitable"
echo "- Tăng win rate bằng cách skip trades có expected return < 0.2%"
echo "- Model học từ historical outcomes để improve prediction"
echo "- Adaptive thresholds dựa trên regime và market conditions"
echo ""
echo "✓ Setup complete! Model sẽ chỉ generate signals"
echo "  khi expected profit > trading fees (0.1%)"
echo "========================================="