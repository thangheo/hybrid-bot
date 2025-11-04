"""
Advanced Hedging Strategy với Auto Trigger
Tự động hedge khi phát hiện thị trường đi xuống
"""

import ccxt
import time
from datetime import datetime
from typing import Optional, Dict, List
import json


class AdvancedHedgingStrategy:
    def __init__(self, api_key: str, api_secret: str, testnet: bool = True):
        """
        Strategy hedging tự động với trigger dựa trên điều kiện thị trường
        """
        if testnet:
            self.exchange = ccxt.binance({
                'apiKey': api_key,
                'secret': api_secret,
                'options': {
                    'defaultType': 'future',
                    'adjustForTimeDifference': True,
                },
                'urls': {
                    'api': {
                        'public': 'https://testnet.binancefuture.com/fapi/v1',
                        'private': 'https://testnet.binancefuture.com/fapi/v1',
                    }
                }
            })
        else:
            self.exchange = ccxt.binance({
                'apiKey': api_key,
                'secret': api_secret,
                'options': {
                    'defaultType': 'future',
                    'adjustForTimeDifference': True,
                }
            })
        
        self.symbol = 'BTC/USDT'
        self.exchange.load_markets()
        
        # Cấu hình hedging
        self.hedge_active = False
        self.hft_exposure = 0  # Số BTC đang có trong HFT strategy
        self.hedge_ratio = 1.0  # Hedge 100% exposure
        self.leverage = 10
        
        # Tracking
        self.price_history: List[float] = []
        self.entry_price = None
        
    def update_hft_exposure(self, btc_amount: float):
        """
        Cập nhật exposure của HFT strategy
        
        Args:
            btc_amount: Tổng BTC đang hold trong HFT (dương = long, âm = short)
        """
        self.hft_exposure = btc_amount
        print(f"📊 HFT Exposure cập nhật: {btc_amount} BTC")
    
    def check_market_downturn(self, lookback: int = 10, threshold: float = -0.5) -> bool:
        """
        Kiểm tra xem thị trường có đang đi xuống không
        
        Args:
            lookback: Số nến để xem xét
            threshold: % giảm giá để trigger hedge (âm)
            
        Returns:
            True nếu thị trường đi xuống
        """
        if len(self.price_history) < lookback:
            return False
        
        recent_prices = self.price_history[-lookback:]
        price_change = ((recent_prices[-1] - recent_prices[0]) / recent_prices[0]) * 100
        
        if price_change <= threshold:
            print(f"⚠️  Phát hiện thị trường giảm {price_change:.2f}% trong {lookback} periods")
            return True
        
        return False
    
    def calculate_hedge_size(self) -> float:
        """
        Tính toán size hedge cần thiết dựa trên HFT exposure
        
        Returns:
            Số lượng BTC cần hedge
        """
        # Nếu HFT đang long BTC, cần short để hedge
        if self.hft_exposure > 0:
            return self.hft_exposure * self.hedge_ratio
        else:
            # Nếu HFT đang short hoặc flat, không cần hedge
            return 0
    
    def open_hedge(self) -> Optional[Dict]:
        """
        Mở vị thế hedge dựa trên HFT exposure
        
        Returns:
            Order info hoặc None
        """
        if self.hedge_active:
            print("⚠️  Hedge đã được kích hoạt rồi")
            return None
        
        hedge_size = self.calculate_hedge_size()
        
        if hedge_size <= 0:
            print("ℹ️  Không cần hedge (HFT không có long exposure)")
            return None
        
        try:
            # Set leverage
            self.exchange.set_leverage(self.leverage, self.symbol)
            
            # Mở SHORT để hedge
            order = self.exchange.create_market_sell_order(
                symbol=self.symbol,
                amount=hedge_size,
                params={'positionSide': 'SHORT'}
            )
            
            self.hedge_active = True
            self.entry_price = self.exchange.fetch_ticker(self.symbol)['last']
            
            print(f"\n🛡️  HEDGE ACTIVATED!")
            print(f"💰 Hedge Size: {hedge_size} BTC")
            print(f"💵 Entry Price: ${self.entry_price:,.2f}")
            print(f"📊 HFT Exposure: {self.hft_exposure} BTC")
            print(f"📈 Leverage: {self.leverage}x")
            
            return order
            
        except Exception as e:
            print(f"❌ Lỗi khi mở hedge: {e}")
            return None
    
    def close_hedge(self) -> Optional[Dict]:
        """
        Đóng vị thế hedge
        
        Returns:
            Order info hoặc None
        """
        if not self.hedge_active:
            print("ℹ️  Không có hedge nào đang active")
            return None
        
        try:
            # Lấy position size
            positions = self.exchange.fetch_positions([self.symbol])
            short_position = None
            
            for pos in positions:
                if pos['side'] == 'short' and float(pos['contracts']) != 0:
                    short_position = pos
                    break
            
            if not short_position:
                print("❌ Không tìm thấy hedge position")
                self.hedge_active = False
                return None
            
            amount = abs(float(short_position['contracts']))
            
            # Đóng hedge (BUY để đóng SHORT)
            order = self.exchange.create_market_buy_order(
                symbol=self.symbol,
                amount=amount,
                params={
                    'positionSide': 'SHORT',
                    'reduceOnly': True
                }
            )
            
            exit_price = self.exchange.fetch_ticker(self.symbol)['last']
            pnl = (self.entry_price - exit_price) * amount if self.entry_price else 0
            
            print(f"\n🛡️  HEDGE CLOSED!")
            print(f"💰 Size: {amount} BTC")
            print(f"💵 Exit Price: ${exit_price:,.2f}")
            print(f"📊 PnL: ${pnl:,.2f}")
            
            self.hedge_active = False
            self.entry_price = None
            
            return order
            
        except Exception as e:
            print(f"❌ Lỗi khi đóng hedge: {e}")
            return None
    
    def update_price_history(self):
        """Cập nhật lịch sử giá"""
        try:
            ticker = self.exchange.fetch_ticker(self.symbol)
            current_price = ticker['last']
            self.price_history.append(current_price)
            
            # Giữ tối đa 100 giá gần nhất
            if len(self.price_history) > 100:
                self.price_history.pop(0)
            
            return current_price
        except Exception as e:
            print(f"❌ Lỗi khi cập nhật giá: {e}")
            return None
    
    def run_monitoring_loop(self, interval_seconds: int = 60):
        """
        Chạy vòng lặp giám sát và tự động hedge
        
        Args:
            interval_seconds: Thời gian giữa mỗi lần check
        """
        print("\n" + "="*60)
        print("🔄 BẮT ĐẦU MONITORING LOOP")
        print("="*60)
        
        try:
            while True:
                # Cập nhật giá
                current_price = self.update_price_history()
                timestamp = datetime.now().strftime("%H:%M:%S")
                
                print(f"\n[{timestamp}] 💵 BTC: ${current_price:,.2f}")
                
                # Kiểm tra điều kiện hedge
                if not self.hedge_active:
                    # Chưa hedge, check xem có cần hedge không
                    if self.check_market_downturn(lookback=10, threshold=-0.5):
                        print("🚨 TRIGGER HEDGE: Thị trường đi xuống!")
                        self.open_hedge()
                else:
                    # Đang hedge, check xem có nên đóng không
                    if len(self.price_history) >= 10:
                        recent_prices = self.price_history[-10:]
                        price_change = ((recent_prices[-1] - recent_prices[0]) / recent_prices[0]) * 100
                        
                        # Đóng hedge nếu thị trường phục hồi > 1%
                        if price_change > 1.0:
                            print("📈 Thị trường phục hồi, đóng hedge!")
                            self.close_hedge()
                
                # Hiển thị trạng thái
                status = "🛡️  HEDGED" if self.hedge_active else "🟢 UNHEDGED"
                print(f"Status: {status} | HFT Exposure: {self.hft_exposure} BTC")
                
                time.sleep(interval_seconds)
                
        except KeyboardInterrupt:
            print("\n\n⏹️  Dừng monitoring loop")
            if self.hedge_active:
                print("⚠️  Bạn vẫn đang có hedge position mở!")

import os
def demo_advanced_hedging():
    """Demo advanced hedging strategy"""
    
    # Thay API key của bạn
    API_KEY = os.getenv("BINANCE_API_KEY")
    API_SECRET = os.getenv("BINANCE_API_SECRET")

    # Khởi tạo strategy
    strategy = AdvancedHedgingStrategy(
        api_key=str(API_KEY),
        api_secret=str(API_SECRET),
        testnet=True
    )
    
    print("\n🚀 ADVANCED HEDGING STRATEGY - DEMO")
    
    # Giả sử HFT của bạn đang hold 0.5 BTC long
    strategy.update_hft_exposure(0.5)
    
    # Demo 1: Mở hedge thủ công
    print("\n📝 Demo 1: Mở hedge thủ công")
    strategy.open_hedge()
    
    time.sleep(3)
    
    # Demo 2: Đóng hedge
    print("\n📝 Demo 2: Đóng hedge")
    strategy.close_hedge()
    
    # Demo 3: Auto monitoring (comment out nếu không muốn chạy)
    # print("\n📝 Demo 3: Auto monitoring (Nhấn Ctrl+C để dừng)")
    # strategy.run_monitoring_loop(interval_seconds=30)


if __name__ == "__main__":
    demo_advanced_hedging()
