# ================================================================
# src/strategy/filters/regime_detector.py
# Detect market regime to avoid trading in unfavorable conditions
# ================================================================

import pandas as pd
import numpy as np
from typing import Dict, Tuple
from ..indicators.technical_indicators import TechnicalIndicators

class MarketRegimeDetector:
    """
    Detect market regime to filter out ranging/choppy/mean-reverting periods

    IMPROVEMENTS:
    - Priority hierarchy (volatile > choppy > ranging > trending)
    - Regime smoothing to reduce flickering
    - Better volatile detection
    - Reduced Hurst weight or replacement
    - Enhanced choppy detection
    """

    def __init__(self, config: dict):
        self.config = config

        # ADX thresholds
        self.adx_trending_threshold = config.get('adx_trending', 25)  # ADX > 25 = trending
        self.adx_strong_trend = config.get('adx_strong_trend', 40)

        # Volatility thresholds
        self.atr_percentile_threshold = config.get('atr_percentile', 30)  # ATR > 30th percentile

        # Bollinger Band squeeze
        self.bb_squeeze_threshold = config.get('bb_squeeze_threshold', 0.5)

        # EMA separation
        self.ema_separation_min = config.get('ema_separation_min', 0.015)  # 1.5% minimum gap

        # Choppiness index
        self.chop_threshold = config.get('chop_threshold', 61.8)  # CI > 61.8 = choppy

        # Hurst exponent (reduced importance)
        self.hurst_trending_min = config.get('hurst_trending_min', 0.55)  # H > 0.55 = trending
        self.use_hurst = config.get('use_hurst', False)  # Disabled by default

        # NEW: Regime smoothing
        self.regime_smoothing_window = config.get('regime_smoothing_window', 3)  # Consensus filter

        # NEW: Volatile detection
        self.volatile_atr_threshold = config.get('volatile_atr_threshold', 85)  # ATR > 85th percentile
        self.volatile_return_threshold = config.get('volatile_return_threshold', 90)  # Return > 90th percentile

        # NEW: Score thresholds for priority hierarchy
        self.volatile_score_threshold = config.get('volatile_score_threshold', 6)
        self.choppy_score_threshold = config.get('choppy_score_threshold', 6)
        self.ranging_score_threshold = config.get('ranging_score_threshold', 6)
        self.trending_score_threshold = config.get('trending_score_threshold', 6)
        
    def _apply_regime_smoothing(self, regime_raw: pd.Series) -> pd.Series:
        """
        Apply consensus filter to reduce regime flickering

        Uses mode (most common value) over a rolling window
        """
        if self.regime_smoothing_window <= 1:
            return regime_raw

        regime_smoothed = regime_raw.copy()

        # Apply rolling mode
        for i in range(len(regime_raw)):
            start_idx = max(0, i - self.regime_smoothing_window + 1)
            window_values = regime_raw.iloc[start_idx:i+1]

            # Get mode (most common value)
            if len(window_values) > 0:
                mode_value = window_values.mode()
                if len(mode_value) > 0:
                    regime_smoothed.iloc[i] = mode_value.iloc[0]

        return regime_smoothed

    def calculate_adx(self, high: pd.Series, low: pd.Series, close: pd.Series,
                     period: int = 14) -> pd.Series:
        """Calculate Average Directional Index"""
        return TechnicalIndicators.adx(high, low, close, period)
    
    def calculate_choppiness_index(self, high: pd.Series, low: pd.Series, 
                                   close: pd.Series, period: int = 14) -> pd.Series:
        """
        Choppiness Index (CI)
        CI > 61.8 = choppy/ranging
        CI < 38.2 = trending
        """
        tr = TechnicalIndicators.true_range(high, low, close)
        atr_sum = tr.rolling(period).sum()
        
        high_low_range = high.rolling(period).max() - low.rolling(period).min()
        
        ci = 100 * np.log10(atr_sum / high_low_range) / np.log10(period)
        return ci.fillna(50)
    
    def calculate_hurst_exponent(self, prices: pd.Series, window: int = 100) -> pd.Series:
        """
        Hurst Exponent (simplified rolling version)
        H < 0.5 = mean reverting
        H = 0.5 = random walk
        H > 0.5 = trending
        """
        def hurst_window(series):
            if len(series) < 20:
                return 0.5
            
            lags = range(2, min(20, len(series) // 2))
            tau = [np.std(np.subtract(series[lag:], series[:-lag])) for lag in lags]
            
            # Filter out zeros to avoid log errors
            valid_indices = [i for i, t in enumerate(tau) if t > 0]
            if len(valid_indices) < 2:
                return 0.5
            
            lags_valid = [lags[i] for i in valid_indices]
            tau_valid = [tau[i] for i in valid_indices]
            
            poly = np.polyfit(np.log(lags_valid), np.log(tau_valid), 1)
            return poly[0]
        
        hurst = prices.rolling(window).apply(hurst_window, raw=False)
        return hurst.fillna(0.5)
    
    def calculate_bollinger_bandwidth(self, close: pd.Series, period: int = 20, 
                                     std_dev: float = 2.0) -> pd.Series:
        """Calculate Bollinger Band Width (normalized)"""
        bb = TechnicalIndicators.bollinger_bands(close, period, std_dev)
        bandwidth = (bb['upper'] - bb['lower']) / bb['middle']
        return bandwidth
    
    def detect_regime(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        """
        Detect market regime for each bar
        
        Returns dict with:
            - regime: 'trending', 'ranging', 'choppy', 'volatile'
            - is_tradeable: bool (True if favorable for EMA pullback strategy)
            - confidence: 0-1 score
        """
        high = data['high']
        low = data['low']
        close = data['close']
        
        # === Calculate regime indicators ===
        
        # 1. ADX (trend strength)
        adx = self.calculate_adx(high, low, close, 14)
        
        # 2. Choppiness Index
        chop_index = self.calculate_choppiness_index(high, low, close, 14)
        
        # 3. Hurst Exponent (trending vs mean reverting)
        hurst = self.calculate_hurst_exponent(close, 100)
        
        # 4. ATR percentile rank (volatility)
        atr = TechnicalIndicators.atr(high, low, close, 14)
        atr_percentile = atr.rolling(100).apply(
            lambda x: pd.Series(x).rank(pct=True).iloc[-1] * 100 if len(x) > 0 else 50
        )
        
        # 5. Bollinger Band Width
        bb_width = self.calculate_bollinger_bandwidth(close, 20, 2.0)
        bb_width_percentile = bb_width.rolling(100).apply(
            lambda x: pd.Series(x).rank(pct=True).iloc[-1] * 100 if len(x) > 0 else 50
        )
        
        # 6. EMA separation
        ema_20 = TechnicalIndicators.ema(close, 20)
        ema_50 = TechnicalIndicators.ema(close, 50)
        ema_separation = abs(ema_20 - ema_50) / ema_50
        
        # 7. Price range consistency (detect sideways)
        rolling_high = high.rolling(20).max()
        rolling_low = low.rolling(20).min()
        price_range_consistency = (close - rolling_low) / (rolling_high - rolling_low)
        is_stuck_in_range = (price_range_consistency > 0.3) & (price_range_consistency < 0.7)
        
        # === Calculate additional indicators for better detection ===

        # Absolute returns percentile (for volatile detection)
        returns = close.pct_change().abs()
        returns_percentile = returns.rolling(100).apply(
            lambda x: pd.Series(x).rank(pct=True).iloc[-1] * 100 if len(x) > 0 else 50
        )

        # High-Low range relative to ATR (for volatile detection)
        hl_range = (high - low) / (atr + 1e-10)
        hl_range_percentile = hl_range.rolling(100).apply(
            lambda x: pd.Series(x).rank(pct=True).iloc[-1] * 100 if len(x) > 0 else 50
        )

        # === Regime Classification with IMPROVED SCORING ===
        regime_raw = pd.Series('unknown', index=data.index)
        is_tradeable = pd.Series(False, index=data.index)
        confidence = pd.Series(0.0, index=data.index)

        # TRENDING (favorable for EMA pullback)
        # Reduced Hurst weight, increased ADX/Chop importance
        if self.use_hurst:
            trending_score = (
                (adx > self.adx_trending_threshold).astype(int) * 3 +  # Weight: 3
                (hurst > self.hurst_trending_min).astype(int) * 1 +    # Weight: 1 (reduced!)
                (chop_index < 50).astype(int) * 3 +                    # Weight: 3 (increased!)
                (ema_separation > self.ema_separation_min).astype(int) * 2 +  # Weight: 2
                (atr_percentile > self.atr_percentile_threshold).astype(int) * 1  # Weight: 1
            )
            # Max score = 10
        else:
            # Without Hurst - rebalanced
            trending_score = (
                (adx > self.adx_trending_threshold).astype(int) * 4 +  # Weight: 4
                (chop_index < 50).astype(int) * 3 +                    # Weight: 3
                (ema_separation > self.ema_separation_min).astype(int) * 2 +  # Weight: 2
                (atr_percentile > self.atr_percentile_threshold).astype(int) * 1  # Weight: 1
            )
            # Max score = 10
        
        # RANGING (unfavorable)
        ranging_score = (
            (adx < 20).astype(int) * 3 +
            (chop_index > self.chop_threshold).astype(int) * 3 +
            (ema_separation < 0.005).astype(int) * 2 +
            is_stuck_in_range.astype(int) * 2
        )
        # Max score = 10

        # CHOPPY (very unfavorable) - IMPROVED
        if self.use_hurst:
            choppy_score = (
                (adx < 15).astype(int) * 3 +                          # Weight: 3 (increased!)
                (chop_index > 70).astype(int) * 4 +                   # Weight: 4 (increased!)
                (bb_width_percentile < 20).astype(int) * 2 +          # Weight: 2
                (hurst < 0.45).astype(int) * 1                        # Weight: 1 (reduced!)
            )
            # Max score = 10
        else:
            choppy_score = (
                (adx < 15).astype(int) * 3 +                          # Weight: 3
                (chop_index > 70).astype(int) * 5 +                   # Weight: 5
                (bb_width_percentile < 20).astype(int) * 2            # Weight: 2
            )
            # Max score = 10

        # HIGH VOLATILITY / NEWS EVENT (unfavorable) - IMPROVED!
        volatile_score = (
            (atr_percentile > self.volatile_atr_threshold).astype(int) * 4 +      # Weight: 4 (increased!)
            (returns_percentile > self.volatile_return_threshold).astype(int) * 3 + # Weight: 3 (NEW!)
            (bb_width_percentile > 85).astype(int) * 2 +                          # Weight: 2
            (hl_range_percentile > 85).astype(int) * 1                            # Weight: 1 (NEW!)
        )
        # Max score = 10

        # === Assign regime using PRIORITY HIERARCHY (not max-score!) ===
        for i in range(len(data)):
            idx = data.index[i]

            vol_score = volatile_score.iloc[i]
            chop_score = choppy_score.iloc[i]
            rang_score = ranging_score.iloc[i]
            trend_score = trending_score.iloc[i]

            # Priority: volatile > choppy > ranging > trending
            if vol_score >= self.volatile_score_threshold:
                regime_raw.iloc[i] = 'volatile'
                confidence.iloc[i] = min(vol_score / 10.0, 1.0)
                is_tradeable.iloc[i] = False

            elif chop_score >= self.choppy_score_threshold:
                regime_raw.iloc[i] = 'choppy'
                confidence.iloc[i] = min(chop_score / 10.0, 1.0)
                is_tradeable.iloc[i] = False

            elif rang_score >= self.ranging_score_threshold:
                regime_raw.iloc[i] = 'ranging'
                confidence.iloc[i] = min(rang_score / 10.0, 1.0)
                is_tradeable.iloc[i] = False

            elif trend_score >= self.trending_score_threshold:
                regime_raw.iloc[i] = 'trending'
                confidence.iloc[i] = min(trend_score / 10.0, 1.0)
                is_tradeable.iloc[i] = True

            else:
                # No clear regime
                regime_raw.iloc[i] = 'unknown'
                confidence.iloc[i] = 0.0
                is_tradeable.iloc[i] = False

        # === Apply regime smoothing to reduce flickering ===
        regime = self._apply_regime_smoothing(regime_raw)
        
        # === Return regime data ===
        return {
            'regime': regime,
            'regime_raw': regime_raw,  # Before smoothing
            'is_tradeable': is_tradeable,
            'confidence': confidence,

            # Raw indicators for debugging
            'adx': adx,
            'chop_index': chop_index,
            'hurst': hurst,
            'atr_percentile': atr_percentile,
            'returns_percentile': returns_percentile,  # NEW
            'bb_width_percentile': bb_width_percentile,
            'hl_range_percentile': hl_range_percentile,  # NEW
            'ema_separation': ema_separation,

            # Scores
            'trending_score': trending_score,
            'ranging_score': ranging_score,
            'choppy_score': choppy_score,
            'volatile_score': volatile_score
        }
    
    def get_regime_summary(self, regime_data: Dict) -> str:
        """Get human-readable regime summary for latest bar"""
        latest = {k: v.iloc[-1] if hasattr(v, 'iloc') else v
                 for k, v in regime_data.items()}

        summary = f"""
📊 Market Regime Analysis:
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Regime: {latest['regime'].upper()}
Tradeable: {'✅ YES' if latest['is_tradeable'] else '❌ NO'}
Confidence: {latest['confidence']:.1%}

📈 Indicators:
  ADX: {latest['adx']:.1f} ({'Strong trend' if latest['adx'] > 40 else 'Trending' if latest['adx'] > 25 else 'Weak/Ranging'})
  Choppiness: {latest['chop_index']:.1f} ({'Choppy' if latest['chop_index'] > 61.8 else 'Trending' if latest['chop_index'] < 38.2 else 'Neutral'})
  Hurst: {latest['hurst']:.2f} ({'Trending' if latest['hurst'] > 0.55 else 'Mean reverting' if latest['hurst'] < 0.45 else 'Random'})

🎯 Scores:
  Trending: {latest['trending_score']:.0f}/10
  Ranging: {latest['ranging_score']:.0f}/10
  Choppy: {latest['choppy_score']:.0f}/9
  Volatile: {latest['volatile_score']:.0f}/5
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
"""
        return summary

    def should_exit_position(self, current_regime: str, entry_regime: str,
                           confidence: float, min_confidence: float = 0.4) -> bool:
        """
        Check if position should be exited due to regime change

        Args:
            current_regime: Current market regime
            entry_regime: Regime when position was entered
            confidence: Current regime confidence
            min_confidence: Minimum confidence to stay in position

        Returns:
            True if should exit position due to regime change
        """
        # Exit if regime changed from trending to unfavorable
        unfavorable_regimes = ['choppy', 'ranging', 'volatile']

        # If we entered during trending regime
        if entry_regime == 'trending':
            # Exit if regime switched to unfavorable
            if current_regime in unfavorable_regimes:
                return True

            # Exit if confidence drops too low (even if still "trending")
            if confidence < min_confidence:
                return True

        return False