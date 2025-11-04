"""
Example: Tích hợp Hedging vào HFT Strategy
Simple integration để hedge nhanh khi cần
"""

from binance_hedging_bot import BinanceHedgingBot
import time


class HFTWithHedging:
    """
    Wrapper class để tích hợp hedging vào HFT strategy có sẵn
    """
    
    def __init__(self, api_key: str, api_secret: str, testnet: bool = True):
        # Khởi tạo hedge bot
        self.hedge_bot = BinanceHedgingBot(
            api_key=api_key,
            api_secret=api_secret,
            testnet=testnet
        )
        
        # Tracking
        self.is_hedged = False
        self.hft_btc_position = 0  # BTC amount trong HFT
        
    def update_hft_position(self, btc_amount: float):
        """
        Gọi hàm này mỗi khi HFT update position
        
        Args:
            btc_amount: Số BTC đang hold trong HFT (dương = long)
        """
        self.hft_btc_position = btc_amount
        print(f"📊 HFT Position updated: {btc_amount} BTC")
    
    def emergency_hedge(self, leverage: int = 10):
        """
        Emergency hedge - đóng toàn bộ exposure ngay lập tức
        Dùng khi phát hiện thị trường đột ngột đi xuống mạnh
        """
        if self.hft_btc_position <= 0:
            print("ℹ️  HFT không có long position, không cần hedge")
            return
        
        if self.is_hedged:
            print("⚠️  Đã hedge rồi")
            return
        
        print(f"\n🚨 EMERGENCY HEDGE ACTIVATED!")
        print(f"💰 Hedging {self.hft_btc_position} BTC...")
        
        # Mở short position để hedge
        order = self.hedge_bot.open_short_position(
            amount=abs(self.hft_btc_position),
            leverage=leverage
        )
        
        if order:
            self.is_hedged = True
            print("✅ Emergency hedge thành công")
        else:
            print("❌ Không thể mở hedge")
    
    def remove_hedge(self):
        """
        Đóng hedge khi thị trường ổn định trở lại
        """
        if not self.is_hedged:
            print("ℹ️  Không có hedge để đóng")
            return
        
        print("\n🔄 Đóng hedge position...")
        
        order = self.hedge_bot.close_position_market(side='short')
        
        if order:
            self.is_hedged = False
            print("✅ Đã đóng hedge")
        else:
            print("❌ Không thể đóng hedge")
    
    def partial_hedge(self, hedge_ratio: float = 0.5, leverage: int = 10):
        """
        Hedge một phần exposure (không phải 100%)
        
        Args:
            hedge_ratio: Tỷ lệ hedge (0.5 = 50%, 1.0 = 100%)
            leverage: Đòn bẩy
        """
        if self.hft_btc_position <= 0:
            print("ℹ️  HFT không có long position")
            return
        
        if self.is_hedged:
            print("⚠️  Đã hedge rồi")
            return
        
        hedge_amount = abs(self.hft_btc_position) * hedge_ratio
        
        print(f"\n🛡️  PARTIAL HEDGE: {hedge_ratio*100}%")
        print(f"💰 Hedging {hedge_amount} BTC (từ {self.hft_btc_position} BTC)")
        
        order = self.hedge_bot.open_short_position(
            amount=hedge_amount,
            leverage=leverage
        )
        
        if order:
            self.is_hedged = True
            print(f"✅ Đã hedge {hedge_ratio*100}% exposure")
        else:
            print("❌ Không thể mở hedge")
    
    def check_and_hedge_if_needed(self, 
                                   current_price: float, 
                                   trigger_pct: float = -2.0,
                                   leverage: int = 10):
        """
        Tự động check và hedge nếu giá giảm quá trigger
        
        Args:
            current_price: Giá hiện tại
            trigger_pct: % giảm để trigger hedge (âm)
            leverage: Đòn bẩy
        """
        if not hasattr(self, 'last_price'):
            self.last_price = current_price
            return
        
        price_change_pct = ((current_price - self.last_price) / self.last_price) * 100
        
        if price_change_pct <= trigger_pct and not self.is_hedged:
            print(f"⚠️  Giá giảm {price_change_pct:.2f}% - Trigger hedge!")
            self.emergency_hedge(leverage=leverage)
        
        self.last_price = current_price
    
    def get_status(self):
        """Hiển thị trạng thái hiện tại"""
        status = "🛡️  HEDGED" if self.is_hedged else "🟢 UNHEDGED"
        print(f"\n{'='*50}")
        print(f"Status: {status}")
        print(f"HFT Position: {self.hft_btc_position} BTC")
        print(f"Current BTC Price: ${self.hedge_bot.get_current_price():,.2f}")
        print(f"{'='*50}\n")


def demo_hft_integration():
    """
    Demo tích hợp hedging vào HFT
    """
    # API credentials
    API_KEY = 'your_api_key_here'
    API_SECRET = 'your_api_secret_here'
    
    # Khởi tạo
    hft = HFTWithHedging(
        api_key=API_KEY,
        api_secret=API_SECRET,
        testnet=True
    )
    
    print("\n🚀 HFT WITH HEDGING - DEMO\n")
    
    # Scenario 1: HFT mở position long
    print("📝 Scenario 1: HFT vừa mua 0.5 BTC")
    hft.update_hft_position(0.5)
    hft.get_status()
    
    time.sleep(2)
    
    # Scenario 2: Thị trường giảm mạnh → Emergency hedge
    print("\n📝 Scenario 2: Thị trường đột ngột giảm mạnh!")
    hft.emergency_hedge(leverage=10)
    hft.get_status()
    
    time.sleep(2)
    
    # Scenario 3: Thị trường ổn định → Đóng hedge
    print("\n📝 Scenario 3: Thị trường ổn định trở lại")
    input("Nhấn Enter để đóng hedge...")
    hft.remove_hedge()
    hft.get_status()
    
    print("\n✅ Demo hoàn tất!")


def demo_partial_hedge():
    """
    Demo hedge một phần (50%)
    """
    API_KEY = 'your_api_key_here'
    API_SECRET = 'your_api_secret_here'
    
    hft = HFTWithHedging(API_KEY, API_SECRET, testnet=True)
    
    print("\n🚀 PARTIAL HEDGE DEMO\n")
    
    # HFT có 1 BTC long
    hft.update_hft_position(1.0)
    
    # Chỉ hedge 50%
    print("\n📝 Hedge 50% exposure (0.5 BTC từ 1 BTC)")
    hft.partial_hedge(hedge_ratio=0.5, leverage=10)
    
    hft.get_status()
    
    input("\nNhấn Enter để đóng hedge...")
    hft.remove_hedge()
    
    print("\n✅ Demo hoàn tất!")


def demo_auto_trigger():
    """
    Demo tự động hedge khi giá giảm
    """
    API_KEY = 'your_api_key_here'
    API_SECRET = 'your_api_secret_here'
    
    hft = HFTWithHedging(API_KEY, API_SECRET, testnet=True)
    
    print("\n🚀 AUTO TRIGGER HEDGE DEMO\n")
    
    # HFT có position
    hft.update_hft_position(0.5)
    
    print("🔄 Monitoring giá... (Nhấn Ctrl+C để dừng)")
    print("Sẽ tự động hedge nếu giá giảm > 2%\n")
    
    try:
        while True:
            current_price = hft.hedge_bot.get_current_price()
            print(f"💵 BTC: ${current_price:,.2f}")
            
            # Check và hedge nếu cần
            hft.check_and_hedge_if_needed(
                current_price=current_price,
                trigger_pct=-2.0,  # Hedge nếu giảm > 2%
                leverage=10
            )
            
            time.sleep(10)  # Check mỗi 10 giây
            
    except KeyboardInterrupt:
        print("\n\n⏹️  Dừng monitoring")
        hft.get_status()


if __name__ == "__main__":
    """
    Chọn demo muốn chạy:
    """
    
    print("\n" + "="*60)
    print("Chọn demo:")
    print("1. Demo tích hợp cơ bản")
    print("2. Demo partial hedge (50%)")
    print("3. Demo auto trigger hedge")
    print("="*60)
    
    choice = input("\nNhập lựa chọn (1/2/3): ").strip()
    
    if choice == "1":
        demo_hft_integration()
    elif choice == "2":
        demo_partial_hedge()
    elif choice == "3":
        demo_auto_trigger()
    else:
        print("Lựa chọn không hợp lệ")
