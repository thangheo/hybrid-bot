# ================================================================
# Enhanced Strategy Manager with ETF Integration
# File: src/strategy/enhanced_manager.py
# ================================================================

import pandas as pd
import numpy as np
from typing import Dict, List, Optional, Tuple, Any
from datetime import datetime, timedelta
import logging
import json
import asyncio

from .base_strategy import BaseStrategy
from .strategies.statistical_mean_reversion import StatisticalMeanReversionStrategy
from .strategies.etf_enhanced_e8_strategy import ETFEnhancedE8Strategy, E8RegimeInfo, MarketRegime
from ..data.etf_flow_manager import ETFFlowManager, CompositeETFFlow

logger = logging.getLogger(__name__)
# Import signal decay if available
try:
    from .exit.signal_decay_manager import (
        SignalDecayExitManager,
        SignalDecayConfig,
        CONSERVATIVE_EXIT_CONFIG,
        AGGRESSIVE_EXIT_CONFIG,
        ADAPTIVE_EXIT_CONFIG
    )
    SIGNAL_DECAY_AVAILABLE = True
except ImportError:
    SIGNAL_DECAY_AVAILABLE = False
    logger.warning("⚠️ Signal decay exit manager not available")

logger = logging.getLogger(__name__)

class ETFEnhancedStrategyManager:
    """Enhanced Strategy Manager with ETF Flow Integration and 9-Regime Support"""
    
    def __init__(self, config: dict):
        self.config = config
        self.strategies = {}
        self.data_manager = None
        
        # ETF Integration
        self.etf_enabled = config.get('etf_enhancement', {}).get('enabled', True)
        self.etf_manager = None
        if self.etf_enabled:
            self.etf_manager = ETFFlowManager(config.get('etf_enhancement', {}))
        
        # Primary strategy selection
        primary_strategy = config.get('strategy', {}).get('primary_strategy', 'etf_enhanced_e8')
        
        # Strategy registry with ETF support
        self.strategy_registry = {
            'etf_enhanced_e8': ETFEnhancedE8Strategy,
            'statistical_mean_reversion': StatisticalMeanReversionStrategy,
            # Add more strategies here
        }
        
        # Initialize primary strategy
        self._initialize_primary_strategy(primary_strategy)
        
        # Load additional strategies from config
        self._load_additional_strategies()
        
        # Signal decay initialization
        self.decay_managers: Dict[str, Any] = {}
        self.exit_config = None
        if SIGNAL_DECAY_AVAILABLE:
            self._initialize_signal_decay()
        
        # Performance tracking
        self.performance_tracker = ETFPerformanceTracker(config)
        
        # Regime tracking
        self.regime_history = []
        self.current_regime_info = None
        
        logger.info(f"🚀 ETF-Enhanced Strategy Manager initialized")
        logger.info(f"   Primary Strategy: {primary_strategy}")
        logger.info(f"   ETF Integration: {'Enabled' if self.etf_enabled else 'Disabled'}")
        logger.info(f"   Total Strategies: {len(self.strategies)}")
    
    def _initialize_primary_strategy(self, strategy_name: str):
        """Initialize the primary strategy"""
        
        if strategy_name not in self.strategy_registry:
            logger.error(f"❌ Unknown primary strategy: {strategy_name}")
            strategy_name = 'etf_enhanced_e8'  # Fallback to ETF-enhanced E8
        
        strategy_class = self.strategy_registry[strategy_name]
        
        # Pass full config to strategy
        strategy_config = self.config.copy()
        strategy = strategy_class(strategy_config)
        
        self.strategies[strategy_name] = {
            'strategy': strategy,
            'weight': 1.0,
            'enabled': True,
            'is_primary': True,
            'etf_enhanced': strategy_name == 'etf_enhanced_e8'
        }
        
        logger.info(f"✅ Primary strategy initialized: {strategy_name}")
    
    def _load_additional_strategies(self):
        """Load additional strategies from configuration"""
        available_strategies = self.config.get('strategy', {}).get('available_strategies', [])
        
        for strategy_config in available_strategies:
            name = strategy_config['name']
            
            # Skip if already loaded as primary
            if name in self.strategies:
                continue
            
            if strategy_config.get('enabled', False):
                self._add_strategy_from_config(strategy_config)
    
    def _add_strategy_from_config(self, strategy_config: dict):
        """Add strategy from configuration"""
        name = strategy_config['name']
        class_name = strategy_config.get('class', name)
        weight = strategy_config.get('weight', 1.0)
        
        if class_name not in self.strategy_registry:
            logger.warning(f"⚠️ Unknown strategy class: {class_name}")
            return
        
        try:
            strategy_class = self.strategy_registry[class_name]
            strategy = strategy_class(self.config)
            
            self.strategies[name] = {
                'strategy': strategy,
                'weight': weight,
                'enabled': True,
                'is_primary': False,
                'etf_enhanced': 'etf' in name.lower() or 'e8' in name.lower()
            }
            
            logger.info(f"✅ Additional strategy loaded: {name} (weight: {weight})")
            
        except Exception as e:
            logger.error(f"❌ Failed to load strategy {name}: {e}")
    
    def _initialize_signal_decay(self):
        """Initialize signal decay exit system"""
        if not SIGNAL_DECAY_AVAILABLE:
            return
        
        exit_settings = self.config.get('exit_strategy', {})
        
        if not exit_settings.get('enabled', False):
            logger.info("📄 Signal decay exit disabled")
            return
        
        # Create exit configuration
        self.exit_config = self._create_exit_config(exit_settings)
        
        # Initialize decay managers for symbols
        symbols = exit_settings.get('symbols', ['BTCUSDT', 'ETHUSDT', 'SOLUSDT', 'ARBUSDT'])
        
        for symbol in symbols:
            self.decay_managers[symbol] = SignalDecayExitManager(self.exit_config)
        
        logger.info(f"📄 Signal decay exit enabled for {len(symbols)} symbols")
    
    def _create_exit_config(self, exit_settings: dict):
        """Create signal decay configuration"""
        if not SIGNAL_DECAY_AVAILABLE:
            return None
        
        exit_type = exit_settings.get('type', 'adaptive')
        
        # Get base config
        if exit_type == 'conservative':
            base_config = CONSERVATIVE_EXIT_CONFIG
        elif exit_type == 'aggressive':
            base_config = AGGRESSIVE_EXIT_CONFIG
        else:
            base_config = ADAPTIVE_EXIT_CONFIG
        
        # Override with custom settings
        return SignalDecayConfig(
            decay_method=exit_settings.get('decay_method', base_config.decay_method),
            half_life_hours=exit_settings.get('half_life_hours', base_config.half_life_hours),
            min_signal_threshold=exit_settings.get('min_signal_threshold', base_config.min_signal_threshold),
            exit_speed_factor=exit_settings.get('exit_speed_factor', base_config.exit_speed_factor),
            momentum_protection=exit_settings.get('momentum_protection', base_config.momentum_protection),
            confidence_weighting=exit_settings.get('confidence_weighting', base_config.confidence_weighting)
        )
    
    async def run_strategies(self, symbol: str, data: pd.DataFrame, 
                           end_date: Optional[datetime] = None) -> Dict[str, Any]:
        """Run all strategies with ETF enhancement"""
        
        internal_symbol = symbol.replace('/', '')
        results = {}
        
        # Get ETF flow data once for all strategies
        etf_flow_data = None
        if self.etf_enabled and self.etf_manager:
            try:
                etf_flow_data = await self.etf_manager.get_composite_etf_flow()
                logger.debug(f"📊 ETF Flow: {etf_flow_data.flow_strength:.3f} "
                           f"({etf_flow_data.institutional_sentiment})")
            except Exception as e:
                logger.warning(f"⚠️ Failed to get ETF flow data: {e}")
        
        # Run each strategy
        for strategy_name, strategy_info in self.strategies.items():
            if not strategy_info['enabled']:
                continue
            
            strategy = strategy_info['strategy']
            
            try:
                # Set symbol if strategy supports it
                if hasattr(strategy, 'set_current_symbol'):
                    strategy.set_current_symbol(internal_symbol)
                
                # Validate data
                if not strategy.validate_data(data):
                    logger.warning(f"⚠️ Data validation failed for {strategy_name} on {symbol}")
                    continue
                
                # Calculate indicators
                indicators = strategy.calculate_indicators(data)
                
                # Generate signals (async for ETF-enhanced strategies)
                if strategy_info.get('etf_enhanced', False):
                    signals = await strategy.generate_signals(data, indicators)
                else:
                    signals = strategy.generate_signals(data, indicators)
                
                # Enhance with ETF flow if not already enhanced
                if not strategy_info.get('etf_enhanced', False) and etf_flow_data:
                    signals = self._enhance_signals_with_etf(signals, etf_flow_data)
                
                # Store results
                results[strategy_name] = {
                    'signals': signals,
                    'indicators': indicators,
                    'data': data,
                    'weight': strategy_info['weight'],
                    'etf_enhanced': strategy_info.get('etf_enhanced', False),
                    'strategy_info': self._get_strategy_info(strategy),
                    'etf_flow_data': etf_flow_data
                }
                
                # Update regime tracking for ETF-enhanced strategies
                if strategy_info.get('etf_enhanced', False) and hasattr(strategy, 'current_regime_info'):
                    self.current_regime_info = strategy.current_regime_info
                    if self.current_regime_info:
                        self.regime_history.append(self.current_regime_info)
                        if len(self.regime_history) > 100:  # Keep last 100 regime changes
                            self.regime_history.pop(0)
                
                logger.debug(f"✅ Strategy {strategy_name} completed for {symbol}")
                
            except Exception as e:
                logger.error(f"❌ Strategy {strategy_name} failed for {symbol}: {e}")
                continue
        
        # Track performance
        if results:
            await self.performance_tracker.track_strategy_run(symbol, results, etf_flow_data)
        
        return results
    
    def _enhance_signals_with_etf(self, signals: pd.DataFrame, 
                                 etf_flow: CompositeETFFlow) -> pd.DataFrame:
        """Enhance traditional strategy signals with ETF flow data"""
        
        if etf_flow is None:
            return signals
        
        # Calculate ETF enhancement factor
        flow_strength = etf_flow.flow_strength
        confidence = etf_flow.confidence
        
        # Apply ETF flow enhancement
        etf_enhancement = 1.0 + (flow_strength * confidence * 0.3)
        etf_enhancement = np.clip(etf_enhancement, 0.7, 1.5)
        
        # Enhance final signal
        if 'final_signal' in signals.columns:
            signals['etf_enhanced_signal'] = signals['final_signal'] * etf_enhancement
        elif 'signal' in signals.columns:
            signals['etf_enhanced_signal'] = signals['signal'] * etf_enhancement
        
        # Add ETF metadata
        signals['etf_flow_strength'] = flow_strength
        signals['etf_sentiment'] = etf_flow.institutional_sentiment
        signals['etf_confidence'] = confidence
        signals['etf_enhancement_factor'] = etf_enhancement
        
        return signals
    
    def _get_strategy_info(self, strategy) -> Dict[str, Any]:
        """Get strategy information"""
        if hasattr(strategy, 'get_strategy_info'):
            return strategy.get_strategy_info()
        else:
            return {
                'strategy_name': strategy.__class__.__name__,
                'strategy_type': 'traditional'
            }
    
    def aggregate_signals(self, strategy_results: Dict) -> pd.DataFrame:
        """Enhanced signal aggregation with ETF flow consideration"""
        
        if not strategy_results:
            return pd.DataFrame()
        
        # Get base aggregated signals
        aggregated = self._basic_signal_aggregation(strategy_results)
        
        # Add ETF flow information to aggregated signals
        etf_flow_data = None
        for result in strategy_results.values():
            if result.get('etf_flow_data'):
                etf_flow_data = result['etf_flow_data']
                break
        
        if etf_flow_data:
            aggregated['etf_flow_strength'] = etf_flow_data.flow_strength
            aggregated['etf_sentiment'] = etf_flow_data.institutional_sentiment
            aggregated['etf_confidence'] = etf_flow_data.confidence
            aggregated['etf_dominant_etf'] = etf_flow_data.dominant_etf
        
        # Add regime information if available
        if self.current_regime_info:
            aggregated['current_regime'] = self.current_regime_info.regime.value
            aggregated['volatility_regime'] = self.current_regime_info.volatility_regime.value
            aggregated['etf_flow_regime'] = self.current_regime_info.etf_flow_regime.value
            aggregated['regime_confidence'] = self.current_regime_info.confidence
            aggregated['regime_stability'] = self.current_regime_info.stability_score
        
        # Enhanced final signal with regime and ETF considerations
        if etf_flow_data and self.current_regime_info:
            regime_adjustment = self._get_regime_signal_adjustment(self.current_regime_info)
            etf_adjustment = self._get_etf_signal_adjustment(etf_flow_data)
            
            total_adjustment = (regime_adjustment + etf_adjustment) / 2
            aggregated['regime_etf_adjusted_signal'] = aggregated['weighted_signal'] * total_adjustment
            
            # Update final signal with adjustments
            signal_threshold = self.config.get('signal_threshold', 0.3)
            aggregated['final_signal'] = 0
            
            adjusted_signal = aggregated['regime_etf_adjusted_signal']
            aggregated.loc[adjusted_signal > signal_threshold, 'final_signal'] = 1
            aggregated.loc[adjusted_signal < -signal_threshold, 'final_signal'] = -1
            
            aggregated['final_strength'] = abs(adjusted_signal)
        
        return aggregated
    
    def _basic_signal_aggregation(self, strategy_results: Dict) -> pd.DataFrame:
        """Basic signal aggregation logic"""
        
        first_strategy = list(strategy_results.values())[0]
        combined_index = first_strategy['signals'].index
        aggregated = pd.DataFrame(index=combined_index)
        
        strategy_signals = []
        weights = []
        
        for strategy_name, result in strategy_results.items():
            signals_col = 'final_signal' if 'final_signal' in result['signals'].columns else 'signal'
            signals = result['signals'][signals_col].fillna(0)
            weight = result['weight']
            signals = signals.reindex(combined_index, fill_value=0)
            
            strategy_signals.append(signals)
            weights.append(weight)
            
            aggregated[f'{strategy_name}_signal'] = signals
            if 'signal_strength' in result['signals'].columns:
                aggregated[f'{strategy_name}_strength'] = result['signals']['signal_strength']
        
        if not strategy_signals:
            return aggregated
        
        # Calculate weighted signals
        signals_array = np.array(strategy_signals)
        weights_array = np.array(weights)
        weights_normalized = weights_array / weights_array.sum()
        
        aggregated['weighted_signal'] = np.average(signals_array, axis=0, weights=weights_normalized)
        aggregated['signal_consensus'] = np.sum(signals_array != 0, axis=0)
        aggregated['bullish_consensus'] = np.sum(signals_array > 0, axis=0)
        aggregated['bearish_consensus'] = np.sum(signals_array < 0, axis=0)
        
        # Basic final signal
        signal_threshold = self.config.get('signal_threshold', 0.3)
        aggregated['final_signal'] = 0
        
        aggregated.loc[aggregated['weighted_signal'] > signal_threshold, 'final_signal'] = 1
        aggregated.loc[aggregated['weighted_signal'] < -signal_threshold, 'final_signal'] = -1
        
        aggregated['final_strength'] = abs(aggregated['weighted_signal'])
        
        total_strategies = len(strategy_signals)
        aggregated['confidence'] = aggregated['signal_consensus'] / total_strategies
        
        return aggregated
    
    def _get_regime_signal_adjustment(self, regime_info: E8RegimeInfo) -> float:
        """Get signal adjustment based on current regime"""
        
        regime_adjustments = {
            MarketRegime.ACCUMULATION: 1.2,      # More aggressive in accumulation
            MarketRegime.INSTITUTIONAL_FOMO: 1.1,
            MarketRegime.EUPHORIA: 0.7,          # More conservative in euphoria
            MarketRegime.SIDEWAYS: 1.0,
            MarketRegime.BALANCED: 1.0,
            MarketRegime.UNCERTAIN: 0.8,
            MarketRegime.DISTRIBUTION: 0.9,
            MarketRegime.PROFIT_TAKING: 0.8,
            MarketRegime.PANIC: 0.5              # Very conservative in panic
        }
        
        base_adjustment = regime_adjustments.get(regime_info.regime, 1.0)
        confidence_factor = regime_info.confidence
        
        return base_adjustment * confidence_factor + (1.0 - confidence_factor)
    
    def _get_etf_signal_adjustment(self, etf_flow: CompositeETFFlow) -> float:
        """Get signal adjustment based on ETF flow"""
        
        flow_strength = abs(etf_flow.flow_strength)
        confidence = etf_flow.confidence
        
        # Strong institutional flow = more confidence in signals
        if etf_flow.institutional_sentiment == 'bullish':
            adjustment = 1.0 + (flow_strength * confidence * 0.3)
        elif etf_flow.institutional_sentiment == 'bearish':
            adjustment = 1.0 + (flow_strength * confidence * 0.2)  # Less enhancement for bearish
        else:
            adjustment = 1.0
        
        return np.clip(adjustment, 0.7, 1.4)
    
    # ================================================================
    # ETF-Specific Methods
    # ================================================================
    
    async def get_current_etf_analysis(self) -> Optional[Dict]:
        """Get current ETF flow analysis"""
        if not self.etf_enabled or not self.etf_manager:
            return None
        
        try:
            etf_flow = await self.etf_manager.get_composite_etf_flow()
            return {
                'total_flow': etf_flow.total_flow,
                'flow_strength': etf_flow.flow_strength,
                'sentiment': etf_flow.institutional_sentiment,
                'confidence': etf_flow.confidence,
                'dominant_etf': etf_flow.dominant_etf,
                'timestamp': etf_flow.timestamp.isoformat()
            }
        except Exception as e:
            logger.error(f"❌ Failed to get ETF analysis: {e}")
            return None
    
    def get_regime_analysis(self) -> Optional[Dict]:
        """Get current regime analysis"""
        if not self.current_regime_info:
            return None
        
        return {
            'regime': self.current_regime_info.regime.value,
            'volatility_regime': self.current_regime_info.volatility_regime.value,
            'etf_flow_regime': self.current_regime_info.etf_flow_regime.value,
            'confidence': self.current_regime_info.confidence,
            'stability_score': self.current_regime_info.stability_score,
            'e8_state': self.current_regime_info.e8_state,
            'etf_flow_strength': self.current_regime_info.etf_flow_strength
        }
    
    def get_etf_diagnostic_info(self) -> Dict:
        """Get ETF system diagnostic information"""
        info = {
            'etf_enabled': self.etf_enabled,
            'etf_manager_available': self.etf_manager is not None,
            'strategies_with_etf': sum(1 for s in self.strategies.values() if s.get('etf_enhanced', False)),
            'total_strategies': len(self.strategies),
            'current_regime': None,
            'etf_manager_diagnostics': None
        }
        
        if self.current_regime_info:
            info['current_regime'] = self.current_regime_info.regime.value
        
        if self.etf_manager:
            info['etf_manager_diagnostics'] = self.etf_manager.get_diagnostic_info()
        
        return info
    
    # ================================================================
    # Performance and Monitoring
    # ================================================================
    
    async def get_performance_summary(self, days: int = 30) -> Dict:
        """Get performance summary including ETF metrics"""
        return await self.performance_tracker.get_performance_summary(days)
    
    def get_regime_transition_history(self, limit: int = 20) -> List[Dict]:
        """Get recent regime transition history"""
        recent_regimes = self.regime_history[-limit:] if self.regime_history else []
        
        return [
            {
                'regime': r.regime.value,
                'volatility_regime': r.volatility_regime.value,
                'etf_flow_regime': r.etf_flow_regime.value,
                'confidence': r.confidence,
                'stability_score': r.stability_score,
                'etf_flow_strength': r.etf_flow_strength
            }
            for r in recent_regimes
        ]


class ETFPerformanceTracker:
    """Track performance of ETF-enhanced strategies"""
    
    def __init__(self, config: dict):
        self.config = config
        self.performance_history = []
        self.etf_signal_accuracy = []
        self.regime_classification_accuracy = []
        
    async def track_strategy_run(self, symbol: str, results: Dict, etf_flow: Optional[CompositeETFFlow]):
        """Track performance of strategy run"""
        
        performance_data = {
            'timestamp': datetime.now(),
            'symbol': symbol,
            'strategies_run': len(results),
            'etf_enhanced_strategies': sum(1 for r in results.values() if r.get('etf_enhanced', False)),
            'etf_flow_available': etf_flow is not None,
            'etf_sentiment': etf_flow.institutional_sentiment if etf_flow else None,
            'etf_flow_strength': etf_flow.flow_strength if etf_flow else None,
            'etf_confidence': etf_flow.confidence if etf_flow else None
        }
        
        self.performance_history.append(performance_data)
        
        # Keep only recent history
        if len(self.performance_history) > 1000:
            self.performance_history = self.performance_history[-500:]
    
    async def get_performance_summary(self, days: int = 30) -> Dict:
        """Get performance summary for specified period"""
        
        cutoff_date = datetime.now() - timedelta(days=days)
        recent_data = [
            d for d in self.performance_history 
            