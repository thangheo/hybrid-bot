import pandas as pd
import numpy as np
import requests
import time
import threading
from datetime import datetime, timedelta
import json
import sqlite3
from typing import Dict, List, Optional
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import warnings
warnings.filterwarnings('ignore')

class RealTimeCryptoStrategy:
    def __init__(self, symbol='BTCUSDT', lookback_window=30, z_threshold=2.0, 
                 update_interval=60, db_path='crypto_data.db'):
        """
        Real-time crypto strategy với Gaussian log returns
        
        Parameters:
        - symbol: Trading pair (e.g., 'BTCUSDT', 'ETHUSDT')
        - lookback_window: Số periods để tính rolling statistics
        - z_threshold: Ngưỡng z-score
        - update_interval: Interval update data (giây)
        - db_path: Path to SQLite database
        """
        self.symbol = symbol
        self.lookback_window = lookback_window
        self.z_threshold = z_threshold
        self.update_interval = update_interval
        self.db_path = db_path
        
        # Trading state
        self.current_position = 0  # -1, 0, 1
        self.portfolio_value = 10000  # Starting capital
        self.trade_history = []
        self.is_running = False
        
        # Data storage
        self.data = pd.DataFrame()
        self.signals = pd.DataFrame()
        
        # Initialize database
        self.init_database()
        
        # Load historical data
        self.load_historical_data()
        
    def init_database(self):
        """Khởi tạo SQLite database để lưu data"""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        # Tạo bảng price data
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS price_data (
                timestamp INTEGER PRIMARY KEY,
                symbol TEXT,
                open REAL,
                high REAL,
                low REAL,
                close REAL,
                volume REAL
            )
        ''')
        
        # Tạo bảng signals
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS signals (
                timestamp INTEGER PRIMARY KEY,
                symbol TEXT,
                z_score REAL,
                position INTEGER,
                signal_type TEXT,
                confidence REAL
            )
        ''')
        
        # Tạo bảng trades
        cursor.execute('''
            CREATE TABLE IF NOT EXISTS trades (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                timestamp INTEGER,
                symbol TEXT,
                side TEXT,
                price REAL,
                quantity REAL,
                pnl REAL
            )
        ''')
        
        conn.commit()
        conn.close()
        
    def fetch_binance_data(self, interval='1h', limit=500):
        """Lấy dữ liệu từ Binance API"""
        try:
            url = "https://api.binance.com/api/v3/klines"
            params = {
                'symbol': self.symbol,
                'interval': interval,
                'limit': limit
            }
            
            response = requests.get(url, params=params, timeout=10)
            response.raise_for_status()
            
            data = response.json()
            
            # Convert to DataFrame
            df = pd.DataFrame(data, columns=[
                'timestamp', 'open', 'high', 'low', 'close', 'volume',
                'close_time', 'quote_volume', 'trades_count', 
                'taker_buy_base', 'taker_buy_quote', 'ignore'
            ])
            
            # Clean data
            df['timestamp'] = pd.to_datetime(df['timestamp'], unit='ms')
            for col in ['open', 'high', 'low', 'close', 'volume']:
                df[col] = df[col].astype(float)
                
            df = df[['timestamp', 'open', 'high', 'low', 'close', 'volume']]
            df.set_index('timestamp', inplace=True)
            
            return df
            
        except Exception as e:
            print(f"❌ Lỗi fetch data từ Binance: {e}")
            return None
    
    def fetch_coingecko_data(self, days=30):
        """Backup: Lấy dữ liệu từ CoinGecko API"""
        try:
            # Map symbol to CoinGecko ID
            symbol_map = {
                'BTCUSDT': 'bitcoin',
                'ETHUSDT': 'ethereum',
                'ADAUSDT': 'cardano',
                'SOLUSDT': 'solana',
                'DOTUSDT': 'polkadot'
            }
            
            coin_id = symbol_map.get(self.symbol, 'bitcoin')
            
            url = f"https://api.coingecko.com/api/v3/coins/{coin_id}/market_chart"
            params = {
                'vs_currency': 'usd',
                'days': days,
                'interval': 'hourly'
            }
            
            response = requests.get(url, params=params, timeout=10)
            response.raise_for_status()
            
            data = response.json()
            
            # Convert prices to DataFrame
            prices = data['prices']
            df = pd.DataFrame(prices, columns=['timestamp', 'close'])
            df['timestamp'] = pd.to_datetime(df['timestamp'], unit='ms')
            df.set_index('timestamp', inplace=True)
            
            # Add dummy OHLV data
            df['open'] = df['close'].shift(1).fillna(df['close'])
            df['high'] = df[['open', 'close']].max(axis=1) * 1.001
            df['low'] = df[['open', 'close']].min(axis=1) * 0.999
            df['volume'] = 1000000  # Dummy volume
            
            return df[['open', 'high', 'low', 'close', 'volume']]
            
        except Exception as e:
            print(f"❌ Lỗi fetch data từ CoinGecko: {e}")
            return None
    
    def load_historical_data(self):
        """Load dữ liệu lịch sử và tính toán indicators"""
        print(f"📊 Đang load dữ liệu lịch sử cho {self.symbol}...")
        
        # Try Binance first, then CoinGecko
        data = self.fetch_binance_data()
        if data is None:
            print("🔄 Thử CoinGecko API...")
            data = self.fetch_coingecko_data()
        
        if data is not None:
            self.data = data
            self.calculate_indicators()
            self.save_to_database()
            print(f"✅ Loaded {len(self.data)} data points")
        else:
            print("❌ Không thể load được dữ liệu!")
    
    def calculate_indicators(self):
        """Tính toán các chỉ báo kỹ thuật"""
        if len(self.data) < self.lookback_window:
            return
            
        # Log returns
        self.data['log_return'] = np.log(self.data['close'] / self.data['close'].shift(1))
        
        # Rolling statistics
        self.data['rolling_mean'] = self.data['log_return'].rolling(self.lookback_window).mean()
        self.data['rolling_std'] = self.data['log_return'].rolling(self.lookback_window).std()
        self.data['z_score'] = (self.data['log_return'] - self.data['rolling_mean']) / self.data['rolling_std']
        
        # Trend indicators
        self.data['ema_short'] = self.data['close'].ewm(span=12).mean()
        self.data['ema_long'] = self.data['close'].ewm(span=26).mean()
        self.data['ema_trend'] = np.where(self.data['ema_short'] > self.data['ema_long'], 1, -1)
        
        # Volatility regime
        self.data['bb_middle'] = self.data['close'].rolling(20).mean()
        self.data['bb_std'] = self.data['close'].rolling(20).std()
        self.data['bb_width'] = (2 * self.data['bb_std']) / self.data['bb_middle']
        self.data['low_vol_regime'] = self.data['bb_width'] < self.data['bb_width'].quantile(0.3)
        
        # VaR
        from scipy import stats
        self.data['var_95'] = self.data['rolling_mean'] - stats.norm.ppf(0.95) * self.data['rolling_std']
        self.data['extreme_loss'] = self.data['log_return'] < self.data['var_95']
        
    def generate_signal(self) -> Dict:
        """Tạo trading signal dựa trên dữ liệu hiện tại"""
        if len(self.data) < self.lookback_window:
            return {'signal': 0, 'confidence': 0, 'reason': 'Insufficient data'}
        
        latest = self.data.iloc[-1]
        
        signal = 0
        confidence = 0
        reason = "No signal"
        
        # Mean reversion signal
        if latest['z_score'] <= -self.z_threshold and latest['low_vol_regime']:
            signal = 1
            confidence = min(abs(latest['z_score']) / self.z_threshold, 1.0)
            reason = f"Mean reversion long (z={latest['z_score']:.2f})"
            
        elif latest['z_score'] >= self.z_threshold and latest['low_vol_regime']:
            signal = -1
            confidence = min(abs(latest['z_score']) / self.z_threshold, 1.0)
            reason = f"Mean reversion short (z={latest['z_score']:.2f})"
        
        # Trend following signal
        elif latest['rolling_mean'] > 0 and latest['ema_trend'] == 1 and latest['z_score'] > -1:
            signal = 1
            confidence = 0.7
            reason = f"Trend following long (drift={latest['rolling_mean']:.4f})"
        
        # Risk-off signal
        elif latest['extreme_loss']:
            signal = -1
            confidence = 0.9
            reason = "Risk-off: Extreme loss detected"
        
        return {
            'signal': signal,
            'confidence': confidence,
            'reason': reason,
            'z_score': latest['z_score'],
            'price': latest['close']
        }
    
    def execute_trade(self, signal_info: Dict):
        """Thực hiện trade (giả lập)"""
        new_position = signal_info['signal']
        
        if new_position != self.current_position:
            timestamp = datetime.now()
            price = signal_info['price']
            
            # Calculate trade details
            position_change = new_position - self.current_position
            trade_value = abs(position_change) * price * 0.1  # 10% of capital per trade
            
            # Calculate PnL of previous position
            pnl = 0
            if self.current_position != 0 and len(self.trade_history) > 0:
                last_trade = self.trade_history[-1]
                pnl = self.current_position * (price - last_trade['price']) * last_trade['quantity']
                self.portfolio_value += pnl
            
            # Record trade
            trade = {
                'timestamp': timestamp,
                'side': 'BUY' if position_change > 0 else 'SELL',
                'price': price,
                'quantity': trade_value / price,
                'position_change': position_change,
                'pnl': pnl,
                'portfolio_value': self.portfolio_value,
                'reason': signal_info['reason']
            }
            
            self.trade_history.append(trade)
            self.current_position = new_position
            
            # Save to database
            self.save_trade_to_database(trade)
            
            print(f"🔄 TRADE EXECUTED:")
            print(f"   Time: {timestamp.strftime('%Y-%m-%d %H:%M:%S')}")
            print(f"   {trade['side']} {self.symbol} @ ${price:.2f}")
            print(f"   Position: {self.current_position}")
            print(f"   PnL: ${pnl:.2f}")
            print(f"   Portfolio: ${self.portfolio_value:.2f}")
            print(f"   Reason: {signal_info['reason']}")
            print("-" * 50)
    
    def save_to_database(self):
        """Lưu data vào database"""
        conn = sqlite3.connect(self.db_path)
        
        # Save price data
        df_db = self.data.reset_index()
        df_db['symbol'] = self.symbol
        df_db['timestamp'] = df_db['timestamp'].astype(int) // 10**9
        
        df_db[['timestamp', 'symbol', 'open', 'high', 'low', 'close', 'volume']].to_sql(
            'price_data', conn, if_exists='replace', index=False
        )
        
        conn.close()
    
    def save_trade_to_database(self, trade):
        """Lưu trade vào database"""
        conn = sqlite3.connect(self.db_path)
        cursor = conn.cursor()
        
        cursor.execute('''
            INSERT INTO trades (timestamp, symbol, side, price, quantity, pnl)
            VALUES (?, ?, ?, ?, ?, ?)
        ''', (
            int(trade['timestamp'].timestamp()),
            self.symbol,
            trade['side'],
            trade['price'],
            trade['quantity'],
            trade['pnl']
        ))
        
        conn.commit()
        conn.close()
    
    def update_data_loop(self):
        """Main loop để update data real-time"""
        print(f"🚀 Bắt đầu real-time monitoring cho {self.symbol}")
        print(f"⏰ Update interval: {self.update_interval} giây")
        
        while self.is_running:
            try:
                # Fetch latest data
                new_data = self.fetch_binance_data(limit=2)  # Get latest 2 candles
                
                if new_data is not None and len(new_data) > 0:
                    # Update data
                    latest_time = new_data.index[-1]
                    
                    if len(self.data) == 0 or latest_time > self.data.index[-1]:
                        # Add new data point
                        if len(self.data) > 0:
                            # Only add if it's actually new
                            self.data = pd.concat([self.data, new_data.iloc[-1:]])
                        else:
                            self.data = new_data
                        
                        # Keep only recent data to save memory
                        if len(self.data) > 1000:
                            self.data = self.data.tail(1000)
                        
                        # Recalculate indicators
                        self.calculate_indicators()
                        
                        # Generate signal
                        signal_info = self.generate_signal()
                        
                        # Execute trade if signal exists
                        if signal_info['signal'] != 0:
                            self.execute_trade(signal_info)
                        
                        # Print status
                        current_price = self.data['close'].iloc[-1]
                        z_score = self.data['z_score'].iloc[-1] if not pd.isna(self.data['z_score'].iloc[-1]) else 0
                        
                        print(f"📊 {datetime.now().strftime('%H:%M:%S')} | "
                              f"{self.symbol}: ${current_price:.2f} | "
                              f"Z-Score: {z_score:.2f} | "
                              f"Position: {self.current_position} | "
                              f"Portfolio: ${self.portfolio_value:.2f}")
                
            except Exception as e:
                print(f"❌ Error in update loop: {e}")
            
            # Wait for next update
            time.sleep(self.update_interval)
    
    def start_monitoring(self):
        """Bắt đầu monitoring real-time"""
        if not self.is_running:
            self.is_running = True
            self.monitor_thread = threading.Thread(target=self.update_data_loop)
            self.monitor_thread.daemon = True
            self.monitor_thread.start()
            print("✅ Real-time monitoring started!")
        else:
            print("⚠️ Monitoring already running!")
    
    def stop_monitoring(self):
        """Dừng monitoring"""
        self.is_running = False
        if hasattr(self, 'monitor_thread'):
            self.monitor_thread.join()
        print("🛑 Real-time monitoring stopped!")
    
    def get_performance_summary(self):
        """Lấy tóm tắt performance"""
        if not self.trade_history:
            return "No trades executed yet."
        
        total_trades = len(self.trade_history)
        total_pnl = sum(trade['pnl'] for trade in self.trade_history)
        win_trades = sum(1 for trade in self.trade_history if trade['pnl'] > 0)
        win_rate = win_trades / total_trades if total_trades > 0 else 0
        
        current_price = self.data['close'].iloc[-1] if len(self.data) > 0 else 0
        
        summary = f"""
🎯 PERFORMANCE SUMMARY
{'='*50}
Symbol: {self.symbol}
Current Price: ${current_price:.2f}
Current Position: {self.current_position}
Portfolio Value: ${self.portfolio_value:.2f}
Total Return: {((self.portfolio_value - 10000) / 10000 * 100):.2f}%

📊 TRADING STATS
Total Trades: {total_trades}
Win Rate: {win_rate:.1%}
Total PnL: ${total_pnl:.2f}
Avg PnL per Trade: ${total_pnl/total_trades if total_trades > 0 else 0:.2f}

📈 RECENT TRADES
"""
        
        # Show last 5 trades
        for trade in self.trade_history[-5:]:
            summary += f"{trade['timestamp'].strftime('%H:%M:%S')} | {trade['side']} @ ${trade['price']:.2f} | PnL: ${trade['pnl']:.2f}\n"
        
        return summary
    
    def plot_live_chart(self, figsize=(15, 10)):
        """Vẽ biểu đồ real-time"""
        if len(self.data) < 10:
            print("⚠️ Chưa đủ dữ liệu để vẽ biểu đồ")
            return
        
        fig, axes = plt.subplots(3, 1, figsize=figsize)
        
        # 1. Price chart
        ax1 = axes[0]
        recent_data = self.data.tail(100)  # Show last 100 points
        
        ax1.plot(recent_data.index, recent_data['close'], label='Price', linewidth=2)
        ax1.plot(recent_data.index, recent_data['ema_short'], label='EMA 12', alpha=0.7)
        ax1.plot(recent_data.index, recent_data['ema_long'], label='EMA 26', alpha=0.7)
        
        # Mark trades
        if self.trade_history:
            for trade in self.trade_history[-10:]:  # Last 10 trades
                trade_time = trade['timestamp']
                if trade_time >= recent_data.index[0]:
                    color = 'green' if trade['side'] == 'BUY' else 'red'
                    marker = '^' if trade['side'] == 'BUY' else 'v'
                    ax1.scatter(trade_time, trade['price'], color=color, marker=marker, s=100, alpha=0.8)
        
        ax1.set_title(f'{self.symbol} Real-time Price Chart')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # 2. Z-Score
        ax2 = axes[1]
        ax2.plot(recent_data.index, recent_data['z_score'], color='purple', linewidth=2, label='Z-Score')
        ax2.axhline(y=self.z_threshold, color='red', linestyle='--', alpha=0.7)
        ax2.axhline(y=-self.z_threshold, color='red', linestyle='--', alpha=0.7)
        ax2.axhline(y=0, color='black', linestyle='-', alpha=0.5)
        ax2.fill_between(recent_data.index, -self.z_threshold, self.z_threshold, alpha=0.2, color='gray')
        ax2.set_title('Z-Score của Log Returns')
        ax2.legend()
        ax2.grid(True, alpha=0.3)
        
        # 3. Portfolio value
        ax3 = axes[2]
        if self.trade_history:
            trade_times = [trade['timestamp'] for trade in self.trade_history]
            portfolio_values = [trade['portfolio_value'] for trade in self.trade_history]
            ax3.plot(trade_times, portfolio_values, color='green', linewidth=2, marker='o')
            ax3.set_title('Portfolio Value Over Time')
            ax3.grid(True, alpha=0.3)
        
        plt.tight_layout()
        plt.show()
        
        return fig

# Dashboard class để quản lý multiple strategies
class CryptoDashboard:
    def __init__(self):
        self.strategies = {}
    
    def add_strategy(self, name: str, symbol: str, **kwargs):
        """Thêm strategy mới"""
        self.strategies[name] = RealTimeCryptoStrategy(symbol=symbol, **kwargs)
        print(f"✅ Added strategy '{name}' for {symbol}")
    
    def start_all(self):
        """Start tất cả strategies"""
        for name, strategy in self.strategies.items():
            strategy.start_monitoring()
            print(f"🚀 Started {name}")
    
    def stop_all(self):
        """Stop tất cả strategies"""
        for name, strategy in self.strategies.items():
            strategy.stop_monitoring()
            print(f"🛑 Stopped {name}")
    
    def get_summary(self):
        """Lấy tóm tắt tất cả strategies"""
        summary = "🎯 MULTI-STRATEGY DASHBOARD\n" + "="*60 + "\n"
        
        for name, strategy in self.strategies.items():
            summary += f"\n📊 {name.upper()}:\n"
            summary += strategy.get_performance_summary()
            summary += "\n" + "-"*50 + "\n"
        
        return summary

# Example usage
if __name__ == "__main__":
    print("🚀 CRYPTO REAL-TIME STRATEGY SYSTEM")
    print("=" * 50)
    
    # Tạo single strategy
    btc_strategy = RealTimeCryptoStrategy(
        symbol='BTCUSDT',
        lookback_window=30,
        z_threshold=2.0,
        update_interval=300  # 5 phút
    )
    
    print("\n📊 Performance Summary:")
    print(btc_strategy.get_performance_summary())
    
    # Uncomment để start monitoring
    # print("\n🎯 Starting real-time monitoring...")
    # btc_strategy.start_monitoring()
    
    # # Run for some time
    # time.sleep(60)  # Run for 1 minute
    
    # # Stop and show results
    # btc_strategy.stop_monitoring()
    # print(btc_strategy.get_performance_summary())
    
    # Multi-strategy dashboard example
    print("\n" + "="*50)
    print("🎛️ MULTI-STRATEGY DASHBOARD EXAMPLE")
    print("="*50)
    
    dashboard = CryptoDashboard()
    dashboard.add_strategy("BTC_Conservative", "BTCUSDT", z_threshold=2.5, update_interval=300)
    dashboard.add_strategy("ETH_Aggressive", "ETHUSDT", z_threshold=1.5, update_interval=180)
    
    print("\n📊 Dashboard Summary:")
    print(dashboard.get_summary())
    
    print("\n✅ Setup completed! Uncomment monitoring code to start trading.")
    print("💡 Tips:")
    print("   - Adjust update_interval based on your needs (60-300s recommended)")
    print("   - Monitor multiple pairs with CryptoDashboard")
    print("   - Use plot_live_chart() to visualize real-time data")
    print("   - Check database files for historical data")