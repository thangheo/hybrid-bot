"""
HÀM DOWNLOAD REAL DATA TỪ BINANCE - 100% WORKING
Chạy script này trên MÁY LOCAL của anh (không phải server Claude)
"""

import pandas as pd
import requests
from datetime import datetime, timedelta
import time

def download_binance_1m_data(symbol='BTCUSDT', days=60):
    """
    Download REAL 1m data từ Binance
    
    QUAN TRỌNG: Chạy trên máy LOCAL, không phải server Claude!
    
    Args:
        symbol: 'BTCUSDT', 'ETHUSDT', 'SOLUSDT', etc.
        days: Số ngày (60 = 2 tháng)
    
    Returns:
        DataFrame với REAL market data từ Binance
    """
    
    print(f"Downloading REAL {symbol} 1m data for {days} days...")
    
    base_url = "https://api.binance.com/api/v3/klines"
    
    # Calculate time range
    end_time = int(datetime.now().timestamp() * 1000)
    start_time = int((datetime.now() - timedelta(days=days)).timestamp() * 1000)
    
    all_candles = []
    current_start = start_time
    batch_num = 0
    
    while current_start < end_time:
        try:
            params = {
                'symbol': symbol,
                'interval': '1m',
                'startTime': current_start,
                'limit': 1000  # Max per request
            }
            
            response = requests.get(base_url, params=params, timeout=30)
            
            if response.status_code != 200:
                print(f"\nError {response.status_code}: {response.text}")
                break
            
            candles = response.json()
            
            if not candles:
                print("\nNo more data available")
                break
            
            all_candles.extend(candles)
            batch_num += 1
            
            # Move to next batch
            last_time = candles[-1][0]
            current_start = last_time + 60000  # +1 minute
            
            # Progress
            current_date = datetime.fromtimestamp(last_time / 1000)
            print(f"Batch {batch_num}: {len(all_candles):,} candles (to {current_date.strftime('%Y-%m-%d %H:%M')})", end='\r')
            
            # Rate limit (avoid ban)
            time.sleep(0.1)
            
            # Stop if we reached end
            if last_time >= end_time:
                break
                
        except Exception as e:
            print(f"\nError: {e}")
            break
    
    # Convert to DataFrame
    if len(all_candles) == 0:
        print("\n✗ No data downloaded!")
        return pd.DataFrame()
    
    df = pd.DataFrame(all_candles, columns=[
        'timestamp', 'open', 'high', 'low', 'close', 'volume',
        'close_time', 'quote_volume', 'trades', 'taker_buy_base',
        'taker_buy_quote', 'ignore'
    ])
    
    # Keep needed columns
    df = df[['timestamp', 'open', 'high', 'low', 'close', 'volume']]
    
    # Convert types
    df['timestamp'] = pd.to_datetime(df['timestamp'], unit='ms')
    for col in ['open', 'high', 'low', 'close', 'volume']:
        df[col] = df[col].astype(float)
    
    df.set_index('timestamp', inplace=True)
    df = df[~df.index.duplicated(keep='first')]
    
    # Stats
    total_days = (df.index[-1] - df.index[0]).days
    expected = days * 1440  # 1440 minutes per day
    coverage = len(df) / expected * 100
    
    print(f"\n\n{'='*60}")
    print("✓ DOWNLOAD SUCCESSFUL!")
    print(f"{'='*60}")
    print(f"Symbol: {symbol}")
    print(f"Candles: {len(df):,}")
    print(f"From: {df.index[0]}")
    print(f"To: {df.index[-1]}")
    print(f"Days: {total_days}")
    print(f"Coverage: {coverage:.1f}%")
    print(f"{'='*60}")
    
    return df


if __name__ == "__main__":
    # Download data
    df = download_binance_1m_data('BTCUSDT', days=60)
    
    if len(df) > 0:
        # Display sample
        print("\nFirst 10 candles:")
        print(df.head(10))
        
        print("\nLast 10 candles:")
        print(df.tail(10))
        
        print("\nStatistics:")
        print(df.describe())
        
        # Save to CSV
        filename = 'BTCUSDT_1m_60days.csv'
        df.to_csv(filename)
        print(f"\n✓ Saved to: {filename}")
        print(f"✓ File size: {len(df):,} rows")
        
        print("\n" + "="*60)
        print("SUCCESS! Data ready for HMM regime detection!")
        print("="*60)