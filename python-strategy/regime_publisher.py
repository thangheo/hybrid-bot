#!/usr/bin/env python3
"""
Regime Publisher - Chỉ publish regimes, KHÔNG train gì cả.

Chạy: python3 regime_publisher.py
Hoặc: python3 regime_publisher.py --interval 30
"""

import sys
import time
import json
import zmq
import joblib
import pandas as pd
import numpy as np
from pathlib import Path
from datetime import datetime
import argparse
import yaml

# Import Regime class để dùng compute_regime_features và download_crypto_data
HERE = Path(__file__).resolve().parent
SRC_DIR = HERE / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))
# Also add the python-strategy package root so modules like `data.download` are importable
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from src.regime.market_regime import Regime


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


def load_models(data_dir):
    """Load tất cả models đã train"""
    models = {}
    for pkl_file in data_dir.glob('regime_model_*.pkl'):
        try:
            key = pkl_file.stem.replace('regime_model_', '')
            models[key] = joblib.load(pkl_file)
            print(f"✓ Loaded model: {key}")
        except Exception as e:
            print(f"✗ Failed to load {pkl_file.name}: {e}")
    return models


def get_recent_data(regime_obj, symbol, data_dir, days=1):
    """Lấy data gần nhất (từ file hoặc download)"""
    csv_file = data_dir / f"{symbol_filename(symbol)}.csv"
    
    # Thử đọc từ file trước
    if csv_file.exists():
        try:
            df = pd.read_csv(csv_file, index_col=0, parse_dates=True)
            # Lấy data mới nhất (1 ngày)
            cutoff = datetime.utcnow() - pd.Timedelta(days=days)
            df = df[df.index >= cutoff]
            if not df.empty:
                return df
        except Exception as e:
            print(f"Lỗi đọc file {csv_file}: {e}")
    
    # Không có file hoặc data cũ → download mới
    try:
        df = regime_obj.download_crypto_data(symbol, days=days)
        return df
    except Exception as e:
        print(f"Lỗi download data cho {symbol}: {e}")
        return None


def predict_regime(regime_obj, model_entry, df):
    """Predict regime từ dataframe"""
    model = model_entry['model']
    scaler = model_entry['scaler']
    feature_cols = model_entry['feature_cols']
    
    # Compute features
    features = regime_obj.compute_regime_features(df)
    
    if features.empty:
        # Thử light mode
        features = regime_obj.compute_regime_features(df, light=True)
    
    if features.empty:
        raise RuntimeError("Không tạo được features")
    
    # Lấy row cuối cùng
    available_cols = [c for c in feature_cols if c in features.columns]
    if not available_cols:
        raise RuntimeError("Không có feature columns")
    
    X = features[available_cols].iloc[-1:].values
    X_scaled = scaler.transform(X)
    
    regime = int(model.predict(X_scaled)[0])
    probs = model.predict_proba(X_scaled)[0].tolist()
    
    return regime, probs


def publish_regimes(symbols, data_dir, socket, interval=60):
    """
    Main loop: publish regimes mỗi interval giây
    """
    regime_obj = Regime()
    models = load_models(data_dir)
    
    if not models:
        print("✗ Không tìm thấy models nào trong", data_dir)
        print("  Chạy training trước khi dùng publisher!")
        return
    
    print(f"\n{'='*60}")
    print(f"Regime Publisher started")
    print(f"ZMQ: tcp://*:5555")
    print(f"Interval: {interval}s")
    print(f"Symbols: {len(symbols)}")
    print(f"{'='*60}\n")
    
    topic = 'regimes_update'
    
    try:
        while True:
            payload = {
                'ts': datetime.utcnow().isoformat(),
                'source': 'regime_publisher',
                'regimes': {}
            }
            
            for symbol in symbols:
                key = symbol_filename(symbol)
                model_entry = models.get(key)
                
                entry = {
                    'regime': None,
                    'probs': None,
                    'last_ts': None
                }
                
                if not model_entry:
                    entry['error'] = 'no_model'
                    payload['regimes'][symbol] = entry
                    continue
                
                try:
                    # Lấy data mới
                    df = get_recent_data(regime_obj, symbol, data_dir, days=1)
                    
                    if df is None or df.empty:
                        entry['error'] = 'no_data'
                        payload['regimes'][symbol] = entry
                        continue
                    
                    # Predict
                    regime, probs = predict_regime(regime_obj, model_entry, df)
                    
                    entry['regime'] = regime
                    entry['probs'] = [round(p, 3) for p in probs]
                    entry['last_ts'] = df.index[-1].isoformat()
                    
                    print(f"{symbol:12} regime={regime} probs={entry['probs']}")
                    
                except Exception as e:
                    entry['error'] = str(e)
                    print(f"{symbol:12} ERROR: {e}")
                
                payload['regimes'][symbol] = entry
            
            # Publish
            try:
                msg = json.dumps(payload)
                socket.send_string(f"{topic} {msg}")
                print(f"\n✓ Published at {payload['ts']}\n")
            except Exception as e:
                print(f"✗ Publish failed: {e}")
            
            time.sleep(interval)
            
    except KeyboardInterrupt:
        print("\n\nStopped by user")


def main():
    parser = argparse.ArgumentParser(description='Regime Publisher - chỉ publish, không train')
    parser.add_argument('--interval', type=int, default=60, help='Interval giữa các lần publish (giây)')
    parser.add_argument('--config', type=str, default=None, help='Path to trading_bot.yaml')
    args = parser.parse_args()
    
    # Load config
    try:
        cfg = load_config(args.config)
        symbols = cfg.get('data_ingestion', {}).get('symbols', [])
    except Exception as e:
        print(f"✗ Lỗi load config: {e}")
        symbols = []
    
    if not symbols:
        print("✗ Không có symbols trong config")
        print("  Thêm symbols vào config/trading_bot.yaml:")
        print("  data_ingestion:")
        print("    symbols:")
        print("      - BTC/USDT")
        print("      - ETH/USDT")
        return
    
    # Data dir
    data_dir = HERE / 'data'
    if not data_dir.exists():
        print(f"✗ Không tìm thấy thư mục {data_dir}")
        print("  Train models trước khi chạy publisher!")
        return
    
    # Setup ZMQ
    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    socket.bind("tcp://*:5555")
    
    try:
        publish_regimes(symbols, data_dir, socket, interval=args.interval)
    finally:
        socket.close()
        context.term()


if __name__ == '__main__':
    main()
