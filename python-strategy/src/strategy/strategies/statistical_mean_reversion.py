# ================================================================
# FIXED: src/strategy/strategies/statistical_mean_reversion.py
# Fix the signal generation logic
# ================================================================

import pandas as pd
import numpy as np
from typing import Dict
from ..base_strategy import BaseStrategy
from ..indicators.technical_indicators import TechnicalIndicators

class StatisticalMeanReversionStrategy(BaseStrategy):
    """FIXED: Mean reversion strategy with better signal generation"""
    
    def __init__(self, config: dict):
        super().__init__(config)
        
        # Strategy parameters with more aggressive defaults for testing
        self.zscore_window = config.get('zscore_window', 20)
        self.zscore_entry = config.get('zscore_entry', 1.5)  # REDUCED from 2.0
        self.zscore_exit = config.get('zscore_exit', 0.3)    # REDUCED from 0.5
        self.ema_short = config.get('ema_short', 10)
        self.ema_long = config.get('ema_long', 30)
        self.volume_threshold = config.get('volume_threshold', 1.2)  # REDUCED from 1.5
        
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        """Calculate all required indicators"""
        close = data['close']
        volume = data.get('volume', pd.Series(1.0, index=data.index))
        
        indicators = {
            # Core indicators
            'ema_short': TechnicalIndicators.ema(close, self.ema_short),
            'ema_long': TechnicalIndicators.ema(close, self.ema_long),
            'zscore': TechnicalIndicators.zscore(close, self.zscore_window),
            
            # Price metrics
            'price_ratio': close / close.rolling(self.zscore_window).mean(),
            'volatility': close.pct_change().rolling(20).std(),
            
            # Volume indicators
            'volume_ma': volume.rolling(20).mean(),
            'volume_ratio': volume / volume.rolling(20).mean(),
            
            # Bollinger Bands for additional confirmation
            **TechnicalIndicators.bollinger_bands(close, 20, 2.0),
            
            # RSI for momentum check
            'rsi': TechnicalIndicators.rsi(close)
        }
        
        # Derived indicators
        indicators['ema_ratio'] = indicators['ema_short'] / indicators['ema_long']
        indicators['bb_position'] = (close - indicators['lower']) / (indicators['upper'] - indicators['lower'])
        
        return indicators
    
    def generate_signals(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        """FIXED: Generate trading signals with more lenient conditions"""
        signals = pd.DataFrame(index=data.index)
        
        # Initialize signal columns
        signals['zscore_signal'] = 0
        signals['trend_filter'] = 0
        signals['volume_filter'] = 0
        signals['signal'] = 0
        signals['signal_strength'] = 0
        
        # Get clean data (remove NaN)
        zscore = indicators['zscore'].fillna(0)
        ema_ratio = indicators['ema_ratio'].fillna(1)
        volume_ratio = indicators['volume_ratio'].fillna(1)
        rsi = indicators['rsi'].fillna(50)
        
        # Z-Score based signals (MORE AGGRESSIVE THRESHOLDS)
        signals.loc[zscore < -self.zscore_entry, 'zscore_signal'] = 1   # Buy oversold
        signals.loc[zscore > self.zscore_entry, 'zscore_signal'] = -1   # Sell overbought
        signals.loc[abs(zscore) < self.zscore_exit, 'zscore_signal'] = 0  # Exit
        
        # MORE LENIENT trend filter
        signals.loc[ema_ratio > 1.01, 'trend_filter'] = 1    # Reduced from 1.02
        signals.loc[ema_ratio < 0.99, 'trend_filter'] = -1   # Reduced from 0.98
        signals.loc[(ema_ratio >= 0.99) & (ema_ratio <= 1.01), 'trend_filter'] = 0
        
        # MORE LENIENT volume filter
        signals.loc[volume_ratio >= self.volume_threshold, 'volume_filter'] = 1
        
        # RSI filter - more permissive
        rsi_ok = (rsi > 25) & (rsi < 75)  # Widened from 20-80
        
        # SIMPLIFIED signal logic for better signal generation
        # Buy conditions
        buy_condition = (
            (signals['zscore_signal'] == 1) &
            (signals['trend_filter'] >= -1) &  # Allow neutral/up trend
            rsi_ok
        )
        
        # Sell conditions  
        sell_condition = (
            (signals['zscore_signal'] == -1) &
            (signals['trend_filter'] <= 1) &   # Allow neutral/down trend
            rsi_ok
        )
        
        # Apply signals
        signals.loc[buy_condition, 'signal'] = 1
        signals.loc[sell_condition, 'signal'] = -1
        
        # Signal strength based on Z-Score magnitude
        signals['signal_strength'] = np.clip(
            abs(zscore) / self.zscore_entry, 0, 1
        )
        
        # Debug info - add context
        signals['zscore_value'] = zscore
        signals['ema_ratio'] = ema_ratio
        signals['volume_ratio'] = volume_ratio
        signals['rsi_value'] = rsi
        
        return signals
