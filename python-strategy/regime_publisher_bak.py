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
from datetime import datetime, timezone
import argparse
import yaml
import requests
import threading
import os
import tempfile
from typing import Any, Optional
import math

# Import Regime class để dùng compute_regime_features và download_crypto_data
HERE = Path(__file__).resolve().parent
SRC_DIR = HERE / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))
# Also add the python-strategy package root so modules like `data.download` are importable
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

from src.regime.market_regime import Regime


def format_probs(probs, decimals: int = 4, n_states: int | None = None):
    """Round probabilities to `decimals` and pad/truncate to `n_states` if provided.

    Args:
        probs: iterable of numeric probabilities
        decimals: number of decimal places to round
        n_states: if provided, ensure returned list has this length (pad with 0.0 or truncate)
    Returns:
        list[float]
    """
    lst = [round(float(p), decimals) for p in probs]
    if n_states is not None:
        if len(lst) < n_states:
            lst += [0.0] * (n_states - len(lst))
        elif len(lst) > n_states:
            lst = lst[:n_states]
    return lst


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
    # Deprecated: use DataCache for incremental updates. Keep as fallback to download full data.
    try:
        df = regime_obj.download_crypto_data(symbol, days=days)
        return df
    except Exception as e:
        print(f"Lỗi download data cho {symbol}: {e}")
        return None


class DataCache:
    """In-memory cache of symbol DataFrames + background updater that fetches recent 1m candles
    and appends them to the CSV on disk. Designed to download full history once and then only
    fetch small recent windows every minute to avoid rate limits.
    """
    def __init__(self, data_dir: Path, regime_obj, per_symbol_delay: float = 0.2, retention_days: int = 7):
        # DataCache is intentionally in-memory-only. We do NOT read or write CSVs in production.
        # data_dir is accepted for API compatibility but not used.
        self.data_dir = Path(data_dir)
        self.regime_obj = regime_obj
        self.lock = threading.Lock()
        self.dfs = {}  # symbol_key -> DataFrame
        self.running = False
        self.thread = None
        self.per_symbol_delay = per_symbol_delay
        # retention_days: keep only the most recent N days of data in memory (FIFO). Default 7 days.
        self.retention_days = retention_days

    @staticmethod
    def _binance_symbol(symbol: str) -> str:
        return symbol.replace('/', '').replace('-', '')

    @staticmethod
    def _klines_to_df(klines):
        # klines: list of [open_time, open, high, low, close, volume, ...]
        import pandas as _pd
        rows = []
        for k in klines:
            ts = datetime.utcfromtimestamp(k[0] / 1000)
            rows.append([ts, float(k[1]), float(k[2]), float(k[3]), float(k[4]), float(k[5])])
        df = _pd.DataFrame(rows, columns=['timestamp', 'open', 'high', 'low', 'close', 'volume'])
        df['timestamp'] = _pd.to_datetime(df['timestamp'])
        df.set_index('timestamp', inplace=True)
        return df

    def load_symbol_once(self, symbol: str, days: int = 7):
        """Load CSV if exists, else download full history once and save. Returns DataFrame."""
        key = symbol_filename(symbol)
        try:
            print(f"Downloading in-memory history for {symbol} (days={days})...")
            df = self.regime_obj.download_crypto_data(symbol, days=days)
            if df is None or df.empty:
                return pd.DataFrame()
            # ensure index name and sorting
            df.index.name = 'timestamp'
            df = df[~df.index.duplicated(keep='first')].sort_index()
            # Trim to retention immediately if configured
            if self.retention_days is not None and not df.empty:
                cutoff_time = df.index[-1] - pd.Timedelta(days=self.retention_days)
                df = df[df.index >= cutoff_time]
            with self.lock:
                self.dfs[key] = df
            return df
        except Exception as e:
            print(f"Error downloading history for {symbol}: {e}")
            return pd.DataFrame()

    def fetch_recent(self, symbol: str, limit: int = 5):
        """Fetch most recent `limit` 1m klines from Binance REST and return DataFrame."""
        bsym = self._binance_symbol(symbol)
        url = 'https://api.binance.com/api/v3/klines'
        params = {'symbol': bsym, 'interval': '1m', 'limit': limit}
        try:
            r = requests.get(url, params=params, timeout=10)
            r.raise_for_status()
            klines = r.json()
            return self._klines_to_df(klines)
        except Exception as e:
            print(f"fetch_recent failed for {symbol}: {e}")
            return pd.DataFrame()

    def append_new_candles(self, symbol: str):
        key = symbol_filename(symbol)
        with self.lock:
            df = self.dfs.get(key)

        recent = self.fetch_recent(symbol, limit=10)
        if recent.empty:
            return

        # append only rows newer than last index
        with self.lock:
            if key not in self.dfs or self.dfs[key] is None or self.dfs[key].empty:
                self.dfs[key] = recent
                to_save = recent
            else:
                last_ts = self.dfs[key].index[-1]
                new_rows = recent[recent.index > last_ts]
                if new_rows.empty:
                    return
                # append new rows and remove duplicates
                self.dfs[key] = pd.concat([self.dfs[key], new_rows]).drop_duplicates(keep='first').sort_index()
            # Trim old rows if retention_days is configured (FIFO behavior)
            if self.retention_days is not None and not self.dfs[key].empty:
                cutoff_time = self.dfs[key].index[-1] - pd.Timedelta(days=self.retention_days)
                self.dfs[key] = self.dfs[key][self.dfs[key].index >= cutoff_time]

    def start(self, symbols, initial_days: int = 60, interval: int = 60):
        # load all symbols once (full history)
        for s in symbols:
            try:
                self.load_symbol_once(s, days=initial_days)
            except Exception as e:
                print(f"Error loading symbol {s}: {e}")

        # background updater
        def run_loop():
            self.running = True
            while self.running:
                for s in symbols:
                    try:
                        self.append_new_candles(s)
                    except Exception as e:
                        print(f"Updater error for {s}: {e}")
                    time.sleep(self.per_symbol_delay)
                # sleep until next minute
                for _ in range(max(1, int(interval))):
                    if not self.running:
                        break
                    time.sleep(1)

        self.thread = threading.Thread(target=run_loop, daemon=True)
        self.thread.start()

    def stop(self):
        self.running = False
        if self.thread and self.thread.is_alive():
            self.thread.join(timeout=5)

    def get_df(self, symbol: str):
        key = symbol_filename(symbol)
        with self.lock:
            df = self.dfs.get(key)
            if df is None:
                return pd.DataFrame()
            return df.copy()


def predict_regime(regime_obj, model_entry, df):
    """Predict regime từ dataframe"""
    model = model_entry['model']
    scaler = model_entry['scaler']
    feature_cols = model_entry['feature_cols']
    # Try quantum-style realtime API first (if model provides it).
    if hasattr(model, 'extract_factors') and hasattr(model, 'compute_regime_probabilities'):
        features = regime_obj.compute_regime_features(df)
        if features.empty:
            features = regime_obj.compute_regime_features(df, light=True)
        if features.empty:
            # No features available for quantum path; fall back to sklearn-like below
            pass
        else:
            last_row = features.iloc[-1:]
            # Construct X for the model: prefer feature_cols if present
            if scaler is not None and feature_cols:
                available_cols = [c for c in feature_cols if c in last_row.columns]
                if available_cols:
                    X = last_row[available_cols].values
                else:
                    X = last_row.select_dtypes(include=[float, int]).values
            else:
                X = last_row.select_dtypes(include=[float, int]).values

            try:
                F = model.extract_factors(X)
                # extract_factors may return array-like with shape (n_samples, n_factors)
                if hasattr(F, '__len__'):
                    F0 = F[0]
                else:
                    F0 = F
                probs = model.compute_regime_probabilities(F0)
                probs = list(map(float, probs))
                regime = int(np.argmax(probs))
                return regime, probs
            except Exception:
                # If quantum path fails, continue to sklearn-like fallback
                pass

    # Fallback path: sklearn-like model using scaler + predict_proba
    features = regime_obj.compute_regime_features(df)
    if features.empty:
        features = regime_obj.compute_regime_features(df, light=True)
    if features.empty:
        raise RuntimeError("Không tạo được features")

    available_cols = [c for c in feature_cols if c in features.columns]
    if not available_cols:
        raise RuntimeError("Không có feature columns")

    last_row = features[available_cols].iloc[-1:].copy()
    if last_row.isna().any().any():
        features_light = regime_obj.compute_regime_features(df, light=True)
        available_cols_light = [c for c in feature_cols if c in features_light.columns]
        if available_cols_light:
            last_row_light = features_light[available_cols_light].iloc[-1:].copy()
            if not last_row_light.isna().any().any():
                last_row = last_row_light
                available_cols = available_cols_light

    nan_cols = [c for c in available_cols if pd.isna(last_row[c].iloc[0])]
    if nan_cols:
        raise RuntimeError(f"Input contains NaN in features: {', '.join(nan_cols)}")

    X = last_row.values
    X_scaled = scaler.transform(X)
    regime = int(model.predict(X_scaled)[0])
    probs = model.predict_proba(X_scaled)[0].tolist()
    return regime, probs


def publish_regimes(symbols, data_dir, socket, interval=600, probs_decimals: int = 4):
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
    
    # Load configuration (thresholds for drift gating etc.)
    try:
        cfg = load_config()
    except Exception:
        cfg = {}

    # Create and start DataCache: download full history once and then update per-minute
    cache = DataCache(data_dir, regime_obj)
    try:
        cache.start(symbols, initial_days=7, interval=300)

        while True:
            payload = {
                'ts': datetime.now(timezone.utc).isoformat(),
                'source': 'regime_publisher',
                'regimes': {}
            }
            
            for symbol in symbols:
                key = symbol_filename(symbol)
                model_entry = models.get(key)
                
                entry: dict[str, Any] = {
                    'regime': None,
                    'probs': None,
                    'last_ts': None
                }
                
                if not model_entry:
                    entry['error'] = 'no_model'
                    # Be explicit: use -1 as the "unknown" sentinel (matches C++ RegimeInfo)
                    entry['regime'] = -1
                    entry['probs'] = []
                    payload['regimes'][symbol] = entry
                    continue
                
                try:
                    # Use cached DataFrame (initially downloaded once, then appended per-minute)
                    df = cache.get_df(symbol)
                    if df is None or df.empty:
                        entry['error'] = 'no_data'
                        entry['regime'] = -1
                        entry['probs'] = []
                        payload['regimes'][symbol] = entry
                        continue
                    
                    # Predict regime and get posterior probs
                    regime, probs = predict_regime(regime_obj, model_entry, df)

                    entry['regime'] = regime
                    n_states = getattr(model_entry.get('model'), 'n_components', None)
                    entry['probs'] = format_probs(probs, decimals=probs_decimals, n_states=n_states)
                    entry['last_ts'] = df.index[-1].isoformat()

                    # --- Additional drift / slow-gate diagnostics and recommendation ---
                    # Configurable parameters with conservative defaults
                    neutral_threshold = cfg.get('neutral_threshold', 0.002)  # short alpha threshold
                    tau_stay = cfg.get('tau_stay', 0.80)
                    tau_trend = cfg.get('tau_trend', 0.65)
                    k_half_life = cfg.get('k_half_life', 1.0)
                    spread_cap = cfg.get('microstructure', {}).get('max_spread_frac', 0.01)
                    min_volume = cfg.get('microstructure', {}).get('min_avg_volume', 1.0)
                    # EWMA params and logistic slope configurable
                    ewma_span = cfg.get('ewma_span_short_vol', 60)
                    vol_floor = cfg.get('vol_floor', 1e-6)
                    trend_logistic_k = cfg.get('trend_logistic_k', 3.0)
                    half_life_min_clamp = cfg.get('half_life_min_clamp', 0.5)

                    # short_alpha proxy: 1-minute return (or normalized by 1m vol)
                    short_alpha = 0.0
                    try:
                        if len(df) >= 2:
                            last_close = float(df['close'].iloc[-1])
                            prev_close = float(df['close'].iloc[-2])
                            short_ret = (last_close / prev_close) - 1.0
                        else:
                            short_ret = 0.0
                        # short vol estimate over last 60 minutes if available
                        returns = df['close'].pct_change().dropna()
                        if returns.empty:
                            vol1m = 0.0
                        else:
                            # Use EWMA std to adapt faster to recent volatility
                            try:
                                vol1m = float(returns.ewm(span=ewma_span).std().iloc[-1])
                            except Exception:
                                vol1m = float(returns.std())
                        # soft floor to avoid division blow-up
                        vol1m = max(vol1m, vol_floor)

                        short_alpha = short_ret / vol1m if vol1m > 0 else short_ret
                    except Exception:
                        short_alpha = 0.0

                    # slow trend score: 4h return -> logistic squashed probability
                    p_trend_up = None
                    mixture_half_life_min = None
                    stay_prob = None
                    try:
                        # 4h = 240 minutes
                        if len(df) >= 240:
                            close_now = float(df['close'].iloc[-1])
                            close_4h = float(df['close'].iloc[-240])
                            ret4h = (close_now / close_4h) - 1.0
                            # vol over 4h for scaling
                            ret_series = df['close'].pct_change().dropna().iloc[-240:]
                            try:
                                vol4h = float(ret_series.std()) if not ret_series.empty else 0.0
                            except Exception:
                                vol4h = 0.0
                            # scale factor: if vol small, amplify; if vol big, normalize
                            scale = 1.0 / (vol4h + 1e-6)
                            # logistic transform with configurable slope
                            try:
                                p_trend_up = 1.0 / (1.0 + math.exp(-float(trend_logistic_k) * ret4h * scale))
                            except Exception:
                                p_trend_up = None
                        else:
                            p_trend_up = None
                    except Exception:
                        p_trend_up = None

                    # Estimate P_stay per regime: try model_entry or config, fallback to conservative default
                    try:
                        configured_pstay = model_entry.get('P_stay')
                        if configured_pstay and isinstance(configured_pstay, (list, tuple)) and len(configured_pstay) == len(probs):
                            P_stay_vec = list(map(float, configured_pstay))
                        else:
                            # try global config mapping
                            cfg_pstay = cfg.get('pstay_by_regime')
                            if cfg_pstay and isinstance(cfg_pstay, list) and len(cfg_pstay) == len(probs):
                                P_stay_vec = list(map(float, cfg_pstay))
                            else:
                                P_stay_vec = [cfg.get('default_pstay', 0.80)] * (len(probs) if probs else 4)
                    except Exception:
                        P_stay_vec = [cfg.get('default_pstay', 0.80)] * (len(probs) if probs else 4)

                    # mixture stay probability and half-life
                    try:
                        stay_prob = float(sum([p * s for p, s in zip(probs, P_stay_vec)])) if probs else float(sum(P_stay_vec) / len(P_stay_vec))
                        HLs = []
                        for s in P_stay_vec:
                            try:
                                if s >= 0.999999:
                                    HL = 1e6
                                elif s <= 0.0:
                                    HL = 0.0
                                else:
                                    HL = math.log(2.0) / (-math.log(max(min(float(s), 0.999999), 1e-6)))
                                # clamp lower bound to avoid meaningless tiny half-lives
                                if HL < float(half_life_min_clamp):
                                    HL = float(half_life_min_clamp)
                                HLs.append(HL)
                            except Exception:
                                HLs.append(float(half_life_min_clamp))
                        mixture_half_life_min = float(sum([p * h for p, h in zip(probs, HLs)])) if probs else float(sum(HLs) / len(HLs))
                    except Exception:
                        stay_prob = None
                        mixture_half_life_min = None

                    # simple microstructure check: use recent high-low as proxy for spread and avg volume
                    micro_ok = True
                    try:
                        # use last candle spread fraction = (high-low)/close
                        last_high = float(df['high'].iloc[-1])
                        last_low = float(df['low'].iloc[-1])
                        last_close = float(df['close'].iloc[-1])
                        spread_frac = (last_high - last_low) / (last_close + 1e-12)
                        avg_volume = float(df['volume'].iloc[-60:].mean()) if len(df) >= 60 else float(df['volume'].mean())
                        micro_ok = (spread_frac <= spread_cap) and (avg_volume >= min_volume)
                    except Exception:
                        micro_ok = True

                    # Decision: only consider neutral band using our short_alpha proxy
                    recommended_action = 'none'
                    reason = ''
                    suggested_time_cap_min = None
                    # boolean flag for downstream consumers
                    should_extend = False

                    try:
                        if abs(short_alpha) <= neutral_threshold:
                            # neutral zone: apply slow gate
                            if (stay_prob is not None and stay_prob >= tau_stay and
                                (p_trend_up is not None and p_trend_up >= tau_trend) and micro_ok):
                                recommended_action = 'extend'
                                should_extend = True
                                suggested_time_cap_min = None if mixture_half_life_min is None else float(k_half_life * mixture_half_life_min)
                                reason = f"extend: stay_prob={stay_prob:.3f},p_trend={p_trend_up:.3f},micro_ok={micro_ok}"
                            else:
                                recommended_action = 'recommend_exit'
                                should_extend = False
                                reason = f"exit: stay_prob={None if stay_prob is None else f'{stay_prob:.3f}'},p_trend={None if p_trend_up is None else f'{p_trend_up:.3f}'},micro_ok={micro_ok}"
                        else:
                            recommended_action = 'none'
                            reason = 'not_neutral'
                    except Exception:
                        recommended_action = 'none'
                        reason = 'error_deciding'

                    # Attach new diagnostics to payload
                    entry['short_alpha'] = None if short_alpha is None else float(short_alpha)
                    entry['p_trend_up'] = None if p_trend_up is None else float(p_trend_up)
                    entry['stay_prob'] = None if stay_prob is None else float(stay_prob)
                    entry['mixture_half_life_min'] = None if mixture_half_life_min is None else float(mixture_half_life_min)
                    entry['micro_ok'] = bool(micro_ok)
                    entry['recommended_action'] = recommended_action
                    entry['reason'] = reason
                    entry['suggested_time_cap_min'] = None if suggested_time_cap_min is None else float(suggested_time_cap_min)
                    entry['should_extend'] = bool(should_extend)

                    print(f"{symbol:12} regime={regime} probs={entry['probs']} trend={entry['p_trend_up']} stay={entry['stay_prob']} rec={entry['recommended_action']}")
                    
                except Exception as e:
                    entry['error'] = str(e)
                    # explicit unknown regime on error
                    entry['regime'] = -1
                    entry['probs'] = []
                    print(f"{symbol:12} ERROR: {e}")
                
                payload['regimes'][symbol] = entry
            
            # Publish
            try:
                msg = json.dumps(payload)
                socket.send_string(f"{topic} {msg}")
                print(f"\n✓ Published at {payload['ts']}\n")
            except Exception as e:
                print(f"✗ Publish failed: {e}")
            # Stable timing: aim to publish at fixed cadence regardless of loop runtime
            try:
                next_tick = time.time() + float(interval)
                sleep = max(0.0, next_tick - time.time())
                time.sleep(sleep)
            except Exception:
                time.sleep(interval)
            
    except KeyboardInterrupt:
        print("\n\nStopped by user")
    finally:
        try:
            cache.stop()
        except Exception:
            pass


def main():
    parser = argparse.ArgumentParser(description='Regime Publisher - chỉ publish, không train')
    parser.add_argument('--interval', type=int, default=60, help='Interval giữa các lần publish (giây)')
    parser.add_argument('--probs-decimals', type=int, default=4, help='Số lượng chữ số thập phân khi format probabilities')
    parser.add_argument('--config', type=str, default=None, help='Path to trading_bot.yaml')
    args = parser.parse_args()
    
    # Load config
    try:
        cfg = load_config(args.config)
        symbols = cfg.get('data_ingestion', {}).get('symbols', [])
    except Exception as e:
        print(f"✗ Lỗi load config: {e}")
        symbols = []
    
    # If no config or empty symbols, auto-discover symbols from data directory so
    # the user can run the publisher with a single command (no config required).
    if not symbols:
        data_dir = HERE / 'data'
        symbols = []
        # Prefer trained model files (regime_model_<SYMBOL>.pkl)
        for p in sorted(data_dir.glob('regime_model_*.pkl')):
            symbols.append(p.stem.replace('regime_model_', ''))
        # Fallback: use CSV filenames (skip helper files prefixed with data_ or __)
        if not symbols:
            for p in sorted(data_dir.glob('*.csv')):
                stem = p.stem
                if stem.startswith('data_') or stem.startswith('__'):
                    continue
                symbols.append(stem)

        if not symbols:
            print("✗ Không có symbols trong config hoặc data/ — cần models hoặc CSVs trong python-strategy/data")
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
        publish_regimes(symbols, data_dir, socket, interval=args.interval, probs_decimals=args.probs_decimals)
    finally:
        socket.close()
        context.term()


if __name__ == '__main__':
    main()
