import pandas as pd 
import numpy as np
from typing import Dict, Any, List
from .strategies import StatisticalMeanReversionStrategy
from .backtesting import Backtester
# from models import StrategyConfig, StrategyResult
from .base_strategy import BaseStrategy
from .manager import StrategyManager
# ================================================================
# Example usage and test
# ================================================================

if __name__ == "__main__":
    # Example configuration
    config = {
        'active_strategies': [
            {
                'name': 'statistical_mean_reversion',
                'weight': 1.0,
                'params': {
                    'zscore_window': 20,
                    'zscore_entry': 2.0,
                    'zscore_exit': 0.5,
                    'ema_short': 10,
                    'ema_long': 30
                }
            }
        ],
        'signal_threshold': 0.3
    }
    
    # Create sample data for testing
    dates = pd.date_range('2023-01-01', periods=100, freq='D')
    np.random.seed(42)
    
    # Generate realistic OHLCV data
    close_prices = 100 + np.cumsum(np.random.randn(100) * 0.02)
    high_prices = close_prices + np.random.rand(100) * 2
    low_prices = close_prices - np.random.rand(100) * 2
    open_prices = close_prices + np.random.randn(100) * 0.5
    volume = np.random.randint(1000000, 5000000, 100)
    
    test_data = pd.DataFrame({
        'open': open_prices,
        'high': high_prices,
        'low': low_prices,
        'close': close_prices,
        'volume': volume
    }, index=dates)
    
    # Test the strategy manager
    manager = StrategyManager(config)
    results = manager.run_strategies('BTCUSDT', test_data)
    
    if results:
        aggregated = manager.aggregate_signals(results)
        print("Strategy Engine Test Results:")
        print(f"Signals generated: {len(aggregated)}")
        print(f"Buy signals: {(aggregated['final_signal'] == 1).sum()}")
        print(f"Sell signals: {(aggregated['final_signal'] == -1).sum()}")
        print(f"Average confidence: {aggregated['confidence'].mean():.2f}")
        
        # Test backtesting
        from .backtesting.backtester import Backtester
        
        backtest_config = {
            'initial_capital': 100000,
            'commission': 0.001,
            'slippage': 0.0005
        }
        
        backtester = Backtester(backtest_config)
        strategy = list(results.values())[0]['strategy_info']
        
        # For testing, create a mock strategy object
        class MockStrategy:
            def __init__(self):
                self.name = "Test Strategy"
                self.config = config['active_strategies'][0]['params']
            
            def calculate_indicators(self, data):
                return results['statistical_mean_reversion']['indicators']
            
            def generate_signals(self, data, indicators):
                return results['statistical_mean_reversion']['signals']
        
        mock_strategy = MockStrategy()
        backtest_results = backtester.run_backtest(mock_strategy, test_data)
        
        print("\nBacktest Results:")
        metrics = backtest_results['metrics']
        print(f"Total Return: {metrics['total_return']:.2%}")
        print(f"Sharpe Ratio: {metrics['sharpe_ratio']:.2f}")
        print(f"Max Drawdown: {metrics['max_drawdown']:.2%}")
        print(f"Win Rate: {metrics['win_rate']:.2%}")
        
    else:
        print("No strategy results generated")