import numpy as np
import pandas as pd
import yfinance as yf
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple, Any, Union
from sklearn.mixture import GaussianMixture
from scipy import stats
import warnings
warnings.filterwarnings("ignore")

@dataclass
class StrategyConfig:
    """Enhanced configuration with advanced risk management"""
    # Basic parameters
    lookback_window: int = 30
    z_threshold: float = 1.5
    transaction_cost: float = 0.001
    max_position: float = 1.0
    var_confidence: float = 0.99
    
    # Stop-loss parameters
    max_drawdown_threshold: float = 0.20  # 20% max drawdown
    trailing_stop_threshold: float = 0.15  # 15% trailing stop
    enable_stop_loss: bool = True
    
    # Regime-specific strategy weights
    regime_strategy_weights: Dict[str, Dict[str, float]] = field(default_factory=lambda: {
        'low_vol': {
            'mean_reversion': 2.0,
            'trend_following': 0.5,
            'breakout': 0.0,
            'rsi_strategy': 1.0,
            'risk_off': -2.0
        },
        'medium_vol': {
            'mean_reversion': 0.5,
            'trend_following': 2.0,
            'breakout': 1.0,
            'rsi_strategy': 1.0,
            'risk_off': -2.0
        },
        'high_vol': {
            'mean_reversion': 0.0,
            'trend_following': 0.0,
            'breakout': 1.5,
            'rsi_strategy': 0.0,
            'risk_off': -3.0
        }
    })

class BaseStrategy(ABC):
    """Enhanced base strategy with regime awareness"""
    
    def __init__(self, name: str, preferred_regimes: List[str] = None):
        self.name = name
        self.preferred_regimes = preferred_regimes or ['low_vol', 'medium_vol', 'high_vol']
    
    @abstractmethod
    def generate_signals(self, data: pd.DataFrame, config: StrategyConfig) -> pd.Series:
        pass
    
    @abstractmethod
    def get_required_features(self) -> List[str]:
        pass
    
    def is_regime_suitable(self, regime: str) -> bool:
        """Check if current regime is suitable for this strategy"""
        return regime in self.preferred_regimes

class MeanReversionStrategy(BaseStrategy):
    """Enhanced mean reversion - works best in low volatility"""
    
    def __init__(self):
        super().__init__("mean_reversion", preferred_regimes=['low_vol', 'medium_vol'])
    
    def generate_signals(self, data: pd.DataFrame, config: StrategyConfig) -> pd.Series:
        z_score = data['z_score']
        ema_trend = data['ema_trend']
        bb_position = data['bb_position']  # Bollinger Band position
        
        # Enhanced mean reversion with Bollinger Bands
        signals = (
            (z_score <= -config.z_threshold) & 
            (ema_trend == -1) &
            (bb_position < 0.2)  # Near lower Bollinger Band
        ).astype(int)
        
        return signals
    
    def get_required_features(self) -> List[str]:
        return ['z_score', 'ema_trend', 'bb_position']

class TrendFollowingStrategy(BaseStrategy):
    """Enhanced trend following with ATR filter"""
    
    def __init__(self):
        super().__init__("trend_following", preferred_regimes=['medium_vol'])
    
    def generate_signals(self, data: pd.DataFrame, config: StrategyConfig) -> pd.Series:
        rolling_mean = data['rolling_mean']
        ema_trend = data['ema_trend']
        macd_signal = data['macd_signal']
        atr_trend = data['atr_trend']
        
        # Enhanced trend following with MACD and ATR
        signals = (
            (rolling_mean > 0) & 
            (ema_trend == 1) &
            (macd_signal == 1) &
            (atr_trend == 1)  # Strong trend confirmed by ATR
        ).astype(int)
        
        return signals
    
    def get_required_features(self) -> List[str]:
        return ['rolling_mean', 'ema_trend', 'macd_signal', 'atr_trend']

class BreakoutStrategy(BaseStrategy):
    """Volume-confirmed breakout strategy for high volatility"""
    
    def __init__(self):
        super().__init__("breakout", preferred_regimes=['high_vol', 'medium_vol'])
    
    def generate_signals(self, data: pd.DataFrame, config: StrategyConfig) -> pd.Series:
        log_return = data['log_return']
        rolling_std = data['rolling_std']
        ema_trend = data['ema_trend']
        volume_breakout = data['volume_breakout']
        
        # Volume-confirmed breakout
        signals = (
            (log_return > 2 * rolling_std) & 
            (ema_trend == 1) &
            (volume_breakout == 1)  # Volume confirmation
        ).astype(int)
        
        return signals
    
    def get_required_features(self) -> List[str]:
        return ['log_return', 'rolling_std', 'ema_trend', 'volume_breakout']

class RSIStrategy(BaseStrategy):
    """RSI-based strategy for oversold/overbought conditions"""
    
    def __init__(self):
        super().__init__("rsi_strategy", preferred_regimes=['low_vol', 'medium_vol'])
    
    def generate_signals(self, data: pd.DataFrame, config: StrategyConfig) -> pd.Series:
        rsi = data['rsi']
        ema_trend = data['ema_trend']
        
        # RSI oversold in downtrend (contrarian)
        signals = (
            (rsi < 30) & 
            (ema_trend == -1)
        ).astype(int)
        
        return signals
    
    def get_required_features(self) -> List[str]:
        return ['rsi', 'ema_trend']

class StopLossManager:
    """Advanced stop-loss and risk management"""
    
    def __init__(self, config: StrategyConfig):
        self.config = config
        self.position_entry_prices = {}
        self.position_high_water_marks = {}
        self.cumulative_pnl = 0.0
        self.max_cumulative_pnl = 0.0
    
    def update_positions(self, current_price: float, current_position: float, 
                        current_return: float, timestamp: Any) -> float:
        """Update position tracking and apply stop-loss logic"""
        if not self.config.enable_stop_loss:
            return current_position
        
        # Update cumulative P&L tracking
        self.cumulative_pnl += current_return
        self.max_cumulative_pnl = max(self.max_cumulative_pnl, self.cumulative_pnl)
        
        # Check portfolio-level drawdown stop
        if self.max_cumulative_pnl > 0:
            current_drawdown = (self.max_cumulative_pnl - self.cumulative_pnl) / self.max_cumulative_pnl
            if current_drawdown > self.config.max_drawdown_threshold:
                # Force exit all positions
                self._reset_tracking()
                return 0.0
        
        # Handle position entry/exit tracking
        if current_position != 0 and timestamp not in self.position_entry_prices:
            # New position entry
            self.position_entry_prices[timestamp] = current_price
            self.position_high_water_marks[timestamp] = current_price
        elif current_position != 0 and timestamp in self.position_entry_prices:
            # Update high water mark for trailing stop
            if current_position > 0:  # Long position
                self.position_high_water_marks[timestamp] = max(
                    self.position_high_water_marks[timestamp], current_price
                )
                
                # Check trailing stop
                entry_price = self.position_entry_prices[timestamp]
                high_water = self.position_high_water_marks[timestamp]
                
                # If we have profit, check trailing stop
                if current_price > entry_price:
                    trailing_stop_price = high_water * (1 - self.config.trailing_stop_threshold)
                    if current_price < trailing_stop_price:
                        # Trigger trailing stop
                        del self.position_entry_prices[timestamp]
                        del self.position_high_water_marks[timestamp]
                        return 0.0
        elif current_position == 0:
            # Position closed, clean up tracking
            if timestamp in self.position_entry_prices:
                del self.position_entry_prices[timestamp]
            if timestamp in self.position_high_water_marks:
                del self.position_high_water_marks[timestamp]
        
        return current_position
    
    def _reset_tracking(self):
        """Reset all position tracking"""
        self.position_entry_prices.clear()
        self.position_high_water_marks.clear()

class EnhancedFeatureEngineer:
    """Advanced feature engineering with technical indicators"""
    
    @staticmethod
    def engineer_features(data: pd.DataFrame, config: StrategyConfig) -> pd.DataFrame:
        """Engineer comprehensive technical features"""
        data = data.copy()
        
        # Ensure volume column exists (use dummy if not available)
        if 'volume' not in data.columns:
            data['volume'] = 1.0  # Dummy volume for crypto pairs without volume data
        
        # Basic features
        data['log_return'] = np.log(data['close'] / data['close'].shift(1))
        data['rolling_mean'] = data['log_return'].rolling(config.lookback_window).mean()
        data['rolling_std'] = data['log_return'].rolling(config.lookback_window).std()
        data['z_score'] = (data['log_return'] - data['rolling_mean']) / data['rolling_std']
        
        # EMA trend
        data['ema_short'] = data['close'].ewm(span=12).mean()
        data['ema_long'] = data['close'].ewm(span=26).mean()
        data['ema_trend'] = np.where(data['ema_short'] > data['ema_long'], 1, -1)
        
        # Bollinger Bands
        data = EnhancedFeatureEngineer._add_bollinger_bands(data, config.lookback_window)
        
        # MACD
        data = EnhancedFeatureEngineer._add_macd(data)
        
        # RSI
        data = EnhancedFeatureEngineer._add_rsi(data)
        
        # ATR and trend strength
        data = EnhancedFeatureEngineer._add_atr_features(data)
        
        # Volume features
        data = EnhancedFeatureEngineer._add_volume_features(data, config.lookback_window)
        
        return data
    
    @staticmethod
    def _add_bollinger_bands(data: pd.DataFrame, window: int) -> pd.DataFrame:
        """Add Bollinger Bands features"""
        sma = data['close'].rolling(window).mean()
        std = data['close'].rolling(window).std()
        
        data['bb_upper'] = sma + (2 * std)
        data['bb_lower'] = sma - (2 * std)
        data['bb_middle'] = sma
        
        # Position within Bollinger Bands (0 = lower band, 1 = upper band)
        bb_range = data['bb_upper'] - data['bb_lower']
        data['bb_position'] = (data['close'] - data['bb_lower']) / bb_range
        
        # Handle division by zero
        data['bb_position'] = data['bb_position'].fillna(0.5)
        
        return data
    
    @staticmethod
    def _add_macd(data: pd.DataFrame) -> pd.DataFrame:
        """Add MACD features"""
        ema12 = data['close'].ewm(span=12).mean()
        ema26 = data['close'].ewm(span=26).mean()
        
        data['macd_line'] = ema12 - ema26
        data['macd_signal_line'] = data['macd_line'].ewm(span=9).mean()
        data['macd_histogram'] = data['macd_line'] - data['macd_signal_line']
        
        # MACD signal (1 = bullish, -1 = bearish)
        data['macd_signal'] = np.where(data['macd_line'] > data['macd_signal_line'], 1, -1)
        
        return data
    
    @staticmethod
    def _add_rsi(data: pd.DataFrame, window: int = 14) -> pd.DataFrame:
        """Add RSI indicator"""
        delta = data['close'].diff()
        gain = (delta.where(delta > 0, 0)).rolling(window=window).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(window=window).mean()
        
        # Avoid division by zero
        rs = gain / (loss + 1e-8)
        data['rsi'] = 100 - (100 / (1 + rs))
        
        return data
    
    @staticmethod
    def _add_atr_features(data: pd.DataFrame, window: int = 14) -> pd.DataFrame:
        """Add ATR and trend strength features"""
        # For crypto pairs, calculate True Range using high/low of recent prices
        data['high_proxy'] = data['close'].rolling(2).max()
        data['low_proxy'] = data['close'].rolling(2).min()
        
        data['tr'] = data['high_proxy'] - data['low_proxy']
        data['atr'] = data['tr'].rolling(window).mean()
        
        # ATR-based trend strength
        price_change = abs(data['close'] - data['close'].shift(window))
        data['trend_strength'] = price_change / (data['atr'] + 1e-8)  # Avoid division by zero
        
        # Strong trend signal (1 if trend strength > threshold)
        data['atr_trend'] = np.where(data['trend_strength'] > 1.5, 1, 0)
        
        return data
    
    @staticmethod
    def _add_volume_features(data: pd.DataFrame, window: int) -> pd.DataFrame:
        """Add volume-based features"""
        # Volume moving average
        data['volume_ma'] = data['volume'].rolling(window).mean()
        
        # Volume breakout (volume > 1.5x average)
        data['volume_breakout'] = np.where(
            data['volume'] > 1.5 * data['volume_ma'], 1, 0
        )
        #fill zero
        data['volume_breakout'] = data['volume_breakout'].fillna(0)
        
        # Price-volume trend
        data['pv_trend'] = data['log_return'] * (data['volume'] / (data['volume_ma'] + 1e-8))
        
        return data

class RegimeAwareSignalComposer:
    """Regime-aware signal composition with dynamic weighting"""
    
    def __init__(self, strategies: List[BaseStrategy], config: StrategyConfig):
        self.strategies = strategies
        self.config = config
    
    def compose_signals(self, data: pd.DataFrame, regime: pd.Series, 
                       risk_manager, stop_loss_manager: StopLossManager) -> pd.DataFrame:
        """Generate regime-aware composed signals"""
        signals_df = pd.DataFrame(index=data.index)
        
        # Generate individual strategy signals
        for strategy in self.strategies:
            try:
                raw_signals = strategy.generate_signals(data, self.config)
                signals_df[strategy.name] = raw_signals
            except KeyError as e:
                print(f"Warning: Missing features for {strategy.name}: {e}")
                signals_df[strategy.name] = 0  # Default to no signal
        
        # Regime-aware signal composition
        final_signals = []
        for idx, current_regime in regime.items():
            if pd.isna(current_regime):
                final_signals.append(0)
                continue
            
            # Get regime-specific weights
            regime_weights = self.config.regime_strategy_weights.get(current_regime, {})
            
            # Compose signal based on regime
            signal = 0
            for strategy in self.strategies:
                strategy_signal = signals_df.loc[idx, strategy.name]
                weight = regime_weights.get(strategy.name, 0)
                
                # Only use signal if regime is suitable for strategy
                if strategy.is_regime_suitable(current_regime):
                    signal += strategy_signal * weight
            
            final_signals.append(signal)
        
        signals_df['regime_signal'] = final_signals
        
        # Apply risk management
        data_with_var = risk_manager.calculate_var_metrics(data)
        risk_adjusted_signals = []
        
        for idx in signals_df.index:
            if idx not in data_with_var.index or idx not in regime.index:
                risk_adjusted_signals.append(0)
                continue
                
            base_signal = signals_df.loc[idx, 'regime_signal']
            
            # Apply extreme loss risk-off
            if data_with_var.loc[idx, 'extreme_loss']:
                current_regime = regime.loc[idx] if not pd.isna(regime.loc[idx]) else 'medium_vol'
                risk_weight = self.config.regime_strategy_weights.get(current_regime, {}).get('risk_off', -2.0)
                base_signal += risk_weight
            
            # Clip to position limits
            base_signal = np.clip(base_signal, -self.config.max_position, self.config.max_position)
            
            # Apply stop-loss management
            current_price = data.loc[idx, 'close']
            current_return = data.loc[idx, 'log_return'] if not pd.isna(data.loc[idx, 'log_return']) else 0
            
            final_signal = stop_loss_manager.update_positions(
                current_price, base_signal, current_return, idx
            )
            
            risk_adjusted_signals.append(final_signal)
        
        signals_df['final_position'] = risk_adjusted_signals
        
        return signals_df

class CryptoTradingSystemV3:
    """Advanced crypto trading system with regime-aware strategies and stop-loss"""
    
    def __init__(self, config: StrategyConfig = None):
        self.config = config or StrategyConfig()
        
        # Initialize enhanced components
        self.strategies = [
            MeanReversionStrategy(),
            TrendFollowingStrategy(),
            BreakoutStrategy(),
            RSIStrategy()
        ]
        
        self.regime_detector = RegimeDetector()  # Reuse from V2
        self.risk_manager = RiskManager(self.config)  # Reuse from V2
        self.feature_engineer = EnhancedFeatureEngineer()
        self.performance_analyzer = PerformanceAnalyzer()  # Reuse from V2
    
    def run_backtest(self, data: pd.DataFrame) -> Tuple[Dict[str, Any], pd.DataFrame]:
        """Run enhanced backtest with stop-loss and regime awareness"""
        # Feature engineering
        data_with_features = self.feature_engineer.engineer_features(data, self.config)
        
        # Regime detection
        regime = self.regime_detector.fit_detect(data_with_features)
        
        # Initialize stop-loss manager
        stop_loss_manager = StopLossManager(self.config)
        
        # Signal generation with regime awareness
        signal_composer = RegimeAwareSignalComposer(self.strategies, self.config)
        signals_df = signal_composer.compose_signals(
            data_with_features, regime, self.risk_manager, stop_loss_manager
        )
        
        # Add market data for performance calculation
        signals_df['market_return'] = data_with_features['log_return']
        signals_df['regime'] = regime
        
        # Performance analysis
        performance_metrics = self.performance_analyzer.calculate_metrics(
            signals_df.rename(columns={'final_position': 'position'}), self.config
        )
        
        return performance_metrics, signals_df
    
    def optimize_parameters(self, data: pd.DataFrame, param_grid: Dict[str, List]) -> pd.DataFrame:
        """Enhanced parameter optimization with regime-aware weights"""
        results = []
        total_combinations = 1
        
        # Calculate total combinations for progress tracking
        for param_values in param_grid.values():
            total_combinations *= len(param_values)
        
        print(f"Running optimization with {total_combinations} parameter combinations...")
        
        combination_count = 0
        
        # Basic parameter combinations
        lookback_windows = param_grid.get('lookback_window', [self.config.lookback_window])
        z_thresholds = param_grid.get('z_threshold', [self.config.z_threshold])
        regime_filters = param_grid.get('regime_filter', [True])
        stop_loss_settings = param_grid.get('enable_stop_loss', [self.config.enable_stop_loss])
        drawdown_thresholds = param_grid.get('max_drawdown_threshold', [self.config.max_drawdown_threshold])
        trailing_stops = param_grid.get('trailing_stop_threshold', [self.config.trailing_stop_threshold])
        
        for lookback in lookback_windows:
            for z_thresh in z_thresholds:
                for regime_filter in regime_filters:
                    for enable_sl in stop_loss_settings:
                        for dd_thresh in drawdown_thresholds:
                            for trail_stop in trailing_stops:
                                combination_count += 1
                                
                                try:
                                    # Create config for this iteration
                                    test_config = StrategyConfig(
                                        lookback_window=lookback,
                                        z_threshold=z_thresh,
                                        enable_stop_loss=enable_sl,
                                        max_drawdown_threshold=dd_thresh,
                                        trailing_stop_threshold=trail_stop,
                                        transaction_cost=self.config.transaction_cost,
                                        regime_strategy_weights=self.config.regime_strategy_weights
                                    )
                                    
                                    # Create temporary system with test config
                                    temp_system = CryptoTradingSystemV3(test_config)
                                    performance, signals = temp_system.run_backtest(data)
                                    
                                    # Extract numerical values from formatted strings
                                    total_return_num = float(performance['total_return'].strip('%')) / 100
                                    sharpe_num = float(performance['sharpe_ratio'])
                                    max_dd_num = float(performance['max_drawdown'].strip('%')) / 100
                                    win_rate_num = float(performance['win_rate'].strip('%')) / 100
                                    
                                    # Store results
                                    result = {
                                        'lookback_window': lookback,
                                        'z_threshold': z_thresh,
                                        'enable_stop_loss': enable_sl,
                                        'max_drawdown_threshold': dd_thresh,
                                        'trailing_stop_threshold': trail_stop,
                                        'total_return': total_return_num,
                                        'sharpe_ratio': sharpe_num,
                                        'max_drawdown': max_dd_num,
                                        'win_rate': win_rate_num,
                                        'num_trades': performance['num_trades'],
                                        'avg_trade_return': float(performance['avg_trade_return']),
                                        # Add regime distribution
                                        'regime_distribution': dict(signals['regime'].value_counts()) if 'regime' in signals.columns else {}
                                    }
                                    results.append(result)
                                    
                                    # Progress update
                                    if combination_count % 10 == 0 or combination_count == total_combinations:
                                        print(f"Progress: {combination_count}/{total_combinations} "
                                              f"({combination_count/total_combinations*100:.1f}%)")
                                    
                                except Exception as e:
                                    print(f"Error with params combination {combination_count}: {e}")
                                    continue
        
        results_df = pd.DataFrame(results)
        
        if not results_df.empty:
            # Sort by Sharpe ratio by default
            results_df = results_df.sort_values('sharpe_ratio', ascending=False)
            print(f"\nOptimization completed! Best Sharpe ratio: {results_df.iloc[0]['sharpe_ratio']:.3f}")
        
        return results_df
    
    def run_regime_weight_optimization(self, data: pd.DataFrame, 
                                     weight_ranges: Dict[str, List[float]] = None) -> pd.DataFrame:
        """Optimize regime-specific strategy weights"""
        if weight_ranges is None:
            weight_ranges = {
                'mean_reversion': [0.0, 0.5, 1.0, 1.5, 2.0],
                'trend_following': [0.0, 0.5, 1.0, 1.5, 2.0],
                'breakout': [0.0, 0.5, 1.0, 1.5, 2.0],
                'rsi_strategy': [0.0, 0.5, 1.0, 1.5],
                'risk_off': [-3.0, -2.5, -2.0, -1.5, -1.0]
            }
        
        results = []
        regimes = ['low_vol', 'medium_vol', 'high_vol']
        
        print("Optimizing regime-specific strategy weights...")
        
        # For each regime, try different weight combinations
        for regime in regimes:
            print(f"\nOptimizing weights for {regime} regime...")
            
            for mr_weight in weight_ranges['mean_reversion']:
                for tf_weight in weight_ranges['trend_following']:
                    for bo_weight in weight_ranges['breakout']:
                        for rsi_weight in weight_ranges['rsi_strategy']:
                            for risk_weight in weight_ranges['risk_off']:
                                
                                try:
                                    # Create custom regime weights
                                    regime_weights = self.config.regime_strategy_weights.copy()
                                    regime_weights[regime] = {
                                        'mean_reversion': mr_weight,
                                        'trend_following': tf_weight,
                                        'breakout': bo_weight,
                                        'rsi_strategy': rsi_weight,
                                        'risk_off': risk_weight
                                    }
                                    
                                    # Create test config
                                    test_config = StrategyConfig(
                                        lookback_window=self.config.lookback_window,
                                        z_threshold=self.config.z_threshold,
                                        regime_strategy_weights=regime_weights
                                    )
                                    
                                    # Run backtest
                                    temp_system = CryptoTradingSystemV3(test_config)
                                    performance, _ = temp_system.run_backtest(data)
                                    
                                    # Store results
                                    result = {
                                        'optimized_regime': regime,
                                        'mean_reversion_weight': mr_weight,
                                        'trend_following_weight': tf_weight,
                                        'breakout_weight': bo_weight,
                                        'rsi_strategy_weight': rsi_weight,
                                        'risk_off_weight': risk_weight,
                                        'total_return': float(performance['total_return'].strip('%')) / 100,
                                        'sharpe_ratio': float(performance['sharpe_ratio']),
                                        'max_drawdown': float(performance['max_drawdown'].strip('%')) / 100,
                                        'win_rate': float(performance['win_rate'].strip('%')) / 100
                                    }
                                    results.append(result)
                                    
                                except Exception as e:
                                    continue
        
        results_df = pd.DataFrame(results)
        
        if not results_df.empty:
            # Group by regime and show best weights for each
            print("\nBest weights by regime (sorted by Sharpe ratio):")
            for regime in regimes:
                regime_results = results_df[results_df['optimized_regime'] == regime]
                if not regime_results.empty:
                    best = regime_results.loc[regime_results['sharpe_ratio'].idxmax()]
                    print(f"\n{regime}:")
                    print(f"  Mean Reversion: {best['mean_reversion_weight']}")
                    print(f"  Trend Following: {best['trend_following_weight']}")
                    print(f"  Breakout: {best['breakout_weight']}")
                    print(f"  RSI Strategy: {best['rsi_strategy_weight']}")
                    print(f"  Risk Off: {best['risk_off_weight']}")
                    print(f"  Sharpe: {best['sharpe_ratio']:.3f}")
        
        return results_df

# Reuse previous classes for compatibility
class RegimeDetector:
    """Market regime detection using Gaussian Mixture Models"""
    
    def __init__(self, n_components: int = 3):
        self.n_components = n_components
        self.gmm = None
        self.regime_mapping = {}
    
    def fit_detect(self, data: pd.DataFrame) -> pd.Series:
        """Fit GMM and detect regimes"""
        features = data[['log_return', 'rolling_std']].dropna()
        
        self.gmm = GaussianMixture(n_components=self.n_components, random_state=42)
        self.gmm.fit(features)
        
        regime_labels = self.gmm.predict(features)
        regime_series = pd.Series(index=features.index, data=regime_labels)
        
        # Create regime mapping based on volatility
        self._create_regime_mapping(data, regime_series)
        
        # Forward fill regime data
        regime_series = regime_series.reindex(data.index, method='ffill')
        regime_names = regime_series.map(self.regime_mapping)
        
        return regime_names
    
    def _create_regime_mapping(self, data: pd.DataFrame, regime_series: pd.Series):
        """Create mapping from regime numbers to volatility levels"""
        stats_list = []
        for i in range(self.n_components):
            subset = data.loc[regime_series[regime_series == i].index]
            if len(subset) > 0:
                vol_mean = subset['rolling_std'].mean()
                stats_list.append((i, vol_mean))
        
        # Sort by volatility
        stats_list.sort(key=lambda x: x[1])
        
        regime_names = ['low_vol', 'medium_vol', 'high_vol']
        self.regime_mapping = {
            stats_list[i][0]: regime_names[i] 
            for i in range(min(len(stats_list), len(regime_names)))
        }

class RiskManager:
    """Risk management and position sizing"""
    
    def __init__(self, config: StrategyConfig):
        self.config = config
    
    def calculate_var_metrics(self, data: pd.DataFrame) -> pd.DataFrame:
        """Calculate Value at Risk metrics"""
        data = data.copy()
        rolling_mean = data['rolling_mean']
        rolling_std = data['rolling_std']
        
        data['var_threshold'] = rolling_mean - stats.norm.ppf(self.config.var_confidence) * rolling_std
        data['extreme_loss'] = data['log_return'] < data['var_threshold']
        
        return data

class PerformanceAnalyzer:
    """Comprehensive performance analysis"""
    
    @staticmethod
    def calculate_metrics(signals_df: pd.DataFrame, config: StrategyConfig) -> Dict[str, Any]:
        """Calculate comprehensive performance metrics"""
        # Strategy returns
        strategy_returns = (
            signals_df['position'].shift(1) * signals_df['market_return'] -
            abs(signals_df['position'].diff()) * config.transaction_cost
        )
        
        cum_returns = strategy_returns.cumsum()
        
        # Basic metrics
        total_return = np.exp(strategy_returns.sum()) - 1
        annualized_return = np.exp(strategy_returns.mean() * 365) - 1
        volatility = strategy_returns.std() * np.sqrt(365)
        sharpe_ratio = strategy_returns.mean() / strategy_returns.std() * np.sqrt(365) if strategy_returns.std() > 0 else 0
        
        # Drawdown analysis
        running_max = cum_returns.cummax()
        drawdown = cum_returns - running_max
        max_drawdown = drawdown.min()
        
        # Trade analysis
        trades = signals_df['position'].diff().fillna(0)
        num_trades = (trades != 0).sum()
        win_rate = (strategy_returns > 0).mean()
        
        return {
            'total_return': f"{total_return:.2%}",
            'annualized_return': f"{annualized_return:.2%}",
            'volatility': f"{volatility:.2%}",
            'sharpe_ratio': f"{sharpe_ratio:.2f}",
            'max_drawdown': f"{max_drawdown:.2%}",
            'win_rate': f"{win_rate:.2%}",
            'num_trades': num_trades,
            'avg_trade_return': f"{strategy_returns.mean():.4f}",
            'raw_metrics': {
                'strategy_returns': strategy_returns,
                'cumulative_returns': cum_returns,
                'drawdown': drawdown
            }
        }
#function for optimization
def optimize_strategy_parameters(data: pd.DataFrame, config: StrategyConfig) -> pd.DataFrame:
    """Run parameter optimization for the trading strategy"""
    trading_system = CryptoTradingSystemV3(config)
    
    # Define parameter grid
    param_grid = {
        'lookback_window': [20, 30, 45],
        'z_threshold': [1.0, 1.5, 2.0],
        'enable_stop_loss': [True, False],
        'max_drawdown_threshold': [0.10, 0.15, 0.20],
        'trailing_stop_threshold': [0.08, 0.10, 0.15]
    }
    
    # Run optimization
    optimization_results = trading_system.optimize_parameters(data, param_grid)
    
    if not optimization_results.empty:
        # Sort by Sharpe ratio
        optimization_results = optimization_results.sort_values('sharpe_ratio', ascending=False)
        print(f"\nOptimization completed! Best Sharpe ratio: {optimization_results.iloc[0]['sharpe_ratio']:.3f}")
    return optimization_results

# Example usage
if __name__ == '__main__':
    # Test enhanced system
    symbol_list = ['BTC-USD', 'ETH-USD', 'SOL-USD','ARB-USD', 'MATIC-USD', 'XRP-USD']
    symbol = 'XRP-USD'
    
    # Enhanced configuration
    config = StrategyConfig(
        lookback_window=20,
        z_threshold= 2.0,
        max_drawdown_threshold= 0.10,  # 10% max drawdown
        trailing_stop_threshold= 0.15,  # 15% trailing stop
        enable_stop_loss= False,  # Disable stop-loss for this example
        regime_strategy_weights={
            'low_vol': {
                'mean_reversion': 2.0,
                'trend_following': 0.5,
                'breakout': 0.0,
                'rsi_strategy': 1.5,
                'risk_off': -2.0
            },
            'medium_vol': {
                'mean_reversion': 1.0,
                'trend_following': 2.0,
                'breakout': 1.0,
                'rsi_strategy': 1.0,
                'risk_off': -2.0
            },
            'high_vol': {
                'mean_reversion': 0.0,
                'trend_following': 0.0,
                'breakout': 2.0,
                'rsi_strategy': 0.0,
                'risk_off': -3.0
            }
        }
    )
    
    # Download data with auto_adjust=False to maintain compatibility
    df = yf.download(symbol, start="2021-01-01", end="2024-12-31", 
                     interval='1d', progress=False, auto_adjust=False)
    #validation data is from 2025 January 1st till now
    df_validation = yf.download(symbol, start="2025-01-01", end="2025-05-26", 
                                interval='1d', progress=False, auto_adjust=False)
    if not df.empty:
        # Handle potential MultiIndex columns
        if isinstance(df.columns, pd.MultiIndex):
            df.columns = df.columns.droplevel(1)
        if isinstance(df_validation.columns, pd.MultiIndex):
            df_validation.columns = df_validation.columns.droplevel(1)
        df = df.rename(columns={'Close': 'close', 'Volume': 'volume'})[['close', 'volume']].dropna()
        df_validation = df_validation.rename(columns={'Close': 'close', 'Volume': 'volume'})[['close', 'volume']].dropna()
        # Initialize trading system
        trading_system = CryptoTradingSystemV3(config)
        
        # === SINGLE BACKTEST ===
        print(f"=== Single Backtest Results for {symbol} ===")
        performance, signals = trading_system.run_backtest(df)
        
        for key, value in performance.items():
            if key != 'raw_metrics':
                print(f"  {key}: {value}")
        
        # Show regime distribution
        if 'regime' in signals.columns:
            regime_dist = signals['regime'].value_counts()
            print(f"\nRegime Distribution:")
            for regime, count in regime_dist.items():
                print(f"  {regime}: {count} days ({count/len(signals)*100:.1f}%)")
        
        #we already optimized, no need to do so
        # Save backtest results to CSV
        signals.to_csv(f'{symbol}_backtest_signals.csv', index=True)
        print(f"\n📁 Backtest results saved to {symbol}_backtest_signals.csv")

        #run validation
        print(f"\n=== Validation Backtest Results for {symbol} ===")
        validation_performance, validation_signals = trading_system.run_backtest(df_validation)
        for key, value in validation_performance.items():
            if key != 'raw_metrics':
                print(f"  {key}: {value}")
        # Show validation regime distribution
        if 'regime' in validation_signals.columns:
            validation_regime_dist = validation_signals['regime'].value_counts()
            print(f"\nValidation Regime Distribution:")
            for regime, count in validation_regime_dist.items():
                print(f"  {regime}: {count} days ({count/len(validation_signals)*100:.1f}%)")
        # Save validation results to CSV
        validation_signals.to_csv(f'{symbol}_validation_signals.csv', index=True)
        print(f"\n📁 Validation results saved to {symbol}_validation_signals.csv")

        #quit
        #optimized result
        # === TOP 5 PARAMETER COMBINATIONS ===


        PARAMS_OPTIMIZE = False  # Set to True to run parameter optimization
        if PARAMS_OPTIMIZE:
            # === PARAMETER OPTIMIZATION ===
            print(f"\n" + "="*50)
            print("PARAMETER OPTIMIZATION")
            print("="*50)
            
            # Define parameter grid
            param_grid = {
                'lookback_window': [20, 30, 45],
                'z_threshold': [1.0, 1.5, 2.0],
                'enable_stop_loss': [True, False],
                'max_drawdown_threshold': [0.10, 0.15, 0.20],
                'trailing_stop_threshold': [0.08, 0.10, 0.15]
            }
            
            # Run parameter optimization
            optimization_results = trading_system.optimize_parameters(df, param_grid)
            
            if not optimization_results.empty:
                print(f"\n=== TOP 5 PARAMETER COMBINATIONS ===")
                top_5 = optimization_results.head(5)
                
                for i, (idx, row) in enumerate(top_5.iterrows(), 1):
                    print(f"\nRank {i}:")
                    print(f"  Lookback: {row['lookback_window']}, Z-threshold: {row['z_threshold']}")
                    print(f"  Stop-loss: {row['enable_stop_loss']}, Max DD: {row['max_drawdown_threshold']:.2f}")
                    print(f"  Trailing Stop: {row['trailing_stop_threshold']:.2f}")
                    print(f"  Total Return: {row['total_return']:.2%}, Sharpe: {row['sharpe_ratio']:.3f}")
                    print(f"  Max Drawdown: {row['max_drawdown']:.2%}, Win Rate: {row['win_rate']:.2%}")
                
                # Save results to CSV
                optimization_results.to_csv(f'{symbol}_optimization_results.csv', index=False)
                print(f"\n📁 Results saved to {symbol}_optimization_results.csv")
            
            # === REGIME WEIGHT OPTIMIZATION (OPTIONAL - COMMENT OUT FOR SPEED) ===
            print(f"\n" + "="*50)
            print("REGIME WEIGHT OPTIMIZATION (Reduced Grid)")
            print("="*50)
            
            # Smaller grid for faster testing
            weight_ranges = {
                'mean_reversion': [0.0, 1.0, 2.0],
                'trend_following': [0.0, 1.0, 2.0],
                'breakout': [0.0, 1.0, 2.0],
                'rsi_strategy': [0.0, 1.0],
                'risk_off': [-3.0, -2.0, -1.0]
            }
            
            # Run regime weight optimization (uncomment to enable)
            # regime_results = trading_system.run_regime_weight_optimization(df, weight_ranges)
            # if not regime_results.empty:
            #     regime_results.to_csv(f'{symbol}_regime_optimization.csv', index=False)
            #     print(f"📁 Regime optimization results saved to {symbol}_regime_optimization.csv")
            
            print("\n✅ All optimizations completed!")
            
            