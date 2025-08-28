# ================================================================
# UPDATED: Symbol-Specific Multi-Timeframe Strategy 
# Uses individual parameters for each symbol from environment variables
# ================================================================

import pandas as pd
import numpy as np
import os
import json
from typing import Dict, List, Optional
from scipy import stats
from sklearn.mixture import GaussianMixture
import warnings
warnings.filterwarnings("ignore")

from ..base_strategy import BaseStrategy
from ..indicators.technical_indicators import TechnicalIndicators
import logging

logger = logging.getLogger(__name__)

class AdvancedStatisticalStrategy(BaseStrategy):
    """UPDATED: Symbol-specific multi-timeframe strategy using parameters from environment"""
    
    def __init__(self, config: dict):
        super().__init__(config)
        
        # Load optimized parameters from environment
        self.optimized_params = self._load_optimized_parameters()
        
        # Current symbol being processed (set by strategy manager)
        self.current_symbol = None
        
        # Default parameters (fallback if symbol not found in optimized params)
        self.default_params = {
            'lookback_window': config.get('lookback_window', 20),
            'z_threshold': config.get('z_threshold', 2.0),
            'var_confidence': config.get('var_confidence', 0.95),
            'stop_loss': config.get('stop_loss', False),
            'trailing_stop': config.get('trailing_stop', 0.05)
        }
        
        # Multi-timeframe configuration (same for all symbols)
        self.use_4h_filter = config.get('use_4h_filter', True)
        self.h4_ema_short = config.get('h4_ema_short', 12)
        self.h4_ema_long = config.get('h4_ema_long', 26)
        self.h4_lookback = config.get('h4_lookback', 50)
        
        # Regime detection (same for all symbols)
        self.n_regimes = config.get('n_regimes', 3)
        self.gmm = None
        self.regime_mapping = {}
        #create current regime info
        self.current_regime_info = None
        # Strategy weights per regime (same for all symbols)
        self.regime_weights = config.get('regime_strategy_weights', {
            'low_vol': {
                'mean_reversion': 1.5,
                'trend_following': 0.3,
                'breakout': 0.0,
                'rsi_strategy': 0.8,
                'risk_off': -1.5
            },
            'medium_vol': {
                'mean_reversion': 0.3,
                'trend_following': 1.5,
                'breakout': 0.8,
                'rsi_strategy': 0.8,
                'risk_off': -1.5
            },
            'high_vol': {
                'mean_reversion': 0.0,
                'trend_following': 0.0,
                'breakout': 1.2,
                'rsi_strategy': 0.0,
                'risk_off': -2.0
            }
        })
        
        # Risk management
        self.max_position = config.get('max_position', 1.0)
        
        # Store 4h data for analysis
        self.h4_data = None
        
        logger.info(f"Loaded optimized parameters for {len(self.optimized_params)} symbols")
    
    def _load_optimized_parameters(self) -> Dict[str, Dict]:
        """Load optimized parameters from environment variable"""
        try:
            # Get the JSON string from environment
            params_json = os.getenv('OPTIMIZED_STRATEGY_PARAMS', '{}')
            
            # Parse JSON
            optimized_params = json.loads(params_json)
            #replace XRPUSDT with XRP/USDT
            # optimized_params = {k.replace('XRPUSDT', 'XRP/USDT'): v for k, v in optimized_params.items()}
            logger.info(f"Successfully loaded optimized parameters for symbols: {list(optimized_params.keys())}")
            return optimized_params
            
        except json.JSONDecodeError as e:
            logger.error(f"Failed to parse OPTIMIZED_STRATEGY_PARAMS JSON: {e}")
            return {}
        except Exception as e:
            logger.error(f"Failed to load optimized parameters: {e}")
            return {}
    
    def set_current_symbol(self, symbol: str):
        """Set the current symbol being processed to use its specific parameters"""
        self.current_symbol = symbol
        logger.debug(f"Set current symbol to: {symbol}")
    
    def get_symbol_params(self) -> Dict:
        """Get parameters for the current symbol"""
        if not self.current_symbol:
            logger.warning("No current symbol set, using default parameters")
            return self.default_params
        
        # Try to get symbol-specific parameters
        symbol_params = self.optimized_params.get(self.current_symbol, {})
        
        if not symbol_params:
            logger.warning(f"No optimized parameters found for {self.current_symbol}, using defaults")
            return self.default_params
        
        # Merge with defaults for missing parameters
        params = self.default_params.copy()
        params.update(symbol_params)
        
        logger.debug(f"Using parameters for {self.current_symbol}: {params}")
        return params
    
    def set_4h_data(self, h4_data: pd.DataFrame):
        """Set the 4h data for multi-timeframe analysis"""
        self.h4_data = h4_data

    def _calculate_4h_trend_filter(self, h4_data: pd.DataFrame, daily_index: pd.DatetimeIndex) -> pd.Series:
        """Calculate 4h trend filter using REAL 4h data"""
        try:
            if h4_data is None or h4_data.empty:
                logger.warning("No 4h data available for trend filter")
                return pd.Series(0.0, index=daily_index, dtype='float64')
            
            # Get recent 4h data
            recent_4h = h4_data.tail(self.h4_lookback).copy()

            if len(recent_4h) < self.h4_ema_long:
                logger.warning(f"Insufficient 4h data: {len(recent_4h)} < {self.h4_ema_long}")
                return pd.Series(0.0, index=daily_index, dtype='float64')
            
            # Calculate 4h EMAs on real data
            h4_close = recent_4h['close'].astype('float64')
            h4_ema_short = h4_close.ewm(span=self.h4_ema_short).mean()
            h4_ema_long = h4_close.ewm(span=self.h4_ema_long).mean()
            
            # 4h trend signal (1 = bullish, -1 = bearish)
            h4_trend_raw = np.where(h4_ema_short > h4_ema_long, 1.0, -1.0)
            h4_trend_series = pd.Series(h4_trend_raw, index=recent_4h.index, dtype='float64')
            
            # Map 4h signals to daily timeframe
            daily_filter = []
            
            for daily_timestamp in daily_index:
                # Find 4h bars for this daily period
                day_start = daily_timestamp.floor('D')
                day_end = day_start + pd.Timedelta(days=1)
                
                # Get 4h bars within this day
                day_4h_mask = (h4_trend_series.index >= day_start) & (h4_trend_series.index < day_end)
                day_4h_data = h4_trend_series[day_4h_mask]
                
                if len(day_4h_data) > 0:
                    # Use the latest 4h signal of the day
                    daily_filter.append(float(day_4h_data.iloc[-1]))
                else:
                    # If no 4h data for this day, look for closest previous signal
                    previous_data = h4_trend_series[h4_trend_series.index < day_start]
                    if len(previous_data) > 0:
                        daily_filter.append(float(previous_data.iloc[-1]))
                    else:
                        daily_filter.append(0.0)  # Neutral if no data
            
            return pd.Series(daily_filter, index=daily_index, dtype='float64')
            
        except Exception as e:
            logger.error(f"4h filter calculation failed: {e}")
            return pd.Series(0.0, index=daily_index, dtype='float64')
    
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        """Calculate indicators using symbol-specific parameters"""
        
        # Get symbol-specific parameters
        params = self.get_symbol_params()
        lookback_window = params['lookback_window']
        var_confidence = params['var_confidence']
        
        # CRITICAL FIX: Ensure all input data is float64
        close = data['close'].astype('float64')
        volume = data.get('volume', pd.Series(1.0, index=data.index)).astype('float64')
        
        # Initialize all indicators
        indicators = {}
        
        # === Basic Statistical Features (1d) with symbol-specific lookback ===
        price_ratio = close / close.shift(1)
        price_ratio = price_ratio.replace([np.inf, -np.inf], 1.0).fillna(1.0)
        indicators['log_return'] = np.log(price_ratio).astype('float64')
        
        indicators['rolling_mean'] = indicators['log_return'].rolling(
            lookback_window, min_periods=1
        ).mean().fillna(0.0).astype('float64')
        
        indicators['rolling_std'] = indicators['log_return'].rolling(
            lookback_window, min_periods=1
        ).std().fillna(0.01).astype('float64')
        
        # Z-Score (symbol-specific lookback)
        std_safe = indicators['rolling_std'].replace(0, 0.01)
        indicators['z_score'] = (
            (indicators['log_return'] - indicators['rolling_mean']) / std_safe
        ).fillna(0.0).astype('float64')
        
        # === EMA Trend (1d) ===
        try:
            indicators['ema_short'] = TechnicalIndicators.ema(close, 12).fillna(close).astype('float64')
            indicators['ema_long'] = TechnicalIndicators.ema(close, 26).fillna(close).astype('float64')
        except:
            indicators['ema_short'] = close.ewm(span=12).mean().fillna(close).astype('float64')
            indicators['ema_long'] = close.ewm(span=26).mean().fillna(close).astype('float64')
        
        ema_trend_values = np.where(indicators['ema_short'] > indicators['ema_long'], 1.0, -1.0)
        indicators['ema_trend'] = pd.Series(ema_trend_values, index=data.index, dtype='float64')
        
        # === UPDATED: REAL 4H TIMEFRAME FILTER ===
        if self.use_4h_filter and self.h4_data is not None:
            indicators['h4_trend_filter'] = self._calculate_4h_trend_filter(self.h4_data, data.index)
        else:
            indicators['h4_trend_filter'] = pd.Series(0.0, index=data.index, dtype='float64')
        
        # === Bollinger Bands (symbol-specific lookback) ===
        try:
            bb_data = TechnicalIndicators.bollinger_bands(close, lookback_window)
            indicators['bb_upper'] = bb_data['upper'].fillna(close * 1.02).astype('float64')
            indicators['bb_lower'] = bb_data['lower'].fillna(close * 0.98).astype('float64')
            indicators['bb_middle'] = bb_data['middle'].fillna(close).astype('float64')
        except:
            bb_sma = close.rolling(lookback_window).mean().fillna(close)
            bb_std = close.rolling(lookback_window).std().fillna(close * 0.02)
            indicators['bb_upper'] = (bb_sma + 2 * bb_std).astype('float64')
            indicators['bb_lower'] = (bb_sma - 2 * bb_std).astype('float64')
            indicators['bb_middle'] = bb_sma.astype('float64')
        
        bb_range = indicators['bb_upper'] - indicators['bb_lower']
        bb_range_safe = bb_range.replace(0, close.mean() * 0.01)
        indicators['bb_position'] = (
            (close - indicators['bb_lower']) / bb_range_safe
        ).fillna(0.5).clip(0, 1).astype('float64')
        
        # === MACD ===
        try:
            macd_data = TechnicalIndicators.macd(close)
            indicators['macd_line'] = macd_data['macd'].fillna(0.0).astype('float64')
            indicators['macd_signal_line'] = macd_data['signal'].fillna(0.0).astype('float64')
            indicators['macd_histogram'] = macd_data['histogram'].fillna(0.0).astype('float64')
        except:
            ema12 = close.ewm(span=12).mean()
            ema26 = close.ewm(span=26).mean()
            indicators['macd_line'] = (ema12 - ema26).fillna(0.0).astype('float64')
            indicators['macd_signal_line'] = indicators['macd_line'].ewm(span=9).mean().fillna(0.0).astype('float64')
            indicators['macd_histogram'] = (indicators['macd_line'] - indicators['macd_signal_line']).astype('float64')
        
        macd_signal_values = np.where(
            indicators['macd_line'] > indicators['macd_signal_line'], 1.0, -1.0
        )
        indicators['macd_signal'] = pd.Series(macd_signal_values, index=data.index, dtype='float64')
        
        # === RSI ===
        try:
            indicators['rsi'] = TechnicalIndicators.rsi(close).fillna(50.0).astype('float64')
        except:
            delta = close.diff()
            gain = delta.where(delta > 0, 0).rolling(14).mean()
            loss = (-delta.where(delta < 0, 0)).rolling(14).mean()
            rs = gain / (loss + 1e-8)
            indicators['rsi'] = (100 - (100 / (1 + rs))).fillna(50.0).astype('float64')
        
        # === ATR and other indicators (symbol-specific lookback) ===
        if all(col in data.columns for col in ['high', 'low']):
            try:
                high = data['high'].astype('float64')
                low = data['low'].astype('float64')
                indicators['atr'] = TechnicalIndicators.atr(high, low, close).fillna(close * 0.02).astype('float64')
            except:
                tr1 = (data['high'] - data['low']).astype('float64')
                tr2 = abs(data['high'] - close.shift(1)).astype('float64')
                tr3 = abs(data['low'] - close.shift(1)).astype('float64')
                tr = pd.concat([tr1, tr2, tr3], axis=1).max(axis=1).fillna(close * 0.01)
                indicators['atr'] = tr.rolling(14).mean().fillna(close * 0.02).astype('float64')
        else:
            high_proxy = close.rolling(2).max()
            low_proxy = close.rolling(2).min()
            tr = (high_proxy - low_proxy).fillna(close * 0.01)
            indicators['atr'] = tr.rolling(14).mean().fillna(close * 0.02).astype('float64')
        
        price_change = abs(close - close.shift(lookback_window)).fillna(0.0)
        atr_safe = indicators['atr'].replace(0, close.mean() * 0.01)
        indicators['trend_strength'] = (price_change / atr_safe).fillna(0.0).astype('float64')
        
        atr_trend_values = np.where(indicators['trend_strength'] > 1.0, 1.0, 0.0)
        indicators['atr_trend'] = pd.Series(atr_trend_values, index=data.index, dtype='float64')
        
        # === Volume Features (symbol-specific lookback) ===
        indicators['volume_ma'] = volume.rolling(
            lookback_window, min_periods=1
        ).mean().fillna(volume).astype('float64')
        
        volume_ma_safe = indicators['volume_ma'].replace(0, volume.mean() + 1e-8)
        volume_ratio = (volume / volume_ma_safe).fillna(1.0).astype('float64')
        
        volume_breakout_values = np.where(volume_ratio > 1.3, 1.0, 0.0)
        indicators['volume_breakout'] = pd.Series(volume_breakout_values, index=data.index, dtype='float64')
        
        indicators['pv_trend'] = (indicators['log_return'] * volume_ratio).fillna(0.0).astype('float64')
        
        # === VaR Risk Metrics (symbol-specific confidence) ===
        var_multiplier = stats.norm.ppf(var_confidence) if var_confidence < 1.0 else 2.0
        indicators['var_threshold'] = (
            indicators['rolling_mean'] - var_multiplier * indicators['rolling_std']
        ).fillna(-0.05).astype('float64')
        
        extreme_loss_values = (indicators['log_return'] < indicators['var_threshold']).astype('float64')
        indicators['extreme_loss'] = pd.Series(extreme_loss_values, index=data.index, dtype='float64')
        
        # FINAL CHECK: Ensure ALL indicators are float64
        for key, value in indicators.items():
            indicators[key] = pd.Series(value, index=data.index, dtype='float64')
        
        return indicators
    
    def _detect_regimes(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.Series:
        """Regime detection (same as before)"""
        try:
            features_df = pd.DataFrame({
                'log_return': indicators['log_return'].astype('float64'),
                'rolling_std': indicators['rolling_std'].astype('float64')
            }).dropna()
            
            if len(features_df) < 20:
                return pd.Series('medium_vol', index=data.index, dtype='object')
            
            self.gmm = GaussianMixture(n_components=self.n_regimes, random_state=42, max_iter=50)
            self.gmm.fit(features_df.values)
            
            regime_labels = self.gmm.predict(features_df.values)
            regime_series = pd.Series(index=features_df.index, data=regime_labels, dtype='int64')
            
            self._create_regime_mapping(indicators, regime_series)
            
            regime_series = regime_series.reindex(data.index, method='ffill')
            regime_names = regime_series.map(self.regime_mapping).fillna('medium_vol')
            #set regime info
            # self.current_regime_info = regime_info
            return pd.Series(regime_names, index=data.index, dtype='object')
            
        except Exception as e:
            logger.warning(f"Regime detection failed: {e}, using default regime")
            return pd.Series('medium_vol', index=data.index, dtype='object')
    
    def _create_regime_mapping(self, indicators: Dict[str, pd.Series], 
                              regime_series: pd.Series):
        """Create regime mapping (same as before)"""
        stats_list = []
        
        for i in range(self.n_regimes):
            regime_mask = regime_series == i
            if regime_mask.sum() > 0:
                vol_mean = indicators['rolling_std'][regime_mask].mean()
                stats_list.append((i, vol_mean))
        
        if not stats_list:
            self.regime_mapping = {0: 'low_vol', 1: 'medium_vol', 2: 'high_vol'}
            return
        
        stats_list.sort(key=lambda x: x[1])
        regime_names = ['low_vol', 'medium_vol', 'high_vol']
        
        self.regime_mapping = {}
        for i, (regime_num, _) in enumerate(stats_list):
            if i < len(regime_names):
                self.regime_mapping[regime_num] = regime_names[i]
    
    def _calculate_sub_strategy_signals(self, data: pd.DataFrame, 
                                       indicators: Dict[str, pd.Series]) -> Dict[str, pd.Series]:
        """Calculate sub-strategy signals using symbol-specific parameters"""
        signals = {}
        
        # Get symbol-specific parameters
        params = self.get_symbol_params()
        z_threshold = params['z_threshold']
        
        # Get REAL 4h filter
        h4_filter = indicators['h4_trend_filter']
        
        # === Mean Reversion Strategy (with symbol-specific z_threshold) ===
        mean_rev_base = (
            (indicators['z_score'] <= -z_threshold) & 
            (indicators['ema_trend'] == -1.0) &
            (indicators['bb_position'] < 0.3)
        )
        
        # Apply 4h filter: allow mean reversion when 4h trend is bearish or neutral
        if self.use_4h_filter:
            mean_rev_filtered = mean_rev_base & (h4_filter <= 0.0)  # 4h bearish/neutral
        else:
            mean_rev_filtered = mean_rev_base
            
        signals['mean_reversion'] = pd.Series(mean_rev_filtered.astype('float64'), index=data.index)
        
        # === Trend Following Strategy (with 4h filter) ===
        trend_base = (
            (indicators['rolling_mean'] > 0.001) &
            (indicators['ema_trend'] == 1.0) &
            (indicators['macd_signal'] == 1.0) &
            (indicators['atr_trend'] == 1.0)
        )
        
        # Apply 4h filter: only allow trend following when 4h trend is bullish
        if self.use_4h_filter:
            trend_filtered = trend_base & (h4_filter > 0.0)  # 4h bullish
        else:
            trend_filtered = trend_base
            
        signals['trend_following'] = pd.Series(trend_filtered.astype('float64'), index=data.index)
        
        # === Breakout Strategy (with 4h filter) ===
        breakout_base = (
            (indicators['log_return'] > 1.5 * indicators['rolling_std']) &
            (indicators['ema_trend'] == 1.0) &
            (indicators['volume_breakout'] == 1.0)
        )
        
        # Apply 4h filter: only allow breakouts when 4h trend is bullish
        if self.use_4h_filter:
            breakout_filtered = breakout_base & (h4_filter > 0.0)  # 4h bullish
        else:
            breakout_filtered = breakout_base
            
        signals['breakout'] = pd.Series(breakout_filtered.astype('float64'), index=data.index)
        
        # === RSI Strategy (with 4h filter) ===
        rsi_base = (
            (indicators['rsi'] < 35.0) &
            (indicators['ema_trend'] == -1.0)
        )
        
        # Apply 4h filter: allow RSI buy when 4h trend is bearish/neutral (oversold bounce)
        if self.use_4h_filter:
            rsi_filtered = rsi_base & (h4_filter <= 0.0)  # 4h bearish/neutral
        else:
            rsi_filtered = rsi_base
            
        signals['rsi_strategy'] = pd.Series(rsi_filtered.astype('float64'), index=data.index)
        
        # === Risk-off Signal (no symbol-specific params needed) ===
        signals['risk_off'] = indicators['extreme_loss'].astype('float64')
        
        # ENSURE all signals are float64
        for key, value in signals.items():
            signals[key] = pd.Series(value, index=data.index, dtype='float64')
        
        return signals
    
    def generate_signals(self, data: pd.DataFrame, 
                        indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        """Generate signals using symbol-specific parameters"""
        
        # Initialize DataFrame
        signals_df = pd.DataFrame(index=data.index)
        
        # Detect market regimes
        regime = self._detect_regimes(data, indicators)
        signals_df['regime'] = regime.astype('object')
        
        # Calculate individual strategy signals (with symbol-specific parameters)
        sub_signals = self._calculate_sub_strategy_signals(data, indicators)
        
        # Add individual signals to dataframe
        for strategy_name, signal in sub_signals.items():
            signals_df[f'{strategy_name}_signal'] = signal.astype('float64')
        
        # === Regime-Aware Signal Composition ===
        regime_signal_values = np.zeros(len(data), dtype='float64')
        
        for idx, current_regime in enumerate(regime):
            regime_weights = self.regime_weights.get(current_regime, 
                                                   self.regime_weights['medium_vol'])
            
            composite_signal = 0.0
            for strategy_name in sub_signals.keys():
                strategy_signal = sub_signals[strategy_name].iloc[idx]
                weight = regime_weights.get(strategy_name, 0.0)
                composite_signal += strategy_signal * weight
            
            regime_signal_values[idx] = np.clip(composite_signal, -self.max_position, self.max_position)
        
        signals_df['regime_signal'] = pd.Series(regime_signal_values, index=data.index, dtype='float64')
        
        # === Final Signal Processing ===
        signal_threshold = 0.3
        
        signals_df['signal'] = 0.0
        signals_df['signal'] = signals_df['signal'].astype('float64')
        
        buy_mask = signals_df['regime_signal'] > signal_threshold
        sell_mask = signals_df['regime_signal'] < -signal_threshold
        
        signals_df.loc[buy_mask, 'signal'] = 1.0
        signals_df.loc[sell_mask, 'signal'] = -1.0
        
        # Signal strength
        signal_strength = np.abs(signals_df['regime_signal']) / self.max_position
        signals_df['signal_strength'] = pd.Series(signal_strength, index=data.index, dtype='float64').clip(0, 1)
        
        # Add context information
        signals_df['z_score_value'] = indicators['z_score'].astype('float64')
        signals_df['volatility_regime'] = regime.astype('object')
        signals_df['rsi_value'] = indicators['rsi'].astype('float64')
        signals_df['trend_strength'] = indicators['trend_strength'].astype('float64')
        
        # Add symbol-specific parameter info for debugging
        params = self.get_symbol_params()
        signals_df['z_threshold_used'] = params['z_threshold']
        signals_df['lookback_window_used'] = params['lookback_window']
        
        # UPDATED: Add REAL 4h filter information
        signals_df['h4_trend_filter'] = indicators['h4_trend_filter'].astype('float64')
        signals_df['h4_trend_direction'] = signals_df['h4_trend_filter'].map({
            1.0: 'BULLISH', -1.0: 'BEARISH', 0.0: 'NEUTRAL'
        }).fillna('NEUTRAL')
        
        # FINAL CHECK: Ensure proper dtypes
        numeric_columns = ['regime_signal', 'signal', 'signal_strength', 'z_score_value', 
                          'rsi_value', 'trend_strength', 'h4_trend_filter', 
                          'z_threshold_used', 'lookback_window_used']
        for col in numeric_columns:
            if col in signals_df.columns:
                signals_df[col] = pd.to_numeric(signals_df[col], errors='coerce').fillna(0.0).astype('float64')
        
        for strategy_name in sub_signals.keys():
            col_name = f'{strategy_name}_signal'
            if col_name in signals_df.columns:
                signals_df[col_name] = signals_df[col_name].astype('float64')
        
        return signals_df
    
    def get_strategy_info(self) -> Dict:
        """Get strategy information including current symbol parameters"""
        params = self.get_symbol_params()
        return {
            'name': 'AdvancedStatisticalStrategy',
            'type': 'multi_strategy',
            'current_symbol': self.current_symbol,
            'symbol_specific_params': params,
            'timeframes': ['1d', '4h'],
            'strategies': ['mean_reversion', 'trend_following', 'breakout', 'rsi_strategy'],
            'use_4h_filter': self.use_4h_filter
        }