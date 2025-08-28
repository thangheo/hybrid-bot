import yfinance as yf
import numpy as np
import pandas as pd
from sklearn.mixture import GaussianMixture
from scipy import stats
import warnings
warnings.filterwarnings("ignore")

class MinimalCryptoStrategy:
    def __init__(self, symbol, lookback=30, z_threshold=1.5, enable_stop_loss=True, 
                 max_drawdown=0.15, trailing_stop=0.10):
        self.symbol = symbol
        self.lookback = lookback
        self.z_threshold = z_threshold
        self.enable_stop_loss = enable_stop_loss
        self.max_drawdown_threshold = max_drawdown
        self.trailing_stop_threshold = trailing_stop
        
    def download_data(self, start_date="2021-01-01", end_date="2024-12-31"):
        """Download price data"""
        print(f"Downloading {self.symbol} data...")
        df = yf.download(self.symbol, start=start_date, end=end_date, 
                        interval='1d', progress=False, auto_adjust=False)
        
        if isinstance(df.columns, pd.MultiIndex):
            df.columns = df.columns.droplevel(1)
            
        return df[['Close']].rename(columns={'Close': 'close'}).dropna()
    
    def add_features(self, data):
        """Add technical features"""
        data = data.copy()
        
        # Basic features
        data['log_return'] = np.log(data['close'] / data['close'].shift(1))
        data['rolling_mean'] = data['log_return'].rolling(self.lookback).mean()
        data['rolling_std'] = data['log_return'].rolling(self.lookback).std()
        data['z_score'] = (data['log_return'] - data['rolling_mean']) / data['rolling_std']
        
        # Trend
        data['ema_short'] = data['close'].ewm(span=12).mean()
        data['ema_long'] = data['close'].ewm(span=26).mean()
        data['ema_trend'] = np.where(data['ema_short'] > data['ema_long'], 1, -1)
        
        # RSI
        delta = data['close'].diff()
        gain = (delta.where(delta > 0, 0)).rolling(14).mean()
        loss = (-delta.where(delta < 0, 0)).rolling(14).mean()
        rs = gain / (loss + 1e-8)
        data['rsi'] = 100 - (100 / (1 + rs))
        
        return data
    
    def detect_regime(self, data):
        """Simple regime detection"""
        features = data[['log_return', 'rolling_std']].dropna()
        
        gmm = GaussianMixture(n_components=3, random_state=42)
        gmm.fit(features)
        
        regime_labels = gmm.predict(features)
        regime_series = pd.Series(index=features.index, data=regime_labels)
        
        # Map to volatility levels
        vol_stats = []
        for i in range(3):
            subset = data.loc[regime_series[regime_series == i].index]
            if len(subset) > 0:
                vol_stats.append((i, subset['rolling_std'].mean()))
        
        vol_stats.sort(key=lambda x: x[1])
        regime_map = {vol_stats[0][0]: 'low_vol', vol_stats[1][0]: 'medium_vol', vol_stats[2][0]: 'high_vol'}
        
        regime_series = regime_series.reindex(data.index, method='ffill')
        return regime_series.map(regime_map)
    
    def generate_signals(self, data, regime):
        """Generate trading signals based on regime"""
        signals = pd.Series(0, index=data.index)
        
        for idx in data.index:
            if idx not in regime.index or pd.isna(regime[idx]):
                continue
                
            current_regime = regime[idx]
            signal = 0
            
            # Regime-specific strategies
            if current_regime == 'low_vol':
                # Mean reversion
                if data.loc[idx, 'z_score'] <= -self.z_threshold and data.loc[idx, 'ema_trend'] == -1:
                    signal += 2.0
                # RSI oversold
                if data.loc[idx, 'rsi'] < 30 and data.loc[idx, 'ema_trend'] == -1:
                    signal += 1.0
                    
            elif current_regime == 'medium_vol':
                # Trend following
                if data.loc[idx, 'rolling_mean'] > 0 and data.loc[idx, 'ema_trend'] == 1:
                    signal += 2.0
                # Mean reversion (weaker)
                if data.loc[idx, 'z_score'] <= -self.z_threshold:
                    signal += 0.5
                    
            elif current_regime == 'high_vol':
                # Only breakout
                if (data.loc[idx, 'log_return'] > 2 * data.loc[idx, 'rolling_std'] and 
                    data.loc[idx, 'ema_trend'] == 1):
                    signal += 2.0
            
            # Risk-off (VaR)
            var_threshold = (data.loc[idx, 'rolling_mean'] - 
                           stats.norm.ppf(0.99) * data.loc[idx, 'rolling_std'])
            if data.loc[idx, 'log_return'] < var_threshold:
                signal -= 2.0
            
            signals[idx] = np.clip(signal, -1, 1)
        
        return signals
    
    def apply_stop_loss(self, data, signals):
        """Apply stop-loss management"""
        if not self.enable_stop_loss:
            return signals
            
        final_positions = []
        entry_prices = {}
        high_water_marks = {}
        cum_pnl = 0.0
        max_cum_pnl = 0.0
        
        for idx, current_signal in signals.items():
            current_price = data.loc[idx, 'close']
            current_return = data.loc[idx, 'log_return'] if not pd.isna(data.loc[idx, 'log_return']) else 0
            
            # Update cumulative P&L
            cum_pnl += current_return
            max_cum_pnl = max(max_cum_pnl, cum_pnl)
            
            # Portfolio drawdown stop
            if max_cum_pnl > 0:
                drawdown = (max_cum_pnl - cum_pnl) / max_cum_pnl
                if drawdown > self.max_drawdown_threshold:
                    final_positions.append(0.0)
                    entry_prices.clear()
                    high_water_marks.clear()
                    continue
            
            # Position tracking
            if current_signal != 0 and idx not in entry_prices:
                entry_prices[idx] = current_price
                high_water_marks[idx] = current_price
                
            elif current_signal != 0 and idx in entry_prices:
                # Update high water mark
                if current_signal > 0:
                    high_water_marks[idx] = max(high_water_marks[idx], current_price)
                    
                    # Trailing stop
                    entry_price = entry_prices[idx]
                    high_water = high_water_marks[idx]
                    
                    if current_price > entry_price:  # In profit
                        trailing_stop_price = high_water * (1 - self.trailing_stop_threshold)
                        if current_price < trailing_stop_price:
                            # Trigger trailing stop
                            final_positions.append(0.0)
                            del entry_prices[idx]
                            del high_water_marks[idx]
                            continue
                            
            elif current_signal == 0 and idx in entry_prices:
                del entry_prices[idx]
                del high_water_marks[idx]
            
            final_positions.append(current_signal)
        
        return pd.Series(final_positions, index=signals.index)
    
    def backtest(self, data, positions):
        """Calculate performance metrics"""
        # Strategy returns
        strategy_returns = (positions.shift(1) * data['log_return'] - 
                          abs(positions.diff()) * 0.001)  # 0.1% transaction cost
        
        cum_returns = strategy_returns.cumsum()
        
        # Metrics
        total_return = np.exp(strategy_returns.sum()) - 1
        sharpe = strategy_returns.mean() / strategy_returns.std() * np.sqrt(365) if strategy_returns.std() > 0 else 0
        
        # Drawdown
        running_max = cum_returns.cummax()
        drawdown = cum_returns - running_max
        max_dd = drawdown.min()
        
        # Trades
        trades = positions.diff().fillna(0)
        num_trades = (trades != 0).sum()
        win_rate = (strategy_returns > 0).mean()
        
        return {
            'total_return': f"{total_return:.2%}",
            'sharpe_ratio': f"{sharpe:.3f}",
            'max_drawdown': f"{max_dd:.2%}",
            'win_rate': f"{win_rate:.2%}",
            'num_trades': num_trades,
            'strategy_returns': strategy_returns,
            'cumulative_returns': cum_returns
        }
    
    def run(self):
        """Main execution"""
        print(f"\n🚀 Running strategy for {self.symbol}")
        print(f"Parameters: lookback={self.lookback}, z_threshold={self.z_threshold}")
        print(f"Stop-loss: {self.enable_stop_loss}, Max DD: {self.max_drawdown_threshold:.1%}")
        
        # Download and prepare data
        data = self.download_data()
        data = self.add_features(data)
        
        # Detect regime
        regime = self.detect_regime(data)
        
        # Generate signals
        raw_signals = self.generate_signals(data, regime)
        final_positions = self.apply_stop_loss(data, raw_signals)
        
        # Backtest
        results = self.backtest(data, final_positions)
        
        # Display results
        print(f"\n📊 Results:")
        for key, value in results.items():
            if key not in ['strategy_returns', 'cumulative_returns']:
                print(f"  {key}: {value}")
        
        # Regime distribution
        regime_dist = regime.value_counts()
        print(f"\n🎯 Regime Distribution:")
        for reg, count in regime_dist.items():
            print(f"  {reg}: {count/len(regime)*100:.1f}%")
        
        return results, {'data': data, 'regime': regime, 'positions': final_positions}

def run_multiple_strategies():
    """Run strategy on multiple symbols with different parameters"""
    
    # INPUT SECTION - MODIFY THESE
    symbols = ['BTC-USD', 'ETH-USD']  # Add more symbols here
    
    parameter_sets = [
        {'lookback': 20, 'z_threshold': 1.0, 'enable_stop_loss': True, 'max_drawdown': 0.10, 'trailing_stop': 0.08},
        {'lookback': 30, 'z_threshold': 1.5, 'enable_stop_loss': True, 'max_drawdown': 0.15, 'trailing_stop': 0.10},
        {'lookback': 45, 'z_threshold': 2.0, 'enable_stop_loss': False, 'max_drawdown': 0.20, 'trailing_stop': 0.15},
    ]
    
    all_results = []
    
    for symbol in symbols:
        for i, params in enumerate(parameter_sets, 1):
            print(f"\n{'='*60}")
            print(f"TESTING {symbol} - Parameter Set {i}")
            print(f"{'='*60}")
            
            try:
                strategy = MinimalCryptoStrategy(
                    symbol=symbol,
                    lookback=params['lookback'],
                    z_threshold=params['z_threshold'],
                    enable_stop_loss=params['enable_stop_loss'],
                    max_drawdown=params['max_drawdown'],
                    trailing_stop=params['trailing_stop']
                )
                
                results, _ = strategy.run()
                
                # Store results
                result_row = {
                    'symbol': symbol,
                    'param_set': i,
                    **params,
                    'total_return': float(results['total_return'].strip('%')) / 100,
                    'sharpe_ratio': float(results['sharpe_ratio']),
                    'max_drawdown': float(results['max_drawdown'].strip('%')) / 100,
                    'win_rate': float(results['win_rate'].strip('%')) / 100,
                    'num_trades': results['num_trades']
                }
                all_results.append(result_row)
                
            except Exception as e:
                print(f"❌ Error with {symbol}: {e}")
                continue
    
    # Summary
    if all_results:
        results_df = pd.DataFrame(all_results)
        results_df = results_df.sort_values('sharpe_ratio', ascending=False)
        
        print(f"\n{'='*80}")
        print("📈 SUMMARY - TOP 5 RESULTS (by Sharpe Ratio)")
        print(f"{'='*80}")
        
        top_5 = results_df.head(5)
        for i, (_, row) in enumerate(top_5.iterrows(), 1):
            print(f"\n{i}. {row['symbol']} (Set {row['param_set']}):")
            print(f"   Return: {row['total_return']:.2%}, Sharpe: {row['sharpe_ratio']:.3f}")
            print(f"   Params: lookback={row['lookback']}, z={row['z_threshold']}, stop_loss={row['enable_stop_loss']}")
        
        # Save to CSV
        results_df.to_csv('crypto_strategy_results.csv', index=False)
        print(f"\n💾 Full results saved to 'crypto_strategy_results.csv'")
    
    return all_results

# MAIN EXECUTION
if __name__ == '__main__':
    # Option 1: Run single strategy
    print("Choose execution mode:")
    print("1. Single strategy test")
    print("2. Multiple symbols and parameters")
    
    choice = input("Enter choice (1 or 2): ").strip()
    
    if choice == '1':
        # Single strategy input
        symbol = input("Enter symbol (e.g., BTC-USD): ").strip() or 'BTC-USD'
        lookback = int(input("Enter lookback window (default 30): ") or 30)
        z_threshold = float(input("Enter z-threshold (default 1.5): ") or 1.5)
        enable_sl = input("Enable stop-loss? (y/n, default y): ").strip().lower() != 'n'
        
        strategy = MinimalCryptoStrategy(
            symbol=symbol,
            lookback=lookback,
            z_threshold=z_threshold,
            enable_stop_loss=enable_sl
        )
        
        results, data = strategy.run()
        
    else:
        # Multiple strategies
        run_multiple_strategies()