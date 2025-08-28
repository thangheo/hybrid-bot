# src/portfolio/models.py
"""
Portfolio data models using simple dataclasses and pandas
No complex ORM or database models - just working data structures
"""

from dataclasses import dataclass, field
from datetime import datetime
from typing import Dict, List, Optional
from enum import Enum

class PositionSide(Enum):
    LONG = "LONG"
    SHORT = "SHORT"
    FLAT = "FLAT"

class TradeType(Enum):
    BUY = "BUY"
    SELL = "SELL"

@dataclass
class PortfolioSnapshot:
    """Simple portfolio snapshot for persistence"""
    timestamp: datetime
    total_value: float
    cash_balance: float
    positions_value: float
    unrealized_pnl: float
    realized_pnl: float
    daily_pnl: float
    num_positions: int
    return_pct: float

@dataclass
class PositionSummary:
    """Position summary for reporting"""
    symbol: str
    quantity: float
    average_price: float
    current_price: float
    market_value: float
    unrealized_pnl: float
    unrealized_pnl_pct: float
    weight: float
    side: PositionSide
    
    @classmethod
    def from_position(cls, position, current_price: float, total_portfolio_value: float):
        """Create summary from position object"""
        market_value = position.quantity * current_price
        unrealized_pnl = (current_price - position.average_price) * position.quantity
        unrealized_pnl_pct = unrealized_pnl / (position.average_price * abs(position.quantity)) * 100
        weight = abs(market_value) / total_portfolio_value if total_portfolio_value > 0 else 0
        
        if position.quantity > 0:
            side = PositionSide.LONG
        elif position.quantity < 0:
            side = PositionSide.SHORT
        else:
            side = PositionSide.FLAT
            
        return cls(
            symbol=position.symbol,
            quantity=position.quantity,
            average_price=position.average_price,
            current_price=current_price,
            market_value=market_value,
            unrealized_pnl=unrealized_pnl,
            unrealized_pnl_pct=unrealized_pnl_pct,
            weight=weight,
            side=side
        )

@dataclass
class TradeRecord:
    """Simple trade record"""
    timestamp: datetime
    symbol: str
    side: TradeType
    quantity: float
    price: float
    commission: float
    order_id: str
    realized_pnl: Optional[float] = None
    
    @property
    def gross_value(self) -> float:
        return self.quantity * self.price
    
    @property
    def net_value(self) -> float:
        return self.gross_value - self.commission

@dataclass
class PerformanceMetrics:
    """Portfolio performance metrics"""
    period_days: int
    total_return: float
    annualized_return: float
    volatility: float
    sharpe_ratio: float
    sortino_ratio: float
    max_drawdown: float
    calmar_ratio: float
    win_rate: float
    profit_factor: float
    avg_win: float
    avg_loss: float
    num_trades: int
    
    def to_dict(self) -> dict:
        """Convert to dictionary for easy serialization"""
        return {
            'period_days': self.period_days,
            'total_return': self.total_return,
            'annualized_return': self.annualized_return,
            'volatility': self.volatility,
            'sharpe_ratio': self.sharpe_ratio,
            'sortino_ratio': self.sortino_ratio,
            'max_drawdown': self.max_drawdown,
            'calmar_ratio': self.calmar_ratio,
            'win_rate': self.win_rate,
            'profit_factor': self.profit_factor,
            'avg_win': self.avg_win,
            'avg_loss': self.avg_loss,
            'num_trades': self.num_trades
        }

@dataclass
class RiskMetrics:
    """Risk metrics for portfolio"""
    total_exposure: float
    max_position_weight: float
    num_positions: int
    concentration_risk: float
    var_95: float  # Value at Risk
    expected_shortfall: float
    beta: Optional[float] = None
    correlation_with_market: Optional[float] = None

@dataclass
class PortfolioState:
    """Complete portfolio state for persistence"""
    timestamp: datetime
    cash_balance: float
    positions: Dict[str, dict]  # symbol -> position data
    total_realized_pnl: float
    total_commission_paid: float
    initial_capital: float
    
    def to_dict(self) -> dict:
        """Convert to dictionary for JSON serialization"""
        return {
            'timestamp': self.timestamp.isoformat(),
            'cash_balance': self.cash_balance,
            'positions': self.positions,
            'total_realized_pnl': self.total_realized_pnl,
            'total_commission_paid': self.total_commission_paid,
            'initial_capital': self.initial_capital
        }
    
    @classmethod
    def from_dict(cls, data: dict):
        """Create from dictionary"""
        return cls(
            timestamp=datetime.fromisoformat(data['timestamp']),
            cash_balance=data['cash_balance'],
            positions=data['positions'],
            total_realized_pnl=data['total_realized_pnl'],
            total_commission_paid=data['total_commission_paid'],
            initial_capital=data['initial_capital']
        )
