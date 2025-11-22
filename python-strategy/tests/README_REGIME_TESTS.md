# Regime Detection Test Suite

Các test scripts để validate regime detection filter cho EMA Pullback with Imbalance strategy.

## 📁 Files

### 1. `test_regime_detection.py` - Comprehensive Test Suite
Test đầy đủ với synthetic data và visualizations.

**Features:**
- Generate synthetic data cho các loại regime khác nhau
- Test detection accuracy
- Visual plots (ADX, Choppiness, Hurst, etc.)
- Strategy integration test

**Chạy test:**
```bash
cd python-strategy/tests
python test_regime_detection.py
```

**Output:**
- Console logs với detection statistics
- PNG plots trong `tests/output/`
- Accuracy scores cho từng regime type

**Kết quả mong đợi:**
```
Regime Type     Accuracy    Tradeable %    Avg Confidence
------------------------------------------------------------
trending         >70%          >60%            >60%
ranging          >60%          <20%            <40%
choppy           >60%          <10%            <30%
volatile         >50%          <15%            <35%
```

---

### 2. `test_regime_real_data.py` - Quick Real Data Test
Test nhanh trên real/sample data.

**Features:**
- Load data từ CSV hoặc generate sample data
- Quick regime summary
- Export kết quả ra CSV

**Chạy test:**
```bash
cd python-strategy/tests
python test_regime_real_data.py
```

**Sử dụng real data:**
1. Đặt file CSV vào `data/BTCUSDT_5m.csv` với format:
   ```
   timestamp,open,high,low,close,volume
   2024-01-01 00:00:00,50000,50100,49900,50050,1234.5
   ...
   ```

2. Script sẽ tự động load và analyze

**Output:**
- Console summary với regime breakdown
- CSV export trong `tests/output/` với tất cả indicators

---

## 🎯 Regime Types Explained

### 1. **TRENDING** ✅ (TRADEABLE)
**Đặc điểm:**
- ADX > 25 (strong trend)
- Choppiness Index < 50
- Hurst > 0.55 (persistent trend)
- EMA separation > 1.5%

**Strategy behavior:**
- ✅ Generate signals
- High confidence entries
- Follow trend direction

---

### 2. **RANGING** ❌ (AVOID)
**Đặc điểm:**
- ADX < 20 (weak trend)
- Price stuck in narrow range
- EMA separation < 0.5%
- Choppiness Index > 61.8

**Strategy behavior:**
- ❌ No signals
- Avoid false breakouts
- Wait for clear trend

---

### 3. **CHOPPY** ❌ (AVOID)
**Đặc điểm:**
- Choppiness Index > 70
- Frequent direction changes
- Many EMA crossovers
- Bollinger Band squeeze

**Strategy behavior:**
- ❌ No signals
- High whipsaw risk
- Filter out noise

---

### 4. **VOLATILE** ⚠️ (CAUTION)
**Đặc điểm:**
- ATR percentile > 90
- Large price spikes
- Wide Bollinger Bands
- News events / flash crashes

**Strategy behavior:**
- ❌ Usually no signals
- Risk of large slippage
- Wait for stabilization

---

## 📊 Interpreting Results

### Good Detection (✅):
```
Regime: TRENDING
Tradeable: ✅ YES
Confidence: 85%

Indicators:
  ADX: 38.5 (Trending)
  Choppiness: 42.1 (Trending)
  Hurst: 0.62 (Trending)

Scores:
  Trending: 8/10
  Ranging: 1/10
  Choppy: 0/9
  Volatile: 0/5
```
→ **Strong trending market, safe to trade**

---

### Poor Detection (❌):
```
Regime: CHOPPY
Tradeable: ❌ NO
Confidence: 15%

Indicators:
  ADX: 12.3 (Weak/Ranging)
  Choppiness: 72.5 (Choppy)
  Hurst: 0.42 (Mean reverting)

Scores:
  Trending: 2/10
  Ranging: 4/10
  Choppy: 7/9
  Volatile: 1/5
```
→ **Choppy market, avoid trading**

---

## 🔧 Configuration Tuning

Nếu detection không chính xác, điều chỉnh thresholds trong `config/trading_bot.yaml`:

```yaml
ema_pullback_with_imbalance:
  regime_config:
    # Make detection MORE STRICT (fewer trades, higher quality)
    adx_trending: 30          # Was: 25 (higher = stricter)
    chop_threshold: 55        # Was: 61.8 (lower = stricter)
    hurst_trending_min: 0.60  # Was: 0.55 (higher = stricter)
    min_regime_confidence: 0.7  # Was: 0.6 (higher = stricter)

    # Make detection MORE RELAXED (more trades, lower quality)
    adx_trending: 20          # Was: 25 (lower = more relaxed)
    chop_threshold: 65        # Was: 61.8 (higher = more relaxed)
    hurst_trending_min: 0.50  # Was: 0.55 (lower = more relaxed)
    min_regime_confidence: 0.5  # Was: 0.6 (lower = more relaxed)
```

---

## 📈 Indicator Reference

### ADX (Average Directional Index)
- **< 20**: Weak trend / ranging
- **20-25**: Moderate trend
- **25-40**: Strong trend ✅
- **> 40**: Very strong trend ✅✅

### Choppiness Index
- **< 38.2**: Trending market ✅
- **38.2-61.8**: Transitioning
- **> 61.8**: Choppy market ❌

### Hurst Exponent
- **< 0.45**: Mean reverting ❌
- **0.45-0.55**: Random walk
- **> 0.55**: Trending / persistent ✅

### EMA Separation
- **< 0.5%**: EMAs too close ❌
- **0.5-1.5%**: Acceptable
- **> 1.5%**: Good separation ✅

---

## 🐛 Troubleshooting

### Issue: "No tradeable bars detected"
**Cause:** Thresholds quá strict

**Fix:**
1. Giảm `adx_trending` từ 25 → 20
2. Tăng `chop_threshold` từ 61.8 → 65
3. Giảm `min_regime_confidence` từ 0.6 → 0.5

---

### Issue: "Too many false signals"
**Cause:** Detection không đủ strict

**Fix:**
1. Tăng `adx_trending` từ 25 → 30
2. Giảm `chop_threshold` từ 61.8 → 55
3. Tăng `min_regime_confidence` từ 0.6 → 0.7

---

### Issue: "Hurst calculation errors"
**Cause:** Không đủ data

**Fix:**
1. Cần tối thiểu 100 bars để tính Hurst
2. Check `hurst_window` config (default: 100)

---

## 📞 Support

Nếu có vấn đề:
1. Check console output cho error messages
2. Verify data format (OHLCV columns)
3. Ensure minimum 200 bars of data
4. Review exported CSV in `tests/output/`

---

## ✅ Success Criteria

Regime filter hoạt động tốt khi:
- ✅ Trending markets: >60% bars tradeable
- ✅ Ranging markets: <20% bars tradeable
- ✅ Choppy markets: <10% bars tradeable
- ✅ Overall accuracy: >65% correct regime classification
- ✅ Average confidence: >55% for tradeable bars

---

**Happy Testing! 🚀**
