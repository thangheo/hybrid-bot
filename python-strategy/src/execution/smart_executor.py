"""
Smart Order Execution Strategies
Uses CCXT for actual execution with intelligent routing
"""
import asyncio
import ccxt.pro as ccxt
import logging
from datetime import datetime, timedelta
from typing import Dict, List, Optional, Tuple
from models import Order, OrderType, OrderSide, OrderStatus

logger = logging.getLogger(__name__)

class SmartOrderExecutor:
    """Intelligent order execution with TWAP, Iceberg, and adaptive strategies"""
    
    def __init__(self, exchange: ccxt.Exchange, config: dict):
        self.exchange = exchange
        self.config = config
        
        # Execution parameters
        self.large_order_threshold = config.get('large_order_threshold', 0.05)  # 5% of daily volume
        self.twap_chunks = config.get('twap_chunks', 10)
        self.twap_interval = config.get('twap_interval', 30)  # seconds
        self.iceberg_chunk_size = config.get('iceberg_chunk_size', 0.1)  # 10% of order
        self.max_slippage = config.get('max_slippage', 0.005)  # 0.5%
        
        # Market data cache
        self._market_cache = {}
        self._cache_expiry = {}
    
    async def execute_order(self, order: Order) -> Tuple[bool, str]:
        """Execute order with smart routing"""
        try:
            # Determine execution strategy
            strategy = await self._determine_execution_strategy(order)
            logger.info(f"Using execution strategy: {strategy} for order {order.id}")
            
            # Execute based on strategy
            if strategy == "direct":
                return await self._execute_direct(order)
            elif strategy == "twap":
                return await self._execute_twap(order)
            elif strategy == "iceberg":
                return await self._execute_iceberg(order)
            elif strategy == "adaptive_limit":
                return await self._execute_adaptive_limit(order)
            else:
                return await self._execute_direct(order)  # fallback
                
        except Exception as e:
            logger.error(f"Smart execution failed for order {order.id}: {e}")
            return False, str(e)
    
    async def _determine_execution_strategy(self, order: Order) -> str:
        """Determine best execution strategy based on market conditions"""
        try:
            # Get market data
            market_data = await self._get_market_data(order.symbol)
            
            # Calculate order impact
            daily_volume = market_data.get('volume_24h', 0)
            order_value = order.getQuantity() * market_data.get('last_price', 0)
            
            if daily_volume > 0:
                volume_ratio = order_value / daily_volume
            else:
                volume_ratio = 0
            
            spread_pct = market_data.get('spread_pct', 0)
            volatility = market_data.get('volatility', 0.02)
            
            # Decision logic
            if volume_ratio > self.large_order_threshold:
                # Large order - use TWAP for low volatility, Iceberg for high
                return "twap" if volatility < 0.03 else "iceberg"
            elif spread_pct > 0.002:  # Wide spread > 0.2%
                return "adaptive_limit"
            elif order.order_type == OrderType.MARKET:
                return "direct"
            else:
                return "direct"
                
        except Exception as e:
            logger.warning(f"Failed to determine strategy: {e}")
            return "direct"
    
    async def _execute_direct(self, order: Order) -> Tuple[bool, str]:
        """Direct execution using CCXT"""
        try:
            if order.order_type == OrderType.MARKET:
                result = await self.exchange.create_market_order(
                    symbol=order.symbol,
                    side=order.side.value,
                    amount=order.getQuantity()
                )
            elif order.order_type == OrderType.LIMIT:
                result = await self.exchange.create_limit_order(
                    symbol=order.symbol,
                    side=order.side.value,
                    amount=order.getQuantity(),
                    price=order.getPrice()
                )
            else:
                return False, f"Unsupported order type: {order.order_type}"
            
            # Update order with result
            order.exchange_order_id = result['id']
            order.status = OrderStatus.FILLED if result['status'] == 'closed' else OrderStatus.SUBMITTED
            order.filled_quantity = float(result.get('filled', 0))
            order.average_price = float(result.get('average', 0)) if result.get('average') else 0
            
            return True, f"Direct execution completed: {result['status']}"
            
        except Exception as e:
            return False, f"Direct execution failed: {str(e)}"
    
    async def _execute_twap(self, order: Order) -> Tuple[bool, str]:
        """Time-Weighted Average Price execution"""
        try:
            chunk_size = order.getQuantity() / self.twap_chunks
            filled_quantity = 0.0
            total_cost = 0.0
            filled_chunks = 0
            
            logger.info(f"Starting TWAP execution: {self.twap_chunks} chunks of {chunk_size:.6f}")
            
            for i in range(self.twap_chunks):
                try:
                    # Create chunk order
                    chunk_result = await self.exchange.create_market_order(
                        symbol=order.symbol,
                        side=order.side.value,
                        amount=chunk_size
                    )
                    
                    if chunk_result['status'] == 'closed':
                        chunk_filled = float(chunk_result['filled'])
                        chunk_price = float(chunk_result['average'])
                        
                        filled_quantity += chunk_filled
                        total_cost += chunk_filled * chunk_price
                        filled_chunks += 1
                        
                        logger.debug(f"TWAP chunk {i+1}/{self.twap_chunks} filled: {chunk_filled} at {chunk_price}")
                    
                    # Wait before next chunk (except for last one)
                    if i < self.twap_chunks - 1:
                        await asyncio.sleep(self.twap_interval)
                        
                except Exception as e:
                    logger.warning(f"TWAP chunk {i+1} failed: {e}")
                    continue
            
            # Update order
            order.filled_quantity = filled_quantity
            order.average_price = total_cost / filled_quantity if filled_quantity > 0 else 0
            order.status = OrderStatus.FILLED if filled_quantity >= order.getQuantity() * 0.95 else OrderStatus.PARTIALLY_FILLED
            
            success_rate = filled_chunks / self.twap_chunks
            return True, f"TWAP completed: {filled_chunks}/{self.twap_chunks} chunks filled ({success_rate:.1%})"
            
        except Exception as e:
            return False, f"TWAP execution failed: {str(e)}"
    
    async def _execute_iceberg(self, order: Order) -> Tuple[bool, str]:
        """Iceberg order execution - hide order size"""
        try:
            chunk_size = order.getQuantity() * self.iceberg_chunk_size
            remaining_quantity = order.getQuantity()
            filled_quantity = 0.0
            total_cost = 0.0
            chunk_count = 0
            
            logger.info(f"Starting Iceberg execution: chunk size {chunk_size:.6f}")
            
            while remaining_quantity > chunk_size * 0.1:  # Continue until small remainder
                try:
                    # Determine current chunk size
                    current_chunk = min(chunk_size, remaining_quantity)
                    
                    # Place limit order slightly better than market
                    market_data = await self._get_market_data(order.symbol)
                    
                    if order.side == OrderSide.BUY:
                        # Buy slightly above bid
                        chunk_price = market_data['bid'] * 1.0001
                    else:
                        # Sell slightly below ask
                        chunk_price = market_data['ask'] * 0.9999
                    
                    chunk_result = await self.exchange.create_limit_order(
                        symbol=order.symbol,
                        side=order.side.value,
                        amount=current_chunk,
                        price=chunk_price
                    )
                    
                    # Wait for fill with timeout
                    fill_timeout = 60  # 1 minute per chunk
                    start_time = datetime.now()
                    
                    while (datetime.now() - start_time).seconds < fill_timeout:
                        chunk_status = await self.exchange.fetch_order(chunk_result['id'], order.symbol)
                        
                        if chunk_status['status'] == 'closed':
                            chunk_filled = float(chunk_status['filled'])
                            chunk_price_actual = float(chunk_status['average'])
                            
                            filled_quantity += chunk_filled
                            total_cost += chunk_filled * chunk_price_actual
                            remaining_quantity -= chunk_filled
                            chunk_count += 1
                            
                            logger.debug(f"Iceberg chunk {chunk_count} filled: {chunk_filled} at {chunk_price_actual}")
                            break
                        
                        await asyncio.sleep(5)  # Check every 5 seconds
                    else:
                        # Timeout - cancel order
                        try:
                            await self.exchange.cancel_order(chunk_result['id'], order.symbol)
                            logger.warning(f"Iceberg chunk {chunk_count + 1} timed out and cancelled")
                        except:
                            pass
                        break
                    
                    # Random delay between chunks to avoid detection
                    await asyncio.sleep(5 + (chunk_count % 10))  # 5-15 seconds
                    
                except Exception as e:
                    logger.warning(f"Iceberg chunk {chunk_count + 1} failed: {e}")
                    break
            
            # Update order
            order.filled_quantity = filled_quantity
            order.average_price = total_cost / filled_quantity if filled_quantity > 0 else 0
            order.status = OrderStatus.FILLED if filled_quantity >= order.getQuantity() * 0.95 else OrderStatus.PARTIALLY_FILLED
            
            fill_rate = filled_quantity / order.getQuantity() if order.getQuantity() > 0 else 0
            return True, f"Iceberg completed: {chunk_count} chunks, {fill_rate:.1%} filled"
            
        except Exception as e:
            return False, f"Iceberg execution failed: {str(e)}"
    
    async def _execute_adaptive_limit(self, order: Order) -> Tuple[bool, str]:
        """Adaptive limit order with price improvement"""
        try:
            if not order.getPrice():
                return False, "Adaptive limit requires initial price"
            
            max_attempts = 5
            price_increment = 0.0001  # 0.01%
            current_price = order.getPrice()
            
            for attempt in range(max_attempts):
                try:
                    # Place limit order
                    result = await self.exchange.create_limit_order(
                        symbol=order.symbol,
                        side=order.side.value,
                        amount=order.getQuantity(),
                        price=current_price
                    )
                    
                    order.exchange_order_id = result['id']
                    
                    # Wait for partial or full fill
                    wait_time = 30  # 30 seconds per attempt
                    start_time = datetime.now()
                    
                    while (datetime.now() - start_time).seconds < wait_time:
                        status = await self.exchange.fetch_order(result['id'], order.symbol)
                        
                        if status['status'] == 'closed':
                            # Fully filled
                            order.status = OrderStatus.FILLED
                            order.filled_quantity = float(status['filled'])
                            order.average_price = float(status['average'])
                            return True, f"Adaptive limit filled at {order.average_price}"
                        
                        elif float(status['filled']) > 0:
                            # Partially filled - keep this price
                            order.status = OrderStatus.PARTIALLY_FILLED
                            order.filled_quantity = float(status['filled'])
                            order.average_price = float(status['average'])
                            return True, f"Adaptive limit partially filled: {order.filled_quantity}"
                        
                        await asyncio.sleep(5)
                    
                    # Not filled - cancel and try better price
                    await self.exchange.cancel_order(result['id'], order.symbol)
                    
                    # Improve price
                    if order.side == OrderSide.BUY:
                        current_price *= (1 + price_increment)
                    else:
                        current_price *= (1 - price_increment)
                    
                    logger.debug(f"Adaptive limit attempt {attempt + 1} failed, trying price {current_price}")
                    
                except Exception as e:
                    logger.warning(f"Adaptive limit attempt {attempt + 1} error: {e}")
                    continue
            
            # All attempts failed
            order.status = OrderStatus.REJECTED
            return False, "Adaptive limit failed after all attempts"
            
        except Exception as e:
            return False, f"Adaptive limit execution failed: {str(e)}"
    
    async def _get_market_data(self, symbol: str) -> Dict:
        """Get cached market data"""
        now = datetime.now()
        
        # Check cache
        if symbol in self._market_cache and symbol in self._cache_expiry:
            if now < self._cache_expiry[symbol]:
                return self._market_cache[symbol]
        
        try:
            # Fetch fresh data
            ticker = await self.exchange.fetch_ticker(symbol)
            orderbook = await self.exchange.fetch_order_book(symbol, limit=5)
            
            # Calculate metrics
            bid = orderbook['bids'][0][0] if orderbook['bids'] else ticker['bid']
            ask = orderbook['asks'][0][0] if orderbook['asks'] else ticker['ask']
            spread = ask - bid
            spread_pct = spread / ((bid + ask) / 2) if bid and ask else 0
            
            # Simple volatility estimate from 24h data
            volatility = (ticker['high'] - ticker['low']) / ticker['average'] if ticker['average'] else 0.02
            
            market_data = {
                'last_price': ticker['last'],
                'bid': bid,
                'ask': ask,
                'spread': spread,
                'spread_pct': spread_pct,
                'volume_24h': ticker['quoteVolume'],
                'volatility': volatility
            }
            
            # Cache for 30 seconds
            self._market_cache[symbol] = market_data
            self._cache_expiry[symbol] = now + timedelta(seconds=30)
            
            return market_data
            
        except Exception as e:
            logger.error(f"Failed to get market data for {symbol}: {e}")
            # Return default values
            return {
                'last_price': 0,
                'bid': 0,
                'ask': 0,
                'spread': 0,
                'spread_pct': 0.001,
                'volume_24h': 0,
                'volatility': 0.02
            }

class OrderStatusMonitor:
    """Monitor and update order status using CCXT"""
    
    def __init__(self, exchange: ccxt.Exchange, order_manager):
        self.exchange = exchange
        self.order_manager = order_manager
        self.monitoring = False
        self.check_interval = 30  # seconds
    
    async def start_monitoring(self):
        """Start order status monitoring"""
        self.monitoring = True
        logger.info("Starting order status monitoring")
        
        while self.monitoring:
            try:
                await self._check_all_orders()
                await asyncio.sleep(self.check_interval)
            except Exception as e:
                logger.error(f"Order monitoring error: {e}")
                await asyncio.sleep(5)  # Short delay on error
    
    def stop_monitoring(self):
        """Stop order status monitoring"""
        self.monitoring = False
        logger.info("Stopping order status monitoring")
    
    async def _check_all_orders(self):
        """Check status of all active orders"""
        active_orders = list(self.order_manager.active_orders.values())
        
        for order in active_orders:
            if order.exchange_order_id:
                await self._update_order_status(order)
    
    async def _update_order_status(self, order: Order):
        """Update single order status"""
        try:
            result = await self.exchange.fetch_order(order.exchange_order_id, order.symbol)
            
            # Update order from exchange data
            old_status = order.status
            new_status = self._map_exchange_status(result['status'])
            
            if old_status != new_status:
                order.status = new_status
                logger.info(f"Order {order.id} status changed: {old_status} -> {new_status}")
            
            # Update quantities and prices
            order.filled_quantity = float(result.get('filled', 0))
            if result.get('average'):
                order.average_price = float(result['average'])
            
            # Move to history if completed
            if order.is_complete:
                self.order_manager._move_to_history(order.id)
                
        except Exception as e:
            logger.error(f"Failed to update order {order.id}: {e}")
    
    def _map_exchange_status(self, exchange_status: str) -> OrderStatus:
        """Map exchange status to our OrderStatus enum"""
        status_map = {
            'open': OrderStatus.SUBMITTED,
            'closed': OrderStatus.FILLED,
            'canceled': OrderStatus.CANCELLED,
            'cancelled': OrderStatus.CANCELLED,
            'rejected': OrderStatus.REJECTED,
            'expired': OrderStatus.EXPIRED,
        }
        return status_map.get(exchange_status.lower(), OrderStatus.SUBMITTED)
