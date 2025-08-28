"""
Strategy Performance Module

This module tracks and analyzes trading strategy performance metrics.

Classes:
    StrategyPerformanceTracker: Main class for strategy performance tracking
"""

import time
import pandas as pd
import numpy as np
from collections import defaultdict


class StrategyPerformanceTracker:
    """
    Tracks and analyzes trading strategy performance.
    
    Attributes:
        strategy_returns (dict): Daily returns by strategy
        strategy_equity (dict): Equity curves by strategy
        strategy_trades (dict): Trades by strategy
        strategy_metrics (dict): Performance metrics by strategy
    """
    
    def __init__(self, pnl_calculator=None):
        """
        Initialize the strategy performance tracker.
        
        Args:
            pnl_calculator: Optional P&L calculator to use
        """
        self.strategy_returns = {}
        self.strategy_equity = {}
        self.strategy_trades = defaultdict(list)
        self.strategy_metrics = {}
        self.pnl_calculator = pnl_calculator
    
    def add_trade(self, trade):
        """
        Add a trade to the performance tracker.
        
        Args:
            trade (dict): Trade information including:
                         strategy_id, asset, quantity, price,
                         timestamp, pnl, etc.
        """
        strategy_id = trade['strategy_id']
        self.strategy_trades[strategy_id].append(trade)
        
        # Update equity and returns if pnl is provided
        if 'pnl' in trade:
            self._update_equity_and_returns(strategy_id, trade)
    
    def _update_equity_and_returns(self, strategy_id, trade):
        """
        Update equity curve and returns for a strategy.
        
        Args:
            strategy_id (str): Strategy identifier
            trade (dict): Trade information with pnl
        """
        # Initialize if this is the first trade
        if strategy_id not in self.strategy_equity:
            self.strategy_equity[strategy_id] = pd.Series(dtype=float)
        
        if strategy_id not in self.strategy_returns:
            self.strategy_returns[strategy_id] = pd.Series(dtype=float)
        
        # Convert timestamp to datetime for pandas
        timestamp = pd.Timestamp.fromtimestamp(trade['timestamp'])
        date = timestamp.floor('D')
        
        # Update equity
        current_equity = self.strategy_equity[strategy_id].iloc[-1] if len(self.strategy_equity[strategy_id]) > 0 else 0
        new_equity = current_equity + trade['pnl']
        
        self.strategy_equity[strategy_id][timestamp] = new_equity
        
        # Update daily returns
        if date in self.strategy_returns[strategy_id].index:
            # Add to existing date
            self.strategy_returns[strategy_id][date] += trade['pnl']
        else:
            # New date
            self.strategy_returns[strategy_id][date] = trade['pnl']
    
    def update_from_pnl_calculator(self, strategy_id=None, timestamp=None):
        """
        Update performance metrics from P&L calculator.
        
        Args:
            strategy_id (str): Optional strategy filter
            timestamp (float): Optional timestamp to use
        
        Returns:
            dict: Updated strategy metrics
        """
        if not self.pnl_calculator:
            return {}
        
        timestamp = timestamp or time.time()
        
        # Get P&L summary
        pnl_summary = self.pnl_calculator.get_pnl_summary(strategy_id=strategy_id)
        
        if strategy_id:
            # Single strategy update
            self._update_strategy_from_pnl(strategy_id, pnl_summary, timestamp)
            return self.strategy_metrics.get(strategy_id, {})
        else:
            # Update all strategies
            for sid in pnl_summary.get('by_strategy', {}):
                strategy_pnl = {
                    'strategy_id': sid,
                    'realized_pnl': pnl_summary['by_strategy'][sid]['realized_pnl'],
                    'unrealized_pnl': pnl_summary['by_strategy'][sid]['unrealized_pnl'],
                    'total_pnl': pnl_summary['by_strategy'][sid]['total_pnl']
                }
                self._update_strategy_from_pnl(sid, strategy_pnl, timestamp)
            
            return self.strategy_metrics
    
    def _update_strategy_from_pnl(self, strategy_id, pnl_data, timestamp):
        """
        Update strategy metrics from P&L data.
        
        Args:
            strategy_id (str): Strategy identifier
            pnl_data (dict): P&L data for the strategy
            timestamp (float): Timestamp to use
        """
        # Initialize if this is the first update
        if strategy_id not in self.strategy_equity:
            self.strategy_equity[strategy_id] = pd.Series(dtype=float)
        
        if strategy_id not in self.strategy_returns:
            self.strategy_returns[strategy_id] = pd.Series(dtype=float)
        
        # Convert timestamp to datetime for pandas
        pd_timestamp = pd.Timestamp.fromtimestamp(timestamp)
        date = pd_timestamp.floor('D')
        
        # Get total P&L
        total_pnl = pnl_data.get('total_pnl', 0)
        
        # Update equity
        self.strategy_equity[strategy_id][pd_timestamp] = total_pnl
        
        # Update daily returns
        if date in self.strategy_returns[strategy_id].index:
            # Update existing date
            current_pnl = self.strategy_returns[strategy_id][date]
            self.strategy_returns[strategy_id][date] = total_pnl
        else:
            # New date
            self.strategy_returns[strategy_id][date] = total_pnl
    
    def calculate_metrics(self, strategy_id=None, start_date=None, end_date=None):
        """
        Calculate performance metrics for strategies.
        
        Args:
            strategy_id (str): Optional strategy filter
            start_date (pd.Timestamp): Optional start date filter
            end_date (pd.Timestamp): Optional end date filter
        
        Returns:
            dict: Performance metrics
        """
        if strategy_id:
            return self._calculate_strategy_metrics(strategy_id, start_date, end_date)
        
        # Calculate for all strategies
        results = {}
        
        for sid in self.strategy_returns:
            results[sid] = self._calculate_strategy_metrics(sid, start_date, end_date)
        
        return results
    
    def _calculate_strategy_metrics(self, strategy_id, start_date=None, end_date=None):
        """
        Calculate metrics for a specific strategy.
        
        Args:
            strategy_id (str): Strategy identifier
            start_date (pd.Timestamp): Optional start date filter
            end_date (pd.Timestamp): Optional end date filter
        
        Returns:
            dict: Strategy performance metrics
        """
        if strategy_id not in self.strategy_returns or len(self.strategy_returns[strategy_id]) == 0:
            return {
                'strategy_id': strategy_id,
                'total_pnl': 0,
                'sharpe_ratio': 0,
                'sortino_ratio': 0,
                'max_drawdown': 0,
                'win_rate': 0,
                'profit_factor': 0
            }
        
        # Get returns series
        returns = self.strategy_returns[strategy_id]
        
        # Apply date filters
        if start_date:
            returns = returns[returns.index >= start_date]
        
        if end_date:
            returns = returns[returns.index <= end_date]
        
        if len(returns) == 0:
            return {
                'strategy_id': strategy_id,
                'total_pnl': 0,
                'sharpe_ratio': 0,
                'sortino_ratio': 0,
                'max_drawdown': 0,
                'win_rate': 0,
                'profit_factor': 0
            }
        
        # Calculate key metrics
        total_pnl = returns.sum()
        
        # Calculate Sharpe ratio (assuming returns are in dollars, not percentages)
        # For percentage returns: returns.mean() / returns.std() * np.sqrt(252)
        daily_returns = returns.resample('D').sum()
        sharpe_ratio = (daily_returns.mean() / daily_returns.std() * np.sqrt(252)) if daily_returns.std() > 0 else 0
        
        # Calculate Sortino ratio (downside deviation)
        downside_returns = daily_returns[daily_returns < 0]
        sortino_ratio = (daily_returns.mean() / downside_returns.std() * np.sqrt(252)) if len(downside_returns) > 0 and downside_returns.std() > 0 else 0
        
        # Calculate maximum drawdown
        equity = returns.cumsum()
        rolling_max = equity.cummax()
        drawdown = (rolling_max - equity) / rolling_max
        max_drawdown = drawdown.max() if len(drawdown) > 0 else 0
        
        # Calculate win rate and profit factor from trades
        trades = self.strategy_trades.get(strategy_id, [])
        
        if trades:
            profitable_trades = [t for t in trades if t.get('pnl', 0) > 0]
            losing_trades = [t for t in trades if t.get('pnl', 0) < 0]
            
            win_rate = len(profitable_trades) / len(trades) if trades else 0
            
            gross_profit = sum(t.get('pnl', 0) for t in profitable_trades)
            gross_loss = abs(sum(t.get('pnl', 0) for t in losing_trades))
            
            profit_factor = gross_profit / gross_loss if gross_loss > 0 else float('inf')
        else:
            win_rate = 0
            profit_factor = 0
        
        # Save and return metrics
        metrics = {
            'strategy_id': strategy_id,
            'total_pnl': total_pnl,
            'sharpe_ratio': sharpe_ratio,
            'sortino_ratio': sortino_ratio,
            'max_drawdown': max_drawdown,
            'win_rate': win_rate,
            'profit_factor': profit_factor,
            'num_trades': len(trades),
            'start_date': returns.index[0],
            'end_date': returns.index[-1]
        }
        
        self.strategy_metrics[strategy_id] = metrics
        
        return metrics
    
    def get_equity_curve(self, strategy_id, start_date=None, end_date=None, resample='D'):
        """
        Get equity curve for a strategy.
        
        Args:
            strategy_id (str): Strategy identifier
            start_date (pd.Timestamp): Optional start date filter
            end_date (pd.Timestamp): Optional end date filter
            resample (str): Resampling frequency (e.g., 'D', 'H')
        
        Returns:
            pd.Series: Equity curve
        """
        if strategy_id not in self.strategy_equity:
            return pd.Series()
        
        equity = self.strategy_equity[strategy_id]
        
        # Apply date filters
        if start_date:
            equity = equity[equity.index >= start_date]
        
        if end_date:
            equity = equity[equity.index <= end_date]
        
        # Resample if requested
        if resample:
            equity = equity.resample(resample).last()
        
        return equity
    
    def get_drawdowns(self, strategy_id, start_date=None, end_date=None):
        """
        Get drawdowns for a strategy.
        
        Args:
            strategy_id (str): Strategy identifier
            start_date (pd.Timestamp): Optional start date filter
            end_date (pd.Timestamp): Optional end date filter
        
        Returns:
            pd.Series: Drawdowns
        """
        equity = self.get_equity_curve(strategy_id, start_date, end_date, resample=None)
        
        if len(equity) == 0:
            return pd.Series()
        
        # Calculate drawdowns
        rolling_max = equity.cummax()
        drawdowns = (rolling_max - equity) / rolling_max
        
        return drawdowns
    
    def compare_strategies(self, strategy_ids=None, start_date=None, end_date=None):
        """
        Compare performance metrics for multiple strategies.
        
        Args:
            strategy_ids (list): List of strategy IDs to compare
            start_date (pd.Timestamp): Optional start date filter
            end_date (pd.Timestamp): Optional end date filter
        
        Returns:
            pd.DataFrame: Comparison of strategy metrics
        """
        # Calculate metrics for all strategies
        all_metrics = self.calculate_metrics(None, start_date, end_date)
        
        # Filter strategies if requested
        if strategy_ids:
            metrics = {sid: all_metrics[sid] for sid in strategy_ids if sid in all_metrics}
        else:
            metrics = all_metrics
        
        # Convert to DataFrame
        df = pd.DataFrame(metrics).T
        
        return df
