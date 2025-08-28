
# ================================================================
# src/strategy/backtesting/backtester.py
# ================================================================

import pandas as pd
import numpy as np
from typing import Dict, Optional
from datetime import datetime, timedelta
import logging

logger = logging.getLogger(__name__)

class Backtester:
    """Simple but effective backtesting framework"""
    
    def __init__(self, config: dict):
        self.config = config
        self.initial_capital = config.get('initial_capital', 100000)
        self.commission = config.get('commission', 0.001)  # 0.1%
        self.slippage = config.get('slippage', 0.0005)     # 0.05%
        
    def run_backtest(self, strategy, data: pd.DataFrame, 
                    start_date: datetime = None, end_date: datetime = None) -> Dict:
        """Run complete backtest for a strategy"""
        
        # Prepare data
        if start_date or end_date:
            test_data = self._filter_data(data, start_date, end_date)
        else:
            test_data = data.copy()
            
        if len(test_data) < 50:
            raise ValueError("Insufficient data for backtesting")
        
        # Calculate indicators and signals
        logger.info(f"Running backtest for {strategy.name} on {len(test_data)} periods")
        
        indicators = strategy.calculate_indicators(test_data)
        signals = strategy.generate_signals(test_data, indicators)
        
        # Simulate trading
        portfolio = self._simulate_trading(test_data, signals)
        
        # Calculate metrics
        metrics = self._calculate_metrics(portfolio)
        
        return {
            'strategy_name': strategy.name,
            'period': {
                'start': test_data.index[0],
                'end': test_data.index[-1],
                'days': len(test_data)
            },
            'portfolio': portfolio,
            'signals': signals,
            'indicators': indicators,
            'metrics': metrics
        }
    
    def _filter_data(self, data: pd.DataFrame, start_date: datetime = None, 
                    end_date: datetime = None) -> pd.DataFrame:
        """Filter data by date range"""
        filtered = data.copy()
        
        if start_date:
            filtered = filtered[filtered.index >= start_date]
        if end_date:
            filtered = filtered[filtered.index <= end_date]
            
        return filtered
    
    def _simulate_trading(self, data: pd.DataFrame, signals: pd.DataFrame) -> pd.DataFrame:
        """Simulate trading execution with realistic costs"""
        portfolio = pd.DataFrame(index=data.index)
        
        # Basic data
        portfolio['price'] = data['close']
        portfolio['signal'] = signals['signal'].fillna(0)
        
        # Calculate position changes
        portfolio['position'] = portfolio['signal'].fillna(method='ffill').fillna(0)
        portfolio['position_change'] = portfolio['position'].diff().fillna(0)
        
        # Calculate returns
        portfolio['returns'] = data['close'].pct_change()
        portfolio['strategy_returns'] = portfolio['position'].shift(1) * portfolio['returns']
        
        # Apply transaction costs
        portfolio['trades'] = abs(portfolio['position_change'])
        portfolio['transaction_costs'] = portfolio['trades'] * (self.commission + self.slippage)
        
        # Net returns after costs
        portfolio['net_returns'] = portfolio['strategy_returns'] - portfolio['transaction_costs']
        
        # Calculate equity curve
        portfolio['cumulative_returns'] = (1 + portfolio['net_returns']).cumprod()
        portfolio['equity_curve'] = self.initial_capital * portfolio['cumulative_returns']
        
        # Drawdown calculation
        portfolio['running_max'] = portfolio['cumulative_returns'].expanding().max()
        portfolio['drawdown'] = (portfolio['cumulative_returns'] - portfolio['running_max']) / portfolio['running_max']
        
        # Trade tracking
        portfolio['trade_num'] = (portfolio['position_change'] != 0).cumsum()
        
        return portfolio
    
    def _calculate_metrics(self, portfolio: pd.DataFrame) -> Dict:
        """Calculate comprehensive performance metrics"""
        returns = portfolio['net_returns'].dropna()
        equity = portfolio['equity_curve'].dropna()
        
        if len(returns) == 0:
            return {}
        
        # Basic metrics
        total_return = (equity.iloc[-1] / self.initial_capital) - 1
        periods_per_year = 252  # Daily data
        annualized_return = (1 + total_return) ** (periods_per_year / len(returns)) - 1
        
        # Risk metrics
        volatility = returns.std() * np.sqrt(periods_per_year)
        sharpe_ratio = annualized_return / volatility if volatility > 0 else 0
        
        # Drawdown
        max_drawdown = portfolio['drawdown'].min()
        
        # Win rate
        winning_trades = returns[returns > 0]
        losing_trades = returns[returns < 0]
        win_rate = len(winning_trades) / len(returns[returns != 0]) if len(returns[returns != 0]) > 0 else 0
        
        # Profit factor
        gross_profit = winning_trades.sum()
        gross_loss = abs(losing_trades.sum())
        profit_factor = gross_profit / gross_loss if gross_loss > 0 else np.inf
        
        # Trade statistics
        total_trades = (portfolio['position_change'] != 0).sum()
        avg_trade_return = returns[returns != 0].mean() if len(returns[returns != 0]) > 0 else 0
        
        # Additional metrics
        calmar_ratio = annualized_return / abs(max_drawdown) if max_drawdown < 0 else 0
        
        # Sortino ratio (downside deviation)
        downside_returns = returns[returns < 0]
        downside_std = downside_returns.std() * np.sqrt(periods_per_year)
        sortino_ratio = annualized_return / downside_std if downside_std > 0 else 0
        
        return {
            'total_return': total_return,
            'annualized_return': annualized_return,
            'volatility': volatility,
            'sharpe_ratio': sharpe_ratio,
            'sortino_ratio': sortino_ratio,
            'calmar_ratio': calmar_ratio,
            'max_drawdown': max_drawdown,
            'win_rate': win_rate,
            'profit_factor': profit_factor,
            'total_trades': total_trades,
            'avg_trade_return': avg_trade_return,
            'gross_profit': gross_profit,
            'gross_loss': gross_loss,
            'final_equity': equity.iloc[-1],
            'days_traded': len(returns)
        }
