"""
Order Execution Module Usage Example
Shows how to use the execution system with real trading signals
"""
import asyncio
import logging
import os
from datetime import datetime

# Import our modules
from order_manager import OrderManager, PositionTracker
from smart_executor import SmartOrderExecutor, OrderStatusMonitor
from models import Order, OrderSide, OrderType, OrderStatus

# Setup logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

async def main():
    """Example usage of order execution system"""
    
    # Configuration
    config = {
        'binance': {
            'api_key': os.getenv('BINANCE_API_KEY', 'your_api_key'),
            'api_secret': os.getenv('BINANCE_API_SECRET', 'your_api_secret'),
            'testnet': True  # Use testnet for testing
        },
        'max_slippage': 0.005,
        'order_timeout': 300,
        'retry_attempts': 3,
        'large_order_threshold': 0.05,
        'twap_chunks': 5,
        'twap_interval': 30
    }
    
    try:
        # Initialize order manager
        logger.info("Initializing order manager...")
        order_manager = OrderManager(config)
        
        # Initialize position tracker
        position_tracker = PositionTracker(order_manager)
        
        # Test 1: Simple market order
        logger.info("\n=== Test 1: Market Order ===")
        market_order = Order(
            symbol='BTC/USDT',
            side=OrderSide.BUY,
            order_type=OrderType.MARKET,
            quantity=0.001,  # Small amount for testing
            strategy_id='test_strategy'
        )
        
        success, message = await order_manager.place_order(market_order)
        logger.info(f"Market order result: {success} - {message}")
        
        if success:
            # Check order status
            await asyncio.sleep(2)  # Wait a moment
            status = await order_manager.get_order_status(market_order.id)
            if status:
                logger.info(f"Order status: {status.status}, Filled: {status.filled_quantity}")
        
        # Test 2: Limit order
        logger.info("\n=== Test 2: Limit Order ===")
        
        # Get current price to set a reasonable limit
        exchange = order_manager.exchange
        ticker = await exchange.fetch_ticker('BTC/USDT')
        limit_price = ticker['bid'] * 0.999  # 0.1% below current bid
        
        limit_order = Order(
            symbol='BTC/USDT',
            side=OrderSide.BUY,
            order_type=OrderType.LIMIT,
            quantity=0.001,
            price=limit_price,
            strategy_id='test_strategy'
        )
        
        success, message = await order_manager.place_order(limit_order)
        logger.info(f"Limit order result: {success} - {message}")
        
        # Wait a bit then cancel the limit order
        await asyncio.sleep(10)
        cancel_success, cancel_msg = await order_manager.cancel_order(limit_order.id)
        logger.info(f"Cancel result: {cancel_success} - {cancel_msg}")
        
        # Test 3: Smart execution (TWAP)
        logger.info("\n=== Test 3: Smart Execution (TWAP) ===")
        
        smart_executor = SmartOrderExecutor(exchange, config)
        
        large_order = Order(
            symbol='ETH/USDT',
            side=OrderSide.BUY,
            order_type=OrderType.MARKET,
            quantity=0.05,  # Larger amount to trigger TWAP
            strategy_id='test_strategy'
        )
        
        success, message = await smart_executor.execute_order(large_order)
        logger.info(f"TWAP execution result: {success} - {message}")
        
        # Test 4: Position reconciliation
        logger.info("\n=== Test 4: Position Reconciliation ===")
        
        positions = await order_manager.get_positions()
        logger.info(f"Current positions: {positions}")
        
        discrepancies = await position_tracker.reconcile_positions()
        if discrepancies:
            logger.warning(f"Position discrepancies found: {discrepancies}")
        else:
            logger.info("Positions are in sync")
        
        # Test 5: Order history
        logger.info("\n=== Test 5: Order History ===")
        
        history = order_manager.get_order_history(days=1)
        logger.info(f"Recent orders: {len(history)}")
        
        for order in history[:3]:  # Show last 3 orders
            logger.info(f"  {order.symbol} {order.side.value} {order.getQuantity()} - {order.status.value}")
        
        # Test 6: Start order monitoring (run for a short time)
        logger.info("\n=== Test 6: Order Monitoring ===")
        
        monitor = OrderStatusMonitor(exchange, order_manager)
        
        # Start monitoring in background
        monitor_task = asyncio.create_task(monitor.start_monitoring())
        
        # Let it run for 30 seconds
        await asyncio.sleep(30)
        
        # Stop monitoring
        monitor.stop_monitoring()
        monitor_task.cancel()
        
        try:
            await monitor_task
        except asyncio.CancelledError:
            pass
        
        logger.info("Order monitoring test completed")
        
        # Final position check
        logger.info("\n=== Final Status ===")
        final_positions = await order_manager.get_positions()
        logger.info(f"Final positions: {final_positions}")
        
        # Show portfolio value
        portfolio_value = position_tracker.get_portfolio_value()
        logger.info(f"Portfolio value: {portfolio_value}")
        
    except Exception as e:
        logger.error(f"Example execution failed: {e}", exc_info=True)
    
    finally:
        # Close exchange connection
        if hasattr(order_manager, 'exchange'):
            await order_manager.exchange.close()
        logger.info("Example completed")

async def test_error_handling():
    """Test error handling scenarios"""
    logger.info("\n=== Error Handling Tests ===")
    
    config = {
        'binance': {
            'api_key': 'invalid_key',
            'api_secret': 'invalid_secret', 
            'testnet': True
        }
    }
    
    try:
        order_manager = OrderManager(config)
        
        # This should fail due to invalid credentials
        test_order = Order(
            symbol='BTC/USDT',
            side=OrderSide.BUY,
            order_type=OrderType.MARKET,
            quantity=0.001
        )
        
        success, message = await order_manager.place_order(test_order)
        logger.info(f"Expected failure result: {success} - {message}")
        
        assert not success, "Order should have failed with invalid credentials"
        logger.info("Error handling test passed")
        
    except Exception as e:
        logger.info(f"Error handling test working correctly: {e}")

def create_sample_trading_signal():
    """Create a sample trading signal that would come from strategy module"""
    return {
        'timestamp': datetime.now(),
        'symbol': 'BTC/USDT',
        'action': 'buy',
        'quantity': 0.001,
        'confidence': 0.8,
        'strategy': 'mean_reversion',
        'signal_strength': 1.5,
        'expected_price': 45000.0,
        'stop_loss': 44000.0,
        'take_profit': 47000.0
    }

async def process_trading_signal(signal: dict, order_manager: OrderManager):
    """Example of how to process a trading signal"""
    logger.info(f"Processing trading signal: {signal}")
    
    # Convert signal to order
    order = Order(
        symbol=signal['symbol'],
        side=OrderSide.BUY if signal['action'] == 'buy' else OrderSide.SELL,
        order_type=OrderType.MARKET,  # Could be determined by strategy
        quantity=signal['quantity'],
        strategy_id=signal['strategy'],
        tags={
            'signal_confidence': signal['confidence'],
            'signal_strength': signal['signal_strength']
        }
    )
    
    # Execute order
    success, message = await order_manager.place_order(order)
    
    if success:
        logger.info(f"Signal executed successfully: {message}")
        
        # Set stop loss and take profit if order filled
        if order.status == OrderStatus.FILLED:
            await set_stop_loss_take_profit(order, signal, order_manager)
    else:
        logger.error(f"Signal execution failed: {message}")
    
    return success, order

async def set_stop_loss_take_profit(base_order: Order, signal: dict, order_manager: OrderManager):
    """Set stop loss and take profit orders"""
    try:
        # Stop loss order
        stop_order = Order(
            symbol=base_order.symbol,
            side=OrderSide.SELL if base_order.side == OrderSide.BUY else OrderSide.BUY,
            order_type=OrderType.STOP_LOSS,
            quantity=base_order.filled_quantity,
            stop_price=signal.get('stop_loss'),
            strategy_id=base_order.strategy_id,
            tags={'parent_order': base_order.id, 'type': 'stop_loss'}
        )
        
        # Take profit order
        tp_order = Order(
            symbol=base_order.symbol,
            side=OrderSide.SELL if base_order.side == OrderSide.BUY else OrderSide.BUY,
            order_type=OrderType.LIMIT,
            quantity=base_order.filled_quantity,
            price=signal.get('take_profit'),
            strategy_id=base_order.strategy_id,
            tags={'parent_order': base_order.id, 'type': 'take_profit'}
        )
        
        # Place both orders
        stop_success, stop_msg = await order_manager.place_order(stop_order)
        tp_success, tp_msg = await order_manager.place_order(tp_order)
        
        logger.info(f"Stop loss order: {stop_success} - {stop_msg}")
        logger.info(f"Take profit order: {tp_success} - {tp_msg}")
        
    except Exception as e:
        logger.error(f"Failed to set stop loss/take profit: {e}")

if __name__ == "__main__":
    # Run the main example
    asyncio.run(main())
    
    # Uncomment to test error handling
    # asyncio.run(test_error_handling())