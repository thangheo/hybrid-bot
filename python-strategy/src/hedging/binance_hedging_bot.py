"""
Binance Bitcoin Hedging Bot
Mục đích: Hedge cho chiến lược HFT khi thị trường đi xuống
Sử dụng: ccxt library để tương tác với Binance Futures
"""

import os
import ccxt
import time
from typing import Optional, Dict
import json


class BinanceHedgingBot:
    def __init__(self, api_key: str, api_secret: str, testnet: bool = True):
        """
        Khởi tạo bot hedging
        
        Args:
            api_key: Binance API key
            api_secret: Binance API secret
            testnet: True để dùng testnet, False để dùng mainnet
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
            print("🔧 Đang sử dụng TESTNET")
        else:
            self.exchange = ccxt.binance({
                'apiKey': api_key,
                'secret': api_secret,
                'options': {
                    'defaultType': 'future',
                    'adjustForTimeDifference': True,
                }
            })
            print("⚠️  Đang sử dụng MAINNET")
        
        self.symbol = 'BTC/USDT'
        self.exchange.load_markets()
        
    def get_current_price(self) -> float:
        """Lấy giá hiện tại của BTC"""
        ticker = self.exchange.fetch_ticker(self.symbol)
        return ticker['last']
    
    def get_account_balance(self) -> Dict:
        """Lấy số dư tài khoản"""
        balance = self.exchange.fetch_balance()
        return {
            'USDT': {
                'free': balance['USDT']['free'],
                'used': balance['USDT']['used'],
                'total': balance['USDT']['total']
            }
        }
    
    def set_leverage(self, leverage: int = 10):
        """
        Set đòn bẩy cho symbol
        
        Args:
            leverage: Mức đòn bẩy (1-125 tùy symbol)
        """
        try:
            self.exchange.set_leverage(leverage, self.symbol)
            print(f"✅ Đã set leverage {leverage}x cho {self.symbol}")
        except Exception as e:
            print(f"❌ Lỗi khi set leverage: {e}")
    
    def open_short_position(self, amount: float, leverage: int = 10) -> Optional[Dict]:
        """
        Mở vị thế SHORT để hedge (bán khống)
        
        Args:
            amount: Số lượng BTC muốn short
            leverage: Đòn bẩy sử dụng
            
        Returns:
            Order info hoặc None nếu lỗi
        """
        try:
            # Set leverage trước
            self.set_leverage(leverage)
            
            # Lấy giá hiện tại
            current_price = self.get_current_price()
            
            # Mở vị thế SHORT (SELL)
            order = self.exchange.create_market_sell_order(
                symbol=self.symbol,
                amount=amount,
                params={
                    'positionSide': 'SHORT'  # Hedge mode
                }
            )
            
            print(f"\n🔴 ĐÃ MỞ VỊ THẾ SHORT (HEDGE)")
            print(f"📊 Symbol: {self.symbol}")
            print(f"💰 Amount: {amount} BTC")
            print(f"💵 Giá vào lệnh: ${current_price:,.2f}")
            print(f"📈 Leverage: {leverage}x")
            print(f"🆔 Order ID: {order['id']}")
            
            return order
            
        except Exception as e:
            print(f"❌ Lỗi khi mở vị thế SHORT: {e}")
            return None
    
    def open_long_position(self, amount: float, leverage: int = 10) -> Optional[Dict]:
        """
        Mở vị thế LONG (mua)
        
        Args:
            amount: Số lượng BTC muốn long
            leverage: Đòn bẩy sử dụng
            
        Returns:
            Order info hoặc None nếu lỗi
        """
        try:
            # Set leverage trước
            self.set_leverage(leverage)
            
            # Lấy giá hiện tại
            current_price = self.get_current_price()
            
            # Mở vị thế LONG (BUY)
            order = self.exchange.create_market_buy_order(
                symbol=self.symbol,
                amount=amount,
                params={
                    'positionSide': 'LONG'
                }
            )
            
            print(f"\n🟢 ĐÃ MỞ VỊ THẾ LONG")
            print(f"📊 Symbol: {self.symbol}")
            print(f"💰 Amount: {amount} BTC")
            print(f"💵 Giá vào lệnh: ${current_price:,.2f}")
            print(f"📈 Leverage: {leverage}x")
            print(f"🆔 Order ID: {order['id']}")
            
            return order
            
        except Exception as e:
            print(f"❌ Lỗi khi mở vị thế LONG: {e}")
            return None
    
    def get_positions(self) -> list:
        """Lấy tất cả vị thế đang mở"""
        try:
            positions = self.exchange.fetch_positions([self.symbol])
            active_positions = [p for p in positions if float(p['contracts']) != 0]
            return active_positions
        except Exception as e:
            print(f"❌ Lỗi khi lấy positions: {e}")
            return []
    
    def display_positions(self):
        """Hiển thị tất cả vị thế đang có"""
        positions = self.get_positions()
        
        if not positions:
            print("\n📊 Không có vị thế nào đang mở")
            return
        
        print("\n" + "="*60)
        print("📊 VỊ THẾ ĐANG MỞ")
        print("="*60)
        
        for pos in positions:
            side = pos['side']
            amount = float(pos['contracts'])
            entry_price = float(pos['entryPrice'])
            unrealized_pnl = float(pos['unrealizedPnl'])
            leverage = float(pos['leverage'])
            
            emoji = "🔴" if side == 'short' else "🟢"
            pnl_emoji = "💰" if unrealized_pnl > 0 else "📉"
            
            print(f"\n{emoji} {side.upper()} Position:")
            print(f"  💰 Amount: {amount} BTC")
            print(f"  💵 Entry Price: ${entry_price:,.2f}")
            print(f"  📈 Leverage: {leverage}x")
            print(f"  {pnl_emoji} Unrealized PnL: ${unrealized_pnl:,.2f}")
        
        print("="*60)
    
    def close_position_market(self, side: str = 'short') -> Optional[Dict]:
        """
        Đóng vị thế bằng lệnh market
        
        Args:
            side: 'short' hoặc 'long' - vị thế muốn đóng
            
        Returns:
            Order info hoặc None nếu lỗi
        """
        try:
            # Lấy vị thế hiện tại
            positions = self.get_positions()
            target_position = None
            
            for pos in positions:
                if pos['side'].lower() == side.lower():
                    target_position = pos
                    break
            
            if not target_position:
                print(f"❌ Không tìm thấy vị thế {side.upper()} để đóng")
                return None
            
            amount = abs(float(target_position['contracts']))
            
            if amount == 0:
                print(f"❌ Vị thế {side.upper()} đã đóng hoặc không tồn tại")
                return None
            
            # Đóng SHORT thì phải BUY, đóng LONG thì phải SELL
            if side.lower() == 'short':
                order = self.exchange.create_market_buy_order(
                    symbol=self.symbol,
                    amount=amount,
                    params={
                        'positionSide': 'SHORT',
                        'reduceOnly': True
                    }
                )
                action = "BUY (đóng SHORT)"
            else:
                order = self.exchange.create_market_sell_order(
                    symbol=self.symbol,
                    amount=amount,
                    params={
                        'positionSide': 'LONG',
                        'reduceOnly': True
                    }
                )
                action = "SELL (đóng LONG)"
            
            current_price = self.get_current_price()
            
            print(f"\n✅ ĐÃ ĐÓNG VỊ THẾ {side.upper()}")
            print(f"📊 Symbol: {self.symbol}")
            print(f"💰 Amount: {amount} BTC")
            print(f"💵 Giá đóng lệnh: ${current_price:,.2f}")
            print(f"🔄 Action: {action}")
            print(f"🆔 Order ID: {order['id']}")
            
            return order
            
        except Exception as e:
            print(f"❌ Lỗi khi đóng vị thế {side.upper()}: {e}")
            return None
    
    def close_all_positions(self):
        """Đóng tất cả vị thế"""
        positions = self.get_positions()
        
        if not positions:
            print("\n✅ Không có vị thế nào để đóng")
            return
        
        print("\n🔄 Đang đóng tất cả vị thế...")
        
        for pos in positions:
            side = pos['side']
            self.close_position_market(side)
            time.sleep(0.5)  # Delay nhỏ giữa các lệnh
        
        print("\n✅ Đã đóng tất cả vị thế")

import os
def demo_hedging():
    """
    Demo sử dụng bot hedging
    """
    # ⚠️ QUAN TRỌNG: Thay thế bằng API key và secret của bạn
    # get key from .env or config for security
    API_KEY = os.getenv("BINANCE_API_KEY")
    API_SECRET = os.getenv("BINANCE_API_SECRET")

    # Khởi tạo bot (testnet=True để test, testnet=False để chạy thật)
    bot = BinanceHedgingBot(
        api_key=str(API_KEY),
        api_secret=str(API_SECRET),
        testnet=True  # Dùng testnet để test trước
    )
    
    print("\n" + "="*60)
    print("🚀 BINANCE HEDGING BOT - DEMO")
    print("="*60)
    
    # 1. Kiểm tra giá hiện tại
    current_price = bot.get_current_price()
    print(f"\n💵 Giá BTC hiện tại: ${current_price:,.2f}")
    
    # 2. Kiểm tra số dư
    balance = bot.get_account_balance()
    print(f"\n💰 Số dư USDT:")
    print(f"   - Available: ${balance['USDT']['free']:,.2f}")
    print(f"   - In use: ${balance['USDT']['used']:,.2f}")
    print(f"   - Total: ${balance['USDT']['total']:,.2f}")
    
    # 3. Demo mở vị thế SHORT để hedge
    print("\n" + "-"*60)
    print("📝 DEMO: Mở vị thế SHORT (hedge khi thị trường đi xuống)")
    print("-"*60)
    
    # Mở SHORT 0.01 BTC với leverage 10x 1k$
    bot.open_short_position(amount=0.01, leverage=10)
    
    time.sleep(2)  # Đợi lệnh execute
    
    # 4. Hiển thị vị thế hiện tại
    bot.display_positions()
    
    # 5. Demo đóng vị thế
    print("\n" + "-"*60)
    print("📝 DEMO: Đóng vị thế SHORT bằng market order")
    print("-"*60)
    
    input("\n⏸️  Nhấn Enter để đóng vị thế SHORT...")
    
    bot.close_position_market(side='short')
    
    time.sleep(2)
    
    # 6. Kiểm tra lại vị thế
    bot.display_positions()
    
    print("\n" + "="*60)
    print("✅ DEMO HOÀN TẤT")
    print("="*60)


if __name__ == "__main__":
    """
    Hướng dẫn sử dụng:
    
    1. Cài đặt thư viện: pip install ccxt
    
    2. Tạo API key trên Binance:
       - Vào Binance Futures Testnet: https://testnet.binancefuture.com
       - Tạo API key với quyền trading
       
    3. Thay API_KEY và API_SECRET trong hàm demo_hedging()
    
    4. Chạy file: python binance_hedging_bot.py
    
    ⚠️ LƯU Ý:
    - Test trên TESTNET trước khi dùng mainnet
    - Quản lý risk cẩn thận với leverage
    - Luôn có stop-loss khi trade thật
    """
    
    demo_hedging()
