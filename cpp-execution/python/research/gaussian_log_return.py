import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import stats
from sklearn.mixture import GaussianMixture
import warnings
warnings.filterwarnings('ignore')
import ccxt

class CryptoGaussianStrategy:
    def __init__(self, data, lookback_window=30, z_threshold=2.0):
        """
        Khởi tạo chiến thuật trading dựa trên Gaussian log returns
        
        Parameters:
        - data: DataFrame với cột 'close' (giá đóng cửa)
        - lookback_window: số ngày để tính rolling statistics
        - z_threshold: ngưỡng z-score để trigger signals
        """
        self.data = data.copy()
        self.lookback_window = lookback_window
        self.z_threshold = z_threshold
        self.signals = pd.DataFrame(index=data.index)
        
    def calculate_log_returns(self):
        """Tính log returns và các thống kê cơ bản"""
        self.data['log_return'] = np.log(self.data['close'] / self.data['close'].shift(1))
        
        # Rolling statistics
        self.data['rolling_mean'] = self.data['log_return'].rolling(self.lookback_window).mean()
        self.data['rolling_std'] = self.data['log_return'].rolling(self.lookback_window).std()
        self.data['z_score'] = (self.data['log_return'] - self.data['rolling_mean']) / self.data['rolling_std']
        
        return self.data
    
    def calculate_trend_filters(self):
        """Tính các chỉ báo xu hướng để filter signals"""
        # EMA crossover
        self.data['ema_short'] = self.data['close'].ewm(span=12).mean()
        self.data['ema_long'] = self.data['close'].ewm(span=26).mean()
        self.data['ema_trend'] = np.where(self.data['ema_short'] > self.data['ema_long'], 1, -1)
        
        # ADX để đo độ mạnh của trend
        self.data['high_low'] = self.data['close'].rolling(14).max() - self.data['close'].rolling(14).min()
        self.data['adx_proxy'] = self.data['high_low'].rolling(14).mean() / self.data['close'].rolling(14).std()
        self.data['strong_trend'] = self.data['adx_proxy'] > self.data['adx_proxy'].quantile(0.7)
        
        # Bollinger Band width để xác định volatility regime
        self.data['bb_middle'] = self.data['close'].rolling(20).mean()
        self.data['bb_std'] = self.data['close'].rolling(20).std()
        self.data['bb_width'] = (2 * self.data['bb_std']) / self.data['bb_middle']
        self.data['low_vol_regime'] = self.data['bb_width'] < self.data['bb_width'].quantile(0.3)
        
        return self.data
    
    def calculate_var_risk_metrics(self, confidence_level=0.95):
        """Tính Value at Risk và risk metrics"""
        alpha = 1 - confidence_level
        
        # VaR dựa trên Gaussian assumption
        self.data['var_95'] = self.data['rolling_mean'] - stats.norm.ppf(confidence_level) * self.data['rolling_std']
        self.data['var_99'] = self.data['rolling_mean'] - stats.norm.ppf(0.99) * self.data['rolling_std']
        
        # Expected Shortfall (Conditional VaR)
        self.data['es_95'] = self.data['rolling_mean'] - self.data['rolling_std'] * stats.norm.pdf(stats.norm.ppf(alpha)) / alpha
        
        # Risk signal
        self.data['extreme_loss'] = self.data['log_return'] < self.data['var_99']
        self.data['extreme_gain'] = self.data['log_return'] > -self.data['var_99']  # Symmetric
        
        return self.data
    
    def detect_market_regime(self, n_components=3):
        """Sử dụng Gaussian Mixture Model để phân loại market regime"""
        # Chuẩn bị features cho GMM
        features = self.data[['log_return', 'rolling_std', 'bb_width']].dropna()
        
        # Fit GMM
        gmm = GaussianMixture(n_components=n_components, random_state=42)
        regime_labels = gmm.fit_predict(features)
        
        # Map regime labels back to original data
        regime_series = pd.Series(index=features.index, data=regime_labels)
        self.data['regime'] = regime_series.reindex(self.data.index, method='ffill')
        
        # Phân loại regime dựa trên đặc tính
        regime_stats = []
        for i in range(n_components):
            mask = self.data['regime'] == i
            avg_return = self.data.loc[mask, 'log_return'].mean()
            avg_vol = self.data.loc[mask, 'rolling_std'].mean()
            regime_stats.append((i, avg_return, avg_vol))
        
        # Sắp xếp theo volatility để đặt tên regime
        regime_stats = sorted(regime_stats, key=lambda x: x[2])
        self.regime_mapping = {
            regime_stats[0][0]: 'low_vol',
            regime_stats[1][0]: 'medium_vol', 
            regime_stats[2][0]: 'high_vol'
        }
        
        self.data['regime_name'] = self.data['regime'].map(self.regime_mapping)
        return self.data
    
    def generate_signals(self):
        """Tạo trading signals dựa trên các điều kiện khác nhau"""
        regime = self.data['regime_name']
        # 1. Z-Score Mean Reversion Strategy (cho low volatility regime)
        mean_reversion_long = (
            (self.data['z_score'] <= -self.z_threshold) & 
            # (self.data['low_vol_regime']) &
            # (regime == 'low_vol') & 
            (regime != 'high_vol') &  # tránh high vol
            (self.data['rolling_std'] < self.data['rolling_std'].quantile(0.3)) &  # low volatility
            (~self.data['strong_trend'])
        )
        
        mean_reversion_short = (
            (self.data['z_score'] >= self.z_threshold) & 
            (self.data['low_vol_regime']) &
            (~self.data['strong_trend'])
        )
        
        # 2. Trend Following với Positive Drift
        trend_long = (
            (self.data['rolling_mean'] > 0) & 
            (self.data['ema_trend'] == 1) & 
            (self.data['strong_trend']) &
            # (self.data['z_score'] > -1)  # Không quá oversold
            (regime != 'high_vol')  # tránh high vol
        )
        
        # 3. Breakout Strategy dựa trên extreme probabilities
        breakout_long = (
            (self.data['extreme_gain']) &
            (self.data['ema_trend'] == 1) &
            (self.data['rolling_mean'] > 0)
        )
        
        # 4. Risk-off signal khi extreme loss
        risk_off = self.data['extreme_loss']
        
        # Kết hợp signals
        self.signals['mean_rev_long'] = mean_reversion_long.astype(int)
        self.signals['mean_rev_short'] = mean_reversion_short.astype(int)
        self.signals['trend_long'] = trend_long.astype(int)
        self.signals['breakout_long'] = breakout_long.astype(int)
        self.signals['risk_off'] = risk_off.astype(int)
        
        # Final combined signal
        self.signals['final_signal'] = (
            self.signals['mean_rev_long'] * 1 +
            self.signals['trend_long'] * 2 +
            self.signals['breakout_long'] * 1 -
            self.signals['mean_rev_short'] * 1 -
            self.signals['risk_off'] * 3  # Strong negative weight
        )
        # set position in high volatility regime to 0
        self.signals['final_signal'] = np.where(regime == 'high_vol', 0, self.signals['final_signal'])

        # Normalize to -1, 0, 1
        self.signals['position'] = np.clip(self.signals['final_signal'], -1, 1)
        
        return self.signals
    
    def backtest_strategy(self, transaction_cost=0.001):
        """Backtest chiến thuật và tính performance metrics"""
        # Tính returns
        self.signals['market_return'] = self.data['log_return']
        self.signals['strategy_return'] = (
            self.signals['position'].shift(1) * self.signals['market_return'] - 
            abs(self.signals['position'].diff()) * transaction_cost
        )
        
        # Cumulative returns
        self.signals['cum_market_return'] = self.signals['market_return'].cumsum()
        self.signals['cum_strategy_return'] = self.signals['strategy_return'].cumsum()
        
        # Performance metrics
        strategy_ret = self.signals['strategy_return'].dropna()
        market_ret = self.signals['market_return'].dropna()
        
        metrics = {
            'Total Return (Strategy)': f"{np.exp(strategy_ret.sum())-1:.2%}",
            'Total Return (Market)': f"{np.exp(market_ret.sum())-1:.2%}",
            'Sharpe Ratio (Strategy)': f"{strategy_ret.mean()/strategy_ret.std()*np.sqrt(365):.2f}",
            'Sharpe Ratio (Market)': f"{market_ret.mean()/market_ret.std()*np.sqrt(365):.2f}",
            'Max Drawdown (Strategy)': f"{(self.signals['cum_strategy_return'] - self.signals['cum_strategy_return'].expanding().max()).min():.2%}",
            'Win Rate': f"{(strategy_ret > 0).mean():.2%}",
            'Average Trade': f"{strategy_ret.mean():.4f}",
        }
        
        return metrics
    
    def plot_results(self, figsize=(15, 12)):
        """Vẽ biểu đồ kết quả"""
        fig, axes = plt.subplots(4, 1, figsize=figsize)
        
        # 1. Price và Signals
        ax1 = axes[0]
        ax1.plot(self.data.index, self.data['close'], label='Price', alpha=0.7)
        ax1.plot(self.data.index, self.data['ema_short'], label='EMA Short', alpha=0.8)
        ax1.plot(self.data.index, self.data['ema_long'], label='EMA Long', alpha=0.8)
        
        # Highlight signals
        long_signals = self.signals[self.signals['position'] > 0].index
        short_signals = self.signals[self.signals['position'] < 0].index
        
        ax1.scatter(long_signals, self.data.loc[long_signals, 'close'], 
                   color='green', marker='^', s=50, alpha=0.8, label='Long')
        ax1.scatter(short_signals, self.data.loc[short_signals, 'close'], 
                   color='red', marker='v', s=50, alpha=0.8, label='Short')
        
        ax1.set_title('Price Chart với Trading Signals')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        
        # 2. Z-Score
        ax2 = axes[1]
        ax2.plot(self.data.index, self.data['z_score'], label='Z-Score', color='purple')
        ax2.axhline(y=self.z_threshold, color='red', linestyle='--', alpha=0.7, label=f'+{self.z_threshold}')
        ax2.axhline(y=-self.z_threshold, color='red', linestyle='--', alpha=0.7, label=f'-{self.z_threshold}')
        ax2.axhline(y=0, color='black', linestyle='-', alpha=0.5)
        ax2.fill_between(self.data.index, -self.z_threshold, self.z_threshold, alpha=0.2, color='gray')
        ax2.set_title('Z-Score của Log Returns')
        ax2.legend()
        ax2.grid(True, alpha=0.3)
        
        # 3. Market Regime
        ax3 = axes[2]
        regime_colors = {'low_vol': 'green', 'medium_vol': 'orange', 'high_vol': 'red'}
        
        for regime, color in regime_colors.items():
            mask = self.data['regime_name'] == regime
            if mask.any():
                ax3.scatter(self.data.index[mask], [regime]*mask.sum(), 
                           c=color, alpha=0.6, label=regime, s=20)
        
        ax3.set_title('Market Regime Detection')
        ax3.legend()
        ax3.grid(True, alpha=0.3)
        
        # 4. Cumulative Returns
        ax4 = axes[3]
        ax4.plot(self.signals.index, np.exp(self.signals['cum_strategy_return']), 
                label='Strategy', linewidth=2)
        ax4.plot(self.signals.index, np.exp(self.signals['cum_market_return']), 
                label='Buy & Hold', alpha=0.8)
        ax4.set_title('Cumulative Returns Comparison')
        ax4.legend()
        ax4.grid(True, alpha=0.3)
        ax4.set_ylabel('Cumulative Return')
        
        plt.tight_layout()
        plt.show()
        
        return fig
    
    def run_full_analysis(self):
        """Chạy toàn bộ phân tích"""
        print("🚀 Bắt đầu phân tích Crypto Gaussian Strategy...")
        
        # Step 1: Calculate log returns và statistics
        print("📊 Tính toán log returns và rolling statistics...")
        self.calculate_log_returns()
        
        # Step 2: Trend filters
        print("📈 Tính toán trend filters...")
        self.calculate_trend_filters()
        
        # Step 3: Risk metrics
        print("⚠️ Tính toán VaR và risk metrics...")
        self.calculate_var_risk_metrics()
        
        # Step 4: Market regime detection
        print("🔍 Phát hiện market regime bằng GMM...")
        self.detect_market_regime()
        
        # Step 5: Generate signals
        print("💡 Tạo trading signals...")
        self.generate_signals()
        
        # Step 6: Backtest
        print("📈 Backtest strategy...")
        metrics = self.backtest_strategy()
        
        # Display results
        print("\n" + "="*50)
        print("📊 KẾT QUẢ BACKTEST")
        print("="*50)
        for key, value in metrics.items():
            print(f"{key:25}: {value}")
        
        # Show signal statistics
        print("\n" + "="*50)
        print("📈 THỐNG KÊ SIGNALS")
        print("="*50)
        signal_stats = {
            'Mean Reversion Long': self.signals['mean_rev_long'].sum(),
            'Mean Reversion Short': self.signals['mean_rev_short'].sum(),
            'Trend Following Long': self.signals['trend_long'].sum(),
            'Breakout Long': self.signals['breakout_long'].sum(),
            'Risk-Off Signals': self.signals['risk_off'].sum(),
        }
        
        for key, value in signal_stats.items():
            print(f"{key:25}: {value}")
        
        # Regime statistics
        print("\n" + "="*50)
        print("🎯 PHÂN TÍCH MARKET REGIME")
        print("="*50)
        regime_summary = self.data.groupby('regime_name').agg({
            'log_return': ['count', 'mean', 'std'],
            'rolling_std': 'mean'
        }).round(4)
        print(regime_summary)
        
        print("\n✅ Phân tích hoàn tất! Gọi plot_results() để xem biểu đồ.")
        
        return metrics

# Ví dụ sử dụng với sample data
def create_sample_crypto_data(days=500):
    """Tạo dữ liệu crypto giả lập để test"""
    np.random.seed(42)
    
    # Tạo price với regime switching behavior
    dates = pd.date_range(start='2023-01-01', periods=days, freq='D')
    
    # Simulate different regimes
    regime_changes = [0, 150, 300, 450]
    regimes = [0.001, -0.002, 0.003, 0]  # Different drift levels
    vols = [0.02, 0.05, 0.03, 0.04]     # Different volatility levels
    
    price = [100]  # Starting price
    
    for i in range(1, days):
        # Determine current regime
        regime_idx = sum(1 for change in regime_changes if i >= change) - 1
        
        # Generate return with current regime parameters
        log_ret = np.random.normal(regimes[regime_idx], vols[regime_idx])
        new_price = price[-1] * np.exp(log_ret)
        price.append(new_price)
    
    return pd.DataFrame({
        'close': price
    }, index=dates)
# a function to get real crypto data from Binance via ccxt
def get_crypto_data(symbol='BTC/USDT', exchange='binance', days=500):
    """Lấy dữ liệu crypto từ Binance"""
    exchange = getattr(ccxt, exchange)()
    since = exchange.parse8601(f'2023-01-01T00:00:00Z')
    ohlcv = exchange.fetch_ohlcv(symbol, timeframe='1d', since=since, limit=days)
    
    data = pd.DataFrame(ohlcv, columns=['timestamp', 'open', 'high', 'low', 'close', 'volume'])
    data['date'] = pd.to_datetime(data['timestamp'], unit='ms')
    data.set_index('date', inplace=True)
    return data[['close']]

# Chạy thử nghiệm
if __name__ == "__main__":
    # Tạo sample data
    print("🎲 Tạo sample crypto data...")
    # sample_data = create_sample_crypto_data(days=365)
    # Uncomment the line below to use real data from Binance
    sample_data = get_crypto_data(symbol='BTC/USDT', days=365)
    sample_data.index.name = 'date'
    sample_data.reset_index(inplace=True)
    sample_data.rename(columns={'close': 'close'}, inplace=True)
    print("✅ Sample data đã được tạo thành công!")
    print("📅 Dữ liệu từ:", sample_data.index.min(), "đến", sample_data.index.max())
    # Khởi tạo strategy
    strategy = CryptoGaussianStrategy(
        data=sample_data, 
        lookback_window=45, 
        z_threshold=1
    )
    
    # Chạy phân tích đầy đủ
    results = strategy.run_full_analysis()
    
    # Vẽ biểu đồ (uncomment để xem)
    strategy.plot_results()