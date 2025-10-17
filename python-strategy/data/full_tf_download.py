import ccxt
import pandas as pd
from datetime import datetime
import time

def download_crypto_data(symbol="BTC/USDT", timeframe="1h", length=500, exchange_name="binance"):
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


def main():
    """Hàm chính để chạy chương trình"""
    print("=" * 60)
    print("CÔNG CỤ TẢI DỮ LIỆU CRYPTO")
    print("=" * 60)
    
    # Nhập thông tin từ người dùng
    symbol = input("Nhập symbol (vd: BTC/USDT): ").strip() or "BTC/USDT"
    timeframe = input("Nhập timeframe (vd: 1h, 5m, 1d): ").strip() or "1h"
    
    while True:
        try:
            length_input = input("Nhập số lượng nến cần lấy (mặc định 500): ").strip()
            length = int(length_input) if length_input else 500
            if length <= 0:
                print("❌ Số lượng nến phải lớn hơn 0")
                continue
            break
        except ValueError:
            print("❌ Vui lòng nhập số hợp lệ")
    
    exchange_name = input("Nhập tên sàn (mặc định binance): ").strip() or "binance"
    
    print("\n" + "=" * 60)
    
    try:
        # Tải dữ liệu
        df = download_crypto_data(symbol, timeframe, length, exchange_name)
        
        print(f"\n📊 Tổng số nến đã tải: {len(df)}")
        print(f"📅 Từ: {df['datetime'].iloc[0]}")
        print(f"📅 Đến: {df['datetime'].iloc[-1]}")
        print("\n" + "=" * 60)
        print("10 NẾN ĐẦU TIÊN:")
        print(df.head(10).to_string(index=False))
        print("\n...")
        print("\n10 NẾN CUỐI CÙNG:")
        print(df.tail(10).to_string(index=False))
        
        # Lưu ra file CSV
        filename = f"{symbol.replace('/', '_')}_{timeframe}_{length}candles.csv"
        df.to_csv(filename, index=False)
        print(f"\n✅ Đã lưu {len(df)} nến vào {filename}")
        
        # Hiển thị một số thống kê cơ bản
        print("\n" + "=" * 60)
        print("THỐNG KÊ CƠ BẢN:")
        print(f"Giá cao nhất: {df['high'].max():.2f}")
        print(f"Giá thấp nhất: {df['low'].min():.2f}")
        print(f"Giá đóng cửa cuối: {df['close'].iloc[-1]:.2f}")
        print(f"Volume trung bình: {df['volume'].mean():.2f}")
        print("=" * 60)
        
    except Exception as e:
        print(f"\n❌ Lỗi: {e}")
        return


if __name__ == "__main__":
    main()