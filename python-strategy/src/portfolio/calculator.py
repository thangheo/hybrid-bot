
# src/portfolio/calculator.py
"""
P&L Calculator - Uses pandas for efficient calculations
Standard financial calculations using numpy/pandas
"""
import pandas as pd
import numpy as np
from typing import Dict, List
from datetime import datetime
from .models import PortfolioSnapshot, PositionSummary, PositionSide
class PnLCalculator:
    """
    Simple P&L calculator using pandas
    No reinventing financial calculation wheels
    """
    
    @staticmethod
    def calculate_unrealized_pnl(position_qty: float, avg_price: float, current_price: float) -> float:
        """Calculate unrealized P&L"""
        return (current_price - avg_price) * position_qty
    
    @staticmethod
    def calculate_realized_pnl(trades_df: pd.DataFrame, symbol: str) -> float:
        """
        Calculate realized P&L using FIFO method
        Uses pandas for efficient calculation
        """
        if trades_df.empty:
            return 0.0
        
        symbol_trades = trades_df[trades_df['symbol'] == symbol].copy()
        if symbol_trades.empty:
            return 0.0
        
        symbol_trades = symbol_trades.sort_values('timestamp')
        
        # Simple FIFO calculation using pandas
        buys = symbol_trades[symbol_trades['side'] == 'BUY'].copy()
        sells = symbol_trades[symbol_trades['side'] == 'SELL'].copy()
        
        if buys.empty or sells.empty:
            return 0.0
        
        realized_pnl = 0.0
        buy_queue = buys[['quantity', 'price']].values.tolist()
        
        for _, sell in sells.iterrows():
            sell_qty = sell['quantity']
            sell_price = sell['price']
            
            while sell_qty > 0 and buy_queue:
                buy_qty, buy_price = buy_queue[0]
                
                if buy_qty <= sell_qty:
                    # Fully consume this buy
                    realized_pnl += buy_qty * (sell_price - buy_price)
                    sell_qty -= buy_qty
                    buy_queue.pop(0)
                else:
                    # Partially consume this buy
                    realized_pnl += sell_qty * (sell_price - buy_price)
                    buy_queue[0][0] -= sell_qty
                    sell_qty = 0
        
        return realized_pnl
    
    @staticmethod
    def calculate_portfolio_metrics(equity_curve: pd.DataFrame) -> dict:
        """
        Calculate portfolio performance metrics using pandas
        Standard financial metrics - no custom algorithms
        """
        if len(equity_curve) < 2:
            return {}
        
        equity_curve = equity_curve.sort_values('timestamp')
        returns = equity_curve['total_value'].pct_change().dropna()
        
        if len(returns) == 0:
            return {}
        
        # Use standard financial calculations
        total_return = (equity_curve['total_value'].iloc[-1] / equity_curve['total_value'].iloc[0]) - 1
        volatility = returns.std() * np.sqrt(252)
        sharpe_ratio = returns.mean() / returns.std() * np.sqrt(252) if returns.std() > 0 else 0
        
        # Drawdown calculation
        running_max = equity_curve['total_value'].expanding().max()
        drawdown = (equity_curve['total_value'] - running_max) / running_max
        max_drawdown = drawdown.min()
        
        return {
            'total_return': total_return,
            'volatility': volatility,
            'sharpe_ratio': sharpe_ratio,
            'max_drawdown': max_drawdown,
            'calmar_ratio': total_return / abs(max_drawdown) if max_drawdown != 0 else 0
        }