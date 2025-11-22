# Volatility Metrics Fix - Realized Vol 1m Always Zero

## Problem

**Symptom:**
- `realized_volatility_1m` always shows `0.0` in initial updates
- `realized_volatility_5m` and `expected_volatility_5m` work correctly

**Root Cause:**

OrderBookAnalyzer only calculates `realize_volatility_1m` when it has at least 2 volatility snapshots:

```cpp
// order_book_analyzer.cpp:188
if (volatilitySnapshots_.size() >= 2) {
    double rv_raw = calculateRV();
    currentMetrics_.realize_volatility_1m = rv_raw;  // Only set here
    // ...
}
```

Before having 2 snapshots:
- `currentMetrics_.realize_volatility_1m` remains at default value `0.0`
- This happens during warmup period (first few orderbook updates)

## Old Code (generateIntelligenceFromMetrics)

```cpp
// PROBLEM: No fallback when obMetrics.realize_volatility_1m is 0
ai->set_realized_volatility_1m(obMetrics.realize_volatility_1m);  // ← 0.0 during warmup
ai->set_realized_volatility_5m(obMetrics.realize_volatility_1m * 1.2);  // ← 0.0 * 1.2 = 0.0
ai->set_expected_volatility_5m(obMetrics.volatilityExpectation);
```

**Why 5m and expected worked:**
- Other parts of code might have been using the legacy method which has fallback
- Or volatilityExpectation is calculated differently

## Solution

Add fallback to `estimateSimpleVolatility()` when OrderBookAnalyzer doesn't have enough data yet:

```cpp
// FIXED: Use OrderBookAnalyzer metrics when available, fallback during warmup
double volatility_1m = obMetrics.realize_volatility_1m;
if (volatility_1m <= 0.0 || !std::isfinite(volatility_1m)) {
    // Fallback: estimate from spread when OrderBookAnalyzer doesn't have enough data yet
    volatility_1m = estimateSimpleVolatility(relative_spread, profile->volatility_factor);
}

ai->set_realized_volatility_1m(volatility_1m);  // ← Now has value during warmup
ai->set_realized_volatility_5m(volatility_1m * 1.2);  // ← Now calculated correctly

// Same for expected volatility
double expected_vol_5m = obMetrics.volatilityExpectation;
if (expected_vol_5m <= 0.0 || !std::isfinite(expected_vol_5m)) {
    expected_vol_5m = volatility_1m * 1.1;  // Simple estimate
}
ai->set_expected_volatility_5m(expected_vol_5m);
```

## Behavior

### During Warmup (< 2 snapshots)
```
obMetrics.realize_volatility_1m = 0.0  (not enough data)
    ↓
Fallback to estimateSimpleVolatility()
    ↓
volatility_1m = spread * volatility_factor * 5.0
    ↓
ai->realized_volatility_1m = 0.025 (example)  ✓ Has value
ai->realized_volatility_5m = 0.030           ✓ Calculated
```

### After Warmup (≥ 2 snapshots)
```
obMetrics.realize_volatility_1m = 0.0045  (calculated from snapshots)
    ↓
Use comprehensive calculation from OrderBookAnalyzer
    ↓
volatility_1m = 0.0045
    ↓
ai->realized_volatility_1m = 0.0045  ✓ Accurate
ai->realized_volatility_5m = 0.0054  ✓ Based on accurate 1m
```

## Benefits

### Before Fix
```
First 2 updates:
  realized_volatility_1m: 0.0  ← WRONG
  realized_volatility_5m: 0.0  ← WRONG (0.0 * 1.2)
  expected_volatility_5m: 0.0  ← WRONG

After 2 updates:
  realized_volatility_1m: 0.0045  ← OK
  realized_volatility_5m: 0.0054  ← OK
  expected_volatility_5m: 0.005   ← OK
```

### After Fix
```
First 2 updates:
  realized_volatility_1m: 0.025  ← OK (estimated)
  realized_volatility_5m: 0.030  ← OK (estimated)
  expected_volatility_5m: 0.0275 ← OK (estimated)

After 2 updates:
  realized_volatility_1m: 0.0045  ← OK (calculated)
  realized_volatility_5m: 0.0054  ← OK (calculated)
  expected_volatility_5m: 0.005   ← OK (calculated)
```

## Alternative Considered

Could also initialize volatilitySnapshots_ with first snapshot immediately, but:
- ❌ Would still need 2 snapshots for meaningful calculation
- ❌ First snapshot alone can't calculate variance
- ✅ Current solution (fallback) is cleaner

## Testing

To verify the fix works:

```cpp
// Check console output for first 3 intelligence updates per symbol
// Should see non-zero values for realized_volatility_1m immediately

╔═══════════════════════════════════════════════════════════╗
║ ENHANCED ADAPTIVE INTELLIGENCE: BTCUSDT                  ║
║ Volatility Metrics:                                      ║
║   Realized Vol 1m: 0.025    ← Should be > 0 (estimated)  ║
║   Expected Vol 5m: 0.0275   ← Should be > 0              ║
╚═══════════════════════════════════════════════════════════╝
```

After a few updates (once OrderBookAnalyzer has 2+ snapshots):
```
║   Realized Vol 1m: 0.0045   ← More accurate (calculated) ║
```

## Code Location

**File:** `/cpp-execution/src/market_intelligence/adaptive_intelligence.cpp`
**Method:** `generateIntelligenceFromMetrics()`
**Lines:** ~668-684

## Related

- OrderBookAnalyzer warmup period: requires 2+ snapshots
- `estimateSimpleVolatility()`: fallback calculation method
- Legacy `generateIntelligence()` already had this fallback logic

---

**Status:** ✅ Fixed
**Date:** 2025-11-22
**Impact:** Python now receives meaningful volatility values from first update
