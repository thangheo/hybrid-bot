
# ================================================================
# src/strategy/indicators/technical_indicators.py
# ================================================================

import pandas as pd
import numpy as np
from typing import Dict, Optional
from scipy import stats

class TechnicalIndicators:
    """Collection of technical indicators optimized for crypto trading"""
    
    @staticmethod
    def ema(data: pd.Series, period: int) -> pd.Series:
        """Exponential Moving Average - faster than pandas default"""
        return data.ewm(span=period, adjust=False).mean()
    
    @staticmethod
    def sma(data: pd.Series, period: int) -> pd.Series:
        """Simple Moving Average"""
        return data.rolling(window=period).mean()
    
    @staticmethod
    def zscore(data: pd.Series, window: int) -> pd.Series:
        """Rolling Z-Score - key for mean reversion"""
        rolling_mean = data.rolling(window).mean()
        rolling_std = data.rolling(window).std()
        return (data - rolling_mean) / rolling_std
    
    @staticmethod
    def bollinger_bands(data: pd.Series, period: int = 20, std_dev: float = 2.0) -> Dict[str, pd.Series]:
        """Bollinger Bands"""
        sma = data.rolling(period).mean()
        std = data.rolling(period).std()
        return {
            'upper': sma + (std * std_dev),
            'middle': sma,
            'lower': sma - (std * std_dev)
        }
    
    @staticmethod
    def rsi(data: pd.Series, period: int = 14) -> pd.Series:
        """Relative Strength Index - optimized calculation"""
        delta = data.diff()
        gain = delta.where(delta > 0, 0).rolling(window=period).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(window=period).mean()
        rs = gain / loss
        return 100 - (100 / (1 + rs))
    
    @staticmethod
    def atr(high: pd.Series, low: pd.Series, close: pd.Series, period: int = 14) -> pd.Series:
        """Average True Range - for volatility"""
        tr1 = high - low
        tr2 = abs(high - close.shift())
        tr3 = abs(low - close.shift())
        tr = pd.concat([tr1, tr2, tr3], axis=1).max(axis=1)
        return tr.rolling(period).mean()
    
    @staticmethod
    def macd(data: pd.Series, fast: int = 12, slow: int = 26, signal: int = 9) -> Dict[str, pd.Series]:
        """MACD - Moving Average Convergence Divergence"""
        ema_fast = TechnicalIndicators.ema(data, fast)
        ema_slow = TechnicalIndicators.ema(data, slow)
        macd_line = ema_fast - ema_slow
        signal_line = TechnicalIndicators.ema(macd_line, signal)
        histogram = macd_line - signal_line
        
        return {
            'macd': macd_line,
            'signal': signal_line,
            'histogram': histogram
        }


    @staticmethod
    def log_returns(data: pd.Series) -> pd.Series:
        """Calculate log returns for statistical analysis"""
        return np.log(data / data.shift(1))
    
    @staticmethod
    def rolling_zscore(data: pd.Series, window: int) -> pd.Series:
        """Enhanced rolling Z-Score with better handling"""
        rolling_mean = data.rolling(window).mean()
        rolling_std = data.rolling(window).std()
        # Handle division by zero
        zscore = (data - rolling_mean) / (rolling_std + 1e-8)
        return zscore.fillna(0)
    
    @staticmethod
    def var_threshold(returns: pd.Series, confidence: float = 0.99, 
                     window: int = 30) -> pd.Series:
        """Value at Risk threshold for risk management"""
        from scipy import stats
        rolling_mean = returns.rolling(window).mean()
        rolling_std = returns.rolling(window).std()
        return rolling_mean - stats.norm.ppf(confidence) * rolling_std
    
    @staticmethod
    def trend_strength(close: pd.Series, atr: pd.Series, lookback: int = 30) -> pd.Series:
        """Calculate trend strength normalized by ATR"""
        price_change = abs(close - close.shift(lookback))
        return price_change / (atr + 1e-8)
