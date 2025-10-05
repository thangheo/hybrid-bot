#!/usr/bin/env python3
"""
Regime Trainer - Chỉ train models và save, KHÔNG làm gì khác.

Chạy: python3 regime_train.py
Hoặc: python3 regime_train.py --days 90 --n-states 4
"""

import sys
import yaml
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from pathlib import Path
from datetime import datetime, timedelta
from sklearn.preprocessing import StandardScaler
from hmmlearn.hmm import GaussianHMM
import joblib
import argparse
import requests
import time

# Import Regime class
HERE = Path(__file__).resolve().parent
SRC_DIR = HERE / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

from regime.market_regime import Regime


def load_config(config_path=None):
    """Load config file"""
    if config_path:
        p = Path(config_path)
        if p.exists():
            with open(p, 'r') as f:
                return yaml.safe_load(f)
    
    repo_root = HERE.parent
    candidates = [
        repo_root / 'config' / 'trading_bot.yaml',
        repo_root / 'trading_bot.yaml',
        HERE / 'config' / 'trading_bot.yaml'
    ]
    for p in candidates:
        if p.exists():
            with open(p, 'r') as f:
                return yaml.safe_load(f)
    
    raise FileNotFoundError("Không tìm thấy trading_bot.yaml")


def symbol_filename(symbol):
    """Convert symbol to filename"""
    return symbol.replace('/', '_').replace('-', '_')


def download_binance_data(regime_obj, symbol, days=60):
    """Download data từ Binance"""
    print(f"  Downloading {symbol} from Binance ({days} days)...")
    try:
        df = regime_obj.download_crypto_data(symbol, days=days)
        print(f"  ✓ Downloaded {len(df):,} rows")
        return df
    except Exception as e:
        print(f"  ✗ Binance failed: {e}")
        return None


def download_coinbase_data(product_id, days=60):
    """Download data từ Coinbase"""
    print(f"  Downloading {product_id} from Coinbase ({days} days)...")
    
    base = f"https://api.exchange.coinbase.com/products/{product_id}/candles"
    end_dt = datetime.utcnow()
    start_dt = end_dt - timedelta(days=days)
    
    all_rows = []
    step = timedelta(seconds=60*300)  # 300 minutes
    current_start = start_dt
    
    while current_start < end_dt:
        current_end = min(current_start + step, end_dt)
        params = {
            'start': current_start.isoformat() + 'Z',
            'end': current_end.isoformat() + 'Z',
            'granularity': 60
        }
        
        try:
            resp = requests.get(base, params=params, timeout=30)
            if resp.status_code != 200:
                print(f"  ✗ HTTP {resp.status_code}")
                break
            
            candles = resp.json()
            if not candles:
                current_start = current_end + timedelta(seconds=60)
                time.sleep(0.1)
                continue
            
            # Coinbase format: [time, low, high, open, close, volume]
            for c in candles:
                ts = datetime.utcfromtimestamp(c[0])
                low, high, open_, close, vol = c[1], c[2], c[3], c[4], c[5]
                all_rows.append([ts, open_, high, low, close, vol])
            
            current_start = current_end + timedelta(seconds=60)
            print(f"  Progress: {len(all_rows):,} candles", end='\r')
            time.sleep(0.12)
            
        except Exception as e:
            print(f"  ✗ Error: {e}")
            break
    
    if not all_rows:
        print("  ✗ No data downloaded")
        return None
    
    df = pd.DataFrame(all_rows, columns=['timestamp','open','high','low','close','volume'])
    df['timestamp'] = pd.to_datetime(df['timestamp'])
    for c in ['open','high','low','close','volume']:
        df[c] = df[c].astype(float)
    df.set_index('timestamp', inplace=True)
    df = df.sort_index()
    df = df[~df.index.duplicated(keep='first')]
    
    print(f"  ✓ Downloaded {len(df):,} candles")
    return df


def to_coinbase_product(symbol):
    """Convert symbol như BTC/USDT sang BTC-USD"""
    if '-' in symbol:
        return symbol
    p = symbol.replace('/', '-')
    p = p.replace('USDT', 'USD')
    return p


def train_hmm(regime_obj, df, n_states=3, symbol=None):
    """Train HMM model"""
    print(f"  Computing features...")
    features = regime_obj.compute_regime_features(df)
    
    # Nếu không đủ rows, thử resample
    min_rows = max(10, n_states * 5)
    
    if features.empty or len(features) < min_rows:
        print(f"  ! Only {len(features)} feature rows, trying resample...")
        
        for rule in ['5min', '15min', '1h']:
            try:
                minutes_per_bar = None
                if 'min' in rule:
                    minutes_per_bar = int(rule.replace('min', ''))
                elif 'h' in rule:
                    minutes_per_bar = int(rule.replace('h', '')) * 60
                else:
                    continue
                
                df_rs = df.resample(rule).agg({
                    'open': 'first',
                    'high': 'max',
                    'low': 'min',
                    'close': 'last',
                    'volume': 'sum'
                }).dropna()
                
                if df_rs.empty:
                    continue
                
                window_short = max(1, int(15 / minutes_per_bar))
                window_long = max(1, int(60 / minutes_per_bar))
                
                features_rs = regime_obj.compute_regime_features(
                    df_rs, 
                    window_short=window_short, 
                    window_long=window_long
                )
                
                if len(features_rs) >= min_rows:
                    features = features_rs
                    df = df_rs
                    print(f"  ✓ Using {rule} resampled data ({len(features)} rows)")
                    break
                    
            except Exception as e:
                print(f"  ! Resample {rule} failed: {e}")
                continue
    
    if features.empty or len(features) < min_rows:
        raise RuntimeError(f"Not enough feature rows ({len(features)}), need >= {min_rows}")
    
    # Select features
    feature_cols = [
        'vol_1h', 'vol_4h', 'vol_percentile',
        'trend_strength', 'directional_movement',
        'volume_spike', 'rsi_60'
    ]
    feature_cols = [c for c in feature_cols if c in features.columns]
    
    if not feature_cols:
        raise RuntimeError("No feature columns available")
    
    print(f"  Training HMM (n_states={n_states}, features={len(feature_cols)})...")
    
    # Scale and train
    X = features[feature_cols].values
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)
    
    model = GaussianHMM(
        n_components=n_states,
        covariance_type='full',
        n_iter=500,
        random_state=42
    )
    model.fit(X_scaled)
    
    # Predict states
    states = model.predict(X_scaled)
    probs = model.predict_proba(X_scaled)
    
    features = features.copy()
    features['regime'] = states
    for i in range(n_states):
        features[f'regime_prob_{i}'] = probs[:, i]
    
    print(f"  ✓ Training complete")
    
    return model, scaler, features, feature_cols, df


def plot_regimes(symbol, df, features, outpath):
    """Plot regimes (optional)"""
    try:
        df_plot = df.loc[features.index]
        
        plt.style.use('seaborn-v0_8-darkgrid')
        fig, (ax_price, ax_probs) = plt.subplots(
            2, 1, 
            figsize=(14, 8), 
            sharex=True,
            gridspec_kw={'height_ratios': [3, 1]}
        )
        
        # Price with regime colors
        ax_price.plot(df_plot.index, df_plot['close'], label='close', alpha=0.5)
        regimes = features['regime'].values
        cmap = plt.get_cmap('tab10')
        ax_price.scatter(
            features.index, 
            df_plot['close'], 
            c=regimes, 
            cmap=cmap, 
            s=6, 
            alpha=0.8
        )
        ax_price.set_title(f"{symbol} - Regimes")
        ax_price.legend()
        
        # Regime probabilities
        prob_cols = [c for c in features.columns if c.startswith('regime_prob_')]
        for i, col in enumerate(sorted(prob_cols)):
            ax_probs.plot(features.index, features[col], label=col, color=cmap(i))
        
        ax_probs.set_ylim(-0.05, 1.05)
        ax_probs.set_title('Regime Probabilities')
        ax_probs.legend(ncol=min(4, len(prob_cols)), fontsize='small')
        
        plt.tight_layout()
        plt.savefig(outpath)
        plt.close(fig)
        
        print(f"  ✓ Saved plot: {outpath.name}")
        
    except Exception as e:
        print(f"  ! Plot failed: {e}")


def train_symbol(symbol, regime_obj, data_dir, n_states=3, days=60, source='binance', save_plot=True):
    """Train 1 symbol"""
    print(f"\n{'='*60}")
    print(f"Training: {symbol}")
    print(f"{'='*60}")
    
    # Download data
    if source == 'coinbase':
        product = to_coinbase_product(symbol)
        df = download_coinbase_data(product, days=days)
    else:
        df = download_binance_data(regime_obj, symbol, days=days)
    
    if df is None or df.empty:
        print(f"✗ FAILED: No data for {symbol}\n")
        return False
    
    # Save raw data
    csv_file = data_dir / f"{symbol_filename(symbol)}.csv"
    df.to_csv(csv_file)
    print(f"  ✓ Saved data: {csv_file.name}")
    
    # Train model
    try:
        model, scaler, features, feature_cols, df_final = train_hmm(
            regime_obj, 
            df, 
            n_states=n_states, 
            symbol=symbol
        )
    except Exception as e:
        print(f"✗ FAILED: Training error - {e}\n")
        return False
    
    # Save model
    model_file = data_dir / f"regime_model_{symbol_filename(symbol)}.pkl"
    joblib.dump({
        'model': model,
        'scaler': scaler,
        'feature_cols': feature_cols
    }, model_file)
    print(f"  ✓ Saved model: {model_file.name}")
    
    # Plot (optional)
    if save_plot:
        plot_file = data_dir / f"regimes_{symbol_filename(symbol)}.png"
        plot_regimes(symbol, df_final, features, plot_file)
    
    print(f"✓ SUCCESS: {symbol}\n")
    return True


def main():
    parser = argparse.ArgumentParser(description='Regime Trainer - train models và save')
    parser.add_argument('--n-states', type=int, default=3, help='Số states cho HMM')
    parser.add_argument('--days', type=int, default=60, help='Số ngày data để download')
    parser.add_argument('--source', type=str, default='binance', choices=['binance','coinbase'], help='Data source')
    parser.add_argument('--no-plot', action='store_true', help='Không save plots')
    parser.add_argument('--config', type=str, default=None, help='Path to trading_bot.yaml')
    parser.add_argument('--symbols', type=str, nargs='+', help='Symbols để train (override config)')
    args = parser.parse_args()
    
    # Load config
    try:
        cfg = load_config(args.config)
        symbols = args.symbols or cfg.get('data_ingestion', {}).get('symbols', [])
    except Exception as e:
        if args.symbols:
            symbols = args.symbols
        else:
            print(f"✗ Lỗi load config: {e}")
            print("  Dùng --symbols BTC/USDT ETH/USDT để chỉ định symbols")
            return
    
    if not symbols:
        print("✗ Không có symbols")
        print("  Thêm vào config hoặc dùng --symbols BTC/USDT ETH/USDT")
        return
    
    # Setup data dir
    data_dir = HERE / 'data'
    data_dir.mkdir(parents=True, exist_ok=True)
    
    # Train
    print(f"\n{'#'*60}")
    print(f"# REGIME TRAINER")
    print(f"# Symbols: {len(symbols)}")
    print(f"# States: {args.n_states}")
    print(f"# Days: {args.days}")
    print(f"# Source: {args.source}")
    print(f"# Data dir: {data_dir}")
    print(f"{'#'*60}\n")
    
    regime_obj = Regime(n_states=args.n_states)
    
    success = 0
    failed = 0
    
    for symbol in symbols:
        try:
            if train_symbol(
                symbol, 
                regime_obj, 
                data_dir,
                n_states=args.n_states,
                days=args.days,
                source=args.source,
                save_plot=not args.no_plot
            ):
                success += 1
            else:
                failed += 1
        except Exception as e:
            print(f"✗ FAILED: {symbol} - {e}\n")
            failed += 1
    
    # Summary
    print(f"\n{'#'*60}")
    print(f"# SUMMARY")
    print(f"# Success: {success}/{len(symbols)}")
    print(f"# Failed: {failed}/{len(symbols)}")
    print(f"{'#'*60}\n")
    
    if success > 0:
        print(f"✓ Models saved in: {data_dir}")
        print(f"  Chạy publisher: python3 regime_publisher.py")


if __name__ == '__main__':
    main()
