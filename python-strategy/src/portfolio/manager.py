# src/portfolio/manager.py
"""
Portfolio Manager - Tracks positions and calculates P&L
Uses pandas for data manipulation and simple dict structures for state management
"""

import pandas as pd
import numpy as np
from datetime import datetime, timedelta
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass, field
import logging

logger = logging.getLogger(__name__)

@dataclass
class Position:
    """Simple position data structure"""
    symbol: str
    quantity: float
    average_price: float
    current_price: float = 0.0
    unrealized_pnl: float = 0.0
    realized_pnl: float = 0.0
    timestamp: datetime = field(default_factory=datetime.now)
    
    @property
    def market_value(self) -> float:
        return self.quantity * self.current_price
    
    @property
    def cost_basis(self) -> float:
        return abs(self.quantity) * self.average_price

@dataclass
class Trade:
    """Trade record for P&L calculation"""
    symbol: str
    side: str  # 'BUY' or 'SELL'
    quantity: float
    price: float
    commission: float
    timestamp: datetime
    order_id: str

class PortfolioManager:
    """
    Simple portfolio manager using pandas for calculations
    No complex inheritance - just working functions
    """
    
    def __init__(self, config: dict):
        self.config = config
        self.initial_capital = config.get('initial_capital', 5000)
        self.base_currency = config.get('base_currency', 'USDT')
        
        # Use simple dict for positions - fast and reliable
        self.positions: Dict[str, Position] = {}
        
        # Use pandas DataFrame for trade history - excellent for calculations
        self.trades_df = pd.DataFrame(columns=[
            'timestamp', 'symbol', 'side', 'quantity', 'price', 'commission', 'order_id'
        ])
        
        # Portfolio metrics
        self.cash_balance = self.initial_capital
        self.total_realized_pnl = 0.0
        self.total_commission_paid = 0.0
        
        # Performance tracking
        self.equity_curve = pd.DataFrame(columns=['timestamp', 'total_value'])
        self._last_update = datetime.now()
        
        logger.info(f"Portfolio initialized with {self.initial_capital} {self.base_currency}")
    
    async def initialize(self):
        """Initialize portfolio (async compatibility)"""
        # Already initialized in __init__, this is just for async compatibility
        logger.info("Portfolio manager initialized")
        return True
    
    def update_position(self, symbol: str, quantity: float, price: float):
        """Simple update position method for integration"""
        if symbol not in self.positions:
            self.positions[symbol] = Position(symbol=symbol, quantity=0, average_price=0)
        
        position = self.positions[symbol]
        
        if quantity > 0:  # Adding to position
            if position.quantity >= 0:  # Same direction
                total_cost = (position.quantity * position.average_price) + (quantity * price)
                total_quantity = position.quantity + quantity
                position.average_price = total_cost / total_quantity if total_quantity != 0 else 0
                position.quantity = total_quantity
            else:  # Opposite direction
                position.quantity += quantity
        else:  # Reducing position
            position.quantity += quantity
        
        position.current_price = price
        position.unrealized_pnl = (position.current_price - position.average_price) * position.quantity
        
        # Clean up zero positions
        if abs(position.quantity) < 1e-8:
            del self.positions[symbol]
        
        logger.debug(f"Position updated: {symbol} = {position.quantity:.6f}")
    
    async def get_portfolio_summary(self) -> dict:
        """Get portfolio summary (async for integration compatibility)"""
        return self.get_portfolio_summary_sync()
    
    def get_portfolio_summary_sync(self) -> dict:
        """Get comprehensive portfolio summary using pandas for calculations"""
        total_value = self.get_total_portfolio_value()
        total_unrealized_pnl = sum(pos.unrealized_pnl for pos in self.positions.values())
        
        # Calculate daily P&L using pandas
        daily_pnl = 0.0
        if len(self.equity_curve) > 1:
            today_start = datetime.now().replace(hour=0, minute=0, second=0, microsecond=0)
            today_equity = self.equity_curve[self.equity_curve['timestamp'] >= today_start]
            if len(today_equity) > 0:
                start_value = today_equity.iloc[0]['total_value']
                current_value = today_equity.iloc[-1]['total_value']
                daily_pnl = current_value - start_value
        
        return {
            'timestamp': datetime.now(),
            'total_value': total_value,
            'cash_balance': self.cash_balance,
            'positions_value': total_value - self.cash_balance,
            'total_unrealized_pnl': total_unrealized_pnl,
            'total_realized_pnl': self.total_realized_pnl,
            'daily_pnl': daily_pnl,
            'total_commission_paid': self.total_commission_paid,
            'num_positions': len(self.positions),
            'return_pct': (total_value - self.initial_capital) / self.initial_capital * 100
        }
    
    async def update_from_execution(self, order):
        """Update portfolio from order execution (async for integration)"""
        if hasattr(order, 'symbol') and hasattr(order, 'filled_quantity'):
            trade_data = {
                'symbol': order.symbol.replace('/', ''),  # Convert CCXT format
                'side': order.side.value if hasattr(order.side, 'value') else str(order.side),
                'quantity': order.filled_quantity,
                'price': order.average_price,
                'commission': getattr(order, 'commission', 0),
                'order_id': order.id,
                'timestamp': datetime.now()
            }
            self.update_from_trade(trade_data)
    
    async def sync_with_exchange(self):
        """Sync portfolio with exchange (placeholder for integration)"""
        logger.info("Portfolio sync with exchange completed")
        return True
    
    def calculate_performance(self) -> dict:
        """Calculate performance metrics"""
        return self.get_performance_metrics()
    
    def update_from_trade(self, trade_data: dict) -> None:
        """
        Update portfolio from executed trade
        Uses simple arithmetic - no complex position tracking algorithms
        """
        symbol = trade_data['symbol']
        side = trade_data['side']
        quantity = float(trade_data['quantity'])
        price = float(trade_data['price'])
        commission = float(trade_data.get('commission', 0))
        order_id = trade_data.get('order_id', '')
        timestamp = trade_data.get('timestamp', datetime.now())
        
        # Record trade in DataFrame - pandas handles all the heavy lifting
        new_trade = pd.DataFrame([{
            'timestamp': timestamp,
            'symbol': symbol,
            'side': side,
            'quantity': quantity,
            'price': price,
            'commission': commission,
            'order_id': order_id
        }])
        
        self.trades_df = pd.concat([self.trades_df, new_trade], ignore_index=True)
        
        # Update position using simple logic
        if symbol not in self.positions:
            self.positions[symbol] = Position(symbol=symbol, quantity=0, average_price=0)
        
        position = self.positions[symbol]
        
        if side == 'BUY':
            # Calculate new average price
            total_cost = (position.quantity * position.average_price) + (quantity * price)
            total_quantity = position.quantity + quantity
            
            if total_quantity != 0:
                position.average_price = total_cost / total_quantity
            position.quantity = total_quantity
            
            # Update cash
            self.cash_balance -= (quantity * price) + commission
            
        elif side == 'SELL':
            # Calculate realized P&L for the sold portion
            if position.quantity > 0:
                realized_pnl = (price - position.average_price) * quantity - commission
                self.total_realized_pnl += realized_pnl
                position.realized_pnl += realized_pnl
            
            position.quantity -= quantity
            
            # Update cash
            self.cash_balance += (quantity * price) - commission
        
        # Track commission
        self.total_commission_paid += commission
        
        # Clean up zero positions
        if abs(position.quantity) < 1e-8:
            del self.positions[symbol]
        
        logger.info(f"Updated position: {symbol} = {position.quantity:.6f} @ {position.average_price:.4f}")
    
    def update_market_prices(self, price_data: Dict[str, float]) -> None:
        """
        Update current market prices and recalculate unrealized P&L
        Simple and fast - just updates current prices
        """
        for symbol, position in self.positions.items():
            if symbol in price_data:
                position.current_price = price_data[symbol]
                # Calculate unrealized P&L
                position.unrealized_pnl = (position.current_price - position.average_price) * position.quantity
        
        # Update equity curve
        total_value = self.get_total_portfolio_value()
        new_equity_point = pd.DataFrame([{
            'timestamp': datetime.now(),
            'total_value': total_value
        }])
        self.equity_curve = pd.concat([self.equity_curve, new_equity_point], ignore_index=True)
        
        # Keep only recent equity data (last 30 days)
        cutoff_date = datetime.now() - timedelta(days=30)
        self.equity_curve = self.equity_curve[self.equity_curve['timestamp'] > cutoff_date]
    
    def get_current_positions(self) -> Dict[str, dict]:
        """Get current positions as simple dict"""
        return {
            symbol: {
                'quantity': pos.quantity,
                'average_price': pos.average_price,
                'current_price': pos.current_price,
                'market_value': pos.market_value,
                'unrealized_pnl': pos.unrealized_pnl,
                'cost_basis': pos.cost_basis
            }
            for symbol, pos in self.positions.items()
        }
    
    def get_position_weights(self) -> Dict[str, float]:
        """Calculate position weights using pandas"""
        if not self.positions:
            return {}
        
        total_value = self.get_total_portfolio_value()
        if total_value <= 0:
            return {}
        
        return {
            symbol: abs(pos.market_value) / total_value
            for symbol, pos in self.positions.items()
        }
    
    def get_total_portfolio_value(self) -> float:
        """Calculate total portfolio value"""
        positions_value = sum(pos.market_value for pos in self.positions.values())
        return self.cash_balance + positions_value
    
    def get_performance_metrics(self, days: int = 30) -> dict:
        """Calculate performance metrics using pandas"""
        if len(self.equity_curve) < 2:
            return {}
        
        # Get recent equity data
        cutoff_date = datetime.now() - timedelta(days=days)
        recent_equity = self.equity_curve[self.equity_curve['timestamp'] > cutoff_date].copy()
        
        if len(recent_equity) < 2:
            return {}
        
        # Calculate returns using pandas
        recent_equity['returns'] = recent_equity['total_value'].pct_change()
        returns = recent_equity['returns'].dropna()
        
        if len(returns) == 0:
            return {}
        
        # Calculate metrics using pandas/numpy - industry standard
        total_return = (recent_equity['total_value'].iloc[-1] / recent_equity['total_value'].iloc[0]) - 1
        annualized_return = (1 + total_return) ** (365 / days) - 1
        volatility = returns.std() * np.sqrt(252)  # Annualized
        sharpe_ratio = annualized_return / volatility if volatility > 0 else 0
        
        # Max drawdown calculation
        running_max = recent_equity['total_value'].expanding().max()
        drawdown = (recent_equity['total_value'] - running_max) / running_max
        max_drawdown = drawdown.min()
        
        return {
            'period_days': days,
            'total_return': total_return,
            'annualized_return': annualized_return,
            'volatility': volatility,
            'sharpe_ratio': sharpe_ratio,
            'max_drawdown': max_drawdown,
            'win_rate': (returns > 0).mean(),
            'avg_win': returns[returns > 0].mean() if len(returns[returns > 0]) > 0 else 0,
            'avg_loss': returns[returns < 0].mean() if len(returns[returns < 0]) > 0 else 0,
            'unrealized_pnl': sum(pos.unrealized_pnl for pos in self.positions.values())
        }
    
    def get_trade_history(self, symbol: Optional[str] = None, days: Optional[int] = None) -> pd.DataFrame:
        """Get trade history as pandas DataFrame for easy analysis"""
        df = self.trades_df.copy()
        
        if symbol:
            df = df[df['symbol'] == symbol]
        
        if days:
            cutoff_date = datetime.now() - timedelta(days=days)
            df = df[df['timestamp'] > cutoff_date]
        
        return df.sort_values('timestamp', ascending=False)
    
    def calculate_position_pnl(self, symbol: str) -> dict:
        """Calculate detailed P&L for a specific position"""
        if symbol not in self.positions:
            return {}
        
        position = self.positions[symbol]
        
        # Get all trades for this symbol using pandas
        symbol_trades = self.trades_df[self.trades_df['symbol'] == symbol].copy()
        
        if len(symbol_trades) == 0:
            return {}
        
        # Calculate trade-by-trade P&L using pandas
        symbol_trades = symbol_trades.sort_values('timestamp')
        total_bought = symbol_trades[symbol_trades['side'] == 'BUY']['quantity'].sum()
        total_sold = symbol_trades[symbol_trades['side'] == 'SELL']['quantity'].sum()
        total_commission = symbol_trades['commission'].sum()
        
        return {
            'symbol': symbol,
            'current_quantity': position.quantity,
            'average_price': position.average_price,
            'current_price': position.current_price,
            'unrealized_pnl': position.unrealized_pnl,
            'realized_pnl': position.realized_pnl,
            'total_bought': total_bought,
            'total_sold': total_sold,
            'total_commission': total_commission,
            'market_value': position.market_value,
            'cost_basis': position.cost_basis
        }
    
    def export_portfolio_data(self) -> dict:
        """Export all portfolio data for persistence"""
        return {
            'timestamp': datetime.now().isoformat(),
            'cash_balance': self.cash_balance,
            'total_realized_pnl': self.total_realized_pnl,
            'total_commission_paid': self.total_commission_paid,
            'positions': {
                symbol: {
                    'quantity': pos.quantity,
                    'average_price': pos.average_price,
                    'realized_pnl': pos.realized_pnl
                }
                for symbol, pos in self.positions.items()
            },
            'trades': self.trades_df.to_dict('records'),
            'equity_curve': self.equity_curve.to_dict('records')
        }
    
    def import_portfolio_data(self, data: dict) -> None:
        """Import portfolio data from persistence"""
        self.cash_balance = data.get('cash_balance', self.initial_capital)
        self.total_realized_pnl = data.get('total_realized_pnl', 0.0)
        self.total_commission_paid = data.get('total_commission_paid', 0.0)
        
        # Restore positions
        self.positions = {}
        for symbol, pos_data in data.get('positions', {}).items():
            self.positions[symbol] = Position(
                symbol=symbol,
                quantity=pos_data['quantity'],
                average_price=pos_data['average_price'],
                realized_pnl=pos_data.get('realized_pnl', 0.0)
            )
        
        # Restore trade history
        trades_data = data.get('trades', [])
        if trades_data:
            self.trades_df = pd.DataFrame(trades_data)
            # Convert timestamp strings back to datetime
            self.trades_df['timestamp'] = pd.to_datetime(self.trades_df['timestamp'])
        
        # Restore equity curve
        equity_data = data.get('equity_curve', [])
        if equity_data:
            self.equity_curve = pd.DataFrame(equity_data)
            self.equity_curve['timestamp'] = pd.to_datetime(self.equity_curve['timestamp'])
        
        logger.info("Portfolio data imported successfully")