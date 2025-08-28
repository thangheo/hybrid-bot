"""
Order execution models
Simple data structures for order management
"""
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import Optional, Dict, Any
import uuid

class OrderStatus(Enum):
    PENDING = "pending"
    SUBMITTED = "submitted" 
    PARTIALLY_FILLED = "partially_filled"
    FILLED = "filled"
    CANCELLED = "cancelled"
    REJECTED = "rejected"
    EXPIRED = "expired"

class OrderSide(Enum):
    BUY = "buy"
    SELL = "sell"

class OrderType(Enum):
    MARKET = "market"
    LIMIT = "limit"
    STOP_LOSS = "stop_loss"
    TAKE_PROFIT = "take_profit"
    STOP_LOSS_LIMIT = "stop_loss_limit"

@dataclass
class Order:
    """Order data model"""
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    symbol: str = ""
    side: OrderSide = OrderSide.BUY
    order_type: OrderType = OrderType.MARKET
    quantity: float = 0.0
    price: Optional[float] = None
    stop_price: Optional[float] = None
    time_in_force: str = "GTC"  # Good Till Cancelled
    status: OrderStatus = OrderStatus.PENDING
    exchange_order_id: Optional[str] = None
    client_order_id: Optional[str] = None
    
    # Timestamps
    created_at: datetime = field(default_factory=datetime.now)
    updated_at: datetime = field(default_factory=datetime.now)
    filled_at: Optional[datetime] = None
    
    # Execution details
    filled_quantity: float = 0.0
    remaining_quantity: float = 0.0
    average_price: float = 0.0
    last_executed_price: float = 0.0
    last_executed_quantity: float = 0.0
    
    # Costs
    commission: float = 0.0
    commission_asset: str = "USDT"
    
    # Error handling
    error_message: Optional[str] = None
    retry_count: int = 0
    
    # Metadata
    strategy_id: Optional[str] = None
    signal_id: Optional[str] = None
    tags: Dict[str, Any] = field(default_factory=dict)
    
    def __post_init__(self):
        """Set remaining quantity after initialization"""
        if self.remaining_quantity == 0.0:
            self.remaining_quantity = self.quantity
    
    @property
    def is_buy(self) -> bool:
        return self.side == OrderSide.BUY
    
    @property 
    def is_sell(self) -> bool:
        return self.side == OrderSide.SELL
    
    @property
    def is_filled(self) -> bool:
        return self.status == OrderStatus.FILLED
    
    @property
    def is_active(self) -> bool:
        return self.status in [OrderStatus.SUBMITTED, OrderStatus.PARTIALLY_FILLED]
    
    @property
    def is_complete(self) -> bool:
        return self.status in [OrderStatus.FILLED, OrderStatus.CANCELLED, OrderStatus.REJECTED, OrderStatus.EXPIRED]
    
    @property
    def fill_percentage(self) -> float:
        """Percentage of order filled"""
        if self.quantity == 0:
            return 0.0
        return (self.filled_quantity / self.quantity) * 100
    
    @property
    def total_cost(self) -> float:
        """Total cost including commission"""
        base_cost = self.filled_quantity * self.average_price
        return base_cost + self.commission
    
    def update_status(self, new_status: OrderStatus):
        """Update order status with timestamp"""
        if self.status != new_status:
            self.status = new_status
            self.updated_at = datetime.now()
            
            if new_status == OrderStatus.FILLED:
                self.filled_at = datetime.now()
                self.remaining_quantity = 0.0
    
    def add_fill(self, quantity: float, price: float, commission: float = 0.0):
        """Add a partial fill to the order"""
        self.filled_quantity += quantity
        self.remaining_quantity = max(0, self.quantity - self.filled_quantity)
        self.last_executed_quantity = quantity
        self.last_executed_price = price
        
        # Update average price
        if self.filled_quantity > 0:
            total_value = (self.average_price * (self.filled_quantity - quantity)) + (price * quantity)
            self.average_price = total_value / self.filled_quantity
        
        # Update commission
        self.commission += commission
        
        # Update status
        if self.remaining_quantity <= 1e-8:  # Essentially zero
            self.update_status(OrderStatus.FILLED)
        else:
            self.update_status(OrderStatus.PARTIALLY_FILLED)
        
        self.updated_at = datetime.now()
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert order to dictionary"""
        return {
            'id': self.id,
            'symbol': self.symbol,
            'side': self.side.value,
            'order_type': self.order_type.value,
            'quantity': self.quantity,
            'price': self.price,
            'stop_price': self.stop_price,
            'status': self.status.value,
            'exchange_order_id': self.exchange_order_id,
            'created_at': self.created_at.isoformat(),
            'filled_quantity': self.filled_quantity,
            'average_price': self.average_price,
            'commission': self.commission,
            'error_message': self.error_message
        }

@dataclass
class Position:
    """Position data model"""
    symbol: str
    quantity: float = 0.0
    average_price: float = 0.0
    market_value: float = 0.0
    unrealized_pnl: float = 0.0
    realized_pnl: float = 0.0
    last_updated: datetime = field(default_factory=datetime.now)
    
    @property
    def is_long(self) -> bool:
        return self.quantity > 0
    
    @property
    def is_short(self) -> bool:
        return self.quantity < 0
    
    @property
    def is_flat(self) -> bool:
        return abs(self.quantity) < 1e-8
    
    def update_position(self, trade_quantity: float, trade_price: float):
        """Update position with new trade"""
        if self.is_flat:
            # Opening new position
            self.quantity = trade_quantity
            self.average_price = trade_price
        else:
            if (self.quantity > 0 and trade_quantity > 0) or (self.quantity < 0 and trade_quantity < 0):
                # Adding to position
                total_cost = (self.quantity * self.average_price) + (trade_quantity * trade_price)
                self.quantity += trade_quantity
                self.average_price = total_cost / self.quantity if self.quantity != 0 else 0
            else:
                # Reducing or reversing position
                if abs(trade_quantity) >= abs(self.quantity):
                    # Closing and potentially reversing
                    self.realized_pnl += (trade_price - self.average_price) * self.quantity
                    remaining_quantity = trade_quantity + self.quantity  # Note: one is negative
                    
                    if abs(remaining_quantity) > 1e-8:
                        # Position reversal
                        self.quantity = remaining_quantity
                        self.average_price = trade_price
                    else:
                        # Position closed
                        self.quantity = 0.0
                        self.average_price = 0.0
                else:
                    # Partial close
                    close_quantity = -trade_quantity if self.quantity > 0 else trade_quantity
                    self.realized_pnl += (trade_price - self.average_price) * close_quantity
                    self.quantity += trade_quantity
        
        self.last_updated = datetime.now()

@dataclass 
class ExecutionReport:
    """Order execution report"""
    order_id: str
    exchange_order_id: str
    symbol: str
    side: str
    order_type: str
    status: str
    executed_quantity: float
    executed_price: float
    commission: float
    timestamp: datetime = field(default_factory=datetime.now)
    
    @classmethod
    def from_ccxt_order(cls, ccxt_order: Dict[str, Any], our_order_id: str) -> 'ExecutionReport':
        """Create execution report from CCXT order response"""
        return cls(
            order_id=our_order_id,
            exchange_order_id=ccxt_order['id'],
            symbol=ccxt_order['symbol'],
            side=ccxt_order['side'],
            order_type=ccxt_order['type'],
            status=ccxt_order['status'],
            executed_quantity=float(ccxt_order.get('filled', 0)),
            executed_price=float(ccxt_order.get('average', 0)),
            commission=sum(float(fee.get('cost', 0)) for fee in ccxt_order.get('fees', [])),
            timestamp=datetime.now()
        )

@dataclass
class SlippageReport:
    """Slippage analysis report"""
    order_id: str
    symbol: str
    side: str
    expected_price: float
    executed_price: float
    slippage_pct: float
    slippage_cost: float
    quantity: float
    timestamp: datetime = field(default_factory=datetime.now)
    
    @classmethod
    def calculate(cls, order: Order, expected_price: float) -> 'SlippageReport':
        """Calculate slippage report from order"""
        if order.average_price == 0 or expected_price == 0:
            slippage_pct = 0.0
            slippage_cost = 0.0
        else:
            if order.side == OrderSide.BUY:
                slippage_pct = (order.average_price - expected_price) / expected_price
            else:
                slippage_pct = (expected_price - order.average_price) / expected_price
            
            slippage_cost = abs(slippage_pct * expected_price * order.filled_quantity)
        
        return cls(
            order_id=order.id,
            symbol=order.symbol,
            side=order.side.value,
            expected_price=expected_price,
            executed_price=order.average_price,
            slippage_pct=slippage_pct,
            slippage_cost=slippage_cost,
            quantity=order.filled_quantity
        )

@dataclass
class Portfolio:
    """Portfolio snapshot"""
    timestamp: datetime = field(default_factory=datetime.now)
    positions: Dict[str, Position] = field(default_factory=dict)
    cash_balance: float = 0.0
    total_value: float = 0.0
    unrealized_pnl: float = 0.0
    realized_pnl: float = 0.0
    
    def add_position(self, symbol: str, position: Position):
        """Add or update position"""
        self.positions[symbol] = position
        self._recalculate_totals()
    
    def remove_position(self, symbol: str):
        """Remove position"""
        if symbol in self.positions:
            del self.positions[symbol]
            self._recalculate_totals()
    
    def _recalculate_totals(self):
        """Recalculate portfolio totals"""
        self.total_value = self.cash_balance
        self.unrealized_pnl = 0.0
        self.realized_pnl = 0.0
        
        for position in self.positions.values():
            self.total_value += position.market_value
            self.unrealized_pnl += position.unrealized_pnl
            self.realized_pnl += position.realized_pnl
