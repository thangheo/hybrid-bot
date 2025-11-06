# 🚨 CRITICAL BUG: CLASS MAPPING CONFLICT

## 🔴 PHÁT HIỆN VẤN ĐỀ NGHIÊM TRỌNG!

**Root cause của việc model chỉ BUY và không SELL: CLASS MAPPING INCONSISTENCY!**

## 🐛 Conflict Details

### 📍 **Definition 1** (bayesian_trading_model.hpp line 32):
```cpp
static constexpr int NUM_CLASSES = 3; // 0: BUY, 1: SELL, 2: NEUTRAL
```

### 📍 **Definition 2** (bayesian_trading_model.hpp line 129):
```cpp
// @param label 0 = SELL (price down), 1 = NEUTRAL, 2 = BUY (price up)
```

### 📍 **Definition 3** (example_usage.cpp line 50):
```cpp
// Return a class label: 0 SELL, 1 NEUTRAL, 2 BUY
```

### 📍 **Code Implementation** (bayesian_trading_model.cpp lines 246-251):
```cpp
if (prob_class0 > max_prob && prob_class0 > min_confidence_threshold_) {
    predicted_direction = Direction::BUY;  // Class 0 = BUY  ❌ WRONG!
}

if (prob_class1 > max_prob && prob_class1 > min_confidence_threshold_) {
    predicted_direction = Direction::SELL;  // Class 1 = SELL ❌ WRONG!
}
```

## 🚨 IMPACT ANALYSIS

### What's happening:
1. **Training**: Labels are stored as `0=SELL, 1=NEUTRAL, 2=BUY`
2. **Prediction**: Code treats `0=BUY, 1=SELL, 2=NEUTRAL` 
3. **Result**: Model trained to predict SELL (class 0) but code interprets as BUY!

### Why only BUY signals:
- Model learns: Strong negative returns → Class 0 (SELL in training)
- But code reads Class 0 as BUY → Only BUY signals generated!
- Class 1 (NEUTRAL in training) read as SELL → Weak SELL signals
- Class 2 (BUY in training) read as NEUTRAL → No BUY from actual BUY class!

## 🔧 FIXES NEEDED

### Fix 1: **Correct the prediction logic**
```cpp
// CORRECT mapping to match training labels: 0=SELL, 1=NEUTRAL, 2=BUY
if (prob_class0 > max_prob && prob_class0 > min_confidence_threshold_) {
    predicted_direction = Direction::SELL;  // ✅ Class 0 = SELL
    max_prob = prob_class0;
}

if (prob_class2 > max_prob && prob_class2 > min_confidence_threshold_) {
    predicted_direction = Direction::BUY;   // ✅ Class 2 = BUY  
    max_prob = prob_class2;
}
```

### Fix 2: **Update neutral veto logic**
```cpp
// Class 1 = NEUTRAL, not class 2!
if (prob_class1 > neutral_veto_threshold_) {
    return createNeutralSignal();
}
```

### Fix 3: **Fix position size calculation**
```cpp
signal.position_size = calculatePositionSize(prob_class2, prob_class0); // BUY, SELL
```

### Fix 4: **Fix entropy calculation**
```cpp
signal.uncertainty = calculateEntropy(prob_class0, prob_class1, prob_class2); // SELL, NEUTRAL, BUY
```

## 📊 Expected Results After Fix

### Before (Current - WRONG):
- Model trained: 0=SELL, 1=NEUTRAL, 2=BUY  
- Code reads: 0=BUY, 1=SELL, 2=NEUTRAL
- Result: Only BUY signals (from trained SELL class)

### After (CORRECTED):
- Model trained: 0=SELL, 1=NEUTRAL, 2=BUY
- Code reads: 0=SELL, 1=NEUTRAL, 2=BUY  
- Result: Balanced BUY/SELL signals

## 🎯 URGENT ACTION

**PRIORITY: CRITICAL** - This completely breaks the trading logic!

1. ✅ Fix class mapping in prediction code
2. ✅ Test with existing trained models  
3. ✅ Verify signal distribution becomes balanced
4. ✅ Update documentation to prevent future confusion

---

**This explains EXACTLY why you see only BUY and no SELL signals!** 🎯