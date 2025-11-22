#!/usr/bin/env python3
# ================================================================
# tests/test_regime_detection.py
# Test and validate regime detection on historical data
# ================================================================

import sys
import os
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

import pandas as pd
import numpy as np
from datetime import datetime, timedelta
import matplotlib.pyplot as plt
from typing import Dict, List, Tuple

from src.strategy.filters.regime_detector import MarketRegimeDetector
from src.strategy.strategies.ema_pullback_with_imbalance import EMAPullbackImbalanceStrategy


class RegimeDetectionValidator:
    """Validate regime detection on historical data"""

    def __init__(self, config: dict = None):
        """Initialize validator with configuration"""
        self.config = config or self._get_default_config()
        self.detector = MarketRegimeDetector(self.config.get('regime_config', {}))

    def _get_default_config(self) -> dict:
        """Get default configuration for testing"""
        return {
            'regime_config': {
                'adx_trending': 25,
                'adx_strong_trend': 40,
                'chop_threshold': 61.8,
                'hurst_trending_min': 0.55,
                'hurst_window': 100,
                'ema_separation_min': 0.015,
                'atr_percentile': 30,
                'bb_squeeze_threshold': 0.5,
                'weight_adx': 0.3,
                'weight_chop': 0.2,
                'weight_hurst': 0.2,
                'weight_ema_sep': 0.15,
                'weight_atr': 0.15,
            }
        }

    def generate_synthetic_data(self, regime_type: str, periods: int = 500) -> pd.DataFrame:
        """
        Generate synthetic OHLCV data for different regime types

        Args:
            regime_type: 'trending', 'ranging', 'choppy', 'volatile'
            periods: Number of candles to generate
        """
        np.random.seed(42)

        base_price = 50000
        data = pd.DataFrame(index=pd.date_range(start='2024-01-01', periods=periods, freq='5T'))

        if regime_type == 'trending':
            # Strong uptrend with pullbacks
            trend = np.linspace(0, 0.3, periods)  # 30% gain over period
            noise = np.random.normal(0, 0.005, periods)  # Small noise
            returns = trend + noise
            close = base_price * (1 + returns).cumprod()

        elif regime_type == 'ranging':
            # Sideways movement
            noise = np.random.normal(0, 0.003, periods)
            close = base_price + np.cumsum(noise) * 100
            # Keep price within range
            close = np.clip(close, base_price * 0.95, base_price * 1.05)

        elif regime_type == 'choppy':
            # Frequent direction changes
            direction_changes = np.random.choice([-1, 1], periods)
            magnitude = np.random.uniform(0.002, 0.01, periods)
            returns = direction_changes * magnitude
            close = base_price * (1 + returns).cumprod()

        elif regime_type == 'volatile':
            # High volatility spikes
            noise = np.random.normal(0, 0.02, periods)  # High noise
            returns = noise
            close = base_price * (1 + returns).cumprod()

        else:
            raise ValueError(f"Unknown regime type: {regime_type}")

        # Generate OHLC from close
        data['close'] = close
        data['open'] = close * (1 + np.random.uniform(-0.002, 0.002, periods))
        data['high'] = close * (1 + np.abs(np.random.uniform(0, 0.005, periods)))
        data['low'] = close * (1 - np.abs(np.random.uniform(0, 0.005, periods)))
        data['volume'] = np.random.uniform(1000, 5000, periods)

        return data

    def test_regime_detection(self, data: pd.DataFrame, expected_regime: str) -> Dict:
        """
        Test regime detection on data

        Args:
            data: OHLCV DataFrame
            expected_regime: Expected regime type

        Returns:
            Test results dictionary
        """
        print(f"\n{'='*60}")
        print(f"Testing {expected_regime.upper()} regime detection")
        print(f"{'='*60}")

        # Detect regime
        regime_data = self.detector.detect_regime(data)

        # Calculate statistics
        total_bars = len(regime_data)
        regime_counts = regime_data['regime'].value_counts()
        tradeable_count = regime_data['is_tradeable'].sum()

        print(f"\n📊 Results ({total_bars} bars):")
        print(f"\nRegime Distribution:")
        for regime, count in regime_counts.items():
            pct = count / total_bars * 100
            print(f"  {regime:15s}: {count:4d} bars ({pct:5.1f}%)")

        print(f"\nTradeable: {tradeable_count} bars ({tradeable_count/total_bars*100:.1f}%)")
        print(f"Avg Confidence: {regime_data['confidence'].mean():.2%}")

        print(f"\n📈 Indicators (latest values):")
        print(f"  ADX: {regime_data['adx'].iloc[-1]:.1f}")
        print(f"  Choppiness: {regime_data['chop_index'].iloc[-1]:.1f}")
        print(f"  Hurst: {regime_data['hurst'].iloc[-1]:.2f}")
        print(f"  EMA Separation: {regime_data['ema_separation'].iloc[-1]:.2%}")

        # Accuracy check
        if expected_regime == 'trending':
            correct = (regime_data['regime'] == 'trending').sum()
        elif expected_regime == 'ranging':
            correct = (regime_data['regime'] == 'ranging').sum()
        elif expected_regime == 'choppy':
            correct = (regime_data['regime'] == 'choppy').sum()
        elif expected_regime == 'volatile':
            correct = (regime_data['regime'] == 'volatile').sum()
        else:
            correct = 0

        accuracy = correct / total_bars * 100
        print(f"\n✓ Detection Accuracy: {accuracy:.1f}% matched expected regime")

        return {
            'expected': expected_regime,
            'regime_counts': regime_counts.to_dict(),
            'tradeable_pct': tradeable_count / total_bars * 100,
            'avg_confidence': regime_data['confidence'].mean(),
            'accuracy': accuracy,
            'regime_data': regime_data
        }

    def plot_regime_analysis(self, data: pd.DataFrame, regime_data: pd.DataFrame,
                            title: str = "Regime Detection Analysis"):
        """
        Plot regime detection results

        Args:
            data: OHLCV DataFrame
            regime_data: Regime detection results
            title: Plot title
        """
        fig, axes = plt.subplots(6, 1, figsize=(14, 12), sharex=True)

        # 1. Price and regimes
        ax = axes[0]
        ax.plot(data.index, data['close'], label='Close', color='black', linewidth=0.8)

        # Color background by regime
        regimes = regime_data['regime'].unique()
        colors = {'trending': 'green', 'ranging': 'gray', 'choppy': 'orange', 'volatile': 'red'}

        for i in range(len(regime_data)-1):
            regime = regime_data['regime'].iloc[i]
            is_tradeable = regime_data['is_tradeable'].iloc[i]
            alpha = 0.3 if is_tradeable else 0.1
            color = colors.get(regime, 'blue')

            ax.axvspan(data.index[i], data.index[i+1],
                      color=color, alpha=alpha, linewidth=0)

        ax.set_ylabel('Price')
        ax.set_title(title)
        ax.legend()
        ax.grid(True, alpha=0.3)

        # 2. ADX
        ax = axes[1]
        ax.plot(data.index, regime_data['adx'], label='ADX', color='blue')
        ax.axhline(y=25, color='orange', linestyle='--', label='Trending threshold')
        ax.axhline(y=40, color='green', linestyle='--', label='Strong trend')
        ax.set_ylabel('ADX')
        ax.legend()
        ax.grid(True, alpha=0.3)

        # 3. Choppiness Index
        ax = axes[2]
        ax.plot(data.index, regime_data['chop_index'], label='Choppiness', color='purple')
        ax.axhline(y=61.8, color='red', linestyle='--', label='Choppy threshold')
        ax.axhline(y=38.2, color='green', linestyle='--', label='Trending threshold')
        ax.set_ylabel('Choppiness')
        ax.legend()
        ax.grid(True, alpha=0.3)

        # 4. Hurst Exponent
        ax = axes[3]
        ax.plot(data.index, regime_data['hurst'], label='Hurst', color='brown')
        ax.axhline(y=0.5, color='gray', linestyle='--', label='Random walk')
        ax.axhline(y=0.55, color='green', linestyle='--', label='Trending')
        ax.axhline(y=0.45, color='red', linestyle='--', label='Mean reverting')
        ax.set_ylabel('Hurst')
        ax.legend()
        ax.grid(True, alpha=0.3)

        # 5. Confidence
        ax = axes[4]
        ax.plot(data.index, regime_data['confidence'], label='Confidence', color='darkblue')
        ax.fill_between(data.index, 0, regime_data['confidence'],
                        where=regime_data['is_tradeable'],
                        color='green', alpha=0.3, label='Tradeable')
        ax.set_ylabel('Confidence')
        ax.legend()
        ax.grid(True, alpha=0.3)

        # 6. Regime distribution (bar chart of current regime)
        ax = axes[5]
        regime_numeric = regime_data['regime'].map({
            'trending': 3,
            'ranging': 1,
            'choppy': 0,
            'volatile': 2
        })
        ax.fill_between(data.index, 0, regime_numeric, alpha=0.5)
        ax.set_ylabel('Regime Type')
        ax.set_yticks([0, 1, 2, 3])
        ax.set_yticklabels(['Choppy', 'Ranging', 'Volatile', 'Trending'])
        ax.grid(True, alpha=0.3)

        plt.xlabel('Time')
        plt.tight_layout()

        # Save plot
        output_dir = Path(__file__).parent / 'output'
        output_dir.mkdir(exist_ok=True)
        filename = output_dir / f"regime_test_{title.replace(' ', '_').lower()}.png"
        plt.savefig(filename, dpi=150, bbox_inches='tight')
        print(f"\n📊 Plot saved to: {filename}")

        plt.close()

    def test_strategy_integration(self, data: pd.DataFrame) -> Dict:
        """
        Test full strategy integration with regime filter

        Args:
            data: OHLCV DataFrame

        Returns:
            Results dictionary
        """
        print(f"\n{'='*60}")
        print(f"Testing Strategy Integration with Regime Filter")
        print(f"{'='*60}")

        # Create strategy with regime filter enabled
        config = {
            'ema_short': 20,
            'ema_long': 50,
            'rsi_period': 14,
            'use_regime_filter': True,
            'min_regime_confidence': 0.6,
            'regime_config': self.config['regime_config']
        }

        strategy = EMAPullbackImbalanceStrategy(config)

        # Calculate indicators
        indicators = strategy.calculate_indicators(data)

        # Generate signals
        signals = strategy.generate_signals(data, indicators)

        # Analyze results
        total_signals = (signals['signal'] != 0).sum()
        buy_signals = (signals['signal'] == 1).sum()
        sell_signals = (signals['signal'] == -1).sum()
        tradeable_bars = signals['is_tradeable'].sum()

        print(f"\n📊 Signal Generation Results:")
        print(f"  Total bars: {len(signals)}")
        print(f"  Tradeable bars: {tradeable_bars} ({tradeable_bars/len(signals)*100:.1f}%)")
        print(f"  Total signals: {total_signals}")
        print(f"    Buy signals: {buy_signals}")
        print(f"    Sell signals: {sell_signals}")

        if total_signals > 0:
            print(f"\n  Signal rate: {total_signals/tradeable_bars*100:.2f}% of tradeable bars")

        # Regime breakdown
        print(f"\n📈 Regime Breakdown:")
        for regime in signals['regime'].unique():
            regime_mask = signals['regime'] == regime
            regime_count = regime_mask.sum()
            regime_signals = (signals.loc[regime_mask, 'signal'] != 0).sum()

            print(f"  {regime:15s}: {regime_count:4d} bars, {regime_signals:3d} signals")

        return {
            'total_signals': total_signals,
            'buy_signals': buy_signals,
            'sell_signals': sell_signals,
            'tradeable_pct': tradeable_bars / len(signals) * 100,
            'signals': signals
        }

    def run_comprehensive_test(self):
        """Run comprehensive test suite"""
        print("\n" + "="*60)
        print("REGIME DETECTION COMPREHENSIVE TEST SUITE")
        print("="*60)

        results = {}

        # Test 1: Trending market
        print("\n[TEST 1] Generating TRENDING market data...")
        trending_data = self.generate_synthetic_data('trending', periods=500)
        results['trending'] = self.test_regime_detection(trending_data, 'trending')
        self.plot_regime_analysis(trending_data, results['trending']['regime_data'],
                                  "Trending Market Test")

        # Test 2: Ranging market
        print("\n[TEST 2] Generating RANGING market data...")
        ranging_data = self.generate_synthetic_data('ranging', periods=500)
        results['ranging'] = self.test_regime_detection(ranging_data, 'ranging')
        self.plot_regime_analysis(ranging_data, results['ranging']['regime_data'],
                                 "Ranging Market Test")

        # Test 3: Choppy market
        print("\n[TEST 3] Generating CHOPPY market data...")
        choppy_data = self.generate_synthetic_data('choppy', periods=500)
        results['choppy'] = self.test_regime_detection(choppy_data, 'choppy')
        self.plot_regime_analysis(choppy_data, results['choppy']['regime_data'],
                                 "Choppy Market Test")

        # Test 4: Volatile market
        print("\n[TEST 4] Generating VOLATILE market data...")
        volatile_data = self.generate_synthetic_data('volatile', periods=500)
        results['volatile'] = self.test_regime_detection(volatile_data, 'volatile')
        self.plot_regime_analysis(volatile_data, results['volatile']['regime_data'],
                                  "Volatile Market Test")

        # Test 5: Strategy integration
        print("\n[TEST 5] Testing strategy integration...")
        strategy_results = self.test_strategy_integration(trending_data)
        results['strategy'] = strategy_results

        # Summary
        self._print_summary(results)

        return results

    def _print_summary(self, results: Dict):
        """Print test summary"""
        print("\n" + "="*60)
        print("TEST SUMMARY")
        print("="*60)

        print(f"\n{'Regime Type':<15} {'Accuracy':<12} {'Tradeable %':<15} {'Avg Confidence':<15}")
        print("-" * 60)

        for regime_type in ['trending', 'ranging', 'choppy', 'volatile']:
            if regime_type in results:
                r = results[regime_type]
                print(f"{regime_type:<15} {r['accuracy']:>6.1f}%     {r['tradeable_pct']:>6.1f}%        {r['avg_confidence']:>6.1%}")

        print("\n✅ Expected behavior:")
        print("  - Trending: HIGH tradeable %, HIGH confidence")
        print("  - Ranging/Choppy/Volatile: LOW tradeable %, signals filtered out")

        if 'strategy' in results:
            s = results['strategy']
            print(f"\n🎯 Strategy Integration:")
            print(f"  Total signals generated: {s['total_signals']}")
            print(f"  Tradeable bars: {s['tradeable_pct']:.1f}%")
            print(f"  Signal quality: {'GOOD' if s['total_signals'] > 0 else 'No signals (check thresholds)'}")


def main():
    """Main test execution"""
    print("\n🚀 Starting Regime Detection Validation Tests...")

    # Create validator
    validator = RegimeDetectionValidator()

    # Run comprehensive tests
    results = validator.run_comprehensive_test()

    print("\n✅ All tests completed!")
    print(f"   Check output plots in: {Path(__file__).parent / 'output'}")


if __name__ == "__main__":
    main()
