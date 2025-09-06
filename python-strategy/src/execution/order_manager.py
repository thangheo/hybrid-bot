"""
Order Execution Module using CCXT
Fixed version that handles string order types and includes take profit orders
"""
import asyncio
import ccxt.pro as ccxt
import time
import logging
from datetime import datetime, timedelta
from enum import Enum
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple, Union
import uuid

logger = logging.getLogger(__name__)

class OrderStatus(Enum):
    PENDING = "pending"
    SUBMITTED = "submitted"
    FILLED = "filled"
    PARTIALLY_FILLED = "partially_filled"
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

@dataclass
class Order:
    id: str = field(default_factory=lambda: str(uuid.uuid4()))
    symbol: str = ""
    side: Union[OrderSide, str] = OrderSide.BUY
    order_type: Union[OrderType, str] = OrderType.MARKET
    quantity: float = 0.0
    price: Optional[float] = None
    stop_price: Optional[float] = None
    status: OrderStatus = OrderStatus.PENDING
    exchange_order_id: Optional[str] = None
    created_at: datetime = field(default_factory=datetime.now)
    filled_quantity: float = 0.0
    average_price: float = 0.0
    commission: float = 0.0
    error_message: Optional[str] = None

class OrderManager:
    """Simple order manager using CCXT"""
    
    def __init__(self, config: dict):
        self.config = config
        
        # Initialize CCXT exchange
        self.exchange = ccxt.binance({
            'apiKey': config['binance']['api_key'],
            'secret': config['binance']['api_secret'],
            'sandbox': config['binance'].get('testnet', True),
            'enableRateLimit': True,
            'options': {
                'defaultType': 'spot',  # spot trading
            }
        })
        
        # Order tracking
        self.active_orders: Dict[str, Order] = {}
        self.order_history: List[Order] = []
        
        # Settings
        self.max_slippage = config.get('max_slippage', 0.005)  # 0.5%
        self.order_timeout = config.get('order_timeout', 300)  # 5 minutes
        self.retry_attempts = config.get('retry_attempts', 3)
        
        # Position tracking
        self.positions: Dict[str, float] = {}  # symbol -> quantity
    
    def _normalize_to_ccxt_string(self, value: Union[str, Enum], enum_class: type = None) -> str:
        """Convert enum to lowercase string as expected by CCXT"""
        if isinstance(value, Enum):
            return value.value.lower()
        elif isinstance(value, str):
            return value.lower()
        else:
            return str(value).lower()
    
    async def _execute_market_order(self, order: Order) -> Tuple[bool, str]:
        """Execute market order using CCXT"""
        try:
            # Get current price for slippage monitoring
            ticker = await self.exchange.fetch_ticker(order.symbol)
            expected_price = ticker['bid'] if self._normalize_to_ccxt_string(order.side) == 'sell' else ticker['ask']
            
            # CCXT expects lowercase strings
            side_value = self._normalize_to_ccxt_string(order.side)
            
            result = await self.exchange.create_market_order(
                symbol=order.symbol,
                side=side_value,
                amount=order.quantity
            )
            
            # Update order with result
            order.exchange_order_id = result['id']
            order.status = OrderStatus.FILLED if result['status'] == 'closed' else OrderStatus.SUBMITTED
            order.filled_quantity = float(result['filled'])
            order.average_price = float(result['average']) if result['average'] else 0.0
            order.commission = self._calculate_commission(result)
            
            # Check slippage
            if order.average_price > 0:
                slippage = self._calculate_slippage(expected_price, order.average_price, order.side)
                if abs(slippage) > self.max_slippage:
                    logger.warning(f"High slippage detected: {slippage:.4f} for {order.symbol}")
            
            return True, f"Market order executed at {order.average_price}"
            
        except Exception as e:
            return False, f"Market order failed: {str(e)}"
    
    async def _execute_limit_order(self, order: Order) -> Tuple[bool, str]:
        """Execute limit order using CCXT"""
        try:
            if not order.price:
                return False, "Limit order requires price"
            
            # CCXT expects lowercase strings
            side_value = self._normalize_to_ccxt_string(order.side)
            
            result = await self.exchange.create_limit_order(
                symbol=order.symbol,
                side=side_value,
                amount=order.quantity,
                price=order.price
            )
            
            order.exchange_order_id = result['id']
            order.status = OrderStatus.SUBMITTED

            return True, f"Limit order placed at {order.price}"

        except Exception as e:
            return False, f"Limit order failed: {str(e)}"
    
    async def _execute_stop_order(self, order: Order) -> Tuple[bool, str]:
        """Execute stop order using CCXT"""
        try:
            if not order.stop_price:
                return False, "Stop order requires stop price"
            
            # CCXT expects lowercase strings
            side_value = self._normalize_to_ccxt_string(order.side)
            
            # Use stop_loss_limit for more control
            result = await self.exchange.create_order(
                symbol=order.symbol,
                type='stop_loss_limit',
                side=side_value,
                amount=order.quantity,
                price=order.price or order.stop_price,  # limit price
                params={'stopPrice': order.stop_price}
            )
            
            order.exchange_order_id = result['id']
            order.status = OrderStatus.SUBMITTED
            
            return True, f"Stop order placed at {order.stop_price}"
            
        except Exception as e:
            return False, f"Stop order failed: {str(e)}"
    
    async def _execute_take_profit_order(self, order: Order) -> Tuple[bool, str]:
        """Execute take profit order using CCXT"""
        try:
            if not order.price:
                return False, "Take profit order requires price"
            
            # CCXT expects lowercase strings
            side_value = self._normalize_to_ccxt_string(order.side)
            
            # Use take_profit_limit for more control
            # Note: Take profit is essentially a limit order that triggers at a better price
            result = await self.exchange.create_order(
                symbol=order.symbol,
                type='take_profit_limit',
                side=side_value,
                amount=order.quantity,
                price=order.price,  # limit price
                params={'stopPrice': order.price}  # trigger price same as limit price
            )
            
            order.exchange_order_id = result['id']
            order.status = OrderStatus.SUBMITTED

            return True, f"Take profit order placed at {order.price}"

        except Exception as e:
            # Fallback to regular limit order if take_profit_limit not supported
            logger.warning(f"Take profit order type not supported, using limit order: {e}")
            return await self._execute_limit_order(order)
    
    async def cancel_order(self, order_id: str) -> Tuple[bool, str]:
        """Cancel active order"""
        if order_id not in self.active_orders:
            return False, "Order not found"
        
        order = self.active_orders[order_id]
        
        try:
            if order.exchange_order_id:
                await self.exchange.cancel_order(order.exchange_order_id, order.symbol)
                order.status = OrderStatus.CANCELLED
                self._move_to_history(order_id)
                return True, "Order cancelled successfully"
            else:
                return False, "No exchange order ID found"
                
        except Exception as e:
            logger.error(f"Order cancellation failed: {e}")
            return False, str(e)
    
    async def get_order_status(self, order_id: str) -> Optional[Order]:
        """Get current order status"""
        if order_id not in self.active_orders:
            return None
        
        order = self.active_orders[order_id]
        
        try:
            if order.exchange_order_id:
                result = await self.exchange.fetch_order(order.exchange_order_id, order.symbol)
                
                # Update order status
                old_status = order.status
                order.status = self._map_exchange_status(result['status'])
                order.filled_quantity = float(result['filled'])
                
                if result['average']:
                    order.average_price = float(result['average'])
                
                # Move to history if completed
                if order.status in [OrderStatus.FILLED, OrderStatus.CANCELLED, OrderStatus.REJECTED]:
                    self._move_to_history(order_id)
                
                if old_status != order.status:
                    logger.info(f"Order {order_id} status changed: {old_status} -> {order.status}")
            
            return order
            
        except Exception as e:
            logger.error(f"Failed to get order status: {e}")
            return order
    
    async def get_positions(self) -> Dict[str, float]:
        """Get current positions from exchange"""
        try:
            balance = await self.exchange.fetch_balance()
            positions = {}
            
            for asset, data in balance.items():
                if asset == 'info':  # Skip metadata
                    continue
                    
                total = data.get('total', 0)
                if isinstance(total, (int, float)) and total > 0 and asset != 'USDT':  # Skip base currency
                    symbol = f"{asset}/USDT"
                    positions[symbol] = total
            
            # Update local positions
            self.positions = positions
            return positions
            
        except Exception as e:
            logger.error(f"Failed to get positions: {e}")
            return self.positions
    
    def _validate_order(self, order: Order) -> bool:
        """Basic order validation - keep values as strings for CCXT"""
        if not order.symbol or order.quantity <= 0:
            return False
        
        # Convert order_type to lowercase string for CCXT compliance
        order_type_str = self._normalize_to_ccxt_string(order.order_type)

        if order_type_str == 'limit' and not order.price:
            return False
        
        if order_type_str == 'stop_loss' and not order.stop_price:
            return False
        
        if order_type_str == 'take_profit' and not order.price:
            return False
        
        return True
    
    def _normalize_symbol(self, symbol: str) -> str:
        """Normalize symbol format to CCXT standard (BASE/QUOTE)"""
        # Remove any existing slashes and standardize
        clean_symbol = symbol.replace('/', '').upper()
        
        # Handle common patterns
        if clean_symbol.endswith('USDT'):
            base = clean_symbol[:-4]  # Remove 'USDT' 
            return f"{base}/USDT"
        elif clean_symbol.endswith('BTC'):
            base = clean_symbol[:-3]  # Remove 'BTC'
            return f"{base}/BTC"
        elif clean_symbol.endswith('ETH'):
            base = clean_symbol[:-3]  # Remove 'ETH'
            return f"{base}/ETH"
        else:
            # If already in correct format or unknown pattern, return as is
            if '/' in symbol:
                return symbol.upper()
            else:
                # Default to USDT pair
                return f"{clean_symbol}/USDT"

    async def _check_trading_permissions(self, symbol: str) -> bool:
        """Check if trading is allowed for symbol with better error handling"""
        try:
            # Normalize symbol format first
            normalized_symbol = self._normalize_symbol(symbol)
            
            markets = await self.exchange.load_markets()
            
            if not markets:
                logger.warning("No markets loaded, allowing trade (will fail later if invalid)")
                return True  # Fail gracefully - let the exchange reject if needed
            
            if normalized_symbol not in markets:
                logger.error(f"Symbol {normalized_symbol} not found in markets")
                logger.debug(f"Available symbols include: {list(markets.keys())[:10]}...")
                return False
            
            market = markets[normalized_symbol]
            
            # Check if market is active
            is_active = market.get('active', True)  # Default to True if not specified
            
            # Additional checks
            is_spot = market.get('spot', True)  # Ensure it's a spot market
            
            if not is_active:
                logger.warning(f"Market {normalized_symbol} is not active")
                return False
                
            if not is_spot:
                logger.warning(f"Market {normalized_symbol} is not a spot market")
                return False
            
            logger.debug(f"Trading permission granted for {normalized_symbol}")
            return True
            
        except Exception as e:
            logger.error(f"Error checking trading permissions for {symbol}: {e}")
            # Be permissive on error - let the exchange decide
            return True

    async def place_order(self, order: Order) -> Tuple[bool, str]:
        """Place order using CCXT - accepts strings or enums, converts to CCXT format"""
        try:
            # Validate order
            if not self._validate_order(order):
                return False, "Order validation failed"
            
            # Normalize symbol format before checking permissions
            normalized_symbol = self._normalize_symbol(order.symbol)
            order.symbol = normalized_symbol  # Update order with normalized symbol
            
            # Check trading permissions
            if not await self._check_trading_permissions(normalized_symbol):
                return False, f"Trading not permitted for {normalized_symbol}"
            
            # Convert order type to lowercase string for CCXT
            order_type_str = self._normalize_to_ccxt_string(order.order_type)
            
            # Execute order based on type (using string comparison)
            if order_type_str == 'market':
                success, message = await self._execute_market_order(order)
            elif order_type_str == 'limit':
                success, message = await self._execute_limit_order(order)
            elif order_type_str == 'stop_loss':
                success, message = await self._execute_stop_order(order)
            elif order_type_str == 'take_profit':
                success, message = await self._execute_take_profit_order(order)
            else:
                return False, f"Unsupported order type: {order_type_str}"
            
            if success:
                self.active_orders[order.id] = order
                self._update_position(order)
                logger.info(f"Order {order.id} placed successfully: {message}")
            else:
                order.status = OrderStatus.REJECTED
                order.error_message = message
                logger.error(f"Order {order.id} failed: {message}")
            
            return success, message
            
        except Exception as e:
            logger.error(f"Order execution failed: {e}")
            order.status = OrderStatus.REJECTED
            order.error_message = str(e)
            return False, str(e)
        
    def _update_position(self, order: Order):
        """Update position tracking"""
        if order.status not in [OrderStatus.FILLED, OrderStatus.PARTIALLY_FILLED]:
            return
        
        current_position = self.positions.get(order.symbol, 0.0)
        quantity_change = order.filled_quantity
        
        # Use string comparison for side
        side_str = self._normalize_to_ccxt_string(order.side)
        if side_str == "sell":
            quantity_change = -quantity_change
        
        new_position = current_position + quantity_change
        
        if abs(new_position) < 1e-8:  # Close to zero
            self.positions.pop(order.symbol, None)
        else:
            self.positions[order.symbol] = new_position
        
        logger.info(f"Position updated: {order.symbol} = {new_position:.6f}")
    
    def _calculate_slippage(self, expected_price: float, actual_price: float, side: Union[OrderSide, str]) -> float:
        """Calculate slippage percentage"""
        # Convert to lowercase string for consistent comparison
        side_str = self._normalize_to_ccxt_string(side)
        
        if side_str == "buy":
            return (actual_price - expected_price) / expected_price
        else:
            return (expected_price - actual_price) / expected_price
    
    def _calculate_commission(self, result: dict) -> float:
        """Calculate commission from order result"""
        fees = result.get('fees', [])
        total_commission = 0.0
        
        for fee in fees:
            if fee.get('currency') == 'USDT':
                total_commission += float(fee.get('cost', 0))
        
        return total_commission
    
    def _map_exchange_status(self, exchange_status: str) -> OrderStatus:
        """Map exchange status to our OrderStatus"""
        status_map = {
            'open': OrderStatus.SUBMITTED,
            'closed': OrderStatus.FILLED,
            'canceled': OrderStatus.CANCELLED,
            'cancelled': OrderStatus.CANCELLED,
            'rejected': OrderStatus.REJECTED,
            'expired': OrderStatus.EXPIRED,
        }
        return status_map.get(exchange_status.lower(), OrderStatus.SUBMITTED)
    
    def _move_to_history(self, order_id: str):
        """Move order from active to history"""
        if order_id in self.active_orders:
            order = self.active_orders.pop(order_id)
            self.order_history.append(order)
            
            # Keep only recent history (last 1000 orders)
            if len(self.order_history) > 1000:
                self.order_history = self.order_history[-1000:]
    
    def get_order_history(self, symbol: str = None, days: int = 30) -> List[Order]:
        """Get order history"""
        cutoff_date = datetime.now() - timedelta(days=days)
        
        filtered_orders = [
            order for order in self.order_history
            if order.created_at >= cutoff_date
            and (symbol is None or order.symbol == symbol)
        ]
        
        return sorted(filtered_orders, key=lambda x: x.created_at, reverse=True)


# Convenience function to create orders with string types
def create_order(symbol: str, side: str, order_type: str, quantity: float, 
                price: Optional[float] = None, stop_price: Optional[float] = None) -> Order:
    """Create an order with string parameters for convenience"""
    return Order(
        symbol=symbol,
        side=side,  # Will be normalized in validation
        order_type=order_type,  # Will be normalized in validation
        quantity=quantity,
        price=price,
        stop_price=stop_price
    )


class PositionTracker:
    """Simple position tracking"""
    
    def __init__(self, order_manager: OrderManager):
        self.order_manager = order_manager
        self.positions: Dict[str, float] = {}
    
    async def reconcile_positions(self) -> Dict[str, float]:
        """Reconcile local positions with exchange"""
        try:
            exchange_positions = await self.order_manager.get_positions()
            
            # Compare with local positions
            discrepancies = {}
            all_symbols = set(list(self.positions.keys()) + list(exchange_positions.keys()))
            
            for symbol in all_symbols:
                local_pos = self.positions.get(symbol, 0.0)
                exchange_pos = exchange_positions.get(symbol, 0.0)
                
                if abs(local_pos - exchange_pos) > 1e-6:
                    discrepancies[symbol] = {
                        'local': local_pos,
                        'exchange': exchange_pos,
                        'difference': local_pos - exchange_pos
                    }
            
            if discrepancies:
                logger.warning(f"Position discrepancies found: {discrepancies}")
                # Update local positions to match exchange
                self.positions = exchange_positions.copy()
            
            return discrepancies
            
        except Exception as e:
            logger.error(f"Position reconciliation failed: {e}")
            return {}
    
    def get_position(self, symbol: str) -> float:
        """Get position for symbol"""
        return self.positions.get(symbol, 0.0)
    
    def get_portfolio_value(self) -> Dict[str, float]:
        """Get portfolio value in USDT"""
        # This would need market prices to calculate properly
        # For now, just return positions
        return self.positions.copy()