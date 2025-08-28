# src/strategy/regime_adaptive_manager.py
import numpy as np
import pandas as pd
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
from abc import ABC, abstractmethod
import logging

from .base_strategy import BaseStrategy
from .strategies.statistical_mean_reversion import StatisticalMeanReversionStrategy
from ..perception.enhanced_regime_detector import MarketRegime, RegimeSignal

@dataclass
class StrategyAllocation:
    strategy_name: str
    allocation: float  # 0-1
    confidence: float  # 0-1
    expected_performance: float
    risk_score: float
    regime_fit: float  # How well this strategy fits current regime

class RegimeAdaptiveStrategyManager:
    """
    Manager that dynamically allocates between strategies based on market regime
    """
    
    def __init__(self, config: dict):
        self.config = config
        self.strategies = {}
        self.regime_strategy_matrix = self._build_regime_strategy_matrix()
        self.performance_tracker = StrategyPerformanceTracker()
        self.allocation_optimizer = AllocationOptimizer()
        
        # Current allocations
        self.current_allocations = {}
        self.target_allocations = {}
        
        # Risk limits
        self.max_single_strategy = config.get('max_single_strategy', 0.7)
        self.min_active_strategies = config.get('min_active_strategies', 2)
        
        logging.basicConfig(level=logging.INFO)
        self.logger = logging.getLogger(__name__)
    
    def register_strategies(self) -> None:
        """Register all available strategies"""
        
        # Mean Reversion Strategy (existing)
        self.strategies['mean_reversion'] = {
            'strategy': StatisticalMeanReversionStrategy(self.config.get('mean_reversion', {})),
            'preferred_regimes': [MarketRegime.MEAN_REVERSION, MarketRegime.SIDEWAYS_STABLE],
            'risk_level': 'medium',
            'expected_return': 0.15,  # 15% annual
            'max_drawdown': 0.08      # 8% max drawdown
        }
        
        # Trend Following Strategy
        self.strategies['trend_following'] = {
            'strategy': TrendFollowingStrategy(self.config.get('trend_following', {})),
            'preferred_regimes': [MarketRegime.STRONG_UPTREND, MarketRegime.STRONG_DOWNTREND],
            'risk_level': 'high',
            'expected_return': 0.25,
            'max_drawdown': 0.15
        }
        
        # Momentum Strategy
        self.strategies['momentum'] = {
            'strategy': MomentumBreakoutStrategy(self.config.get('momentum', {})),
            'preferred_regimes': [MarketRegime.WEAK_UPTREND, MarketRegime.WEAK_DOWNTREND],
            'risk_level': 'medium',
            'expected_return': 0.18,
            'max_drawdown': 0.10
        }
        
        # Volatility Trading
        self.strategies['volatility'] = {
            'strategy': VolatilityTradingStrategy(self.config.get('volatility', {})),
            'preferred_regimes': [MarketRegime.HIGH_VOLATILITY],
            'risk_level': 'high',
            'expected_return': 0.20,
            'max_drawdown': 0.12
        }
        
        # Low Volatility / Defensive
        self.strategies['defensive'] = {
            'strategy': DefensiveStrategy(self.config.get('defensive', {})),
            'preferred_regimes': [MarketRegime.SIDEWAYS_CHOPPY, MarketRegime.HIGH_VOLATILITY],
            'risk_level': 'low',
            'expected_return': 0.08,
            'max_drawdown': 0.05
        }
    
    def update_allocations(self, regime_signal: RegimeSignal, 
                          portfolio_health: float) -> Dict[str, float]:
        """
        Update strategy allocations based on current regime and portfolio health
        """
        # Calculate base allocations for current regime
        base_allocations = self._calculate_regime_allocations(regime_signal)
        
        # Adjust based on strategy performance
        performance_adjustments = self._calculate_performance_adjustments()
        
        # Adjust based on portfolio health
        health_adjustments = self._calculate_health_adjustments(portfolio_health)
        
        # Combine adjustments
        new_allocations = {}
        for strategy_name in self.strategies.keys():
            base_alloc = base_allocations.get(strategy_name, 0)
            perf_adj = performance_adjustments.get(strategy_name, 1.0)
            health_adj = health_adjustments.get(strategy_name, 1.0)
            
            new_allocations[strategy_name] = base_alloc * perf_adj * health_adj
        
        # Normalize and apply constraints
        new_allocations = self._normalize_and_constrain_allocations(new_allocations)
        
        # Gradual transition to avoid sudden changes
        target_allocations = self._gradual_transition(
            self.current_allocations, 
            new_allocations,
            transition_speed=0.2  # 20% per period
        )
        
        self.target_allocations = target_allocations
        return target_allocations
    
    def _calculate_regime_allocations(self, regime_signal: RegimeSignal) -> Dict[str, float]:
        """Calculate base allocations for current regime"""
        allocations = {}
        current_regime = regime_signal.regime
        confidence = regime_signal.confidence
        
        # Get regime preferences from matrix
        if current_regime in self.regime_strategy_matrix:
            base_weights = self.regime_strategy_matrix[current_regime]
            
            # Adjust weights based on regime confidence
            # Higher confidence = more concentrated allocation
            # Lower confidence = more diversified allocation
            for strategy_name, weight in base_weights.items():
                if confidence > 0.8:
                    # High confidence - concentrate on best strategies
                    allocations[strategy_name] = weight * 1.2
                elif confidence < 0.5:
                    # Low confidence - diversify more
                    allocations[strategy_name] = weight * 0.8 + 0.1  # Add base allocation
                else:
                    allocations[strategy_name] = weight
        
        return allocations
    
    def _calculate_performance_adjustments(self) -> Dict[str, float]:
        """Adjust allocations based on recent strategy performance"""
        adjustments = {}
        
        for strategy_name in self.strategies.keys():
            recent_performance = self.performance_tracker.get_recent_performance(
                strategy_name, 
                lookback_days=30
            )
            
            if recent_performance is None:
                adjustments[strategy_name] = 1.0
                continue
            
            # Calculate performance score (0.5 to 1.5)
            sharpe_ratio = recent_performance.get('sharpe_ratio', 0)
            win_rate = recent_performance.get('win_rate', 0.5)
            drawdown = recent_performance.get('max_drawdown', 0.1)
            
            # Performance adjustment formula
            performance_score = (
                0.4 * min(max(sharpe_ratio / 2.0, 0), 1) +  # Normalize Sharpe
                0.3 * win_rate +
                0.3 * (1 - min(drawdown / 0.15, 1))  # Penalize high drawdown
            )
            
            # Convert to multiplier (0.5x to 1.5x)
            adjustments[strategy_name] = 0.5 + performance_score
        
        return adjustments
    
    def _calculate_health_adjustments(self, portfolio_health: float) -> Dict[str, float]:
        """Adjust allocations based on portfolio health"""
        adjustments = {}
        
        for strategy_name, strategy_info in self.strategies.items():
            risk_level = strategy_info['risk_level']
            
            if portfolio_health > 0.8:
                # Healthy portfolio - can take more risk
                if risk_level == 'high':
                    adjustments[strategy_name] = 1.2
                elif risk_level == 'medium':
                    adjustments[strategy_name] = 1.1
                else:
                    adjustments[strategy_name] = 0.9
            elif portfolio_health < 0.5:
                # Unhealthy portfolio - reduce risk
                if risk_level == 'high':
                    adjustments[strategy_name] = 0.5
                elif risk_level == 'medium':
                    adjustments[strategy_name] = 0.8
                else:
                    adjustments[strategy_name] = 1.3  # Favor defensive
            else:
                # Normal allocation
                adjustments[strategy_name] = 1.0
        
        return adjustments
    
    def _normalize_and_constrain_allocations(self, allocations: Dict[str, float]) -> Dict[str, float]:
        """Normalize allocations and apply constraints"""
        # Remove negative allocations
        allocations = {k: max(v, 0) for k, v in allocations.items()}
        
        # Normalize to sum to 1
        total = sum(allocations.values())
        if total > 0:
            allocations = {k: v/total for k, v in allocations.items()}
        
        # Apply maximum single strategy constraint
        for strategy_name in allocations:
            if allocations[strategy_name] > self.max_single_strategy:
                excess = allocations[strategy_name] - self.max_single_strategy
                allocations[strategy_name] = self.max_single_strategy
                
                # Redistribute excess to other strategies
                other_strategies = [s for s in allocations if s != strategy_name]
                if other_strategies:
                    redistribution = excess / len(other_strategies)
                    for other_strategy in other_strategies:
                        allocations[other_strategy] += redistribution
        
        # Ensure minimum number of active strategies
        active_strategies = [s for s, alloc in allocations.items() if alloc > 0.05]
        if len(active_strategies) < self.min_active_strategies:
            # Activate defensive strategies if not enough active
            sorted_strategies = sorted(
                self.strategies.items(),
                key=lambda x: x[1]['risk_level'] == 'low',
                reverse=True
            )
            
            for strategy_name, _ in sorted_strategies:
                if strategy_name not in active_strategies:
                    allocations[strategy_name] = max(allocations.get(strategy_name, 0), 0.1)
                    active_strategies.append(strategy_name)
                    if len(active_strategies) >= self.min_active_strategies:
                        break
            
            # Renormalize
            total = sum(allocations.values())
            if total > 0:
                allocations = {k: v/total for k, v in allocations.items()}
        
        return allocations
    
    def _gradual_transition(self, current: Dict[str, float], 
                          target: Dict[str, float], 
                          transition_speed: float) -> Dict[str, float]:
        """Gradually transition between allocations"""
        if not current:
            return target
        
        new_allocations = {}
        for strategy_name in self.strategies.keys():
            current_alloc = current.get(strategy_name, 0)
            target_alloc = target.get(strategy_name, 0)
            
            # Linear interpolation
            new_alloc = current_alloc + transition_speed * (target_alloc - current_alloc)
            new_allocations[strategy_name] = new_alloc
        
        return new_allocations
    
    def _build_regime_strategy_matrix(self) -> Dict[MarketRegime, Dict[str, float]]:
        """Build matrix of strategy allocations for each regime"""
        return {
            MarketRegime.STRONG_UPTREND: {
                'trend_following': 0.5,
                'momentum': 0.3,
                'mean_reversion': 0.1,
                'volatility': 0.05,
                'defensive': 0.05
            },
            MarketRegime.WEAK_UPTREND: {
                'momentum': 0.4,
                'mean_reversion': 0.3,
                'trend_following': 0.2,
                'defensive': 0.1,
                'volatility': 0.0
            },
            MarketRegime.SIDEWAYS_STABLE: {
                'mean_reversion': 0.6,
                'defensive': 0.2,
                'momentum': 0.15,
                'trend_following': 0.05,
                'volatility': 0.0
            },
            MarketRegime.SIDEWAYS_CHOPPY: {
                'defensive': 0.4,
                'mean_reversion': 0.3,
                'volatility': 0.2,
                'momentum': 0.1,
                'trend_following': 0.0
            },
            MarketRegime.WEAK_DOWNTREND: {
                'defensive': 0.4,
                'trend_following': 0.3,
                'mean_reversion': 0.2,
                'momentum': 0.1,
                'volatility': 0.0
            },
            MarketRegime.STRONG_DOWNTREND: {
                'trend_following': 0.5,
                'defensive': 0.3,
                'volatility': 0.1,
                'momentum': 0.1,
                'mean_reversion': 0.0
            },
            MarketRegime.HIGH_VOLATILITY: {
                'volatility': 0.4,
                'defensive': 0.3,
                'mean_reversion': 0.2,
                'trend_following': 0.1,
                'momentum': 0.0
            },
            MarketRegime.MEAN_REVERSION: {
                'mean_reversion': 0.7,
                'defensive': 0.15,
                'volatility': 0.1,
                'momentum': 0.05,
                'trend_following': 0.0
            }
        }
    
    def generate_combined_signals(self, data: pd.DataFrame, 
                                regime_signal: RegimeSignal) -> Dict[str, any]:
        """Generate signals from all active strategies and combine them"""
        individual_signals = {}
        combined_signal = {
            'action': 'HOLD',
            'strength': 0.0,
            'confidence': 0.0,
            'position_size': 0.0,
            'metadata': {
                'regime': regime_signal.regime,
                'strategy_signals': {},
                'allocation_weights': self.target_allocations
            }
        }
        
        total_weighted_strength = 0.0
        total_weighted_confidence = 0.0
        total_weight = 0.0
        
        # Generate signals from each active strategy
        for strategy_name, strategy_info in self.strategies.items():
            allocation = self.target_allocations.get(strategy_name, 0)
            
            if allocation > 0.01:  # Only process strategies with meaningful allocation
                try:
                    strategy = strategy_info['strategy']
                    
                    # Calculate indicators for this strategy
                    indicators = strategy.calculate_indicators(data)
                    
                    # Generate signals
                    signals = strategy.generate_signals(data, indicators)
                    
                    if len(signals) > 0:
                        latest_signal = signals.iloc[-1]
                        
                        signal_strength = latest_signal.get('signal', 0)
                        signal_confidence = latest_signal.get('signal_strength', 0.5)
                        
                        individual_signals[strategy_name] = {
                            'action': 'BUY' if signal_strength > 0.1 else 'SELL' if signal_strength < -0.1 else 'HOLD',
                            'strength': abs(signal_strength),
                            'confidence': signal_confidence,
                            'allocation': allocation
                        }
                        
                        # Weight by allocation
                        weighted_strength = signal_strength * allocation
                        weighted_confidence = signal_confidence * allocation
                        
                        total_weighted_strength += weighted_strength
                        total_weighted_confidence += weighted_confidence
                        total_weight += allocation
                        
                except Exception as e:
                    self.logger.error(f"Error generating signal for strategy {strategy_name}: {e}")
                    individual_signals[strategy_name] = {
                        'action': 'HOLD',
                        'strength': 0.0,
                        'confidence': 0.0,
                        'allocation': allocation,
                        'error': str(e)
                    }
        
        # Combine signals
        if total_weight > 0:
            combined_strength = total_weighted_strength / total_weight
            combined_confidence = total_weighted_confidence / total_weight
            
            # Determine action
            if combined_strength > 0.1:
                combined_signal['action'] = 'BUY'
            elif combined_strength < -0.1:
                combined_signal['action'] = 'SELL'
            else:
                combined_signal['action'] = 'HOLD'
            
            combined_signal['strength'] = abs(combined_strength)
            combined_signal['confidence'] = combined_confidence
            
            # Calculate position size based on combined signal and regime
            combined_signal['position_size'] = self._calculate_combined_position_size(
                combined_strength, combined_confidence, regime_signal
            )
        
        combined_signal['metadata']['strategy_signals'] = individual_signals
        
        return combined_signal
    
    def _calculate_combined_position_size(self, strength: float, confidence: float, 
                                        regime_signal: RegimeSignal) -> float:
        """Calculate position size for combined signal"""
        base_size = abs(strength) * confidence
        
        # Adjust based on regime confidence
        regime_adjustment = 0.5 + 0.5 * regime_signal.confidence
        
        # Adjust based on regime type (some regimes are more predictable)
        regime_risk_multiplier = {
            MarketRegime.STRONG_UPTREND: 1.2,
            MarketRegime.WEAK_UPTREND: 1.0,
            MarketRegime.SIDEWAYS_STABLE: 0.8,
            MarketRegime.SIDEWAYS_CHOPPY: 0.6,
            MarketRegime.WEAK_DOWNTREND: 0.8,
            MarketRegime.STRONG_DOWNTREND: 1.0,
            MarketRegime.HIGH_VOLATILITY: 0.5,
            MarketRegime.MEAN_REVERSION: 1.1
        }
        
        regime_mult = regime_risk_multiplier.get(regime_signal.regime, 0.8)
        
        final_size = base_size * regime_adjustment * regime_mult
        
        # Cap at reasonable limits
        return min(final_size, 0.1)  # Max 10% position
    
    def update_performance(self, strategy_name: str, trade_result: Dict) -> None:
        """Update performance tracking for a strategy"""
        self.performance_tracker.record_trade(strategy_name, trade_result)
    
    def get_allocation_summary(self) -> Dict[str, any]:
        """Get current allocation summary"""
        summary = {
            'current_allocations': self.current_allocations,
            'target_allocations': self.target_allocations,
            'active_strategies': len([s for s, a in self.target_allocations.items() if a > 0.01]),
            'allocation_changes': {}
        }
        
        # Calculate allocation changes
        for strategy_name in self.strategies.keys():
            current = self.current_allocations.get(strategy_name, 0)
            target = self.target_allocations.get(strategy_name, 0)
            summary['allocation_changes'][strategy_name] = target - current
        
        return summary


class StrategyPerformanceTracker:
    """Track performance of individual strategies"""
    
    def __init__(self):
        self.trade_history = {}  # strategy_name -> list of trades
        self.performance_cache = {}
    
    def record_trade(self, strategy_name: str, trade_result: Dict) -> None:
        """Record a trade result for a strategy"""
        if strategy_name not in self.trade_history:
            self.trade_history[strategy_name] = []
        
        trade_result['timestamp'] = pd.Timestamp.now()
        self.trade_history[strategy_name].append(trade_result)
        
        # Clear cache for this strategy
        if strategy_name in self.performance_cache:
            del self.performance_cache[strategy_name]
    
    def get_recent_performance(self, strategy_name: str, lookback_days: int = 30) -> Optional[Dict]:
        """Get recent performance metrics for a strategy"""
        if strategy_name not in self.trade_history:
            return None
        
        cache_key = f"{strategy_name}_{lookback_days}"
        if cache_key in self.performance_cache:
            return self.performance_cache[cache_key]
        
        # Filter recent trades
        cutoff_date = pd.Timestamp.now() - pd.Timedelta(days=lookback_days)
        recent_trades = [
            trade for trade in self.trade_history[strategy_name]
            if trade['timestamp'] > cutoff_date
        ]
        
        if len(recent_trades) < 5:  # Need minimum trades for meaningful stats
            return None
        
        # Calculate performance metrics
        returns = [trade.get('return', 0) for trade in recent_trades]
        
        performance = {
            'total_return': sum(returns),
            'avg_return': np.mean(returns),
            'volatility': np.std(returns) if len(returns) > 1 else 0,
            'sharpe_ratio': np.mean(returns) / (np.std(returns) + 1e-6) if len(returns) > 1 else 0,
            'win_rate': len([r for r in returns if r > 0]) / len(returns),
            'max_drawdown': self._calculate_max_drawdown(returns),
            'num_trades': len(recent_trades)
        }
        
        self.performance_cache[cache_key] = performance
        return performance
    
    def _calculate_max_drawdown(self, returns: List[float]) -> float:
        """Calculate maximum drawdown from returns"""
        if not returns:
            return 0
        
        cumulative = np.cumsum(returns)
        running_max = np.maximum.accumulate(cumulative)
        drawdown = (cumulative - running_max) / (running_max + 1e-6)
        return abs(np.min(drawdown))


class AllocationOptimizer:
    """Optimize strategy allocations using various methods"""
    
    def __init__(self):
        pass
    
    def optimize_mean_variance(self, expected_returns: Dict[str, float], 
                             covariance_matrix: np.ndarray, 
                             risk_tolerance: float) -> Dict[str, float]:
        """Mean-variance optimization for strategy allocation"""
        # This would implement Markowitz optimization
        # For now, return equal weight as placeholder
        strategies = list(expected_returns.keys())
        equal_weight = 1.0 / len(strategies)
        return {strategy: equal_weight for strategy in strategies}
    
    def optimize_risk_parity(self, volatilities: Dict[str, float]) -> Dict[str, float]:
        """Risk parity allocation"""
        # Inverse volatility weighting
        inv_vol = {k: 1.0 / (v + 1e-6) for k, v in volatilities.items()}
        total_inv_vol = sum(inv_vol.values())
        return {k: v / total_inv_vol for k, v in inv_vol.items()}


# Additional strategy implementations (placeholders)
class TrendFollowingStrategy(BaseStrategy):
    """Trend following strategy implementation"""
    
    def __init__(self, config: dict):
        super().__init__(config)
        self.ema_fast = config.get('ema_fast', 12)
        self.ema_slow = config.get('ema_slow', 26)
        self.atr_period = config.get('atr_period', 14)
    
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        close = data['close']
        high = data['high']
        low = data['low']
        
        indicators = {
            'ema_fast': close.ewm(span=self.ema_fast).mean(),
            'ema_slow': close.ewm(span=self.ema_slow).mean(),
            'atr': self._calculate_atr(high, low, close, self.atr_period)
        }
        
        indicators['trend_strength'] = (indicators['ema_fast'] - indicators['ema_slow']) / indicators['ema_slow']
        return indicators
    
    def generate_signals(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        signals = pd.DataFrame(index=data.index)
        signals['signal'] = 0.0
        signals['signal_strength'] = 0.0
        
        # Trend following logic
        trend_strength = indicators['trend_strength']
        signals['signal'] = np.where(trend_strength > 0.02, 1, 
                                   np.where(trend_strength < -0.02, -1, 0))
        signals['signal_strength'] = np.abs(trend_strength).clip(0, 1)
        
        return signals
    
    def _calculate_atr(self, high: pd.Series, low: pd.Series, close: pd.Series, period: int) -> pd.Series:
        """Calculate Average True Range"""
        tr1 = high - low
        tr2 = np.abs(high - close.shift())
        tr3 = np.abs(low - close.shift())
        tr = np.maximum(tr1, np.maximum(tr2, tr3))
        return tr.rolling(period).mean()


class MomentumBreakoutStrategy(BaseStrategy):
    """Momentum breakout strategy"""
    
    def __init__(self, config: dict):
        super().__init__(config)
        self.lookback_period = config.get('lookback_period', 20)
        self.breakout_threshold = config.get('breakout_threshold', 0.02)
    
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        close = data['close']
        high = data['high']
        low = data['low']
        
        indicators = {
            'highest_high': high.rolling(self.lookback_period).max(),
            'lowest_low': low.rolling(self.lookback_period).min(),
            'rsi': self._calculate_rsi(close, 14)
        }
        
        return indicators
    
    def generate_signals(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        signals = pd.DataFrame(index=data.index)
        close = data['close']
        
        # Breakout signals
        upper_breakout = close > indicators['highest_high'].shift() * (1 + self.breakout_threshold)
        lower_breakout = close < indicators['lowest_low'].shift() * (1 - self.breakout_threshold)
        
        signals['signal'] = np.where(upper_breakout, 1, np.where(lower_breakout, -1, 0))
        signals['signal_strength'] = np.where(
            signals['signal'] != 0,
            np.abs(indicators['rsi'] - 50) / 50,
            0
        )
        
        return signals
    
    def _calculate_rsi(self, close: pd.Series, period: int) -> pd.Series:
        delta = close.diff()
        gain = (delta.where(delta > 0, 0)).rolling(period).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(period).mean()
        rs = gain / loss
        return 100 - (100 / (1 + rs))


class VolatilityTradingStrategy(BaseStrategy):
    """Strategy for high volatility periods"""
    
    def __init__(self, config: dict):
        super().__init__(config)
        self.vol_lookback = config.get('vol_lookback', 20)
        self.vol_threshold = config.get('vol_threshold', 2.0)
    
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        close = data['close']
        returns = close.pct_change()
        
        indicators = {
            'volatility': returns.rolling(self.vol_lookback).std() * np.sqrt(252),
            'vol_ratio': returns.rolling(5).std() / returns.rolling(20).std(),
            'bollinger_upper': close.rolling(20).mean() + 2 * close.rolling(20).std(),
            'bollinger_lower': close.rolling(20).mean() - 2 * close.rolling(20).std()
        }
        
        return indicators
    
    def generate_signals(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        signals = pd.DataFrame(index=data.index)
        close = data['close']
        
        # Volatility mean reversion
        signals['signal'] = np.where(
            close < indicators['bollinger_lower'], 1,
            np.where(close > indicators['bollinger_upper'], -1, 0)
        )
        
        # Only trade when volatility is elevated
        vol_filter = indicators['vol_ratio'] > self.vol_threshold
        signals['signal'] = signals['signal'] * vol_filter
        signals['signal_strength'] = indicators['vol_ratio'] / 4.0  # Normalize
        
        return signals


class DefensiveStrategy(BaseStrategy):
    """Conservative strategy for uncertain periods"""
    
    def __init__(self, config: dict):
        super().__init__(config)
        self.safety_threshold = config.get('safety_threshold', 0.7)
    
    def calculate_indicators(self, data: pd.DataFrame) -> Dict[str, pd.Series]:
        close = data['close']
        
        indicators = {
            'sma_200': close.rolling(200).mean(),
            'volatility': close.pct_change().rolling(30).std() * np.sqrt(252),
            'drawdown': self._calculate_drawdown(close)
        }
        
        return indicators
    
    def generate_signals(self, data: pd.DataFrame, indicators: Dict[str, pd.Series]) -> pd.DataFrame:
        signals = pd.DataFrame(index=data.index)
        close = data['close']
        
        # Very conservative signals - only when very safe
        safety_score = (
            (close > indicators['sma_200']).astype(int) * 0.4 +
            (indicators['volatility'] < 0.3).astype(int) * 0.3 +
            (indicators['drawdown'] > -0.05).astype(int) * 0.3
        )
        
        signals['signal'] = np.where(safety_score > self.safety_threshold, 0.5, 0)
        signals['signal_strength'] = safety_score
        
        return signals
    
    def _calculate_drawdown(self, close: pd.Series) -> pd.Series:
        rolling_max = close.expanding().max()
        return (close - rolling_max) / rolling_max