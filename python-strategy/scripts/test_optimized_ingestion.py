# scripts/debug_fetch.py
import asyncio
import sys
import os
sys.path.append(os.path.join(os.path.dirname(__file__), '..'))

from dotenv import load_dotenv
from src.data.manager import DataIngestionManager
import logging

# Enable detailed logging
logging.basicConfig(level=logging.DEBUG)

async def debug_fetch():
    load_dotenv()
    
    config = {
        'symbols': ['BTC/USDT'],  # Just one symbol
        'timeframes': ['1d'],
        'database': {
            'url': f"postgresql://trading:{os.getenv('DB_PASSWORD', 'Pass_1234')}@localhost:5432/trading_db"
        },
        'binance': {
            'api_key': os.getenv('BINANCE_API_KEY', ''),
            'api_secret': os.getenv('BINANCE_API_SECRET', ''),
            'testnet': True
        }
    }
    
    manager = DataIngestionManager(config)
    
    try:
        await manager.initialize()
        print("Manager initialized successfully")
        
        # Check existing data
        latest_ts = await manager.get_latest_timestamp('BTC/USDT', '1d')
        print(f"Latest timestamp for BTC/USDT: {latest_ts}")
        
        if latest_ts:
            from datetime import datetime
            latest_date = datetime.fromtimestamp(latest_ts / 1000)
            hours_old = (datetime.now() - latest_date).total_seconds() / 3600
            print(f"Data age: {hours_old:.1f} hours old")
            
        # Try direct fetch
        print("Testing direct OHLCV fetch...")
        ohlcv = await manager._fetch_ohlcv_with_retry('BTC/USDT', '1d', limit=5)
        print(f"Direct fetch result: {len(ohlcv) if ohlcv else 0} candles")
        
        # Try the regular fetch method
        print("Testing regular fetch_latest_data...")
        results = await manager.fetch_latest_data_optimized(['BTC/USDT'])
        print(f"Regular fetch results: {results}")
        
        # Check if optimized method exists
        if hasattr(manager, 'fetch_latest_data_optimized'):
            print("Testing optimized fetch...")
            opt_results = await manager.fetch_latest_data_optimized(['BTC/USDT'])
            print(f"Optimized fetch results: {opt_results}")
        else:
            print("fetch_latest_data_optimized method not found")
        
    except Exception as e:
        print(f"Debug failed: {e}")
        import traceback
        traceback.print_exc()
        
    finally:
        await manager.close()

if __name__ == "__main__":
    asyncio.run(debug_fetch())