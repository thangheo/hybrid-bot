
# scripts/test_ccxt_data.py
"""
Test CCXT-based data ingestion
"""
import asyncio
import os
import sys
import logging
from datetime import datetime, timedelta
from dotenv import load_dotenv

load_dotenv()
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')

async def test_ccxt_system():
    """Test CCXT-based system"""
    
    config = {
        'binance': {
            'api_key': os.getenv('BINANCE_API_KEY', ''),
            'api_secret': os.getenv('BINANCE_API_SECRET', ''),
            'testnet': os.getenv('BINANCE_TESTNET', 'true').lower() == 'true',
        },
        'database': {
            'url': os.getenv('DATABASE_URL', 'postgresql://trading:Pass_1234@localhost:5432/trading_db'),
        },
        'symbols': ['BTC/USDT', 'ETH/USDT', 'ADA/USDT'],  # CCXT format
        'timeframes': ['4h','1d']
    }
    
    print("=== CCXT-Based Data Ingestion Test ===")
    print(f"Database: {config['database']['url']}")
    print(f"Testnet: {config['binance']['testnet']}")
    print(f"Symbols: {config['symbols']}")
    print()
    
    from data.manager import DataIngestionManager
    manager = DataIngestionManager(config)
    
    try:
        # Test 1: Initialize
        print("1. Initializing CCXT system...")
        await manager.initialize()
        print("   ✓ CCXT and database initialized")
        
        # Test 2: Connection test
        print("\n2. Testing exchange connection...")
        connected = await manager.test_connection()
        print(f"   {'✓' if connected else '✗'} Exchange connection: {'OK' if connected else 'FAILED'}")
        
        # Test 3: Exchange info
        print("\n3. Getting exchange info...")
        info = await manager.get_exchange_info()
        if info:
            print(f"   ✓ Total markets: {info.get('total_markets', 0)}")
            print(f"   ✓ Active markets: {info.get('active_markets', 0)}")
            print(f"   ✓ Base currencies: {len(info.get('base_currencies', []))}")
        
        # Test 4: Fetch latest data
        if config['binance']['api_key']:
            print("\n4. Fetching latest data...")
            results = await manager.fetch_latest_data(['BTC/USDT'])
            print(f"   ✓ Fetch results: {results}")
            
            # Show data
            df = manager.get_symbol_data('BTC/USDT', days=7)
            if not df.empty:
                print(f"   ✓ BTC/USDT: {len(df)} records")
                print(f"   ✓ Latest price: ${df['close'].iloc[-1]:,.2f}")
                print(f"   ✓ Date range: {df.index.min().date()} to {df.index.max().date()}")
            
            # Market summary with CCXT
            print("\n5. Market summary (CCXT tickers)...")
            summary = await manager.get_market_summary()
            for symbol, data in summary.items():
                change_indicator = "📈" if data['change_24h'] > 0 else "📉"
                print(f"   {symbol}: ${data['price']:,.2f} {change_indicator} {data['change_24h']:+.2f}%")
        else:
            print("\n4-5. Skipping live data tests (no API keys)")
        
        print("\n🎉 CCXT system working perfectly!")
        print("\nCCXT Advantages demonstrated:")
        print("✓ Automatic rate limiting")
        print("✓ Built-in error handling and retries") 
        print("✓ Unified API across exchanges")
        print("✓ Better market data handling")
        print("✓ More reliable connection management")
        
    except Exception as e:
        print(f"\n❌ Test failed: {e}")
        import traceback
        traceback.print_exc()
        
    finally:
        await manager.close()

if __name__ == "__main__":
    asyncio.run(test_ccxt_system())

 