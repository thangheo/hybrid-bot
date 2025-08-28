# ================================================================
# Simplified Unified Strategy Manager - Clean Implementation
# File: src/strategy/unified_manager.py
# ================================================================

import pandas as pd
import numpy as np
from typing import Dict, List, Optional, Any
from datetime import datetime, timedelta
import logging
import asyncio

from .strategies.markov_etf_unified import MarkovETFUnifiedStrategy, SimplifiedUnifiedManager
from .strategies.statistical_mean_reversion import StatisticalMeanReversionStrategy
from .strategies.advanced_statistical_strategy import AdvancedStatisticalStrategy
logger = logging.getLogger(__name__)

# ================================================================
# UNIFIED STRATEGY MANAGER (SIMPLIFIED FOR MAIN.PY COMPATIBILITY)
# ================================================================

class UnifiedStrategyManager:
    """Simplified Unified Strategy Manager for main.py compatibility"""
    
    def __init__(self, config: dict):
        self.config = config
        
        # Initialize primary strategy
        self.primary_strategy_name = 'markov_etf_unified'
        
        # Try to initialize Markov strategy, fallback to statistical
        try:
            self.primary_strategy = MarkovETFUnifiedStrategy(config)
            logger.info("Primary strategy: Markov ETF Unified")
        except Exception as e:
            logger.warning(f"Failed to initialize Markov strategy: {e}")
            self.primary_strategy = AdvancedStatisticalStrategy(config)
            self.primary_strategy_name = 'statistical_fallback'
            logger.info("Primary strategy: Advanced Statistical Strategy (fallback)")
        
        # Performance tracking
        self.performance_history = []
        
        # Current regime info (for compatibility)
        self.current_regime_info = None
        
        # ETF integration status
        self.etf_enabled = getattr(self.primary_strategy, 'etf_enabled', False)
        # self.etf_enabled = hasattr(self.primary_strategy, 'etf_enabled') and self.primary_strategy.etf_enabled
        
        logger.info(f"Unified Strategy Manager initialized")
        logger.info(f"ETF Integration: {'Enabled' if self.etf_enabled else 'Disabled'}")
    
    # ================================================================
    # MAIN EXECUTION METHODS (REQUIRED BY MAIN.PY)
    # ================================================================
    
    async def run_strategies(self, symbol: str, data: pd.DataFrame, 
                           end_date: Optional[datetime] = None) -> Dict[str, Any]:
        """Run strategies - main.py compatibility method"""
        
        try:
            # Validate data
            if not self._validate_data(data):
                return {'error': 'Data validation failed'}
            
            # Calculate indicators
            indicators = self.primary_strategy.calculate_indicators(data)
            
            # Generate signals
            if hasattr(self.primary_strategy, 'generate_signals'):
                if asyncio.iscoroutinefunction(self.primary_strategy.generate_signals):
                    signals = await self.primary_strategy.generate_signals(data, indicators)
                else:
                    signals = self.primary_strategy.generate_signals(data, indicators)
            else:
                logger.error("Strategy missing generate_signals method")
                return {'error': 'Strategy method missing'}
            
            # Update regime info if available
            if hasattr(self.primary_strategy, 'current_regime_info'):
                self.current_regime_info = self.primary_strategy.current_regime_info
            
            # Package results
            results = {
                self.primary_strategy_name: {
                    'signals': signals,
                    'indicators': indicators,
                    'data': data,
                    'weight': 1.0,
                    'enabled': True,
                    'is_primary': True,
                    'type': 'markov_etf' if 'markov' in self.primary_strategy_name else 'traditional',
                    'strategy_info': self._get_strategy_info(),
                    'etf_flow_data': None,  # Could be populated if ETF data available
                    'enhanced_with_etf': self.etf_enabled
                }
            }
            
            # Track performance
            self._track_performance(symbol, results)
            
            logger.debug(f"Strategy execution completed for {symbol}")
            return results
            
        except Exception as e:
            logger.error(f"Strategy execution failed for {symbol}: {e}")
            import traceback
            logger.debug(traceback.format_exc())
            return {'error': str(e)}
    
    def aggregate_signals(self, strategy_results: Dict) -> pd.DataFrame:
        """Aggregate signals from strategies - main.py compatibility"""
        
        if not strategy_results or 'error' in strategy_results:
            return pd.DataFrame()
        
        # Get the primary strategy result
        primary_result = None
        for name, result in strategy_results.items():
            if isinstance(result, dict) and 'signals' in result:
                primary_result = result
                break
        
        if not primary_result or primary_result['signals'].empty:
            return pd.DataFrame()
        
        # Create aggregated signals from primary strategy
        signals = primary_result['signals'].copy()
        
        # Add aggregation metadata for main.py compatibility
        if 'signal' in signals.columns:
            signals['weighted_signal'] = signals['signal']
            signals['final_signal'] = signals['signal']
        
        if 'signal_strength' in signals.columns:
            signals['final_strength'] = signals['signal_strength']
        elif 'confidence' in signals.columns:
            signals['final_strength'] = signals['confidence']
        else:
            signals['final_strength'] = 0.5
        
        # Add consensus metrics
        signals['signal_consensus'] = 1  # Only one strategy
        signals['confidence'] = signals.get('confidence', 0.5)
        
        regime_info = getattr(self, 'current_regime_info', None)
        if regime_info and hasattr(regime_info, 'current_regime'):
            signals['current_regime'] = str(getattr(regime_info, 'current_regime', 'none'))
            signals['regime_probability'] = getattr(regime_info, 'regime_probability', 0.5)

        # Add ETF information if available
        if self.etf_enabled and hasattr(self.primary_strategy, 'etf_flow_cache'):
            etf_cache = getattr(self.primary_strategy, 'etf_flow_cache', None)
            if etf_cache:
                signals['etf_flow_strength'] = getattr(etf_cache, 'flow_strength', None)
                signals['etf_sentiment'] = getattr(etf_cache, 'institutional_sentiment', None)
                signals['etf_confidence'] = getattr(etf_cache, 'confidence', None)
        return signals
    
    # ================================================================
    # SYSTEM CAPABILITIES (REQUIRED BY MAIN.PY)
    # ================================================================
    
    def get_system_capabilities(self) -> Dict:
        """Get system capabilities - main.py compatibility"""
        
        capabilities = {
            'strategies': {
                'total': 1,
                'primary': self.primary_strategy_name,
                'enabled': [self.primary_strategy_name],
                'types': {
                    'markov_etf': 1 if 'markov' in self.primary_strategy_name else 0,
                    'traditional': 0 if 'markov' in self.primary_strategy_name else 1,
                    'fallback': 1 if 'fallback' in self.primary_strategy_name else 0
                }
            },
            'etf_integration': {
                'available': True,
                'enabled': self.etf_enabled,
                'manager_ready': self.etf_enabled,
                'strategy_type': 'markov_etf' if 'markov' in self.primary_strategy_name else 'traditional'
            },
            'regime_tracking': {
                'current_regime': str(getattr(self.current_regime_info, 'current_regime', 'none')) if self.current_regime_info else None,
                'regime_history_length': len(getattr(self.primary_strategy, 'regime_history', [])),
                'markov_enabled': 'markov' in self.primary_strategy_name
            },
            'features': self._get_strategy_features()
        }
        
        return capabilities
    
    def _get_strategy_features(self) -> List[str]:
        """Get list of strategy features"""
        features = ['signal_generation', 'indicator_calculation']
        
        if 'markov' in self.primary_strategy_name:
            features.extend([
                'regime_detection',
                'markov_forecasting', 
                'transition_awareness',
                'dynamic_weighting'
            ])
        
        if self.etf_enabled:
            features.extend([
                'etf_integration',
                'institutional_sentiment'
            ])
        
        return features
    
    # ================================================================
    # PERFORMANCE AND MONITORING (REQUIRED BY MAIN.PY)
    # ================================================================
    
    async def get_performance_summary(self, days: int = 30) -> Dict:
        """Get performance summary - main.py compatibility"""
        
        cutoff_date = datetime.now() - timedelta(days=days)
        recent_data = [
            d for d in self.performance_history 
            if d['timestamp'] >= cutoff_date
        ]
        
        if not recent_data:
            return {
                'error': 'No performance data available',
                'days_requested': days,
                'total_runs': 0
            }
        
        total_runs = len(recent_data)
        successful_runs = sum(1 for d in recent_data if not d.get('error'))
        
        return {
            'period_days': days,
            'total_strategy_runs': total_runs,
            'successful_runs': successful_runs,
            'success_rate': successful_runs / max(total_runs, 1),
            'strategy_type': self.primary_strategy_name,
            'etf_enabled': self.etf_enabled,
            'regime_switches': self._count_regime_switches(recent_data),
            'average_signal_strength': self._calculate_avg_signal_strength(recent_data)
        }
    
    def get_current_etf_analysis(self) -> Optional[Dict]:
        """Get current ETF analysis - main.py compatibility"""
        if not self.etf_enabled or not hasattr(self.primary_strategy, 'etf_flow_cache'):
            return None
        
        etf_cache = self.primary_strategy.etf_flow_cache
        if not etf_cache:
            return None
        
        return {
            'flow_strength': etf_cache.flow_strength,
            'sentiment': etf_cache.institutional_sentiment,
            'confidence': etf_cache.confidence,
            'total_flow': etf_cache.total_flow,
            'dominant_etf': etf_cache.dominant_etf,
            'timestamp': etf_cache.timestamp.isoformat()
        }
    
    def get_regime_analysis(self) -> Optional[Dict]:
        """Get current regime analysis - main.py compatibility"""
        if not self.current_regime_info:
            return None
        
        try:
            regime_dict = {
                'regime': str(getattr(self.current_regime_info, 'current_regime', 'unknown')),
                'regime_probability': getattr(self.current_regime_info, 'regime_probability', 0.5)
            }
            
            # Add Markov-specific fields if available
            if hasattr(self.current_regime_info, 'next_most_likely'):
                regime_dict['next_most_likely'] = str(self.current_regime_info.next_most_likely)
            if hasattr(self.current_regime_info, 'transition_confidence'):
                regime_dict['transition_confidence'] = self.current_regime_info.transition_confidence
            if hasattr(self.current_regime_info, 'regime_duration'):
                regime_dict['regime_duration'] = self.current_regime_info.regime_duration
            if hasattr(self.current_regime_info, 'expected_duration'):
                regime_dict['expected_duration'] = self.current_regime_info.expected_duration
            if hasattr(self.current_regime_info, 'etf_flow_strength'):
                regime_dict['etf_flow_strength'] = self.current_regime_info.etf_flow_strength
            
            return regime_dict
            
        except Exception as e:
            logger.error(f"Failed to get regime analysis: {e}")
            return None
    
    # ================================================================
    # SIGNAL DECAY COMPATIBILITY (STUBS FOR MAIN.PY)
    # ================================================================
    
    def is_decay_exit_enabled(self) -> bool:
        """Signal decay compatibility - always return False for simplicity"""
        return False
    
    def add_position_to_decay_tracking(self, symbol: str, entry_time: datetime,
                                     entry_price: float, position_size: float,
                                     signal_data: pd.Series) -> bool:
        """Signal decay compatibility - stub"""
        return False
    
    def get_decay_exit_recommendations(self, symbol: str, current_signal_data: pd.Series) -> Dict:
        """Signal decay compatibility - stub"""
        return {'exit_amount': 0.0, 'urgency_level': 0.0, 'recommendations': []}
    
    def execute_decay_exit(self, symbol: str, exit_amount: float,
                          exit_price: float, current_time: datetime = None) -> bool:
        """Signal decay compatibility - stub"""
        return False
    
    def get_decay_performance_summary(self) -> Dict:
        """Signal decay compatibility - stub"""
        return {'error': 'Signal decay not enabled in simplified manager'}
    
    # ================================================================
    # PERSISTENCE COMPATIBILITY (STUBS FOR MAIN.PY)
    # ================================================================
    
    def force_save_strategy_data(self, symbol: str = None) -> bool:
        """Persistence compatibility - stub"""
        return False
    
    def get_persistence_diagnostic(self) -> Dict[str, Any]:
        """Persistence compatibility - stub"""
        return {'enabled': False, 'message': 'Not available in simplified manager'}
    
    # ================================================================
    # HELPER METHODS
    # ================================================================
    
    def _validate_data(self, data: pd.DataFrame) -> bool:
        """Validate input data"""
        required_columns = ['open', 'high', 'low', 'close', 'volume']
        
        if not all(col in data.columns for col in required_columns):
            logger.error(f"Missing required columns: {required_columns}")
            return False
        
        if len(data) < 30:
            logger.error(f"Insufficient data: {len(data)} rows")
            return False
        
        return True
    
    def _get_strategy_info(self) -> Dict[str, Any]:
        """Get strategy information"""
        try:
            if hasattr(self.primary_strategy, 'get_strategy_info'):
                return self.primary_strategy.get_strategy_info()
            else:
                return {
                    'strategy_name': self.primary_strategy.__class__.__name__,
                    'strategy_type': 'traditional'
                }
        except Exception as e:
            logger.warning(f"Failed to get strategy info: {e}")
            return {
                'strategy_name': 'Unknown',
                'strategy_type': 'unknown',
                'error': str(e)
            }
    
    def _track_performance(self, symbol: str, results: Dict):
        """Track performance data"""
        performance_data = {
            'timestamp': datetime.now(),
            'symbol': symbol,
            'strategy': self.primary_strategy_name,
            'success': 'error' not in results,
            'regime': None,
            'signal_generated': False,
            'error': results.get('error')
        }
        
        # Extract additional info if available
        if self.primary_strategy_name in results:
            result = results[self.primary_strategy_name]
            if 'signals' in result and not result['signals'].empty:
                signals = result['signals']
                performance_data['signal_generated'] = signals['signal'].iloc[-1] != 0
                if 'regime' in signals.columns:
                    performance_data['regime'] = signals['regime'].iloc[-1]
        
        self.performance_history.append(performance_data)
        
        # Keep only recent history
        if len(self.performance_history) > 1000:
            self.performance_history = self.performance_history[-500:]
    
    def _count_regime_switches(self, recent_data: List[Dict]) -> int:
        """Count regime switches in recent data"""
        regimes = [d.get('regime') for d in recent_data if d.get('regime')]
        if len(regimes) < 2:
            return 0
        
        switches = 0
        for i in range(1, len(regimes)):
            if regimes[i] != regimes[i-1]:
                switches += 1
        
        return switches
    
    def _calculate_avg_signal_strength(self, recent_data: List[Dict]) -> float:
        """Calculate average signal strength (placeholder)"""
        signal_runs = [d for d in recent_data if d.get('signal_generated')]
        return len(signal_runs) / max(len(recent_data), 1)
    
    # ================================================================
    # LEGACY COMPATIBILITY METHODS
    # ================================================================
    
    def register_strategy(self, name: str, strategy, weight: float = 1.0):
        """Legacy compatibility - not used in simplified version"""
        logger.warning("register_strategy called on simplified manager - ignored")
    
    def enable_strategy(self, name: str):
        """Legacy compatibility"""
        logger.info(f"enable_strategy called: {name}")
    
    def disable_strategy(self, name: str):
        """Legacy compatibility"""
        logger.info(f"disable_strategy called: {name}")
    
    def update_strategy_weight(self, name: str, weight: float):
        """Legacy compatibility"""
        logger.info(f"update_strategy_weight called: {name} -> {weight}")

# ================================================================
# USAGE EXAMPLE
# ================================================================

async def test_simplified_unified_manager():
    """Test the simplified unified manager"""
    
    config = {
        'etf_enhancement': {
            'enabled': True,
            'cache_duration': 15
        },
        'markov_model': {
            'lookback_period': 50,
            'min_history': 20
        },
        'signal_threshold': 0.25
    }
    
    # Create manager
    manager = UnifiedStrategyManager(config)
    
    # Create test data
    dates = pd.date_range('2024-01-01', periods=60, freq='D')
    np.random.seed(42)
    
    price_data = 50000 * np.cumprod(1 + np.random.normal(0, 0.02, 60))
    
    test_data = pd.DataFrame({
        'open': price_data * 0.999,
        'high': price_data * 1.005,
        'low': price_data * 0.995,
        'close': price_data,
        'volume': np.random.lognormal(15, 0.5, 60)
    }, index=dates)
    
    print("Testing Simplified Unified Manager...")
    
    # Test execution
    results = await manager.run_strategies('BTCUSDT', test_data)
    
    if 'error' in results:
        print(f"Error: {results['error']}")
        return
    
    # Test aggregation
    aggregated = manager.aggregate_signals(results)
    
    if aggregated.empty:
        print("No signals generated")
        return
    
    print("\n=== RESULTS ===")
    print(f"Final Signal: {aggregated['signal'].iloc[-1]}")
    print(f"Signal Strength: {aggregated.get('final_strength', [0]).iloc[-1]:.3f}")
    print(f"Confidence: {aggregated.get('confidence', [0]).iloc[-1]:.3f}")
    
    # Test system capabilities
    capabilities = manager.get_system_capabilities()
    print(f"\n=== CAPABILITIES ===")
    print(f"Primary Strategy: {capabilities['strategies']['primary']}")
    print(f"ETF Enabled: {capabilities['etf_integration']['enabled']}")
    print(f"Markov Enabled: {capabilities['regime_tracking']['markov_enabled']}")
    print(f"Features: {', '.join(capabilities['features'])}")
    
    # Test performance summary
    perf = await manager.get_performance_summary(30)
    print(f"\n=== PERFORMANCE ===")
    print(f"Total Runs: {perf.get('total_strategy_runs', 0)}")
    print(f"Success Rate: {perf.get('success_rate', 0):.1%}")
    
    print(f"\n=== SUCCESS ===")
    print("Simplified Unified Manager working correctly!")

if __name__ == "__main__":
    import asyncio
    asyncio.run(test_simplified_unified_manager())