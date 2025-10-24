"""
HÀM DOWNLOAD REAL DATA TỪ BINANCE - 100% WORKING
Chạy script này trên MÁY LOCAL của anh (không phải server Claude)
"""

import pandas as pd
import requests
from datetime import datetime, timedelta
import time
import ccxt

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


def download_crypto_data(symbol="BTC/USDT", timeframe="4h", length=5000, exchange_name="binance"):
    """
    Tải dữ liệu OHLCV từ sàn crypto
    
    Args:
        symbol: Cặp giao dịch (vd: BTC/USDT)
        timeframe: Khung thời gian (vd: 1h, 5m, 1d)
        length: Số nến cần lấy
        exchange_name: Tên sàn giao dịch
    
    Returns:
        DataFrame chứa dữ liệu OHLCV
    """
    # if symbol has format like 'BTCUSDT', convert to 'BTC/USDT'
    if '/' not in symbol and len(symbol) > 3:
        symbol = symbol[:-4] + '/' + symbol[-4:]
    # Tạo đối tượng sàn
    exchange_class = getattr(ccxt, exchange_name)
    exchange = exchange_class({
        'enableRateLimit': True
    })
    
    # Kiểm tra xem sàn có hỗ trợ symbol không
    markets = exchange.load_markets()
    if symbol not in markets:
        raise ValueError(f"{symbol} không có trên {exchange_name}")
    
    # Lấy giới hạn tối đa mỗi lần fetch của sàn
    max_limit = 1000  # Hầu hết các sàn giới hạn 1000 candles/request
    all_data = []
    
    print(f"Đang tải {length} nến {timeframe} cho {symbol}...")
    
    # Tính thời gian bắt đầu cần lấy (lấy từ quá khứ đến hiện tại)
    timeframe_ms = exchange.parse_timeframe(timeframe) * 1000
    now = exchange.milliseconds()
    since = now - (length * timeframe_ms)
    
    # Tính số lần cần fetch
    num_fetches = (length + max_limit - 1) // max_limit
    
    for i in range(num_fetches):
        # Tính số nến cần lấy trong lần này
        remaining = length - len(all_data)
        fetch_limit = min(remaining, max_limit)
        
        try:
            # Tải dữ liệu OHLCV từ thời điểm 'since'
            data = exchange.fetch_ohlcv(
                symbol, 
                timeframe=timeframe, 
                since=since, 
                limit=fetch_limit
            )
            
            if not data:
                print("⚠️ Không còn dữ liệu để tải")
                break
            
            # Thêm dữ liệu vào danh sách
            all_data.extend(data)
            
            # Cập nhật 'since' để lấy dữ liệu tiếp theo
            # Lấy timestamp của nến cuối cùng + 1 khung thời gian
            since = data[-1][0] + timeframe_ms
            
            print(f"Đã tải {len(all_data)}/{length} nến...")
            
            # Nếu đã đủ số lượng cần thiết
            if len(all_data) >= length:
                break
            
            # Nếu số lượng dữ liệu trả về ít hơn limit, nghĩa là đã hết dữ liệu
            if len(data) < fetch_limit:
                print("⚠️ Đã hết dữ liệu lịch sử")
                break
            
            # Tránh rate limit
            time.sleep(exchange.rateLimit / 1000)
            
        except Exception as e:
            print(f"❌ Lỗi khi tải dữ liệu: {e}")
            break
    
    if not all_data:
        raise ValueError("Không thể tải được dữ liệu nào")
    
    # Đưa vào DataFrame
    df = pd.DataFrame(all_data, columns=['timestamp', 'open', 'high', 'low', 'close', 'volume'])
    df['datetime'] = pd.to_datetime(df['timestamp'], unit='ms')
    
    # Loại bỏ duplicate và sắp xếp theo thời gian
    df = df.drop_duplicates(subset=['timestamp']).sort_values('timestamp').reset_index(drop=True)
    
    # Chỉ lấy đúng số lượng nến cuối cùng (mới nhất) nếu có dư
    if len(df) > length:
        df = df.tail(length).reset_index(drop=True)
    
    # Đưa cột datetime lên đầu
    df = df[['datetime', 'timestamp', 'open', 'high', 'low', 'close', 'volume']]
    
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