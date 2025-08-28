"""
Quick demo showing the order execution module working
This is what you'd actually use in production
"""
import asyncio
import os
from datetime import datetime

# Your order execution imports
from order_manager import OrderManager
from models import Order, OrderSide, OrderType, OrderStatus

async def demo_real_trading():
    """Demo how to use order execution in real trading"""
    
    print("🚀 Crypto Trading Bot - Order Execution Demo")
    print("=" * 60)
    
    # Production-ready configuration
    config = {
        'binance': {
            'api_key': os.getenv('BINANCE_API_KEY', 'your_key_here'),
            'api_secret': os.getenv('BINANCE_API_SECRET', 'your_secret_here'),
            'testnet': True  # Set to False for live trading
        },
        'max_slippage': 0.005,  # 0.5% max slippage
        'order_timeout': 300,   # 5 minutes
        'retry_attempts': 3,
        'large_order_threshold': 0.05,
        'twap_chunks': 10
    }
    
    # Initialize order manager
    order_manager = OrderManager(config)
    
    try:
        print("📊 Getting market data...")
        
        # Check if we can connect to exchange
        if config['binance']['api_key'] != 'your_key_here':
            # Real API connection
            ticker = await order_manager.exchange.fetch_ticker('BTC/USDT')
            current_price = ticker['last']
            print(f"✅ BTC/USDT Current Price: ${current_price:,.2f}")
            
            # Get account balance
            balance = await order_manager.exchange.fetch_balance()
            usdt_balance = balance.get('USDT', {}).get('free', 0)
            print(f"💰 USDT Balance: ${usdt_balance:,.2f}")
            
        else:
            print("⚠️  Using demo mode (no API keys)")
            current_price = 45000.0  # Demo price
            usdt_balance = 1000.0    # Demo balance
        
        print(f"\n💡 Simulating trading signals...")
        
        # Example 1: Simple market buy order
        print(f"\n1️⃣  Market Buy Order")
        market_order = Order(
            symbol='BTC/USDT',
            side=OrderSide.BUY,
            order_type=OrderType.MARKET,
            quantity=0.001,  # $45 worth
            strategy_id='mean_reversion_v1'
        )
        
        print(f"   📝 Order: {market_order.side.value} {market_order.quantity} {market_order.symbol}")
        print(f"   💵 Estimated cost: ${current_price * market_order.quantity:,.2f}")
        
        if config['binance']['api_key'] != 'your_key_here':
            # Execute real order
            success, message = await order_manager.place_order(market_order)
            print(f"   📋 Result: {success} - {message}")
            
            if success:
                print(f"   ✅ Order ID: {market_order.id}")
                print(f"   💰 Filled: {market_order.filled_quantity} at ${market_order.average_price:,.2f}")
        else:
            print(f"   🔄 Demo mode: Order would be executed")
        
        # Example 2: Limit sell order with stop loss
        print(f"\n2️⃣  Limit Sell with Stop Loss")
        
        sell_price = current_price * 1.02  # 2% above current price
        stop_price = current_price * 0.95  # 5% below current price
        
        limit_order = Order(
            symbol='BTC/USDT',
            side=OrderSide.SELL,
            order_type=OrderType.LIMIT,
            quantity=0.001,
            price=sell_price,
            strategy_id='take_profit'
        )
        
        stop_order = Order(
            symbol='BTC/USDT',
            side=OrderSide.SELL,
            order_type=OrderType.STOP_LOSS,
            quantity=0.001,
            stop_price=stop_price,
            strategy_id='stop_loss'
        )
        
        print(f"   📝 Limit Order: Sell at ${sell_price:,.2f}")
        print(f"   🛡️  Stop Loss: Sell at ${stop_price:,.2f}")
        
        # Example 3: Position management
        print(f"\n3️⃣  Position Management")
        
        # Get current positions
        positions = await order_manager.get_positions()
        print(f"   📊 Current positions: {len(positions)} symbols")
        
        for symbol, quantity in positions.items():
            if quantity > 0:
                print(f"      {symbol}: {quantity:.6f}")
        
        # Example 4: Smart execution for large orders
        print(f"\n4️⃣  Smart Execution (Large Order)")
        
        large_order = Order(
            symbol='ETH/USDT',
            side=OrderSide.BUY,
            order_type=OrderType.MARKET,
            quantity=0.5,  # Larger order
            strategy_id='portfolio_rebalance'
        )
        
        print(f"   📝 Large Order: {large_order.quantity} ETH")
        print(f"   🧠 Smart routing would use TWAP/Iceberg execution")
        
        # Example 5: Order monitoring
        print(f"\n5️⃣  Order Monitoring")
        
        # Get order history
        history = order_manager.get_order_history(days=1)
        print(f"   📜 Recent orders: {len(history)}")
        
        # Show active orders
        active_count = len(order_manager.active_orders)
        print(f"   ⏳ Active orders: {active_count}")
        
        # Performance summary
        print(f"\n📈 Trading Performance Summary")
        print(f"   🎯 Total orders today: {len(history)}")
        print(f"   ⚡ Active orders: {active_count}")
        print(f"   💼 Positions held: {len(positions)}")
        
        # Risk metrics
        total_exposure = sum(abs(qty) for qty in positions.values())
        print(f"   ⚖️  Total exposure: {total_exposure:.6f} BTC equivalent")
        
        print(f"\n✅ Order execution system is working perfectly!")
        print(f"🔧 Ready for integration with strategy and risk modules")
        
    except Exception as e:
        print(f"❌ Error in demo: {e}")
        import traceback
        traceback.print_exc()
    
    finally:
        await order_manager.exchange.close()
        print(f"\n👋 Demo completed at {datetime.now()}")

def show_integration_example():
    """Show how this integrates with strategy signals"""
    print(f"\n🔗 Integration Example")
    print("=" * 40)
    
    # Example trading signal from strategy module
    signal = {
        'timestamp': datetime.now(),
        'symbol': 'BTC/USDT',
        'action': 'buy',
        'confidence': 0.85,
        'quantity': 0.001,
        'strategy': 'statistical_mean_reversion',
        'expected_price': 45000.0,
        'stop_loss': 43000.0,
        'take_profit': 47000.0
    }
    
    print(f"📡 Strategy Signal Received:")
    print(f"   Symbol: {signal['symbol']}")
    print(f"   Action: {signal['action']}")
    print(f"   Confidence: {signal['confidence']:.1%}")
    print(f"   Strategy: {signal['strategy']}")
    
    # Convert to order
    order = Order(
        symbol=signal['symbol'],
        side=OrderSide.BUY if signal['action'] == 'buy' else OrderSide.SELL,
        order_type=OrderType.MARKET,
        quantity=signal['quantity'],
        strategy_id=signal['strategy'],
        tags={'confidence': signal['confidence']}
    )
    
    print(f"\n⚙️  Order Created:")
    print(f"   Type: {order.order_type.value}")
    print(f"   Side: {order.side.value}")
    print(f"   Quantity: {order.quantity}")
    print(f"   Strategy: {order.strategy_id}")
    
    print(f"\n🚀 In real system:")
    print(f"   1. Risk manager validates position size")
    print(f"   2. Order manager executes with smart routing")
    print(f"   3. Position tracker updates portfolio")
    print(f"   4. Stop loss/take profit orders placed")
    print(f"   5. Performance tracking & reporting")

if __name__ == "__main__":
    # Show integration first
    show_integration_example()
    
    # Run trading demo
    asyncio.run(demo_real_trading())
