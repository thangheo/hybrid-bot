#!/usr/bin/env python3
# ================================================================
# tests/test_regime_real_data.py
# Quick test regime detection on real/sample data
# ================================================================

import sys
import os
from pathlib import Path

# Add parent directory to path
sys.path.insert(0, str(Path(__file__).parent.parent))

import pandas as pd
import numpy as np
from datetime import datetime

from src.strategy.filters.regime_detector import MarketRegimeDetector


def load_sample_data(symbol: str = "BTCUSDT", timeframe: str = "5m") -> pd.DataFrame:
    """
    Load sample data for testing

    Args:
        symbol: Trading symbol
        timeframe: Timeframe (e.g., '5m', '1h')

    Returns:
        OHLCV DataFrame
    """
    # Try to load from CSV if available
    data_dir = Path(__file__).parent.parent.parent / "data"
    csv_file = data_dir / f"{symbol}_{timeframe}.csv"

    if csv_file.exists():
        print(f"📂 Loading data from: {csv_file}")
        df = pd.read_csv(csv_file)

        # Ensure proper column names
        if 'timestamp' in df.columns:
            df['timestamp'] = pd.to_datetime(df['timestamp'])
            df.set_index('timestamp', inplace=True)
        elif 'time' in df.columns:
            df['time'] = pd.to_datetime(df['time'])
            df.set_index('time', inplace=True)

        # Rename columns if needed
        column_mapping = {
            'open': 'open',
            'high': 'high',
            'low': 'low',
            'close': 'close',
            'volume': 'volume'
        }

        df.rename(columns=column_mapping, inplace=True, errors='ignore')

        return df[['open', 'high', 'low', 'close', 'volume']]

    else:
        print(f"⚠️  No CSV data found at {csv_file}")
        print("📝 Generating sample synthetic data instead...")
        return generate_sample_data()


def generate_sample_data(periods: int = 1000) -> pd.DataFrame:
    """Generate sample OHLCV data for testing"""
    np.random.seed(42)

    # Create mixed regime data (trending then ranging then choppy)
    base_price = 50000

    # Part 1: Trending (periods 0-400)
    trend1 = np.linspace(0, 0.2, 400)
    noise1 = np.random.normal(0, 0.005, 400)
    returns1 = trend1 + noise1

    # Part 2: Ranging (periods 400-700)
    noise2 = np.random.normal(0, 0.003, 300)
    returns2 = noise2

    # Part 3: Choppy (periods 700-1000)
    direction_changes = np.random.choice([-1, 1], 300)
    magnitude = np.random.uniform(0.002, 0.01, 300)
    returns3 = direction_changes * magnitude

    # Combine
    all_returns = np.concatenate([returns1, returns2, returns3])
    close = base_price * (1 + all_returns).cumprod()

    # Create DataFrame
    data = pd.DataFrame(index=pd.date_range(start='2024-01-01', periods=periods, freq='5T'))
    data['close'] = close
    data['open'] = close * (1 + np.random.uniform(-0.002, 0.002, periods))
    data['high'] = close * (1 + np.abs(np.random.uniform(0, 0.005, periods)))
    data['low'] = close * (1 - np.abs(np.random.uniform(0, 0.005, periods)))
    data['volume'] = np.random.uniform(1000, 5000, periods)

    return data


def test_regime_quick(data: pd.DataFrame, config: dict = None):
    """Quick regime detection test with summary"""

    if config is None:
        config = {
            'adx_trending': 25,
            'adx_strong_trend': 40,
            'chop_threshold': 61.8,
            'hurst_trending_min': 0.55,
            'ema_separation_min': 0.015,
            'atr_percentile': 30,
        }

    print("\n" + "="*70)
    print("QUICK REGIME DETECTION TEST")
    print("="*70)

    print(f"\n📊 Data Info:")
    print(f"  Period: {data.index[0]} to {data.index[-1]}")
    print(f"  Total bars: {len(data)}")
    print(f"  Price range: ${data['close'].min():.2f} - ${data['close'].max():.2f}")

    # Detect regime
    print("\n🔍 Running regime detection...")
    detector = MarketRegimeDetector(config)
    regime_data = detector.detect_regime(data)

    # Print summary
    print("\n" + detector.get_regime_summary(regime_data))

    # Statistics
    print("\n📈 Overall Statistics:")
    print(f"  Total bars analyzed: {len(regime_data)}")

    regime_counts = regime_data['regime'].value_counts()
    print(f"\n  Regime Distribution:")
    for regime, count in regime_counts.items():
        pct = count / len(regime_data) * 100
        print(f"    {regime:15s}: {count:4d} bars ({pct:5.1f}%)")

    tradeable_count = regime_data['is_tradeable'].sum()
    print(f"\n  Tradeable bars: {tradeable_count} ({tradeable_count/len(regime_data)*100:.1f}%)")
    print(f"  Non-tradeable bars: {len(regime_data) - tradeable_count} ({(len(regime_data) - tradeable_count)/len(regime_data)*100:.1f}%)")

    print(f"\n  Average confidence: {regime_data['confidence'].mean():.1%}")
    print(f"  Max confidence: {regime_data['confidence'].max():.1%}")

    # Indicator averages
    print(f"\n📊 Indicator Averages:")
    print(f"  ADX: {regime_data['adx'].mean():.1f} (trending if > {config['adx_trending']})")
    print(f"  Choppiness: {regime_data['chop_index'].mean():.1f} (choppy if > {config['chop_threshold']})")
    print(f"  Hurst: {regime_data['hurst'].mean():.2f} (trending if > {config['hurst_trending_min']})")
    print(f"  EMA Separation: {regime_data['ema_separation'].mean():.2%}")

    # Recent trend (last 50 bars)
    recent = regime_data.tail(50)
    recent_regime = recent['regime'].value_counts()

    print(f"\n🔥 Recent Trend (last 50 bars):")
    for regime, count in recent_regime.items():
        print(f"  {regime:15s}: {count:2d} bars")

    recent_tradeable = recent['is_tradeable'].sum()
    print(f"\n  Recent tradeable: {recent_tradeable}/50 bars ({recent_tradeable/50*100:.0f}%)")

    # Export to CSV for further analysis
    output_dir = Path(__file__).parent / 'output'
    output_dir.mkdir(exist_ok=True)

    export_file = output_dir / f"regime_analysis_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"

    # Combine data with regime results
    result_df = data.copy()
    for col in regime_data.columns:
        result_df[col] = regime_data[col]

    result_df.to_csv(export_file)
    print(f"\n💾 Full results exported to: {export_file}")

    return regime_data


def main():
    """Main execution"""
    print("\n🚀 Starting Quick Regime Detection Test...")

    # Load data
    data = load_sample_data("BTCUSDT", "5m")

    if data is None or len(data) < 200:
        print("❌ Error: Not enough data for testing (need at least 200 bars)")
        return

    # Run test
    regime_data = test_regime_quick(data)

    print("\n✅ Test completed!")
    print("\n💡 Interpretation:")
    print("  - 'trending' regime = GOOD for EMA pullback strategy")
    print("  - 'ranging', 'choppy', 'volatile' = AVOID trading")
    print("  - High confidence (>60%) = Strong regime classification")
    print("  - Check exported CSV for detailed bar-by-bar analysis")


if __name__ == "__main__":
    main()
