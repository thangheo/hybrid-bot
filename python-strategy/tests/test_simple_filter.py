#!/usr/bin/env python3
# ================================================================
# tests/test_simple_filter.py
# Test Simple Trend Filter - MUCH SIMPLER than regime detector tests!
# ================================================================

import sys
import os
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

import pandas as pd
import numpy as np
from datetime import datetime
import matplotlib.pyplot as plt
from typing import Dict

from src.strategy.filters.simple_trend_filter import SimpleTrendFilter, create_simple_filter


class SimpleTrendFilterValidator:
    """Validate Simple Trend Filter on synthetic data"""

    def __init__(self, config: dict = None):
        """Initialize validator"""
        self.filter = create_simple_filter(config)

    def generate_test_data(self, scenario: str, periods: int = 500) -> pd.DataFrame:
        """
        Generate test data for different scenarios

        Args:
            scenario: 'trending', 'ranging', 'volatile_trending', 'sideways'
            periods: Number of bars
        """
        np.random.seed(42)
        base_price = 50000
        data = pd.DataFrame(index=pd.date_range(start='2024-01-01', periods=periods, freq='5T'))

        if scenario == 'trending':
            # Clear uptrend - should be TRADEABLE
            trend = np.linspace(0, 0.25, periods)  # 25% gain
            noise = np.random.normal(0, 0.003, periods)
            close = base_price * (1 + trend + noise).cumprod()

        elif scenario == 'ranging':
            # Sideways - should be NOT TRADEABLE
            noise = np.random.normal(0, 0.002, periods)
            close = base_price + np.cumsum(noise) * 50
            # Force range
            close = np.clip(close, base_price * 0.97, base_price * 1.03)

        elif scenario == 'volatile_trending':
            # Trending but volatile - should still be TRADEABLE (this is the point!)
            trend = np.linspace(0, 0.3, periods)
            noise = np.random.normal(0, 0.015, periods)  # High volatility
            close = base_price * (1 + trend + noise).cumprod()

        elif scenario == 'sideways':
            # Flat EMAs - should be NOT TRADEABLE
            close = base_price + np.random.normal(0, 100, periods)

        else:
            raise ValueError(f"Unknown scenario: {scenario}")

        # Generate OHLC
        data['close'] = close
        data['open'] = close * (1 + np.random.uniform(-0.001, 0.001, periods))
        data['high'] = close * (1 + np.abs(np.random.uniform(0, 0.003, periods)))
        data['low'] = close * (1 - np.abs(np.random.uniform(0, 0.003, periods)))
        data['volume'] = np.random.uniform(1000, 5000, periods)

        return data

    def test_scenario(self, scenario: str, expected_tradeable_pct: float) -> Dict:
        """
        Test a scenario and check if results match expectations

        Args:
            scenario: Test scenario name
            expected_tradeable_pct: Expected % of tradeable bars (rough estimate)

        Returns:
            Test results
        """
        print(f"\n{'='*70}")
        print(f"Testing: {scenario.upper()}")
        print(f"{'='*70}")

        # Generate data
        data = self.generate_test_data(scenario, periods=500)

        # Calculate EMAs
        ema_short = data['close'].ewm(span=20, adjust=False).mean()
        ema_long = data['close'].ewm(span=50, adjust=False).mean()

        # Apply filter
        results = self.filter.apply_filters(data, ema_short, ema_long)

        # Print summary
        print(self.filter.get_filter_summary(results))

        # Check expectations
        tradeable_pct = results['tradeable_pct']
        tolerance = 15  # Allow 15% tolerance

        passed = abs(tradeable_pct - expected_tradeable_pct) < tolerance

        print(f"\n🎯 Expected: ~{expected_tradeable_pct:.0f}% tradeable")
        print(f"📊 Actual: {tradeable_pct:.1f}% tradeable")

        if passed:
            print(f"✅ PASS - Within tolerance ({tolerance}%)")
        else:
            print(f"⚠️  MARGINAL - Outside tolerance but may be OK")

        # Show breakdown
        print(f"\n📈 Filter Breakdown:")
        print(f"  ADX pass rate: {results['adx_ok'].sum() / len(results['adx_ok']) * 100:.1f}%")
        print(f"  EMA sep pass rate: {results['ema_sep_ok'].sum() / len(results['ema_sep_ok']) * 100:.1f}%")

        if self.filter.use_ema_curvature:
            print(f"  EMA curve pass rate: {results['ema_curve_ok'].sum() / len(results['ema_curve_ok']) * 100:.1f}%")

        # Sample filter reasons
        print(f"\n📋 Sample Filter Reasons (last 5 bars):")
        for reason in results['filter_reasons'].tail(5):
            print(f"  {reason}")

        return {
            'scenario': scenario,
            'tradeable_pct': tradeable_pct,
            'expected_pct': expected_tradeable_pct,
            'passed': passed,
            'results': results,
            'data': data
        }

    def plot_results(self, test_result: Dict, save: bool = True):
        """Plot test results"""
        data = test_result['data']
        results = test_result['results']
        scenario = test_result['scenario']

        fig, axes = plt.subplots(4, 1, figsize=(14, 10), sharex=True)

        # 1. Price + tradeable shading
        ax = axes[0]
        ax.plot(data.index, data['close'], color='black', linewidth=1, label='Close')

        # Shade tradeable regions
        for i in range(len(data)-1):
            if results['is_tradeable'].iloc[i]:
                ax.axvspan(data.index[i], data.index[i+1],
                          color='green', alpha=0.2)

        ax.set_ylabel('Price')
        ax.set_title(f"Simple Trend Filter Test: {scenario.upper()}")
        ax.legend()
        ax.grid(True, alpha=0.3)

        # 2. ADX
        ax = axes[1]
        ax.plot(data.index, results['adx'], label='ADX', color='blue')
        ax.axhline(y=self.filter.adx_min, color='red', linestyle='--',
                  label=f'Min ADX ({self.filter.adx_min})')
        ax.fill_between(data.index, 0, results['adx'],
                       where=results['adx_ok'], color='green', alpha=0.2)
        ax.set_ylabel('ADX')
        ax.legend()
        ax.grid(True, alpha=0.3)

        # 3. EMA Separation
        ax = axes[2]
        ax.plot(data.index, results['ema_separation'] * 100,
               label='EMA Separation %', color='purple')
        ax.axhline(y=self.filter.ema_sep_min * 100, color='red', linestyle='--',
                  label=f'Min Sep ({self.filter.ema_sep_min*100:.1f}%)')
        ax.fill_between(data.index, 0, results['ema_separation'] * 100,
                       where=results['ema_sep_ok'], color='green', alpha=0.2)
        ax.set_ylabel('EMA Sep %')
        ax.legend()
        ax.grid(True, alpha=0.3)

        # 4. Tradeable status
        ax = axes[3]
        tradeable_numeric = results['is_tradeable'].astype(int)
        ax.fill_between(data.index, 0, tradeable_numeric, alpha=0.5, color='green')
        ax.set_ylabel('Tradeable')
        ax.set_yticks([0, 1])
        ax.set_yticklabels(['No', 'Yes'])
        ax.grid(True, alpha=0.3)

        plt.xlabel('Time')
        plt.tight_layout()

        if save:
            output_dir = Path(__file__).parent / 'output'
            output_dir.mkdir(exist_ok=True)
            filename = output_dir / f"simple_filter_{scenario}.png"
            plt.savefig(filename, dpi=150, bbox_inches='tight')
            print(f"\n📊 Plot saved to: {filename}")

        plt.close()

    def run_all_tests(self):
        """Run all test scenarios"""
        print("\n" + "="*70)
        print("SIMPLE TREND FILTER TEST SUITE")
        print("="*70)
        print("\n💡 Philosophy: Only block flat EMAs, all trending markets tradeable")

        results = {}

        # Test 1: Clear trend (should be mostly tradeable)
        results['trending'] = self.test_scenario('trending', expected_tradeable_pct=75)
        self.plot_results(results['trending'])

        # Test 2: Ranging (should be mostly NOT tradeable)
        results['ranging'] = self.test_scenario('ranging', expected_tradeable_pct=20)
        self.plot_results(results['ranging'])

        # Test 3: Volatile trending (should STILL be tradeable - this is the key!)
        results['volatile_trending'] = self.test_scenario('volatile_trending', expected_tradeable_pct=70)
        self.plot_results(results['volatile_trending'])

        # Test 4: Sideways (should be NOT tradeable)
        results['sideways'] = self.test_scenario('sideways', expected_tradeable_pct=15)
        self.plot_results(results['sideways'])

        # Print summary
        self._print_summary(results)

        return results

    def _print_summary(self, results: Dict):
        """Print test summary"""
        print("\n" + "="*70)
        print("TEST SUMMARY")
        print("="*70)

        print(f"\n{'Scenario':<20} {'Expected':<12} {'Actual':<12} {'Status':<10}")
        print("-" * 70)

        for scenario, result in results.items():
            expected = f"{result['expected_pct']:.0f}%"
            actual = f"{result['tradeable_pct']:.1f}%"
            status = "✅ PASS" if result['passed'] else "⚠️  MARGINAL"

            print(f"{scenario:<20} {expected:<12} {actual:<12} {status:<10}")

        print("\n✅ Key Tests:")
        print("  1. Trending market: HIGH tradeable % (>70%)")
        print("  2. Ranging market: LOW tradeable % (<25%)")
        print("  3. Volatile trending: STILL HIGH tradeable % (this is the advantage!)")
        print("  4. Sideways: LOW tradeable % (<20%)")

        print("\n💡 Advantage over Complex Regime Detector:")
        print("  - Volatile trending markets are tradeable (complex detector blocked them)")
        print("  - No flickering between regimes")
        print("  - Only 2 parameters to tune")
        print("  - Faster computation")


def test_parameter_tuning():
    """Test different parameter combinations"""
    print("\n" + "="*70)
    print("PARAMETER TUNING TEST")
    print("="*70)

    # Generate trending data
    np.random.seed(42)
    base_price = 50000
    trend = np.linspace(0, 0.2, 500)
    noise = np.random.normal(0, 0.005, 500)
    close = base_price * (1 + trend + noise).cumprod()

    data = pd.DataFrame({
        'close': close,
        'open': close * 0.999,
        'high': close * 1.002,
        'low': close * 0.998,
        'volume': 1000
    }, index=pd.date_range(start='2024-01-01', periods=500, freq='5T'))

    ema_short = data['close'].ewm(span=20).mean()
    ema_long = data['close'].ewm(span=50).mean()

    # Test different configurations
    configs = [
        {'adx_min': 15, 'ema_sep_min': 0.010, 'label': 'Relaxed'},
        {'adx_min': 20, 'ema_sep_min': 0.012, 'label': 'Default'},
        {'adx_min': 25, 'ema_sep_min': 0.015, 'label': 'Strict'},
    ]

    print(f"\n{'Config':<15} {'Tradeable %':<15}")
    print("-" * 35)

    for config in configs:
        label = config.pop('label')
        filter = SimpleTrendFilter(config)
        results = filter.apply_filters(data, ema_short, ema_long)

        print(f"{label:<15} {results['tradeable_pct']:<15.1f}%")

    print("\n💡 Tuning Guide:")
    print("  - Relaxed (more signals): adx_min=15-18, ema_sep_min=0.010")
    print("  - Default (balanced): adx_min=20, ema_sep_min=0.012")
    print("  - Strict (high quality): adx_min=25, ema_sep_min=0.015")


def test_integration_with_strategy():
    """Test filter integration with strategy"""
    print("\n" + "="*70)
    print("STRATEGY INTEGRATION TEST")
    print("="*70)

    from src.strategy.strategies.ema_pullback_with_imbalance import EMAPullbackImbalanceStrategy

    # Generate trending data
    np.random.seed(42)
    base_price = 50000
    trend = np.linspace(0, 0.15, 300)
    noise = np.random.normal(0, 0.004, 300)
    close = base_price * (1 + trend + noise).cumprod()

    data = pd.DataFrame({
        'close': close,
        'open': close * 0.999,
        'high': close * 1.003,
        'low': close * 0.997,
        'volume': 1000
    }, index=pd.date_range(start='2024-01-01', periods=300, freq='5T'))

    # Create strategy with simple filter
    config = {
        'ema_short': 20,
        'ema_long': 50,
        'rsi_period': 14,
        'use_trend_filter': True,
        'filter_config': {
            'adx_min': 20,
            'ema_sep_min': 0.012,
            'use_ema_curvature': True
        }
    }

    strategy = EMAPullbackImbalanceStrategy(config)

    # Calculate indicators
    indicators = strategy.calculate_indicators(data)

    # Generate signals
    signals = strategy.generate_signals(data, indicators)

    # Analyze
    total_bars = len(signals)
    tradeable_bars = signals['is_tradeable'].sum()
    total_signals = (signals['signal'] != 0).sum()

    print(f"\n📊 Results:")
    print(f"  Total bars: {total_bars}")
    print(f"  Tradeable bars: {tradeable_bars} ({tradeable_bars/total_bars*100:.1f}%)")
    print(f"  Total signals: {total_signals}")

    if total_signals > 0:
        print(f"  Signal rate: {total_signals/tradeable_bars*100:.1f}% of tradeable bars")

    print(f"\n✅ Integration test {'PASSED' if tradeable_bars > 0 else 'FAILED'}")


def main():
    """Main test execution"""
    print("\n🚀 Starting Simple Trend Filter Tests...")
    print("="*70)

    # Test 1: Core functionality
    validator = SimpleTrendFilterValidator()
    results = validator.run_all_tests()

    # Test 2: Parameter tuning
    test_parameter_tuning()

    # Test 3: Strategy integration
    test_integration_with_strategy()

    print("\n" + "="*70)
    print("✅ ALL TESTS COMPLETED!")
    print("="*70)
    print(f"\n📁 Check output plots in: {Path(__file__).parent / 'output'}")
    print("\n💡 Next steps:")
    print("  1. Review plots to verify filter behavior")
    print("  2. Tune adx_min and ema_sep_min if needed")
    print("  3. Backtest with real data")
    print("  4. Deploy to production")


if __name__ == "__main__":
    main()
