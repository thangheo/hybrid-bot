# ================================================================
# Enhanced Technical Indicators - Extracted from all_indicators.py
# File: src/strategy/indicators/enhanced_technical_indicators.py
# ================================================================

import numpy as np
import pandas as pd
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from enum import Enum
from scipy import stats
import logging

logger = logging.getLogger(__name__)

class TrendDirection(Enum):
    STRONG_BULLISH = "strong_bullish"
    BULLISH = "bullish" 
    NEUTRAL = "neutral"
    BEARISH = "bearish"
    STRONG_BEARISH = "strong_bearish"

class VolumeProfile(Enum):
    ACCUMULATION = "accumulation"
    DISTRIBUTION = "distribution"
    BREAKOUT = "breakout"
    NORMAL = "normal"

@dataclass
class MultiTimeframeSignal:
    short_term: float  # 4h signal
    medium_term: float # 1d signal
    long_term: float   # 3d signal
    confluence: float  # Agreement score
    dominant_timeframe: str

class EnhancedTechnicalIndicators:
    """Enhanced technical indicators with multi-dimensional analysis"""
    #get_feature_status
    @staticmethod
    def get_feature_status() -> Dict[str, bool]:
        return {
            "multi_timeframe": True,
            "advanced_volume": True,
            "pattern_recognition": True
        }

    @staticmethod
    def calculate_comprehensive_indicators(data_4h: pd.DataFrame) -> Dict[str, pd.Series]:
                                        #  data_1d: pd.DataFrame) -> Dict[str, pd.Series]:
        """Calculate comprehensive indicator suite"""
        
        indicators = {}
        close_4h = data_4h['close']
        close_1d = data_4h.resample('1D').ffill()['close']
        volume_4h = data_4h['volume']
        high_4h, low_4h = data_4h['high'], data_4h['low']
        
        # ===== 1. ENHANCED TREND INDICATORS =====
        
        # Multi-EMA system (better than simple EMA)
        indicators.update(EnhancedTechnicalIndicators._calculate_ema_system(close_4h))
        
        # Adaptive Moving Average (adjusts to volatility)
        indicators['ama_20'] = EnhancedTechnicalIndicators._adaptive_moving_average(close_4h, 20)
        
        # Hull Moving Average (reduced lag)
        indicators['hma_20'] = EnhancedTechnicalIndicators._hull_moving_average(close_4h, 20)
        
        # Trend Strength Index
        indicators['trend_strength'] = EnhancedTechnicalIndicators._trend_strength_index(close_4h)
        
        # ===== 2. ENHANCED MOMENTUM INDICATORS =====
        
        # Multi-RSI system
        indicators.update(EnhancedTechnicalIndicators._calculate_rsi_system(close_4h))
        
        # Stochastic RSI (more sensitive)
        indicators['stoch_rsi'] = EnhancedTechnicalIndicators._stochastic_rsi(close_4h)
        
        # Williams %R
        indicators['williams_r'] = EnhancedTechnicalIndicators._williams_percent_r(high_4h, low_4h, close_4h)
        
        # Rate of Change (ROC)
        indicators['roc_14'] = close_4h.pct_change(14) * 100
        
        # ===== 3. ENHANCED VOLATILITY INDICATORS =====
        
        # Average True Range (ATR)
        indicators['atr_14'] = EnhancedTechnicalIndicators._average_true_range(high_4h, low_4h, close_4h, 14)
        
        # Bollinger Band %B
        bb = EnhancedTechnicalIndicators._bollinger_bands_enhanced(close_4h)
        indicators.update(bb)
        
        # Keltner Channels
        kc = EnhancedTechnicalIndicators._keltner_channels(high_4h, low_4h, close_4h)
        indicators.update(kc)
        
        # Historical Volatility
        indicators['hist_vol_20'] = close_4h.pct_change().rolling(20).std() * np.sqrt(365)
        
        # ===== 4. ADVANCED VOLUME INDICATORS =====
        
        # On Balance Volume (OBV)
        indicators['obv'] = EnhancedTechnicalIndicators._on_balance_volume(close_4h, volume_4h)
        
        # Volume Weighted Average Price (VWAP)
        indicators['vwap'] = EnhancedTechnicalIndicators._vwap(high_4h, low_4h, close_4h, volume_4h)
        
        # Accumulation/Distribution Line
        indicators['ad_line'] = EnhancedTechnicalIndicators._accumulation_distribution(high_4h, low_4h, close_4h, volume_4h)
        
        # Chaikin Money Flow
        indicators['cmf'] = EnhancedTechnicalIndicators._chaikin_money_flow(high_4h, low_4h, close_4h, volume_4h)
        
        # Volume Profile Analysis
        indicators['volume_profile'] = EnhancedTechnicalIndicators._volume_profile_analysis(close_4h, volume_4h)
        
        # ===== 5. MARKET STRUCTURE INDICATORS =====
        
        # Support/Resistance Levels
        sr_levels = EnhancedTechnicalIndicators._support_resistance_levels(high_4h, low_4h, close_4h)
        indicators.update(sr_levels)
        
        # Market Structure (Higher Highs, Lower Lows)
        indicators['market_structure'] = EnhancedTechnicalIndicators._market_structure_analysis(high_4h, low_4h)
        
        # Fibonacci Retracement Levels
        fib_levels = EnhancedTechnicalIndicators._fibonacci_levels(high_4h, low_4h, close_4h)
        indicators.update(fib_levels)
        
        # ===== 6. MULTI-TIMEFRAME ANALYSIS =====
        
        # Multi-timeframe trend alignment
        mtf_trend = EnhancedTechnicalIndicators._multi_timeframe_trend(close_4h, close_1d)
        indicators.update(mtf_trend)
        
        # Cross-timeframe momentum
        mtf_momentum = EnhancedTechnicalIndicators._multi_timeframe_momentum(close_4h, close_1d)
        indicators.update(mtf_momentum)
        
        # ===== 7. ADVANCED STATISTICAL INDICATORS =====
        
        # Z-Score with adaptive window
        indicators['adaptive_zscore'] = EnhancedTechnicalIndicators._adaptive_zscore(close_4h)
        
        # Percentile Rank
        indicators['percentile_rank'] = EnhancedTechnicalIndicators._percentile_rank(close_4h, 100)
        
        # Linear Regression Slope
        indicators['lr_slope'] = EnhancedTechnicalIndicators._linear_regression_slope(close_4h, 20)
        
        # Coefficient of Variation
        indicators['coefficient_variation'] = EnhancedTechnicalIndicators._coefficient_variation(close_4h, 20)
        
        # ===== 8. DIVERGENCE DETECTION =====
        
        # Price-RSI Divergence
        indicators['rsi_divergence'] = EnhancedTechnicalIndicators._detect_divergence(
            close_4h, indicators['rsi_14']
        )
        
        # Price-Volume Divergence  
        indicators['volume_divergence'] = EnhancedTechnicalIndicators._detect_divergence(
            close_4h, indicators['obv']
        )
        
        return indicators
    
    # ===== IMPLEMENTATION METHODS =====
    
    @staticmethod
    def _calculate_ema_system(close: pd.Series) -> Dict[str, pd.Series]:
        """Multi-EMA system with various periods"""
        return {
            'ema_8': close.ewm(span=8).mean(),
            'ema_13': close.ewm(span=13).mean(),
            'ema_21': close.ewm(span=21).mean(),
            'ema_34': close.ewm(span=34).mean(),
            'ema_55': close.ewm(span=55).mean(),
            'ema_ribbon_score': EnhancedTechnicalIndicators._ema_ribbon_score(close)
        }
    
    @staticmethod
    def _ema_ribbon_score(close: pd.Series) -> pd.Series:
        """Score based on EMA ribbon alignment (-1 to 1)"""
        ema_8 = close.ewm(span=8).mean()
        ema_13 = close.ewm(span=13).mean() 
        ema_21 = close.ewm(span=21).mean()
        ema_34 = close.ewm(span=34).mean()
        ema_55 = close.ewm(span=55).mean()
        
        # Count aligned EMAs (bullish when 8>13>21>34>55)
        bullish_alignment = (
            (ema_8 > ema_13).astype(int) +
            (ema_13 > ema_21).astype(int) + 
            (ema_21 > ema_34).astype(int) +
            (ema_34 > ema_55).astype(int)
        ) / 4.0
        
        bearish_alignment = (
            (ema_8 < ema_13).astype(int) +
            (ema_13 < ema_21).astype(int) +
            (ema_21 < ema_34).astype(int) +
            (ema_34 < ema_55).astype(int)
        ) / 4.0
        
        return bullish_alignment - bearish_alignment
    
    @staticmethod
    def _adaptive_moving_average(close: pd.Series, period: int) -> pd.Series:
        """Adaptive Moving Average (Kaufman's AMA)"""
        change = close.diff(period).abs()
        volatility = close.diff().abs().rolling(period).sum()
        efficiency_ratio = change / volatility
        
        # Smooth constants
        fastest_sc = 2 / (2 + 1)
        slowest_sc = 2 / (30 + 1)
        smooth_const = (efficiency_ratio * (fastest_sc - slowest_sc) + slowest_sc) ** 2
        
        # Calculate AMA
        ama = pd.Series(index=close.index, dtype=float)
        ama.iloc[period] = close.iloc[:period+1].mean()
        
        for i in range(period + 1, len(close)):
            ama.iloc[i] = ama.iloc[i-1] + smooth_const.iloc[i] * (close.iloc[i] - ama.iloc[i-1])
        
        return ama
    
    @staticmethod
    def _hull_moving_average(close: pd.Series, period: int) -> pd.Series:
        """Hull Moving Average - reduced lag"""
        wma_half = close.rolling(period//2).apply(lambda x: np.average(x, weights=np.arange(1, len(x)+1)))
        wma_full = close.rolling(period).apply(lambda x: np.average(x, weights=np.arange(1, len(x)+1)))
        
        hull_data = 2 * wma_half - wma_full
        sqrt_period = int(np.sqrt(period))
        
        return hull_data.rolling(sqrt_period).apply(lambda x: np.average(x, weights=np.arange(1, len(x)+1)))
    
    @staticmethod
    def _trend_strength_index(close: pd.Series, period: int = 20) -> pd.Series:
        """Custom trend strength indicator (0 to 100)"""
        # Linear regression slope
        def calc_slope(y):
            x = np.arange(len(y))
            if len(y) < 2:
                return 0
            slope = stats.linregress(x, y)[0]
            return slope
        
        slopes = close.rolling(period).apply(calc_slope)
        
        # Normalize to 0-100 scale
        slope_percentile = slopes.rolling(100).rank(pct=True) * 100
        
        return slope_percentile.fillna(50)
    
    @staticmethod
    def _calculate_rsi_system(close: pd.Series) -> Dict[str, pd.Series]:
        """Multi-period RSI system"""
        rsi_9 = EnhancedTechnicalIndicators._rsi(close, 9)
        rsi_14 = EnhancedTechnicalIndicators._rsi(close, 14) 
        rsi_21 = EnhancedTechnicalIndicators._rsi(close, 21)
        
        # RSI consensus
        rsi_consensus = (rsi_9 + rsi_14 + rsi_21) / 3
        
        return {
            'rsi_9': rsi_9,
            'rsi_14': rsi_14,
            'rsi_21': rsi_21,
            'rsi_consensus': rsi_consensus,
            'rsi_divergence_strength': EnhancedTechnicalIndicators._rsi_divergence_strength(close, rsi_14)
        }
    
    @staticmethod
    def _rsi(close: pd.Series, period: int = 14) -> pd.Series:
        """RSI calculation"""
        delta = close.diff()
        delta = pd.to_numeric(delta, errors='coerce')
        gain = delta.where(delta > 0, 0).rolling(period).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(period).mean()
        rs = gain / loss
        return 100 - (100 / (1 + rs))
    
    @staticmethod
    def _rsi_divergence_strength(close: pd.Series, rsi: pd.Series, lookback: int = 20) -> pd.Series:
        """Detect RSI divergence strength"""
        price_slope = close.rolling(lookback).apply(lambda x: stats.linregress(range(len(x)), x)[0])
        rsi_slope = rsi.rolling(lookback).apply(lambda x: stats.linregress(range(len(x)), x)[0])
        
        # Divergence when slopes have opposite signs
        divergence = np.where(
            (price_slope > 0) & (rsi_slope < 0), 1,      # Bearish divergence
            np.where((price_slope < 0) & (rsi_slope > 0), -1, 0)  # Bullish divergence
        )
        
        return pd.Series(divergence, index=close.index)
    
    @staticmethod
    def _stochastic_rsi(close: pd.Series, period: int = 14) -> pd.Series:
        """Stochastic RSI - more sensitive momentum"""
        rsi = EnhancedTechnicalIndicators._rsi(close, period)
        
        stoch_rsi = (rsi - rsi.rolling(period).min()) / (rsi.rolling(period).max() - rsi.rolling(period).min()) * 100
        
        return stoch_rsi
    
    @staticmethod
    def _williams_percent_r(high: pd.Series, low: pd.Series, close: pd.Series, period: int = 14) -> pd.Series:
        """Williams %R indicator"""
        highest_high = high.rolling(period).max()
        lowest_low = low.rolling(period).min()
        
        williams_r = ((highest_high - close) / (highest_high - lowest_low)) * -100
        
        return williams_r
    
    @staticmethod
    def _average_true_range(high: pd.Series, low: pd.Series, close: pd.Series, period: int = 14) -> pd.Series:
        """Average True Range"""
        tr1 = high - low
        tr2 = (high - close.shift()).abs()
        tr3 = (low - close.shift()).abs()
        
        tr = pd.concat([tr1, tr2, tr3], axis=1).max(axis=1)
        
        return tr.rolling(period).mean()
    
    @staticmethod
    def _bollinger_bands_enhanced(close: pd.Series, period: int = 20, std_dev: float = 2.0) -> Dict[str, pd.Series]:
        """Enhanced Bollinger Bands with %B and bandwidth"""
        sma = close.rolling(period).mean()
        std = close.rolling(period).std()
        
        upper = sma + (std * std_dev)
        lower = sma - (std * std_dev)
        
        # %B (position within bands)
        percent_b = (close - lower) / (upper - lower)
        
        # Bandwidth (volatility measure)
        bandwidth = (upper - lower) / sma * 100
        
        return {
            'bb_upper': upper,
            'bb_middle': sma,
            'bb_lower': lower,
            'bb_percent_b': percent_b,
            'bb_bandwidth': bandwidth,
            'bb_squeeze': bandwidth < bandwidth.rolling(100).quantile(0.1)  # Squeeze detection
        }
    
    @staticmethod
    def _keltner_channels(high: pd.Series, low: pd.Series, close: pd.Series, period: int = 20, multiplier: float = 2.0) -> Dict[str, pd.Series]:
        """Keltner Channels"""
        ema = close.ewm(span=period).mean()
        atr = EnhancedTechnicalIndicators._average_true_range(high, low, close, period)
        
        upper = ema + (atr * multiplier)
        lower = ema - (atr * multiplier)
        
        return {
            'kc_upper': upper,
            'kc_middle': ema,
            'kc_lower': lower
        }
    
    @staticmethod
    def _on_balance_volume(close: pd.Series, volume: pd.Series) -> pd.Series:
        """On Balance Volume"""
        obv = pd.Series(index=close.index, dtype=float)
        obv.iloc[0] = volume.iloc[0]
        
        for i in range(1, len(close)):
            if close.iloc[i] > close.iloc[i-1]:
                obv.iloc[i] = obv.iloc[i-1] + volume.iloc[i]
            elif close.iloc[i] < close.iloc[i-1]:
                obv.iloc[i] = obv.iloc[i-1] - volume.iloc[i]
            else:
                obv.iloc[i] = obv.iloc[i-1]
        
        return obv
    
    @staticmethod
    def _vwap(high: pd.Series, low: pd.Series, close: pd.Series, volume: pd.Series) -> pd.Series:
        """Volume Weighted Average Price"""
        typical_price = (high + low + close) / 3
        return (typical_price * volume).cumsum() / volume.cumsum()
    
    @staticmethod
    def _accumulation_distribution(high: pd.Series, low: pd.Series, close: pd.Series, volume: pd.Series) -> pd.Series:
        """Accumulation/Distribution Line"""
        money_flow_multiplier = ((close - low) - (high - close)) / (high - low)
        money_flow_multiplier = money_flow_multiplier.fillna(0)
        money_flow_volume = money_flow_multiplier * volume
        
        return money_flow_volume.cumsum()
    
    @staticmethod
    def _chaikin_money_flow(high: pd.Series, low: pd.Series, close: pd.Series, volume: pd.Series, period: int = 20) -> pd.Series:
        """Chaikin Money Flow"""
        money_flow_multiplier = ((close - low) - (high - close)) / (high - low)
        money_flow_multiplier = money_flow_multiplier.fillna(0)
        money_flow_volume = money_flow_multiplier * volume
        
        cmf = money_flow_volume.rolling(period).sum() / volume.rolling(period).sum()
        
        return cmf
    
    @staticmethod
    def _volume_profile_analysis(close: pd.Series, volume: pd.Series) -> pd.Series:
        """Volume Profile Analysis Score"""
        # Volume relative to price movement
        price_change = close.pct_change().abs()
        volume_change = volume.pct_change()
        
        # High volume + small price change = accumulation/distribution
        # High volume + big price change = breakout
        volume_price_ratio = volume_change / (price_change + 1e-8)
        
        # Normalize to score
        profile_score = pd.cut(volume_price_ratio.rank(pct=True), 
                              bins=4, labels=['distribution', 'normal', 'accumulation', 'breakout'])
        
        return profile_score
    
    @staticmethod
    def _support_resistance_levels(high: pd.Series, low: pd.Series, close: pd.Series, period: int = 20) -> Dict[str, pd.Series]:
        """Dynamic Support/Resistance Levels"""
        # Recent highs/lows
        resistance = high.rolling(period).max()
        support = low.rolling(period).min()
        
        # Distance from S/R levels
        resistance_distance = (resistance - close) / close
        support_distance = (close - support) / close
        
        return {
            'resistance_level': resistance,
            'support_level': support,
            'resistance_distance': resistance_distance,
            'support_distance': support_distance,
            'near_resistance': resistance_distance < 0.02,  # Within 2%
            'near_support': support_distance < 0.02         # Within 2%
        }
    
    @staticmethod  
    def _market_structure_analysis(high: pd.Series, low: pd.Series) -> pd.Series:
        """Market Structure: Higher Highs, Lower Lows analysis"""
        swing_highs = high.rolling(5, center=True).max() == high
        swing_lows = low.rolling(5, center=True).min() == low
        
        structure_score = pd.Series(0, index=high.index)
        
        # Higher highs = bullish structure
        for i in range(20, len(high)):
            recent_highs = high[swing_highs][max(0, i-20):i]
            if len(recent_highs) >= 2:
                if recent_highs.iloc[-1] > recent_highs.iloc[-2]:
                    structure_score.iloc[i] += 1
        
        # Lower lows = bearish structure  
        for i in range(20, len(low)):
            recent_lows = low[swing_lows][max(0, i-20):i]
            if len(recent_lows) >= 2:
                if recent_lows.iloc[-1] < recent_lows.iloc[-2]:
                    structure_score.iloc[i] -= 1
        
        return structure_score
    
    @staticmethod
    def _fibonacci_levels(high: pd.Series, low: pd.Series, close: pd.Series, period: int = 50) -> Dict[str, pd.Series]:
        """Dynamic Fibonacci Retracement Levels"""
        swing_high = high.rolling(period).max()
        swing_low = low.rolling(period).min()
        
        # Fibonacci levels
        fib_236 = swing_high - (swing_high - swing_low) * 0.236
        fib_382 = swing_high - (swing_high - swing_low) * 0.382
        fib_500 = swing_high - (swing_high - swing_low) * 0.500
        fib_618 = swing_high - (swing_high - swing_low) * 0.618
        
        return {
            'fib_236': fib_236,
            'fib_382': fib_382,
            'fib_500': fib_500, 
            'fib_618': fib_618
        }
    
    @staticmethod
    def _multi_timeframe_trend(close_4h: pd.Series, close_1d: pd.Series) -> Dict[str, pd.Series]:
        """Multi-timeframe trend analysis"""
        # Resample 4h to 1d for comparison
        close_4h_daily = close_4h.resample('1D').last().dropna()
        
        # Align indices
        common_dates = close_1d.index.intersection(close_4h_daily.index.tolist())
        close_1d_aligned = close_1d.loc[common_dates]
        close_4h_aligned = close_4h_daily.loc[common_dates]
        
        # Trend scores
        trend_4h = (close_4h_aligned / close_4h_aligned.rolling(10).mean() - 1) * 100
        trend_1d = (close_1d_aligned / close_1d_aligned.rolling(10).mean() - 1) * 100
        
        # Confluence score
        confluence = np.where(
            (trend_4h > 0) & (trend_1d > 0), 1,      # Both bullish
            np.where((trend_4h < 0) & (trend_1d < 0), -1, 0)  # Both bearish
        )
        
        # Expand back to 4h timeframe
        confluence_4h = pd.Series(confluence, index=close_1d_aligned.index).reindex(close_4h.index, method='ffill')
        
        return {
            'mtf_trend_4h': trend_4h.reindex(close_4h.index, method='ffill'),
            'mtf_trend_1d': trend_1d.reindex(close_4h.index, method='ffill'),
            'mtf_confluence': confluence_4h
        }
    
    @staticmethod
    def _multi_timeframe_momentum(close_4h: pd.Series, close_1d: pd.Series) -> Dict[str, pd.Series]:
        """Multi-timeframe momentum analysis"""
        # Calculate RSI for both timeframes
        rsi_4h = EnhancedTechnicalIndicators._rsi(close_4h, 14)
        rsi_1d = EnhancedTechnicalIndicators._rsi(close_1d, 14)
        
        # Align timeframes
        rsi_1d_expanded = rsi_1d.reindex(close_4h.index, method='ffill')
        
        # Momentum confluence
        momentum_confluence = np.where(
            (rsi_4h > 50) & (rsi_1d_expanded > 50), 1,    # Both overbought
            np.where((rsi_4h < 50) & (rsi_1d_expanded < 50), -1, 0)  # Both oversold
        )
        
        return {
            'mtf_rsi_4h': rsi_4h,
            'mtf_rsi_1d': rsi_1d_expanded,
            'mtf_momentum_confluence': pd.Series(momentum_confluence, index=close_4h.index)
        }
    
    @staticmethod
    def _adaptive_zscore(close: pd.Series) -> pd.Series:
        """Adaptive Z-Score with dynamic window"""
        # Adaptive window based on volatility
        volatility = close.pct_change().rolling(20).std()
        vol_percentile = volatility.rolling(100).rank(pct=True)
        
        # Higher volatility = shorter window
        adaptive_window = (50 - vol_percentile * 30).fillna(20).astype(int).clip(10, 50)
        
        zscore = pd.Series(index=close.index, dtype=float)
        for i in range(50, len(close)):
            window = adaptive_window.iloc[i]
            data_window = close.iloc[i-window:i]
            if len(data_window) > 1:
                zscore.iloc[i] = (close.iloc[i] - data_window.mean()) / data_window.std()
        
        return zscore
    
    @staticmethod
    def _percentile_rank(close: pd.Series, period: int = 100) -> pd.Series:
        """Percentile rank (0-100)"""
        return close.rolling(period).rank(pct=True) * 100
    
    @staticmethod
    def _linear_regression_slope(close: pd.Series, period: int = 20) -> pd.Series:
        """Linear regression slope"""
        def calc_slope(y):
            if len(y) < 2:
                return 0
            x = np.arange(len(y))
            return stats.linregress(x, y)[0]
        
        return close.rolling(period).apply(calc_slope)
    
    @staticmethod
    def _coefficient_variation(close: pd.Series, period: int = 20) -> pd.Series:
        """Coefficient of Variation (volatility relative to mean)"""
        rolling_mean = close.rolling(period).mean()
        rolling_std = close.rolling(period).std()
        
        return (rolling_std / rolling_mean) * 100
    
    @staticmethod
    def _detect_divergence(price: pd.Series, indicator: pd.Series, lookback: int = 20) -> pd.Series:
        """Generic divergence detection"""
        price_slope = price.rolling(lookback).apply(lambda x: stats.linregress(range(len(x)), x)[0])
        indicator_slope = indicator.rolling(lookback).apply(lambda x: stats.linregress(range(len(x)), x)[0])
        
        # Divergence detection
        divergence = pd.Series(0, index=price.index)
        
        # Bullish divergence: price making lower lows, indicator making higher lows
        bullish_div = (price_slope < -0.001) & (indicator_slope > 0.001)
        
        # Bearish divergence: price making higher highs, indicator making lower highs  
        bearish_div = (price_slope > 0.001) & (indicator_slope < -0.001)
        
        divergence[bullish_div] = 1   # Bullish divergence
        divergence[bearish_div] = -1  # Bearish divergence
        
        return divergence
    #compute_all_indicators
    # @staticmethod
    # def compute_all_indicators(data: pd.DataFrame) -> Dict[str, pd.Series]:
    #     """Compute all enhanced technical indicators"""
    #     close = data['close']
    #     indicators = {
    #         'adaptive_zscore': EnhancedTechnicalIndicators._adaptive_zscore(close),
    #         'percentile_rank': EnhancedTechnicalIndicators._percentile_rank(close),
    #         'linear_regression_slope': EnhancedTechnicalIndicators._linear_regression_slope(close),
    #         'coefficient_variation': EnhancedTechnicalIndicators._coefficient_variation(close)
    #     }
    #     return indicators