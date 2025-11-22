# ================================================================
# src/strategy/filters/simple_trend_filter.py
# MINIMAL but EFFECTIVE filter for EMA pullback strategy
# Only 2-3 core filters, NO complex regime detection
# ================================================================

import pandas as pd
import numpy as np
from typing import Dict, Tuple
from ..indicators.technical_indicators import TechnicalIndicators


class SimpleTrendFilter:
    """
    Minimal trend filter - only avoid flatline EMA periods

    Philosophy:
    - EMA pullback works in ALL trending markets (slow/fast/volatile)
    - EMA pullback ONLY fails when EMA is flat (sideways)
    - So we only need to detect: "Are EMAs separated enough?"

    NO Hurst, NO Choppiness, NO complex scoring
    """

    def __init__(self, config: dict):
        self.config = config

        # === Core Filter 1: ADX (trend existence) ===
        self.adx_period = config.get('adx_period', 14)
        self.adx_min = config.get('adx_min', 20)  # ADX > 20 = có trend

        # === Core Filter 2: EMA Separation (trend persistence) ===
        self.ema_sep_min = config.get('ema_sep_min', 0.012)  # 1.2% minimum gap

        # === Optional Filter 3: EMA Curvature (detect flattening) ===
        self.use_ema_curvature = config.get('use_ema_curvature', True)
        self.ema_curve_window = config.get('ema_curve_window', 5)
        self.ema_curve_min = config.get('ema_curve_min', 0.0003)  # 0.03% slope

        # === Optional Filter 4: Avoid extreme volatility ===
        self.use_vol_ceiling = config.get('use_vol_ceiling', False)
        self.atr_max_percentile = config.get('atr_max_percentile', 95)  # Block top 5% volatile bars

        # === Optional Filter 5: Avoid 3-bar squeeze ===
        self.use_squeeze_filter = config.get('use_squeeze_filter', False)
        self.squeeze_threshold = config.get('squeeze_threshold', 0.005)  # 0.5% range

    def calculate_adx(self, high: pd.Series, low: pd.Series,
                     close: pd.Series) -> pd.Series:
        """Calculate ADX - only indicator we truly need"""
        return TechnicalIndicators.adx(high, low, close, self.adx_period)

    def calculate_ema_separation(self, ema_short: pd.Series,
                                 ema_long: pd.Series) -> pd.Series:
        """
        Calculate EMA separation (normalized)

        Returns: absolute percentage gap between EMAs
        """
        separation = abs(ema_short - ema_long) / ema_long
        return separation

    def calculate_ema_curvature(self, ema: pd.Series) -> pd.Series:
        """
        Calculate EMA curvature (2nd derivative approximation)

        Detects if EMA is flattening out
        Positive curvature = accelerating trend
        Near-zero curvature = flat/dying trend
        """
        # First derivative (slope)
        slope = ema.diff()

        # Second derivative (acceleration)
        curvature = slope.diff()

        # Normalize by price level
        curvature_normalized = abs(curvature) / ema

        return curvature_normalized

    def calculate_squeeze(self, high: pd.Series, low: pd.Series,
                         close: pd.Series, window: int = 3) -> pd.Series:
        """
        Detect price squeeze (3 consecutive bars with tiny range)

        Returns: True if in squeeze
        """
        price_range = (high - low) / close
        rolling_range = price_range.rolling(window).mean()

        is_squeezed = rolling_range < self.squeeze_threshold
        return is_squeezed

    def apply_filters(self, data: pd.DataFrame,
                     ema_short: pd.Series,
                     ema_long: pd.Series) -> Dict[str, pd.Series]:
        """
        Apply all filters and return tradeable status

        Returns dict with:
            - is_tradeable: bool series
            - filter_reasons: text explanation for each bar
            - individual filter results for debugging
        """
        high = data['high']
        low = data['low']
        close = data['close']

        # === Calculate filters ===

        # Filter 1: ADX
        adx = self.calculate_adx(high, low, close)
        adx_ok = adx > self.adx_min

        # Filter 2: EMA Separation
        ema_sep = self.calculate_ema_separation(ema_short, ema_long)
        ema_sep_ok = ema_sep > self.ema_sep_min

        # Filter 3: EMA Curvature (optional)
        if self.use_ema_curvature:
            ema_curve = self.calculate_ema_curvature(ema_long)
            ema_curve_ok = ema_curve > self.ema_curve_min
        else:
            ema_curve = pd.Series(1.0, index=data.index)
            ema_curve_ok = pd.Series(True, index=data.index)

        # Filter 4: Volatility ceiling (optional)
        if self.use_vol_ceiling:
            atr = TechnicalIndicators.atr(high, low, close, 14)
            atr_threshold = atr.rolling(100).quantile(self.atr_max_percentile / 100)
            vol_ok = atr < atr_threshold
        else:
            vol_ok = pd.Series(True, index=data.index)

        # Filter 5: Squeeze (optional)
        if self.use_squeeze_filter:
            is_squeezed = self.calculate_squeeze(high, low, close)
            squeeze_ok = ~is_squeezed
        else:
            squeeze_ok = pd.Series(True, index=data.index)

        # === Combined tradeable status ===
        is_tradeable = (
            adx_ok &
            ema_sep_ok &
            ema_curve_ok &
            vol_ok &
            squeeze_ok
        )

        # === Generate human-readable reasons ===
        filter_reasons = pd.Series('', index=data.index)

        for i in range(len(data)):
            idx = data.index[i]
            reasons = []

            if not adx_ok.iloc[i]:
                reasons.append(f"ADX too low ({adx.iloc[i]:.1f}<{self.adx_min})")

            if not ema_sep_ok.iloc[i]:
                reasons.append(f"EMA too close ({ema_sep.iloc[i]:.1%}<{self.ema_sep_min:.1%})")

            if self.use_ema_curvature and not ema_curve_ok.iloc[i]:
                reasons.append(f"EMA flattening ({ema_curve.iloc[i]:.4%})")

            if self.use_vol_ceiling and not vol_ok.iloc[i]:
                reasons.append("Volatility too high")

            if self.use_squeeze_filter and not squeeze_ok.iloc[i]:
                reasons.append("Price squeeze")

            if is_tradeable.iloc[i]:
                filter_reasons.iloc[i] = "✅ TRADEABLE"
            else:
                filter_reasons.iloc[i] = "❌ " + "; ".join(reasons)

        # === Return all data ===
        return {
            # Main output
            'is_tradeable': is_tradeable,
            'filter_reasons': filter_reasons,

            # Individual filters (for debugging)
            'adx': adx,
            'adx_ok': adx_ok,
            'ema_separation': ema_sep,
            'ema_sep_ok': ema_sep_ok,
            'ema_curvature': ema_curve,
            'ema_curve_ok': ema_curve_ok,
            'vol_ok': vol_ok,
            'squeeze_ok': squeeze_ok,

            # Percentage tradeable
            'tradeable_pct': is_tradeable.sum() / len(is_tradeable) * 100
        }

    def get_filter_summary(self, filter_results: Dict) -> str:
        """Get summary of filter results"""
        latest = {k: v.iloc[-1] if hasattr(v, 'iloc') else v
                 for k, v in filter_results.items()}

        curvature_line = f"  EMA Curvature: {latest['ema_curvature']:.4%}\n" if self.use_ema_curvature else ""

        summary = f"""
🎯 Trend Filter Status (SIMPLE VERSION)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Status: {latest['filter_reasons']}

📊 Core Metrics:
  ADX: {latest['adx']:.1f} (min: {self.adx_min})
  EMA Separation: {latest['ema_separation']:.2%} (min: {self.ema_sep_min:.2%})
{curvature_line}
📈 Overall Stats:
  Tradeable bars: {filter_results['tradeable_pct']:.1f}%

💡 Philosophy:
  - Only avoid flat EMA periods (ADX low + EMA close)
  - All trending markets are tradeable (slow/fast/volatile)
  - No complex regime detection needed
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
"""
        return summary


# ================================================================
# Helper function to create simple filter with recommended defaults
# ================================================================

def create_simple_filter(config: dict = None) -> SimpleTrendFilter:
    """
    Create simple trend filter with recommended defaults

    Recommended config for crypto M5:
    {
        'adx_min': 20,           # Standard ADX threshold
        'ema_sep_min': 0.012,    # 1.2% minimum EMA gap
        'use_ema_curvature': True,
        'ema_curve_min': 0.0003,
        'use_vol_ceiling': False,  # Usually not needed
        'use_squeeze_filter': False  # Usually not needed
    }
    """
    if config is None:
        config = {
            'adx_min': 20,
            'ema_sep_min': 0.012,
            'use_ema_curvature': True,
            'ema_curve_min': 0.0003,
            'use_vol_ceiling': False,
            'use_squeeze_filter': False
        }

    return SimpleTrendFilter(config)
