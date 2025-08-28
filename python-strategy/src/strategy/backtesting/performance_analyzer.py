
# ================================================================
# src/strategy/backtesting/performance_analyzer.py
# ================================================================

import pandas as pd
import numpy as np
from typing import Dict, List
import matplotlib.pyplot as plt
import seaborn as sns

class PerformanceAnalyzer:
    """Advanced performance analysis and visualization"""
    
    @staticmethod
    def analyze_trades(portfolio: pd.DataFrame) -> Dict:
        """Analyze individual trade performance"""
        trades = []
        position = 0
        entry_price = 0
        entry_date = None
        
        for date, row in portfolio.iterrows():
            if row['position_change'] != 0:
                if position == 0 and row['position'] != 0:
                    # Opening trade
                    position = row['position']
                    entry_price = row['price']
                    entry_date = date
                elif position != 0 and row['position'] == 0:
                    # Closing trade
                    exit_price = row['price']
                    exit_date = date
                    
                    # Calculate trade metrics
                    if position > 0:  # Long trade
                        trade_return = (exit_price - entry_price) / entry_price
                    else:  # Short trade
                        trade_return = (entry_price - exit_price) / entry_price
                    
                    hold_days = (exit_date - entry_date).days
                    
                    trades.append({
                        'entry_date': entry_date,
                        'exit_date': exit_date,
                        'entry_price': entry_price,
                        'exit_price': exit_price,
                        'position': position,
                        'return': trade_return,
                        'hold_days': hold_days
                    })
                    
                    position = 0
        
        if not trades:
            return {}
        
        trades_df = pd.DataFrame(trades)
        
        return {
            'trade_count': len(trades_df),
            'avg_return': trades_df['return'].mean(),
            'median_return': trades_df['return'].median(),
            'best_trade': trades_df['return'].max(),
            'worst_trade': trades_df['return'].min(),
            'avg_hold_days': trades_df['hold_days'].mean(),
            'win_rate': (trades_df['return'] > 0).mean(),
            'avg_winner': trades_df[trades_df['return'] > 0]['return'].mean(),
            'avg_loser': trades_df[trades_df['return'] < 0]['return'].mean(),
            'trades_detail': trades_df
        }
    
    @staticmethod
    def monthly_returns(portfolio: pd.DataFrame) -> pd.DataFrame:
        """Calculate monthly returns breakdown"""
        returns = portfolio['net_returns'].dropna()
        monthly = returns.groupby([returns.index.year, returns.index.month]).sum()
        monthly.index = pd.to_datetime([f"{year}-{month:02d}-01" for year, month in monthly.index])
        return monthly
    
    @staticmethod
    def rolling_metrics(portfolio: pd.DataFrame, window: int = 60) -> pd.DataFrame:
        """Calculate rolling performance metrics"""
        returns = portfolio['net_returns'].dropna()
        
        rolling_metrics = pd.DataFrame(index=returns.index)
        rolling_metrics['rolling_return'] = returns.rolling(window).sum()
        rolling_metrics['rolling_volatility'] = returns.rolling(window).std() * np.sqrt(252)
        rolling_metrics['rolling_sharpe'] = (
            rolling_metrics['rolling_return'] * 252 / rolling_metrics['rolling_volatility']
        )
        
        return rolling_metrics
