#!/usr/bin/env python3
"""Train market regime HMMs for symbols listed in config/trading_bot.yaml.

This script will:
- locate symbols in the repo config
- ensure a local data cache at ./data/
- download missing 1m OHLCV data using Regime.download_crypto_data
- compute features with Regime.compute_regime_features
- train a GaussianHMM per-symbol (n_states configurable)
- save trained model with joblib and save a matplotlib plot to data/
"""

from pathlib import Path
import sys
import yaml
import logging
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from sklearn.preprocessing import StandardScaler
from hmmlearn.hmm import GaussianHMM
import joblib
# zmq
import zmq
from src.regime.market_regime import Regime
import time
from datetime import datetime, timedelta, timezone
import argparse
import requests
import json
import multiprocessing
import os
import threading
# import for None
from typing import Optional, Any



# Ensure src is importable (where market_regime.py lives)
HERE = Path(__file__).resolve().parent
SRC_DIR = HERE / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))


def load_config(config_path: Optional[str] = None) -> dict:
    # Look for config in repo root (parent of python-strategy)
    if config_path:
        p = Path(config_path)
        if p.exists():
            with open(p, 'r') as f:
                return yaml.safe_load(f)
        else:
            raise FileNotFoundError(f"Config file {config_path} not found")

    repo_root = HERE.parent
    candidates = [repo_root / 'config' / 'trading_bot.yaml', repo_root / 'trading_bot.yaml', HERE / 'config' / 'trading_bot.yaml']
    for p in candidates:
        if p.exists():
            with open(p, 'r') as f:
                return yaml.safe_load(f)
    raise FileNotFoundError("Could not find trading_bot.yaml in expected locations: " + ",".join(str(x) for x in candidates))
    raise FileNotFoundError("Could not find trading_bot.yaml in expected locations: " + ",".join(str(x) for x in candidates))


def symbol_filename(symbol: str) -> str:
    return symbol.replace('/', '_').replace('-', '_')


def ensure_data_dir():
    data_dir = HERE / 'data'
    data_dir.mkdir(parents=True, exist_ok=True)
    return data_dir


def download_or_load(regime: Regime, symbol: str, data_dir: Path, days: int = 60) -> pd.DataFrame:
    fname = data_dir / f"{symbol_filename(symbol)}.csv"
    if fname.exists():
        # Try a few ways to read existing CSVs that may have different timestamp formats
        try:
            df = pd.read_csv(fname, parse_dates=['timestamp'], index_col='timestamp')
            return df
        except Exception:
            # Try reading with the first column as index (common when index was saved)
            try:
                df = pd.read_csv(fname, index_col=0, parse_dates=True)
                # ensure index is datetime
                df.index = pd.to_datetime(df.index)
                df.index.name = 'timestamp'
                return df
            except Exception:
                # Try to infer a datetime-like column
                df = pd.read_csv(fname)
                for col in df.columns:
                    parsed = pd.to_datetime(df[col], errors='coerce')
                    if parsed.notna().sum() > len(df) * 0.5:
                        df[col] = parsed
                        df.set_index(col, inplace=True)
                        df.index.name = 'timestamp'
                        return df
                raise RuntimeError(f"Could not find a timestamp column or datetime index in existing CSV: {fname}")

    # download using Regime's method and save
    print(f"Downloading data for {symbol} (days={days})...")
    df = regime.download_crypto_data(symbol, days=days)
    # ensure we have timestamp index
    if 'timestamp' in df.columns:
        # save with timestamp column
        df.to_csv(fname, index=False)
        df = pd.read_csv(fname, parse_dates=['timestamp'], index_col='timestamp')
    else:
        # DataFrame index is timestamp
        df = df.copy()
        df.index.name = 'timestamp'
        df.reset_index().to_csv(fname, index=False)
        df = pd.read_csv(fname, parse_dates=['timestamp'], index_col='timestamp')
    return df


def train_hmm_for_symbol(regime: Regime, df: pd.DataFrame, n_states: int = 4, symbol: Optional[str] = None):
    # compute features using the shared code in Regime
    features = regime.compute_regime_features(df)

    # Validate we have enough rows to train
    min_rows = max(10, n_states * 5)
    if features.empty or len(features) < min_rows:
        # Try resampling to coarser timeframes to create enough rows for rolling windows
        tried = []
        # use non-deprecated aliases
        for rule in ['5min', '15min', '1h']:
            try:
                tried.append(rule)
                # compute minutes per bar from rule
                minutes_per_bar = None
                if 'min' in rule:
                    minutes_per_bar = int(rule.replace('min', ''))
                elif 'h' in rule:
                    minutes_per_bar = int(rule.replace('h', '')) * 60
                else:
                    # unknown rule format; skip
                    continue

                df_rs = df.resample(rule).agg({'open': 'first', 'high': 'max', 'low': 'min', 'close': 'last', 'volume': 'sum'}).dropna()
                if df_rs.empty:
                    # nothing after resampling
                    print(f"Resample {rule} produced empty dataframe for symbol={symbol or '<unknown>'}")
                    continue

                # scale windows: original code used minutes (window_short=15, window_long=60)
                window_short = max(1, int(15 / minutes_per_bar))
                window_long = max(1, int(60 / minutes_per_bar))

                features_rs = regime.compute_regime_features(df_rs, window_short=window_short, window_long=window_long)
                if features_rs.empty or len(features_rs) < min_rows:
                    print(f"Resample {rule} produced {len(features_rs)} feature rows (need >= {min_rows}) for symbol={symbol or '<unknown>'}")
                    continue

                # use resampled
                features = features_rs
                df = df_rs
                print(f"Using resampled data ({rule}) for symbol={symbol or '<unknown>'} — rows={len(features)}")
                break
            except Exception as e:
                print(f"Resample {rule} failed for symbol={symbol or '<unknown>'}: {e}")
                # continue to next rule
                continue

        else:
            # Try using a light feature set on the original 1m data as a final fallback
            try:
                # don't rely on window_short/window_long which may not have been set; let Regime use its defaults for 1m data
                features_rs = regime.compute_regime_features(df, light=True)
                if not features_rs.empty and len(features_rs) >= min_rows:
                    features = features_rs
                    print(f"Using light feature set for symbol={symbol or '<unknown>'} — rows={len(features)}")
            except Exception as e:
                print(f"Light feature computation failed for symbol={symbol or '<unknown>'}: {e}")

    if features.empty:
        raise RuntimeError(f'No feature rows after compute_regime_features for symbol={symbol or "<unknown>"}')
    if len(features) < min_rows:
        raise RuntimeError(f'Not enough feature rows ({len(features)}) to train HMM for symbol={symbol or "<unknown>"}; need >= {min_rows}')

    feature_cols = [
        'vol_1h', 'vol_4h', 'vol_percentile',
        'trend_strength', 'directional_movement',
        'volume_spike', 'rsi_60'
    ]
    # guard missing columns
    feature_cols = [c for c in feature_cols if c in features.columns]
    if not feature_cols:
        raise RuntimeError('No feature columns available after feature computation')

    X = features[feature_cols].values
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)

    model = GaussianHMM(n_components=n_states, covariance_type='full', n_iter=500, random_state=42)
    model.fit(X_scaled)

    states = model.predict(X_scaled)
    probs = model.predict_proba(X_scaled)

    features = features.copy()
    features['regime'] = states
    for i in range(n_states):
        features[f'regime_prob_{i}'] = probs[:, i]

    return model, scaler, features, feature_cols


def plot_regimes(symbol: str, df: pd.DataFrame, features: pd.DataFrame, outpath: Path):
    df_plot = df.loc[features.index]

    plt.style.use('seaborn-darkgrid')
    fig, (ax_price, ax_probs) = plt.subplots(2, 1, figsize=(14, 8), sharex=True, gridspec_kw={'height_ratios': [3, 1]})

    ax_price.plot(df_plot.index, df_plot['close'], label='close')
    # color points by regime
    regimes = features['regime'].values
    cmap = plt.get_cmap('tab10')
    scatter = ax_price.scatter(features.index, df_plot['close'], c=regimes, cmap=cmap, s=6, alpha=0.8)
    ax_price.set_title(f"{symbol} close price with regimes")
    ax_price.legend()

    # plot regime probabilities
    prob_cols = [c for c in features.columns if c.startswith('regime_prob_')]
    if prob_cols:
        for i, col in enumerate(sorted(prob_cols)):
            ax_probs.plot(features.index, features[col], label=col, color=cmap(i))
        ax_probs.set_ylim(-0.05, 1.05)
        ax_probs.set_title('Regime probabilities')
        ax_probs.legend(ncol=min(4, len(prob_cols)), fontsize='small')

    plt.tight_layout()
    outpath.parent.mkdir(parents=True, exist_ok=True)
    plt.savefig(outpath)
    plt.close(fig)


def load_saved_models(data_dir: Path):
    """Load saved joblib models from data_dir. Returns dict keyed by symbol_key (filename part).
    symbol_key is the same string used in filenames (slashes replaced by underscores).
    """
    models = {}
    for p in data_dir.glob('regime_model_*.pkl'):
        try:
            key = p.stem.replace('regime_model_', '')
            models[key] = joblib.load(p)
        except Exception as e:
            print(f"Failed to load model {p}: {e}")
    return models


def predict_regime_for_symbol(regime_obj: Regime, model_entry: dict, df: pd.DataFrame, light_fallback=True):
    """Given a Regime instance, a loaded model_entry (dict with model, scaler, feature_cols), and a dataframe
    return (regime, probs) for the latest available row. If not enough rows, optionally try light features.
    """
    model = model_entry.get('model')
    scaler = model_entry.get('scaler')
    feature_cols = model_entry.get('feature_cols')

    if model is None or scaler is None or not feature_cols:
        raise RuntimeError('Invalid model_entry contents')

    # compute features
    features = regime_obj.compute_regime_features(df)

    # if not enough rows, try light mode if requested
    if features.empty and light_fallback:
        features = regime_obj.compute_regime_features(df, light=True)

    if features.empty:
        raise RuntimeError('No feature rows available to predict')

    # pick the last available row
    # ensure feature_cols exist
    cols = [c for c in feature_cols if c in features.columns]
    if not cols:
        raise RuntimeError('Feature columns not found in computed features')

    X_current = features[cols].iloc[-1:].values
    X_scaled = scaler.transform(X_current)

    pred = model.predict(X_scaled)[0]
    probs = model.predict_proba(X_scaled)[0]
    return int(pred), probs


def poll_and_update_regimes(symbols: list, data_dir: Path, interval: int = 60):
    """Continuously poll for new data every `interval` seconds and update regimes for all symbols.
    Returns when interrupted (KeyboardInterrupt). Prints updates to stdout and keeps an in-memory map of current regimes.
    """
    models = load_saved_models(data_dir)
    if not models:
        print('No saved models found in', data_dir)
        return

    regime_obj = Regime()  # used for fetching and feature computation
    current = {}  # map symbol_key -> (regime, probs, timestamp)

    try:
        while True:
            for symbol in symbols:
                key = symbol_filename(symbol)
                model_entry = models.get(key)
                if not model_entry:
                    print(f'No trained model for {symbol} (key={key})')
                    continue

                try:
                    # get recent data (1 day suffices for prediction)
                    df_recent = download_or_load(regime_obj, symbol, data_dir, days=1)
                    pred, probs = predict_regime_for_symbol(regime_obj, model_entry, df_recent)
                    current[key] = {'symbol': symbol, 'regime': int(pred), 'probs': probs.tolist(), 'ts': datetime.now(timezone.utc).isoformat()}
                    print(f"{datetime.now(timezone.utc).isoformat()}  {symbol}: regime={pred}, probs={np.round(probs,3)}")
                except Exception as e:
                    print(f"Failed to update {symbol}: {e}")

            # sleep until next poll
            time.sleep(interval)
    except KeyboardInterrupt:
        print('Polling interrupted by user')
        return current


def get_current_regime(symbol: str, data_dir: Optional[Path] = None):
    """Convenience function: load model for symbol and return current regime and probabilities.
    If model isn't found in data_dir, raises.
    """
    if data_dir is None:
        data_dir = ensure_data_dir()

    key = symbol_filename(symbol)
    models = load_saved_models(data_dir)
    model_entry = models.get(key)
    if not model_entry:
        raise RuntimeError(f'No model found for symbol {symbol} (key={key}) in {data_dir}')

    regime_obj = Regime()
    df_recent = download_or_load(regime_obj, symbol, data_dir, days=1)

    try:
        pred, probs = predict_regime_for_symbol(regime_obj, model_entry, df_recent)
        return {'symbol': symbol, 'regime': int(pred), 'probs': probs.tolist(), 'ts': datetime.now(timezone.utc).isoformat()}
    except Exception as e:
        raise RuntimeError(f'Prediction failed for {symbol}: {e}')


def download_coinbase_1m_data(product_id='BTC-USD', days=60):
    """
    Download REAL 1m data from Coinbase Exchange (public endpoint, no API key required).
    product_id examples: 'BTC-USD', 'ETH-USD', 'ADA-USD', etc.
    days: number of days to fetch going backwards from now.
    Returns: pandas DataFrame indexed by timestamp with columns ['open','high','low','close','volume'].
    """
    print(f"Downloading REAL {product_id} 1m data for {days} days from Coinbase...")
    base = "https://api.exchange.coinbase.com/products/{}/candles".format(product_id)

    end_dt = datetime.now(timezone.utc)
    start_dt = end_dt - timedelta(days=days)

    all_rows = []
    # Coinbase returns max 300 candles per request, so step by 300 minutes
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
                print(f"\nHTTP {resp.status_code}: {resp.text}")
                break
            candles = resp.json()
            if not candles:
                # no data in this window
                current_start = current_end + timedelta(seconds=60)
                time.sleep(0.1)
                continue

            # Coinbase returns list of [time, low, high, open, close, volume]
            for c in candles:
                ts = datetime.utcfromtimestamp(c[0])
                low, high, open_, close, vol = c[1], c[2], c[3], c[4], c[5]
                all_rows.append([ts, open_, high, low, close, vol])

            last_time = candles[0][0] if candles else int(current_end.timestamp())
            # Move forward: because Coinbase returns in descending order sometimes, advance by step
            current_start = current_end + timedelta(seconds=60)
            print(f"Fetched up to {current_end} ({len(all_rows):,} candles)", end='\r')
            time.sleep(0.12)  # small throttle
        except Exception as e:
            print(f"\nError: {e}")
            break

    if not all_rows:
        print("\n✗ No data downloaded!")
        return pd.DataFrame()

    df = pd.DataFrame(all_rows, columns=['timestamp','open','high','low','close','volume'])
    df['timestamp'] = pd.to_datetime(df['timestamp'])
    for c in ['open','high','low','close','volume']:
        df[c] = df[c].astype(float)
    df.set_index('timestamp', inplace=True)
    df = df.sort_index()
    df = df[~df.index.duplicated(keep='first')]

    print(f"\nDownloaded {len(df):,} candles from {df.index[0]} to {df.index[-1]}")
    return df


def publisher(context, socket,topic,msg):
    
    topic = "updates"
    counter = 0

    print("Publisher started, broadcasting every 60 seconds...")
    while True:
        counter += 1
        socket.send_string(f"{topic} {msg}")
        print("Sent:", msg)
        time.sleep(60)  # wait for 1 minute


def monitor_and_publish_regimes(symbols: list, data_dir: Path, socket, interval: int = 10, poll_days: int = 1):
    """Monitor latest candle for each symbol. When any symbol has a new timestamp,
    recompute regimes for all symbols and publish a single JSON message via ZMQ socket.

    Message format (JSON):
    {
      "ts": "2025-...",
      "source": "regime_publisher",
      "regimes": {
         "BTC/USDT": {"regime": 1, "probs": [0.1,0.7,0.2], "last_ts": "..."},
         ...
      }
    }
    """
    topic = 'regimes_update'
    models = load_saved_models(data_dir)
    if not models:
        print('No saved models found, nothing to publish')
        return

    regime_obj = Regime()
    last_ts: dict[str, Optional[str]] = {symbol: None for symbol in symbols}

    print('Starting monitor loop — will check every', interval, 'seconds')
    try:
        while True:
            any_new = False
            # store latest timestamps found this iteration
            latest_seen = {}
            for symbol in symbols:
                try:
                    df_recent = download_or_load(regime_obj, symbol, data_dir, days=poll_days)
                    if df_recent is None or df_recent.empty:
                        latest_seen[symbol] = None
                        continue
                    # latest timestamp as ISO string
                    ts = df_recent.index[-1]
                    ts_s = pd.to_datetime(ts).isoformat()
                    latest_seen[symbol] = ts_s
                    if last_ts.get(symbol) != ts_s:
                        any_new = True
                except Exception as e:
                    print(f'Error fetching recent data for {symbol}: {e}')
                    latest_seen[symbol] = None

            if any_new:
                # (re)load models in case they were updated
                models = load_saved_models(data_dir)
                payload: dict[str, Any] = {'ts': datetime.now(timezone.utc).isoformat(), 'source': 'regime_publisher', 'regimes': {}}
                for symbol in symbols:
                    key = symbol_filename(symbol)
                    model_entry = models.get(key)
                    entry: dict[str, Any] = {'regime': None, 'probs': None, 'last_ts': latest_seen.get(symbol)}
                    if model_entry and latest_seen.get(symbol):
                        try:
                            # predict using the most recent data for that symbol
                            df_recent = download_or_load(regime_obj, symbol, data_dir, days=poll_days)
                            pred, probs = predict_regime_for_symbol(regime_obj, model_entry, df_recent)
                            entry['regime'] = int(pred)
                            entry['probs'] = [float(x) for x in probs]
                        except Exception as e:
                            entry['error'] = str(e)
                    else:
                        if not model_entry:
                            entry['error'] = 'no_model'
                    payload['regimes'][symbol] = entry

                # send single message
                try:
                    socket.send_string(f"{topic} {json.dumps(payload)}")
                    print('Published regimes update:', payload['ts'])
                except Exception as e:
                    print('Failed to publish regimes:', e)

                # update last_ts map
                last_ts.update(latest_seen)

            time.sleep(interval)
    except KeyboardInterrupt:
        print('Monitor stopped by user')
        return


def to_coinbase_product(symbol: str) -> str:
    """Convert a symbol like 'BTC/USDT' or 'BTC-USD' to Coinbase product id like 'BTC-USD'.
    If symbol already looks like Coinbase (contains '-') return as-is.
    """
    if '-' in symbol:
        return symbol
    # replace slash with dash
    p = symbol.replace('/', '-')
    # convert USDT to USD if present (approximation)
    p = p.replace('USDT', 'USD')
    return p


def generate_regimes_snapshot(symbols: list, data_dir: Path) -> dict:
    """Generate a snapshot dict for all symbols based on saved models and latest data.
    This doesn't publish, it only builds the payload and saves it to data_dir/regimes_snapshot.json
    """
    models = load_saved_models(data_dir)
    regime_obj = Regime()
    payload: dict[str, Any] = {'ts': datetime.now(timezone.utc).isoformat(), 'source': 'regime_trainer', 'regimes': {}}
    for symbol in symbols:
        key = symbol_filename(symbol)
        model_entry = models.get(key)
        entry: dict[str, Any] = {'regime': None, 'probs': None, 'last_ts': None}
        try:
            df_recent = download_or_load(regime_obj, symbol, data_dir, days=1)
            if df_recent is None or df_recent.empty:
                entry['error'] = 'no_recent_data'
                payload['regimes'][symbol] = entry
                continue
            entry['last_ts'] = pd.to_datetime(df_recent.index[-1]).isoformat()
            if model_entry:
                pred, probs = predict_regime_for_symbol(regime_obj, model_entry, df_recent)
                entry['regime'] = int(pred)
                entry['probs'] = [float(x) for x in probs]
            else:
                entry['error'] = 'no_model'
        except Exception as e:
            entry['error'] = str(e)
        payload['regimes'][symbol] = entry

    # save snapshot
    out = data_dir / 'regimes_snapshot.json'
    try:
        with open(out, 'w') as f:
            json.dump(payload, f)
    except Exception as e:
        print(f"Failed to write snapshot file: {e}")
    return payload


def download_and_train_all(symbols: list, data_dir: Path, n_states: int = 4, days: int = 60, data_source: str = 'binance', save_plots: bool = True):
    """Download data and train models for all symbols. This is intended to run in a separate process.
    Writes model pickles into data_dir and a regimes_snapshot.json when done.
    """
    # reduce priority to avoid interfering with other processes
    try:
        os.nice(10)
    except Exception:
        pass

    regime_obj = Regime(n_states=n_states)

    for symbol in symbols:
        print(f"[trainer] Processing {symbol} using {data_source}...")
        try:
            if data_source == 'coinbase':
                product = to_coinbase_product(symbol)
                df = download_coinbase_1m_data(product_id=product, days=days)
                if df is None or df.empty:
                    print(f"[trainer] No data for {symbol} (coinbase product {product})")
                    continue
                # normalize columns and index
                df = df.sort_index()
                df.index.name = 'timestamp'
                # save
                df.to_csv(data_dir / f"{symbol_filename(symbol)}.csv", index=True)
            else:
                # binance via Regime
                df = regime_obj.download_crypto_data(symbol, days=days)
                df.to_csv(data_dir / f"{symbol_filename(symbol)}.csv", index=True)

            # train
            try:
                model, scaler, features, feature_cols = train_hmm_for_symbol(regime_obj, df, n_states=n_states, symbol=symbol)
                joblib.dump({'model': model, 'scaler': scaler, 'feature_cols': feature_cols}, data_dir / f"regime_model_{symbol_filename(symbol)}.pkl")
                print(f"[trainer] Saved model for {symbol}")
                if save_plots:
                    plot_regimes(symbol, df, features, data_dir / f"regimes_{symbol_filename(symbol)}.png")
            except Exception as e:
                print(f"[trainer] Training failed for {symbol}: {e}")
                continue
        except Exception as e:
            print(f"[trainer] Error processing {symbol}: {e}")
            continue

    # write snapshot for publisher to pick up
    try:
        payload = generate_regimes_snapshot(symbols, data_dir)
        print('[trainer] Generated snapshot')
    except Exception as e:
        print(f'[trainer] Failed to generate snapshot: {e}')


def training_worker_process(symbols: list, data_dir: Path, n_states: int, days: int, interval_days: int, data_source: str, save_plots: bool):
    """Worker function that loops forever, running training every interval_days. Intended to be started in a Process."""
    while True:
        try:
            print(f"[trainer] Starting training run at {datetime.now(timezone.utc).isoformat()}")
            download_and_train_all(symbols, data_dir, n_states=n_states, days=days, data_source=data_source, save_plots=save_plots)
            print(f"[trainer] Training run complete; sleeping for {interval_days} days")
            time.sleep(interval_days * 86400)
        except Exception as e:
            print(f"[trainer] Uncaught error in worker: {e}")
            time.sleep(60)


def start_scheduled_training(symbols: list, data_dir: Path, n_states: int = 4, days: int = 60, interval_days: int = 7, data_source: str = 'coinbase', save_plots: bool = True):
    """Start a background process to perform scheduled training. Returns the multiprocessing.Process object."""
    p = multiprocessing.Process(target=training_worker_process, args=(symbols, data_dir, n_states, days, interval_days, data_source, save_plots), daemon=False)
    p.start()
    print(f"Started training worker PID={p.pid} (interval_days={interval_days}, data_source={data_source})")
    return p


def watch_and_publish_snapshot(snapshot_path: Path, socket, topic: str = 'regimes_update', poll_interval: int = 5):
    """Watch snapshot_path for modifications and publish its JSON content via ZMQ socket when changed.

    This uses simple polling (stat) so it's portable. The message format sent is:
    '{topic} {json_payload}'
    """
    last_mtime = None
    print(f"Watching snapshot file: {snapshot_path} (poll every {poll_interval}s)")

    # If a snapshot already exists when the watcher starts, publish it once so
    # subscribers can immediately receive the current state. Previously the
    # watcher only set last_mtime and waited for a change which made
    # --watch-snapshot appear silent.
    if snapshot_path.exists():
        try:
            mtime = snapshot_path.stat().st_mtime
            last_mtime = mtime
            with open(snapshot_path, 'r') as f:
                payload = json.load(f)
            try:
                msg = json.dumps(payload)
                socket.send_string(f"{topic} {msg}")
                print(f"Published initial snapshot ({snapshot_path}) at {datetime.now(timezone.utc).isoformat()}")
            except Exception as e:
                print(f"Failed to publish initial snapshot: {e}")
        except Exception as e:
            print(f"Watcher startup error reading snapshot {snapshot_path}: {e}")

    while True:
        try:
            if snapshot_path.exists():
                mtime = snapshot_path.stat().st_mtime
                if last_mtime is None:
                    last_mtime = mtime
                elif mtime != last_mtime:
                    last_mtime = mtime
                    try:
                        with open(snapshot_path, 'r') as f:
                            payload = json.load(f)
                    except Exception as e:
                        print(f"Failed to read snapshot file {snapshot_path}: {e}")
                        payload = {'error': str(e)}

                    try:
                        msg = json.dumps(payload)
                        socket.send_string(f"{topic} {msg}")
                        print(f"Published snapshot ({snapshot_path}) at {datetime.now(timezone.utc).isoformat()}")
                    except Exception as e:
                        print(f"Failed to publish snapshot: {e}")
        except Exception as e:
            print(f"Watcher error: {e}")

        time.sleep(poll_interval)


def main(n_states: int = 4, days: int = 60, save_plots: bool = True, watch_snapshot: bool = False):
    cfg = load_config(config_path=None)
    # data_ingestion/symbols
    symbols = cfg.get('data_ingestion', {}).get('symbols', [])
    if not symbols:
        raise RuntimeError('No symbols found in config/trading_bot.yaml under "symbols"')

    data_dir = ensure_data_dir()

    for symbol in symbols:
        print(f"Processing {symbol}...")
        regime = Regime(n_states=n_states)

        try:
            df = download_or_load(regime, symbol, data_dir, days=days)
            # save df to data_dir
            df.to_csv(data_dir / f"data_{symbol_filename(symbol)}.csv")
        except Exception as e:
            print(f"Failed to get data for {symbol}: {e}")
            continue

        try:
            model, scaler, features, feature_cols = train_hmm_for_symbol(regime, df, n_states=n_states, symbol=symbol)
        except Exception as e:
            print(f"Training failed for {symbol}: {e}")
            continue

        # Save model + scaler + feature cols
        model_fname = data_dir / f"regime_model_{symbol_filename(symbol)}.pkl"
        joblib.dump({'model': model, 'scaler': scaler, 'feature_cols': feature_cols}, model_fname)
        print(f"Saved model to {model_fname}")

        if save_plots:
            plot_path = data_dir / f"regimes_{symbol_filename(symbol)}.png"
            try:
                plot_regimes(symbol, df, features, plot_path)
                print(f"Saved plot to {plot_path}")
            except Exception as e:
                print(f"Failed to plot {symbol}: {e}")

    # After training, start monitoring and publishing regimes
    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    socket.bind("tcp://*:5555")  # listen on port 5555

    try:
        monitor_and_publish_regimes(symbols, data_dir, socket, interval=60, poll_days=1)
    except Exception as e:
        print(f"Error in monitor_and_publish_regimes: {e}")
    finally:
        socket.close()
        context.term()

   
if __name__ == '__main__':
    # print how to use:
    # python3 regime.py --start-trainer --watch-snapshot --trainer-interval-days 7 --data-source coinbase
    print("Usage examples:")
    print("  python3 regime.py --start-trainer --watch-snapshot --trainer-interval-days 66 --data-source binance")
    # small CLI-ish behavior
    p = argparse.ArgumentParser(description='Train regime HMMs for configured symbols')
    p.add_argument('--n-states', type=int, default=4)
    p.add_argument('--days', type=int, default=60)
    p.add_argument('--no-plots', dest='plots', action='store_false')
    p.add_argument('--start-trainer', dest='start_trainer', action='store_true', help='Start scheduled training worker')
    p.add_argument('--trainer-interval-days', dest='trainer_interval_days', type=int, default=7, help='Days between scheduled training runs')
    p.add_argument('--data-source', dest='data_source', type=str, default='binance', choices=['binance','coinbase'], help='Which data source to use for scheduled training')
    # allow forcing startup training even if config disables it
    p.add_argument('--train-now', dest='train_now', action='store_true', help='Run training on startup immediately (overrides config)')
    # config path must be added before parsing args
    p.add_argument('--config', type=str, default=None, help='Path to trading_bot.yaml (default: search in repo root and current dir)')
    p.add_argument('--watch-snapshot', dest='watch_snapshot', action='store_true', help='Watch regimes_snapshot.json and publish updates via ZMQ')
    args = p.parse_args()
    # load config early so we can consult regime.train_on_startup
    try:
        cfg = load_config(config_path=args.config)
    except FileNotFoundError:
        cfg = {}
    train_on_startup = cfg.get('regime', {}).get('train_on_startup', False)
    # start training worker if requested
    if args.start_trainer:
        symbols = cfg.get('data_ingestion', {}).get('symbols', [])
        if not symbols:
            raise RuntimeError('No symbols found in config for scheduled training')
        data_dir = ensure_data_dir()
        start_scheduled_training(symbols, data_dir, n_states=args.n_states, days=args.days, interval_days=args.trainer_interval_days, data_source=args.data_source, save_plots=not args.plots)

    # bind publisher socket
    # context = zmq.Context()
    # socket = context.socket(zmq.PUB)
    # socket.bind("tcp://*:5555")  # listen on port 5555

    # # start watcher if requested
    # if args.watch_snapshot:
    #     snapshot_path = ensure_data_dir() / 'regimes_snapshot.json'
    #     # Run watcher in foreground so the process remains alive (previous daemon thread exited immediately)
    #     try:
    #         print(f"Starting snapshot watcher (foreground) for {snapshot_path}")
    #         watch_and_publish_snapshot(snapshot_path, socket, topic='regimes_update', poll_interval=5)
    #     except KeyboardInterrupt:
    #         print('Snapshot watcher interrupted by user')
    #     finally:
    #         socket.close()
    #         context.term()
    #     # exit after watcher finishes
    #     sys.exit(0)

# decide whether to run one-off startup training
    if not args.watch_snapshot and not args.start_trainer and (train_on_startup or args.train_now):
        main(n_states=args.n_states, days=args.days, save_plots=not args.plots)
    else:
        # If we're not running the watcher and not starting the trainer, report skipped startup
        if not args.watch_snapshot and not args.start_trainer:
            print('Startup training skipped (train_on_startup=False in config).')
            print('To force immediate training use --train-now, or to run scheduled training use --start-trainer')