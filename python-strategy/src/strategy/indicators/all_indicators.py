# ================================================================
# Enhanced Multi-Dimensional Indicators System
# File: src/strategy/indicators/enhanced_technical_indicators.py
# ================================================================

from matplotlib import ticker
import numpy as np
import pandas as pd
from typing import Dict, List, Tuple, Optional
from dataclasses import dataclass
from enum import Enum
import talib  # Technical Analysis Library
from scipy import stats
from sklearn.preprocessing import StandardScaler
import logging
from ..base_strategy import BaseStrategy
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
    
    @staticmethod
    def calculate_comprehensive_indicators(data_4h: pd.DataFrame, 
                                         data_1d: pd.DataFrame) -> Dict[str, pd.Series]:
        """Calculate comprehensive indicator suite"""
        
        indicators = {}
        close_4h = data_4h['close']
        close_1d = data_1d['close']
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
        common_dates = close_1d.index.intersection(close_4h_daily.index)
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
        # adaptive_window = (50 - vol_percentile * 30).astype(int).clip(10, 50)
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

# ================================================================
# ENHANCED MARKOV STRATEGY WITH ADVANCED INDICATORS
# ================================================================

class EnhancedMarkovETFStrategy(BaseStrategy):
    """Markov strategy enhanced with advanced multi-dimensional indicators"""
    
    def __init__(self, config: dict):
        super().__init__(config)
        
        # Enhanced indicator configuration
        self.enhanced_indicators = config.get('enhanced_indicators', {})
        self.use_multi_timeframe = self.enhanced_indicators.get('multi_timeframe', True)
        self.use_volume_analysis = self.enhanced_indicators.get('volume_analysis', True)
        self.use_market_structure = self.enhanced_indicators.get('market_structure', True)
        self.use_divergence_detection = self.enhanced_indicators.get('divergence_detection', True)
        
        # Indicator weights for signal generation
        self.indicator_weights = self._initialize_indicator_weights()
        
        # Multi-timeframe data storage
        self.data_4h = None
        self.data_1d = None
        
        logger.info(f"🚀 Enhanced Markov-ETF Strategy initialized")
        logger.info(f"   Multi-timeframe: {'Enabled' if self.use_multi_timeframe else 'Disabled'}")
        logger.info(f"   Volume analysis: {'Enabled' if self.use_volume_analysis else 'Disabled'}")
        logger.info(f"   Market structure: {'Enabled' if self.use_market_structure else 'Disabled'}")
        logger.info(f"   Divergence detection: {'Enabled' if self.use_divergence_detection else 'Disabled'}")
    
    def _initialize_indicator_weights(self) -> Dict[str, float]:
        """Initialize weights for different indicator categories"""
        return {
            # Traditional indicators (baseline)
            'trend_weight': 0.25,        # EMA, trend strength
            'momentum_weight': 0.20,     # RSI, Stochastic
            'volatility_weight': 0.15,   # Bollinger, ATR
            
            # Enhanced indicators
            'volume_weight': 0.15,       # OBV, VWAP, CMF
            'structure_weight': 0.10,    # Support/Resistance, Market structure
            'divergence_weight': 0.10,   # Divergence signals
            'multi_timeframe_weight': 0.05,  # Cross-timeframe confluence
        }
    
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        """Calculate comprehensive enhanced indicators"""
        
        # Store data for multi-timeframe analysis
        self.data_4h = data
        # For now, use same data for 1d (would need actual 1d data in real implementation)
        self.data_1d = data.resample('1D').agg({
            'open': 'first', 'high': 'max', 'low': 'min', 
            'close': 'last', 'volume': 'sum'
        }).dropna()
        
        # Calculate comprehensive indicators
        indicators = EnhancedTechnicalIndicators.calculate_comprehensive_indicators(
            self.data_4h, self.data_1d
        )
        
        # Add regime-specific enhancements
        indicators.update(self._calculate_regime_specific_indicators(data, indicators))
        
        # Add composite scores
        indicators.update(self._calculate_composite_scores(indicators))
        
        return indicators
    
    def _calculate_regime_specific_indicators(self, data: pd.DataFrame, 
                                           base_indicators: Dict[str, pd.Series]) -> Dict[str, pd.Series]:
        """Calculate indicators specific to regime detection"""
        
        regime_indicators = {}
        close = data['close']
        
        # Regime Strength Indicator (composite of trend + momentum)
        trend_score = base_indicators.get('ema_ribbon_score', pd.Series(0, index=close.index))
        momentum_score = (base_indicators.get('rsi_consensus', pd.Series(50, index=close.index)) - 50) / 50
        
        regime_indicators['regime_strength'] = (trend_score + momentum_score) / 2
        
        # Volatility Regime Classifier (enhanced)
        vol_20 = close.pct_change().rolling(20).std()
        vol_50 = close.pct_change().rolling(50).std()
        vol_100 = close.pct_change().rolling(100).std()
        
        # Multi-period volatility score
        vol_score = (vol_20 + vol_50 + vol_100) / 3
        vol_percentile = vol_score.rolling(200).rank(pct=True)
        
        regime_indicators['volatility_regime_score'] = vol_percentile
        
        # Market Microstructure Health
        if 'volume_profile' in base_indicators:
            volume_health = base_indicators['volume_profile'].map({
                'accumulation': 1, 'normal': 0, 'breakout': 0.5, 'distribution': -1
            }).fillna(0)
            regime_indicators['market_health'] = volume_health
        
        # Momentum Persistence (how long momentum has been in same direction)
        rsi = base_indicators.get('rsi_consensus', pd.Series(50, index=close.index))
        momentum_direction = np.where(rsi > 50, 1, -1)
        momentum_changes = pd.Series(momentum_direction, index=close.index).diff() != 0
        momentum_persistence = pd.Series(0, index=close.index)
        
        count = 0
        for i, changed in enumerate(momentum_changes):
            if changed or i == 0:
                count = 1
            else:
                count += 1
            momentum_persistence.iloc[i] = count
        
        regime_indicators['momentum_persistence'] = momentum_persistence
        
        return regime_indicators
    
    def _calculate_composite_scores(self, indicators: Dict[str, pd.Series]) -> Dict[str, pd.Series]:
        """Calculate composite indicator scores"""
        
        composite = {}
        
        # Trend Composite Score
        trend_indicators = [
            indicators.get('ema_ribbon_score', 0),
            indicators.get('trend_strength', 50) / 100 - 0.5,  # Normalize to -0.5 to 0.5
            indicators.get('lr_slope', 0) * 1000,  # Scale slope
        ]
        composite['trend_composite'] = pd.concat(trend_indicators, axis=1).mean(axis=1)
        
        # Momentum Composite Score
        momentum_indicators = [
            (indicators.get('rsi_consensus', 50) - 50) / 50,  # Normalize RSI
            (indicators.get('stoch_rsi', 50) - 50) / 50,      # Normalize Stoch RSI
            indicators.get('roc_14', 0) / 10,                 # Scale ROC
        ]
        composite['momentum_composite'] = pd.concat(momentum_indicators, axis=1).mean(axis=1)
        
        # Volume Composite Score  
        if self.use_volume_analysis:
            volume_indicators = [
                indicators.get('cmf', 0),                     # Chaikin Money Flow
                indicators.get('market_health', 0),           # Volume profile health
            ]
            composite['volume_composite'] = pd.concat(volume_indicators, axis=1).mean(axis=1)
        else:
            composite['volume_composite'] = pd.Series(0, index=indicators['ema_ribbon_score'].index)
        
        # Volatility Composite Score
        volatility_indicators = [
            indicators.get('bb_percent_b', 0.5) - 0.5,       # Center Bollinger %B
            1 - indicators.get('volatility_regime_score', 0.5),  # Invert vol score (low vol = positive)
        ]
        composite['volatility_composite'] = pd.concat(volatility_indicators, axis=1).mean(axis=1)
        
        # Market Structure Score
        if self.use_market_structure:
            structure_indicators = [
                indicators.get('market_structure', 0),
                indicators.get('resistance_distance', 0.1) - 0.05,  # Distance from resistance
                0.05 - indicators.get('support_distance', 0.1),     # Distance from support
            ]
            composite['structure_composite'] = pd.concat(structure_indicators, axis=1).mean(axis=1)
        else:
            composite['structure_composite'] = pd.Series(0, index=indicators['ema_ribbon_score'].index)
        
        # Divergence Score
        if self.use_divergence_detection:
            divergence_indicators = [
                indicators.get('rsi_divergence', 0),
                indicators.get('volume_divergence', 0),
            ]
            composite['divergence_composite'] = pd.concat(divergence_indicators, axis=1).mean(axis=1)
        else:
            composite['divergence_composite'] = pd.Series(0, index=indicators['ema_ribbon_score'].index)
        
        # Multi-Timeframe Score
        if self.use_multi_timeframe:
            mtf_indicators = [
                indicators.get('mtf_confluence', 0),
                indicators.get('mtf_momentum_confluence', 0),
            ]
            composite['mtf_composite'] = pd.concat(mtf_indicators, axis=1).mean(axis=1)
        else:
            composite['mtf_composite'] = pd.Series(0, index=indicators['ema_ribbon_score'].index)
        
        # MASTER COMPOSITE SIGNAL
        weights = self.indicator_weights
        master_signal = (
            composite['trend_composite'] * weights['trend_weight'] +
            composite['momentum_composite'] * weights['momentum_weight'] +
            composite['volume_composite'] * weights['volume_weight'] +
            composite['volatility_composite'] * weights['volatility_weight'] +
            composite['structure_composite'] * weights['structure_weight'] +
            composite['divergence_composite'] * weights['divergence_weight'] +
            composite['mtf_composite'] * weights['multi_timeframe_weight']
        )
        
        composite['master_signal'] = master_signal
        
        # Signal Confidence (based on agreement between components)
        signal_components = [
            composite['trend_composite'],
            composite['momentum_composite'], 
            composite['volume_composite'],
            composite['volatility_composite']
        ]
        
        # Calculate standard deviation of components (low std = high agreement)
        component_std = pd.concat(signal_components, axis=1).std(axis=1)
        signal_confidence = 1 / (1 + component_std * 2)  # Convert to 0-1 confidence
        
        composite['signal_confidence'] = signal_confidence
        
        return composite
    
    async def generate_signals(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        """Enhanced signal generation using multi-dimensional indicators"""
        
        signals = pd.DataFrame(index=data.index)
        
        # Get ETF flow data (same as original)
        etf_flow = await self._get_etf_flow_data()
        
        # Detect regime with enhanced indicators
        regime_info = await self._detect_regime_enhanced(data, indicators, etf_flow)
        
        # Generate base signals using enhanced indicators
        signals = self._generate_enhanced_signals(data, indicators, regime_info, signals)
        
        # Apply Markov enhancements (same as original)
        signals = self._apply_markov_enhancements(signals, regime_info, indicators)
        
        # Final signal processing
        signals = self._finalize_signals(signals, indicators)
        
        return signals
    
    def _generate_enhanced_signals(self, data: pd.DataFrame, indicators: Dict[str, pd.Series],
                                 regime_info, signals: pd.DataFrame) -> pd.DataFrame:
        """Generate signals using enhanced multi-dimensional approach"""
        
        # 1. Master Signal (primary signal source)
        master_signal = indicators.get('master_signal', pd.Series(0, index=data.index))
        signals['master_signal_raw'] = master_signal
        
        # 2. Regime-adjusted Master Signal
        regime_weights = self.regime_strategy_weights.get(regime_info.current_regime, {})
        regime_adjustment = regime_weights.get('risk_sensitivity', 1.0)
        
        signals['master_signal_adjusted'] = master_signal * regime_adjustment
        
        # 3. Component Signals for transparency
        signals['trend_signal'] = indicators.get('trend_composite', pd.Series(0, index=data.index))
        signals['momentum_signal'] = indicators.get('momentum_composite', pd.Series(0, index=data.index))
        signals['volume_signal'] = indicators.get('volume_composite', pd.Series(0, index=data.index))
        signals['structure_signal'] = indicators.get('structure_composite', pd.Series(0, index=data.index))
        signals['divergence_signal'] = indicators.get('divergence_composite', pd.Series(0, index=data.index))
        
        # 4. Signal Quality Metrics
        signals['signal_confidence'] = indicators.get('signal_confidence', pd.Series(0.5, index=data.index))
        signals['regime_strength'] = indicators.get('regime_strength', pd.Series(0, index=data.index))
        
        # 5. Multi-Timeframe Confirmation
        if self.use_multi_timeframe:
            mtf_confirmation = indicators.get('mtf_composite', pd.Series(0, index=data.index))
            signals['mtf_confirmation'] = mtf_confirmation
            
            # Reduce signal strength if MTF doesn't confirm
            mtf_penalty = np.where(
                (master_signal > 0) & (mtf_confirmation < -0.2), 0.5,  # Bullish signal, bearish MTF
                np.where((master_signal < 0) & (mtf_confirmation > 0.2), 0.5, 1.0)  # Bearish signal, bullish MTF
            )
            signals['mtf_penalty'] = mtf_penalty
            signals['master_signal_adjusted'] *= mtf_penalty
        
        # 6. Volume Confirmation
        if self.use_volume_analysis:
            volume_signal = indicators.get('volume_composite', pd.Series(0, index=data.index))
            volume_confirmation = np.where(
                (master_signal > 0) & (volume_signal > 0), 1.2,     # Bullish + volume = boost
                np.where((master_signal < 0) & (volume_signal < 0), 1.2,  # Bearish + volume = boost
                np.where(abs(volume_signal) < 0.1, 0.8, 1.0))       # Low volume = penalty
            )
            signals['volume_confirmation'] = volume_confirmation
            signals['master_signal_adjusted'] *= volume_confirmation
        
        return signals
    
    def _apply_markov_enhancements(self, signals: pd.DataFrame, regime_info, 
                                 indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        """Apply Markov transition forecasting to enhance signals"""
        
        # Get master signal
        master_signal = signals['master_signal_adjusted']
        
        # Transition-based adjustments (same logic as original Markov strategy)
        if regime_info.transition_confidence > 0.6:
            # High confidence regime change predicted
            transition_adjustment = self._calculate_transition_enhancement(regime_info)
            signals['transition_adjustment'] = transition_adjustment
            signals['markov_enhanced_signal'] = master_signal * transition_adjustment
        else:
            signals['transition_adjustment'] = 1.0
            signals['markov_enhanced_signal'] = master_signal
        
        # Duration-based risk adjustment
        duration_adjustment = self._calculate_duration_risk_adjustment(regime_info)
        signals['duration_adjustment'] = duration_adjustment
        signals['markov_enhanced_signal'] *= duration_adjustment
        
        # ETF flow enhancement
        etf_enhancement = self._calculate_etf_enhancement(regime_info)
        signals['etf_enhancement'] = etf_enhancement
        signals['markov_enhanced_signal'] *= etf_enhancement
        
        return signals
    
    def _finalize_signals(self, signals: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        """Finalize signals with thresholding and risk controls"""
        
        # Final enhanced signal
        final_signal = signals['markov_enhanced_signal']
        
        # Apply confidence weighting
        confidence = signals['signal_confidence']
        confidence_weighted_signal = final_signal * confidence
        
        # Signal thresholding with dynamic thresholds
        base_threshold = self.config.get('signal_threshold', 0.3)
        
        # Dynamic threshold based on volatility regime
        volatility_regime_score = indicators.get('volatility_regime_score', pd.Series(0.5, index=signals.index))
        
        # Higher volatility = higher threshold (more conservative)
        dynamic_threshold = base_threshold * (0.5 + volatility_regime_score)
        
        # Generate final buy/sell/hold signals
        signals['final_signal_raw'] = confidence_weighted_signal
        signals['dynamic_threshold'] = dynamic_threshold
        signals['signal'] = 0
        
        # Apply dynamic thresholding
        signals.loc[confidence_weighted_signal > dynamic_threshold, 'signal'] = 1   # Buy
        signals.loc[confidence_weighted_signal < -dynamic_threshold, 'signal'] = -1  # Sell
        
        # Signal strength for position sizing
        signals['signal_strength'] = abs(confidence_weighted_signal)
        
        # Add metadata
        signals['regime'] = regime_info.current_regime.value
        signals['regime_confidence'] = regime_info.regime_probability
        signals['transition_forecast'] = regime_info.next_most_likely.value
        signals['transition_confidence'] = regime_info.transition_confidence
        
        # Performance tracking metadata
        signals['trend_component'] = signals['trend_signal']
        signals['momentum_component'] = signals['momentum_signal'] 
        signals['volume_component'] = signals['volume_signal']
        signals['structure_component'] = signals['structure_signal']
        signals['divergence_component'] = signals['divergence_signal']
        
        return signals


# ================================================================
# USAGE CONFIGURATION
# ================================================================

def get_enhanced_strategy_config():
    """Get configuration for enhanced indicators"""
    return {
        'enhanced_indicators': {
            'multi_timeframe': True,
            'volume_analysis': True,
            'market_structure': True,
            'divergence_detection': True,
        },
        
        # Enhanced indicator weights
        'indicator_weights': {
            'trend_weight': 0.25,
            'momentum_weight': 0.20,
            'volatility_weight': 0.15,
            'volume_weight': 0.15,
            'structure_weight': 0.10,
            'divergence_weight': 0.10,
            'multi_timeframe_weight': 0.05,
        },
        
        # Signal processing
        'signal_threshold': 0.25,  # Lower threshold with higher quality signals
        'confidence_threshold': 0.6,  # Minimum confidence for signals
        'dynamic_thresholding': True,  # Use volatility-adjusted thresholds
    }

#write a test indicators
if __name__ == "__main__":
    import yfinance as yf
    import matplotlib.pyplot as plt

    # Download sample data
    #create fake data
    # data = pd.DataFrame({
    #     'open': np.random.rand(100),
    #     'high': np.random.rand(100),
    #     'low': np.random.rand(100),
    #     'close': np.random.rand(100),
    #     'volume': np.random.rand(100)
    # }, index=pd.date_range(start='2023-01-01', periods=100, freq='D'))
    data = pd.read_csv('/home/e8/works/py_trade/btc_data.csv', index_col=0, parse_dates=True)
    # Calculate indicators
    indicators = EnhancedTechnicalIndicators.calculate_comprehensive_indicators(data, data.resample('1D').agg({
        'open': 'first', 'high': 'max', 'low': 'min', 
        'close': 'last', 'volume': 'sum'
    }).dropna())
    
    # Plot some indicators
    plt.figure(figsize=(14, 10))
    
    plt.subplot(3, 1, 1)
    plt.plot(data['close'], label='Close Price')
    plt.plot(indicators['ama_20'], label='AMA 20')
    plt.plot(indicators['ema_55'], label='EMA 55')
    plt.title(f'{ticker} Price with EMA')
    plt.legend()
    
    plt.subplot(3, 1, 2)
    plt.plot(indicators['rsi_consensus'], label='RSI Consensus')
    plt.axhline(70, color='red', linestyle='--')
    plt.axhline(30, color='green', linestyle='--')
    plt.title('RSI Consensus')
    plt.legend()
    
    plt.subplot(3, 1, 3)
    plt.plot(indicators['bb_upper'], label='Bollinger Upper')
    plt.plot(indicators['bb_middle'], label='Bollinger Middle')
    plt.plot(indicators['bb_lower'], label='Bollinger Lower')
    plt.fill_between(indicators['bb_upper'].index, indicators['bb_lower'], indicators['bb_upper'], color='lightgray', alpha=0.5)
    plt.title('Bollinger Bands')
    plt.legend()
    
    plt.tight_layout()
    plt.show()