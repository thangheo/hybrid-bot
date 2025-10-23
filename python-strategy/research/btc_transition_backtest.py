"""
BTC Transition Matrix Backtesting Framework
Strategies based on 6-candle binary patterns
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from datetime import datetime
import yfinance as yf
from collections import defaultdict
import warnings
import os
warnings.filterwarnings('ignore')


class TransitionMatrixBacktest:
    def __init__(self, data, initial_capital=10000, commission=0.001):
        """
        Parameters:
        - data: DataFrame with OHLC data
        - initial_capital: starting capital in USD
        - commission: 0.001 = 0.1% per trade
        """
        self.data = data.copy()
        self.initial_capital = initial_capital
        self.commission = commission
        
        # Prepare data
        self._prepare_data()
        
    def _prepare_data(self):
        """Convert price data to binary patterns"""
        # Calculate daily returns (close to close)
        self.data['return'] = self.data['close'].pct_change()
        
        # Binary: 1 if green candle (close > open), 0 if red
        self.data['binary'] = (self.data['close'] > self.data['open']).astype(int)
        
        # Create 6-candle patterns
        self.data['pattern'] = ''
        for i in range(6, len(self.data)):
            pattern = ''.join(self.data['binary'].iloc[i-6:i].astype(str).values)
            self.data.loc[self.data.index[i], 'pattern'] = pattern
            
        # Next candle binary (target)
        self.data['next_binary'] = self.data['binary'].shift(-1)
        
        # Calculate transition probabilities from historical data
        self._calculate_transition_probs()
        
    def _calculate_transition_probs(self):
        """Calculate transition probabilities from data"""
        self.transitions = defaultdict(lambda: {'count': 0, 'next_1': 0, 'next_0': 0})
        
        for i in range(len(self.data) - 1):
            pattern = self.data.iloc[i]['pattern']
            if len(pattern) == 6:
                next_val = self.data.iloc[i + 1]['binary']
                self.transitions[pattern]['count'] += 1
                if next_val == 1:
                    self.transitions[pattern]['next_1'] += 1
                else:
                    self.transitions[pattern]['next_0'] += 1
        
        # Calculate probabilities
        self.transition_probs = {}
        for pattern, stats in self.transitions.items():
            if stats['count'] > 0:
                prob_green = stats['next_1'] / stats['count']
                self.transition_probs[pattern] = prob_green
                
    def strategy_mean_reversion(self, threshold_consecutive=3, min_prob=0.6):
        """
        Strategy 1: Mean Reversion
        - After 3+ consecutive same-color candles, expect reversal
        - Enter when probability supports reversal
        """
        signals = pd.Series(0, index=self.data.index)
        
        for i in range(6, len(self.data)):
            pattern = self.data.iloc[i]['pattern']
            if len(pattern) < 6:
                continue
                
            # Count consecutive same color in last 3 candles
            last_3 = pattern[-3:]
            
            # Check for 3+ greens (111)
            if last_3 == '111':
                prob_green = self.transition_probs.get(pattern, 0.5)
                # Expect reversal to red, so SHORT
                if prob_green < (1 - min_prob):
                    signals.iloc[i] = -1
                    
            # Check for 3+ reds (000)
            elif last_3 == '000':
                prob_green = self.transition_probs.get(pattern, 0.5)
                # Expect reversal to green, so LONG
                if prob_green > min_prob:
                    signals.iloc[i] = 1
                    
        return signals
    
    def strategy_high_prob_patterns(self, min_prob_threshold=0.65, min_samples=10):
        """
        Strategy 2: High Probability Patterns
        - Trade only patterns with >65% probability and sufficient sample size
        """
        signals = pd.Series(0, index=self.data.index)
        
        for i in range(6, len(self.data)):
            pattern = self.data.iloc[i]['pattern']
            if len(pattern) < 6:
                continue
                
            # Check if we have enough data for this pattern
            if pattern in self.transitions:
                count = self.transitions[pattern]['count']
                prob_green = self.transition_probs.get(pattern, 0.5)
                
                if count >= min_samples:
                    # Strong bullish signal
                    if prob_green >= min_prob_threshold:
                        signals.iloc[i] = 1
                    # Strong bearish signal
                    elif prob_green <= (1 - min_prob_threshold):
                        signals.iloc[i] = -1
                        
        return signals
    
    def strategy_fade_extremes(self, extreme_threshold=0.62):
        """
        Strategy 3: Fade Extremes
        - When probability is moderately extreme, fade it (contrarian)
        - Based on mean reversion to 0.50
        """
        signals = pd.Series(0, index=self.data.index)
        
        for i in range(6, len(self.data)):
            pattern = self.data.iloc[i]['pattern']
            if len(pattern) < 6:
                continue
                
            prob_green = self.transition_probs.get(pattern, 0.5)
            
            # If prob is high, fade it (short)
            if prob_green >= extreme_threshold:
                signals.iloc[i] = -1
            # If prob is low, fade it (long)
            elif prob_green <= (1 - extreme_threshold):
                signals.iloc[i] = 1
                
        return signals
    
    def strategy_alternating_pattern(self):
        """
        Strategy 4: Alternating Pattern
        - Based on top patterns like 101010, 010101
        - Trade the expected alternation
        """
        signals = pd.Series(0, index=self.data.index)
        
        alternating_patterns = {
            '101010': 0,  # Expect next to be 0 (red)
            '010101': 1,  # Expect next to be 1 (green)
            '101011': 0,  # Last was green, expect red
            '010110': 1,  # Last was red, expect green
        }
        
        for i in range(6, len(self.data)):
            pattern = self.data.iloc[i]['pattern']
            
            if pattern in alternating_patterns:
                expected = alternating_patterns[pattern]
                signals.iloc[i] = 1 if expected == 1 else -1
                
        return signals
    
    def backtest_strategy(self, signals, strategy_name="Strategy", hold_days=1):
        """
        Backtest a signal series
        - signals: 1 = long, -1 = short, 0 = no position
        - hold_days: how many days to hold position
        """
        # Initialize tracking
        capital = self.initial_capital
        position = 0
        entry_price = 0
        hold_counter = 0
        
        equity_curve = []
        trades = []
        
        for i in range(len(self.data)):
            current_price = self.data.iloc[i]['close']
            signal = signals.iloc[i]
            
            # close position if hold period expired
            if position != 0 and hold_counter >= hold_days:
                # Exit position
                if position == 1:  # Long
                    pnl = (current_price - entry_price) / entry_price
                else:  # Short
                    pnl = (entry_price - current_price) / entry_price
                
                # Apply commission
                pnl -= self.commission * 2  # Entry + exit
                
                capital *= (1 + pnl)
                
                trades.append({
                    'entry_date': self.data.index[i - hold_days],
                    'exit_date': self.data.index[i],
                    'entry_price': entry_price,
                    'exit_price': current_price,
                    'position': 'Long' if position == 1 else 'Short',
                    'pnl_pct': pnl * 100,
                    'capital': capital
                })
                
                position = 0
                hold_counter = 0
                
            # Enter new position
            if position == 0 and signal != 0:
                position = signal
                entry_price = current_price
                hold_counter = 0
                
            # Increment hold counter
            if position != 0:
                hold_counter += 1
                
            equity_curve.append(capital)
            
        # Calculate metrics
        results = self._calculate_metrics(equity_curve, trades, strategy_name)
        
        return results, trades, equity_curve
    
    def _calculate_metrics(self, equity_curve, trades, strategy_name):
        """Calculate performance metrics"""
        if len(trades) == 0:
            return {
                'strategy': strategy_name,
                'total_return': 0,
                'num_trades': 0,
                'win_rate': 0,
                'avg_win': 0,
                'avg_loss': 0,
                'sharpe_ratio': 0,
                'max_drawdown': 0
            }
        
        trades_df = pd.DataFrame(trades)
        
        # Basic metrics
        total_return = (equity_curve[-1] - self.initial_capital) / self.initial_capital * 100
        num_trades = len(trades)
        
        wins = trades_df[trades_df['pnl_pct'] > 0]
        losses = trades_df[trades_df['pnl_pct'] <= 0]
        
        win_rate = len(wins) / num_trades * 100 if num_trades > 0 else 0
        avg_win = wins['pnl_pct'].mean() if len(wins) > 0 else 0
        avg_loss = losses['pnl_pct'].mean() if len(losses) > 0 else 0
        
        # Sharpe ratio (simplified, assuming daily returns)
        returns = pd.Series(equity_curve).pct_change().dropna()
        sharpe = returns.mean() / returns.std() * np.sqrt(365) if returns.std() > 0 else 0
        
        # Max drawdown
        equity_series = pd.Series(equity_curve)
        running_max = equity_series.expanding().max()
        drawdown = (equity_series - running_max) / running_max * 100
        max_drawdown = drawdown.min()
        
        return {
            'strategy': strategy_name,
            'total_return': total_return,
            'num_trades': num_trades,
            'win_rate': win_rate,
            'avg_win': avg_win,
            'avg_loss': avg_loss,
            'profit_factor': abs(avg_win * len(wins) / (avg_loss * len(losses))) if len(losses) > 0 and avg_loss != 0 else float('inf'),
            'sharpe_ratio': sharpe,
            'max_drawdown': max_drawdown,
            'final_capital': equity_curve[-1]
        }
    
    def run_all_strategies(self, hold_days=1):
        """Run all strategies and compare"""
        print("="*80)
        print(f"BACKTESTING BTC TRANSITION MATRIX STRATEGIES")
        print(f"Period: {self.data.index[0].date()} to {self.data.index[-1].date()}")
        print(f"Initial Capital: ${self.initial_capital:,.2f}")
        print(f"Commission: {self.commission*100}%")
        print(f"Hold Period: {hold_days} day(s)")
        print("="*80)
        print()
        
        all_results = []
        all_equity_curves = {}
        
        # Buy & Hold benchmark
        bh_return = (self.data['close'].iloc[-1] - self.data['close'].iloc[0]) / self.data['close'].iloc[0] * 100
        bh_equity = self.initial_capital * (1 + bh_return/100)
        print(f"📊 Buy & Hold Benchmark: {bh_return:.2f}% (${bh_equity:,.2f})")
        print()
        
        # Strategy 1: Mean Reversion
        print("Testing Strategy 1: Mean Reversion...")
        signals_mr = self.strategy_mean_reversion()
        results_mr, trades_mr, equity_mr = self.backtest_strategy(signals_mr, "Mean Reversion", hold_days)
        all_results.append(results_mr)
        all_equity_curves['Mean Reversion'] = equity_mr
        
        # Strategy 2: High Probability Patterns
        print("Testing Strategy 2: High Probability Patterns...")
        signals_hp = self.strategy_high_prob_patterns()
        results_hp, trades_hp, equity_hp = self.backtest_strategy(signals_hp, "High Prob Patterns", hold_days)
        all_results.append(results_hp)
        all_equity_curves['High Prob Patterns'] = equity_hp
        
        # Strategy 3: Fade Extremes
        print("Testing Strategy 3: Fade Extremes...")
        signals_fe = self.strategy_fade_extremes()
        results_fe, trades_fe, equity_fe = self.backtest_strategy(signals_fe, "Fade Extremes", hold_days)
        all_results.append(results_fe)
        all_equity_curves['Fade Extremes'] = equity_fe
        
        # Strategy 4: Alternating Pattern
        print("Testing Strategy 4: Alternating Pattern...")
        signals_ap = self.strategy_alternating_pattern()
        results_ap, trades_ap, equity_ap = self.backtest_strategy(signals_ap, "Alternating Pattern", hold_days)
        all_results.append(results_ap)
        all_equity_curves['Alternating Pattern'] = equity_ap
        
        print("\n" + "="*80)
        print("RESULTS SUMMARY")
        print("="*80)
        
        results_df = pd.DataFrame(all_results)
        print(results_df.to_string(index=False))
        
        # Find best strategy
        best_strategy = results_df.loc[results_df['total_return'].idxmax()]
        print(f"\n🏆 Best Strategy: {best_strategy['strategy']}")
        print(f"   Return: {best_strategy['total_return']:.2f}%")
        print(f"   Win Rate: {best_strategy['win_rate']:.2f}%")
        print(f"   Sharpe: {best_strategy['sharpe_ratio']:.2f}")
        
        return results_df, all_equity_curves, {
            'mr': (signals_mr, trades_mr),
            'hp': (signals_hp, trades_hp),
            'fe': (signals_fe, trades_fe),
            'ap': (signals_ap, trades_ap)
        }
    
    def plot_results(self, equity_curves, results_df):
        """Plot equity curves and performance comparison"""
        fig, axes = plt.subplots(2, 2, figsize=(16, 12))
        
        # 1. Equity Curves
        ax1 = axes[0, 0]
        for strategy, equity in equity_curves.items():
            # Use correct index length
            plot_dates = self.data.index[:len(equity)]
            ax1.plot(plot_dates, equity, label=strategy, linewidth=2)
        
        # Buy & Hold
        bh_prices = self.data['close'].values
        bh_equity = self.initial_capital * bh_prices / bh_prices[0]
        ax1.plot(self.data.index, bh_equity, label='Buy & Hold', 
                linestyle='--', color='black', linewidth=2, alpha=0.7)
        
        ax1.set_title('Equity Curves Comparison', fontsize=14, fontweight='bold')
        ax1.set_xlabel('Date')
        ax1.set_ylabel('Portfolio Value ($)')
        ax1.legend()
        ax1.grid(True, alpha=0.3)
        ax1.axhline(y=self.initial_capital, color='red', linestyle=':', alpha=0.5)
        
        # 2. Returns Comparison
        ax2 = axes[0, 1]
        strategies = results_df['strategy'].values
        returns = results_df['total_return'].values
        colors = ['green' if r > 0 else 'red' for r in returns]
        ax2.bar(strategies, returns, color=colors, alpha=0.7)
        ax2.set_title('Total Returns by Strategy', fontsize=14, fontweight='bold')
        ax2.set_ylabel('Return (%)')
        ax2.grid(True, alpha=0.3, axis='y')
        ax2.axhline(y=0, color='black', linestyle='-', linewidth=0.5)
        plt.setp(ax2.xaxis.get_majorticklabels(), rotation=45, ha='right')
        
        # 3. Win Rate vs Profit Factor
        ax3 = axes[1, 0]
        scatter = ax3.scatter(results_df['win_rate'], results_df['profit_factor'], 
                             s=results_df['num_trades']*2, alpha=0.6, c=returns, cmap='RdYlGn')
        for idx, row in results_df.iterrows():
            ax3.annotate(row['strategy'], (row['win_rate'], row['profit_factor']),
                        fontsize=8, ha='center')
        ax3.set_title('Win Rate vs Profit Factor (size = # trades)', fontsize=14, fontweight='bold')
        ax3.set_xlabel('Win Rate (%)')
        ax3.set_ylabel('Profit Factor')
        ax3.grid(True, alpha=0.3)
        plt.colorbar(scatter, ax=ax3, label='Return (%)')
        
        # 4. Risk-Return Profile
        ax4 = axes[1, 1]
        ax4.scatter(results_df['max_drawdown'], results_df['total_return'], 
                   s=200, alpha=0.6, c=returns, cmap='RdYlGn')
        for idx, row in results_df.iterrows():
            ax4.annotate(row['strategy'], (row['max_drawdown'], row['total_return']),
                        fontsize=8, ha='center')
        ax4.set_title('Risk-Return Profile', fontsize=14, fontweight='bold')
        ax4.set_xlabel('Max Drawdown (%)')
        ax4.set_ylabel('Total Return (%)')
        ax4.grid(True, alpha=0.3)
        ax4.axhline(y=0, color='black', linestyle='-', linewidth=0.5)
        ax4.axvline(x=0, color='black', linestyle='-', linewidth=0.5)
        
        plt.tight_layout()
        return fig


def main(csv_path=None):
    """Main execution function"""
    
    if csv_path and os.path.exists(csv_path):
        print(f"Loading BTC data from {csv_path}...")
        btc = pd.read_csv(csv_path, index_col=0, parse_dates=True)
        print(f"Data loaded: {len(btc)} days")
    else:
        print("Downloading BTC data...")
        # Download BTC data
        btc = yf.download('BTC-USD', start='2020-01-01', end='2025-10-22', progress=False)
        print(f"Data downloaded: {len(btc)} days")
    
    if len(btc) == 0:
        print("ERROR: No data available. Please provide CSV file with OHLC data.")
        return None, None, None
        
    print(f"Period: {btc.index[0].date()} to {btc.index[-1].date()}")
    print()
    
    # Initialize backtest
    bt = TransitionMatrixBacktest(btc, initial_capital=10000, commission=0.001)
    
    # Run all strategies
    results_df, equity_curves, strategy_details = bt.run_all_strategies(hold_days=1)
    
    # Plot results
    print("\nGenerating plots...")
    fig = bt.plot_results(equity_curves, results_df)
    plt.savefig('btc_backtest_results.png', dpi=300, bbox_inches='tight')
    print("✅ Plot saved to btc_backtest_results.png")
    
    # Save detailed results
    results_df.to_csv('./strategy_results.csv', index=False)
    print("✅ Results saved to strategy_results.csv")
    
    # Additional analysis
    print("\n" + "="*80)
    print("PATTERN ANALYSIS")
    print("="*80)
    
    # Show top 10 most reliable patterns
    pattern_stats = []
    for pattern, stats in bt.transitions.items():
        if stats['count'] >= 10:  # At least 10 occurrences
            prob_green = bt.transition_probs[pattern]
            pattern_stats.append({
                'pattern': pattern,
                'count': stats['count'],
                'prob_green': prob_green,
                'prob_red': 1 - prob_green,
                'edge': abs(prob_green - 0.5)
            })
    
    pattern_df = pd.DataFrame(pattern_stats).sort_values('edge', ascending=False).head(15)
    print("\nTop 15 Patterns with Strongest Edge (min 10 occurrences):")
    print(pattern_df.to_string(index=False))
    
    pattern_df.to_csv('./top_patterns.csv', index=False)
    print("\n✅ Pattern analysis saved to outputs/top_patterns.csv")
    
    return bt, results_df, strategy_details


if __name__ == "__main__":
    # Use sample data by default, or provide your own CSV path
    csv_path = input("Enter path to BTC OHLC CSV file (or press Enter to download data): ").strip()
    if csv_path == '':
        csv_path = '../data/BTC_USDT_1d_5000candles.csv'  # Default sample path
    
    bt, results_df, strategy_details = main(csv_path)
    
    if bt is not None:
        print("\n" + "="*80)
        print("BACKTEST COMPLETE!")
        print("="*80)
